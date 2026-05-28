// ImportService.Tasks.cpp — background task functions for ImportService.
//
// buildArtifactStore() is run on a QtConcurrent thread.
// completeImport() / completeReindex() run on the main thread as Qt slot bodies.

#include "app/services/ImportService.Private.h"
#include "io/cache/ParsedCache.h"
#include "io/jsf/JsfReader.h"
#include "io/xtf/XtfReader.h"

#include <QDebug>
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace dolphin::app::import_detail {

ImportTaskResult buildArtifactStore(const ProjectSource& source,
                                    const std::string& raw_path,
                                    const std::string& raw_format,
                                    const std::string& cache_path,
                                    io::ProgressFn progress)
{
    ImportTaskResult result;
    result.artifact_store_path = cache_path;
    result.artifact_store_format = "dlpd";

    if (cache_path.empty()) {
        result.error = "Project cache path is unavailable";
        return result;
    }

    const SourceFingerprint source_fingerprint = inspectSourceFile(raw_path);
    if (!source_fingerprint.exists) {
        result.error = "Cannot open source file: " + raw_path;
        return result;
    }
    result.source_size_bytes = source_fingerprint.size_bytes;
    result.source_modified_utc_ms = source_fingerprint.modified_utc_ms;

    if (sourceFingerprintMatches(source, source_fingerprint)
        && std::filesystem::exists(std::filesystem::path(cache_path))) {
        io::ParsedCacheReader cache_reader;
        if (cache_reader.open(cache_path)) {
            result.artifact_index = cache_reader.buildIndex(progress);
            if (!result.artifact_index.empty()) {
                copyImportMetadata(cache_reader.metadata(), result);
                if (progress) progress(1.0f);
                return result;
            }
        }
    }

    auto reader = makeReader(raw_format);
    if (!reader->open(raw_path)) {
        result.error = "Cannot open source file: " + raw_path;
        return result;
    }

    auto index_progress = progress ? io::ProgressFn([progress](float p) {
        progress(std::clamp(p, 0.0f, 1.0f) * 0.5f);
    }) : io::ProgressFn{};

    auto cache_progress = progress ? io::ProgressFn([progress](float p) {
        progress(0.5f + std::clamp(p, 0.0f, 1.0f) * 0.5f);
    }) : io::ProgressFn{};

    core::ArtifactIndex raw_index = reader->buildIndex(index_progress);
    if (raw_index.empty()) {
        if (progress) progress(1.0f);
        result.error = "No artifacts were indexed from the source file";
        return result;
    }

    copyImportMetadata(reader->metadata(), result);

    core::ArtifactIndex cache_index;
    if (!cache_path.empty()
        && io::writeParsedCache(cache_path, raw_index, *reader, cache_index, cache_progress)) {
        result.artifact_index       = std::move(cache_index);
        result.artifact_store_path   = cache_path;
        result.artifact_store_format = "dlpd";
        return result;
    }

    if (progress) progress(1.0f);
    result.error = "Failed to build parsed cache: " + cache_path;
    return result;
}

void completeImport(ImportService*                   svc,
                    ImportTaskResult                 result,
                    std::shared_ptr<Project>         project,
                    const std::string&               layer_id,
                    const std::string&               source_id,
                    const core::SpatialRef&          user_crs,
                    bool                             import_hf,
                    bool                             import_lf,
                    std::vector<core::ArtifactType>  wanted_modules)
{
    auto* dl = project->findLayer(layer_id);
    if (!dl) {
        releaseSourceJob(source_id);
        removeArtifactStoreFileIfUnused(*project, source_id, result);
        emit svc->indexingFailed(layer_id, "Layer removed during indexing");
        return;
    }

    if (!result.error.empty() || result.artifact_index.empty()) {
        releaseSourceJob(source_id);
        project->removeLayer(layer_id);
        emit svc->indexingFailed(layer_id,
            !result.error.empty()
                ? result.error
                : "No artifacts were indexed from the source file");
        return;
    }

    // User-confirmed CRS overrides whatever the parser auto-detected.
    if (!user_crs.empty()) {
        result.source_spatial_ref       = user_crs;
        result.source_spatial_ref.exact = true;
    }

    applyImportResultToLayer(*dl, result);

    // ── Module routing ────────────────────────────────────────────────────────
    // Policy: one DataLayer per artifact family. Readers decode what exists;
    // import routes each family to its own module layer. dl receives the first
    // non-empty family in kModuleArtifactTypes order (SSS → SBP → MAG → MBES).
    // Additional families each get a new DataLayer sharing the same cache path.
    std::vector<std::string> extra_layer_ids;
    if (dl->modality == Modality::Mixed) {
        const std::string base = dl->label;
        bool first = true;
        for (auto fam_type : kModuleArtifactTypes) {
            // Skip if the user excluded this module via the import dialog.
            if (!wanted_modules.empty()
                && std::find(wanted_modules.begin(), wanted_modules.end(), fam_type)
                   == wanted_modules.end())
                continue;
            auto filtered = filteredByType(result.artifact_index, fam_type);
            if (filtered.empty()) continue;
            if (first) {
                dl->label          = uniqueLayerLabel(*project, base + " " + moduleSuffix(fam_type));
                dl->artifact_index = std::move(filtered);
                dl->modality       = modalityForType(fam_type);
                first              = false;
            } else {
                auto* extra = project->addLayer(
                    source_id,
                    uniqueLayerLabel(*project, base + " " + moduleSuffix(fam_type)));
                if (extra) {
                    applyImportResultToLayer(*extra, result);
                    extra->artifact_index = std::move(filtered);
                    extra->modality       = modalityForType(fam_type);
                    extra_layer_ids.push_back(extra->id);
                }
            }
        }
    }

    // ── Dual-frequency split (SSS only) ──────────────────────────────────────
    // For dual-frequency sidescan files, never create a combined layer.
    // import_hf / import_lf control which bands the user wants.
    // This split operates only on the SSS layer produced by module routing above.
    std::string lf_layer_id;
    const bool is_dual_freq = dl->modality == Modality::Sidescan
                           && result.frequency_hz     > 0.f
                           && result.low_frequency_hz > 0.f
                           && std::fabs(result.frequency_hz - result.low_frequency_hz) > 1.f;

    if (is_dual_freq) {
        const float hf = result.frequency_hz;
        const float lf = result.low_frequency_hz;
        const std::string base_label = dl->label;

        const bool want_hf = import_hf || (!import_hf && !import_lf);
        const bool want_lf = import_lf || (!import_hf && !import_lf);

        if (want_hf) {
            dl->label            = uniqueLayerLabel(*project, base_label + "_HF");
            dl->frequency_hz     = hf;
            dl->low_frequency_hz = 0.f;
        } else {
            // LF only: dl becomes the LF layer
            dl->label            = uniqueLayerLabel(*project, base_label + "_LF");
            dl->frequency_hz     = lf;
            dl->low_frequency_hz = 0.f;
        }

        if (want_hf && want_lf) {
            auto* lf_layer = project->addLayer(source_id,
                                 uniqueLayerLabel(*project, base_label + "_LF"));
            if (lf_layer) {
                applyImportResultToLayer(*lf_layer, result);
                lf_layer->frequency_hz     = lf;
                lf_layer->low_frequency_hz = 0.f;
                lf_layer_id = lf_layer->id;
            }
        }
    }

    if (auto* src = project->findSource(source_id))
        applyImportResultToSource(*src, result);

    releaseSourceJob(source_id);
    if (!project->save()) {
        project->markLayerFailed(layer_id);
        emit svc->indexingFailed(layer_id,
            "Parsed data is ready, but the project could not be saved.");
        return;
    }

    // Transition all produced layers to Ready before notifying consumers.
    // layerReady fires here; layerPending fired earlier in addLayer().
    project->commitLayer(layer_id);
    if (!lf_layer_id.empty())
        project->commitLayer(lf_layer_id);
    for (const auto& id : extra_layer_ids)
        project->commitLayer(id);

    emit svc->indexingComplete(layer_id);
    if (!lf_layer_id.empty())
        emit svc->indexingComplete(lf_layer_id);
    for (const auto& id : extra_layer_ids)
        emit svc->indexingComplete(id);
}

void completeReindex(ImportService*            svc,
                     ImportTaskResult          result,
                     std::shared_ptr<Project>  project,
                     const std::string&        layer_id,
                     const std::string&        source_id)
{
    auto* dl = project->findLayer(layer_id);
    if (!dl) {
        releaseSourceJob(source_id);
        removeArtifactStoreFileIfUnused(*project, source_id, result);
        emit svc->indexingFailed(layer_id, "Layer removed during indexing");
        return;
    }

    if (!result.error.empty() || result.artifact_index.empty()) {
        releaseSourceJob(source_id);
        project->markLayerFailed(layer_id);
        emit svc->indexingFailed(layer_id,
            !result.error.empty()
                ? result.error
                : "No artifacts were indexed from the source file");
        return;
    }

    const auto linked_layers = project->findLayersBySource(dl->source_id);
    for (auto* linked_layer : linked_layers) {
        if (!linked_layer) continue;
        // Pinned single-band layers (split-bands import option) carry
        // low_frequency_hz == 0 as their band identity.  Reindex must not
        // overwrite that with the combined dual-freq result.
        const float    pinned_hz       = linked_layer->frequency_hz;
        const bool     is_freq_pinned  = (linked_layer->low_frequency_hz == 0.f
                                           && pinned_hz > 0.f
                                           && result.low_frequency_hz > 0.f);
        // Module-routed layers (any single-modality layer carved from a Mixed
        // source) must keep their filtered artifact index after reindex.
        const Modality pre_modality = linked_layer->modality;
        const bool is_modality_split =
            (pre_modality == Modality::Sidescan
             || pre_modality == Modality::SubBottom
             || pre_modality == Modality::Magnetometer
             || pre_modality == Modality::Multibeam)
            && inferModality(result.artifact_index) == Modality::Mixed;

        applyImportResultToLayer(*linked_layer, result);

        if (is_freq_pinned) {
            linked_layer->frequency_hz     = pinned_hz;
            linked_layer->low_frequency_hz = 0.f;
        }
        if (is_modality_split) {
            linked_layer->artifact_index = filteredByType(
                linked_layer->artifact_index,
                artifactTypeForModality(pre_modality));
            linked_layer->modality = pre_modality;
        }
    }

    if (auto* src = project->findSource(source_id))
        applyImportResultToSource(*src, result);

    releaseSourceJob(source_id);
    if (!project->save()) {
        emit svc->indexingFailed(layer_id,
            "Parsed data is ready, but the project could not be saved.");
        return;
    }
    for (auto* linked_layer : linked_layers) {
        if (!linked_layer) continue;
        project->commitLayer(linked_layer->id);
        emit svc->indexingComplete(linked_layer->id);
    }
}

} // namespace dolphin::app::import_detail
