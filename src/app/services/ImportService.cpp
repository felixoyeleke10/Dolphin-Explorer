// ImportService.cpp — write-path methods for ImportService
//
// importFile() and reindexLayer() trigger async background indexing.
// Read-path methods (loadSidescan*) live in ImportService.Load.cpp.
// Background task functions (buildArtifactStore, completeImport, completeReindex)
// live in ImportService.Tasks.cpp.

#include "app/services/ImportService.Private.h"
#include "app/import/FormatRegistry.h"
#include "app/import/PreflightChecker.h"
#include "io/jsf/JsfReader.h"
#include "io/cache/ParsedCache.h"
#include "io/segy/SegyReader.h"
#include "io/xtf/XtfReader.h"
#include <QDebug>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QFutureWatcherBase>
#include <QtConcurrent/QtConcurrent>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <mutex>
#include <set>
#include <utility>

namespace dolphin::app {

// -- Job-tracking state (file-local, accessed by import_detail helpers below) --
namespace {
std::mutex            g_active_source_jobs_mutex;
std::set<std::string> g_active_source_jobs;
} // namespace

namespace import_detail {

void copyImportMetadata(const io::FormatMeta& meta, ImportTaskResult& result)
{
    result.sonar_name       = meta.sonar_name;
    result.survey_name      = meta.survey_name;
    result.vessel_name      = meta.vessel_name;
    result.start_time_utc   = meta.start_time;
    result.end_time_utc     = meta.end_time;
    result.frequency_hz     = meta.frequency_hz;
    result.low_frequency_hz = meta.low_frequency_hz;
    result.source_spatial_ref = meta.coordinate_ref;

    const uint8_t mask = meta.bottom_pick_src_mask;
    if      ((mask & 0x03) == 0x03) result.bottom_track_kind = BottomTrackKind::Mixed;
    else if  (mask & 0x01)          result.bottom_track_kind = BottomTrackKind::Auto;
    else if  (mask & 0x02)          result.bottom_track_kind = BottomTrackKind::Manual;
    else                            result.bottom_track_kind = BottomTrackKind::None;
}

void applyImportResultToLayer(DataLayer& layer, const ImportTaskResult& result)
{
    layer.artifact_index          = result.artifact_index;
    layer.artifact_index.source_id = layer.source_id;
    layer.artifact_store_path     = result.artifact_store_path;
    layer.artifact_store_format   = result.artifact_store_format;
    layer.modality                = inferModality(layer.artifact_index);
    layer.source_spatial_ref      = result.source_spatial_ref;
    layer.sonar_name              = result.sonar_name;
    layer.survey_name             = result.survey_name;
    layer.vessel_name             = result.vessel_name;
    layer.start_time_utc          = result.start_time_utc;
    layer.end_time_utc            = result.end_time_utc;
    layer.frequency_hz            = result.frequency_hz;
    layer.low_frequency_hz        = result.low_frequency_hz;
    layer.bottom_track_kind       = result.bottom_track_kind;
    layer.index_built             = !layer.artifact_index.empty();
    layer.pipeline_applied        = false;  // raw DLPD — pipeline has not been run yet
}

void applyImportResultToSource(ProjectSource& source, const ImportTaskResult& result)
{
    source.source_spatial_ref = result.source_spatial_ref;
    source.size_bytes         = result.source_size_bytes;
    source.modified_utc_ms    = result.source_modified_utc_ms;
}

std::string normaliseFormat(std::string format)
{
    for (auto& c : format)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return format;
}

std::unique_ptr<io::IFormatReader> makeReader(const std::string& format)
{
    const std::string fmt = normaliseFormat(format);
    if (fmt == "dlpd" || fmt == "dpcache")   // dlpd is the current format; dpcache is the legacy alias
        return std::make_unique<io::ParsedCacheReader>();
    if (fmt == "jsf")
        return std::make_unique<io::JsfReader>();
    if (fmt == "segy" || fmt == "sgy")
        return std::make_unique<io::SegyReader>();
    return std::make_unique<io::XtfReader>();
}

std::string uniqueLayerLabel(const Project& project, const std::string& base_label)
{
    std::string label = base_label.empty() ? "Layer" : base_label;
    int suffix = 2;

    auto label_exists = [&](const std::string& candidate) {
        return std::any_of(project.layers().begin(), project.layers().end(),
            [&](const auto& layer) { return layer && layer->label == candidate; });
    };

    if (!label_exists(label))
        return label;

    std::string candidate;
    do {
        candidate = label + " (" + std::to_string(suffix++) + ")";
    } while (label_exists(candidate));
    return candidate;
}

SourceFingerprint inspectSourceFile(const std::string& path)
{
    QFileInfo info(QString::fromStdString(path));
    SourceFingerprint fingerprint;
    fingerprint.exists = info.exists();
    if (!fingerprint.exists)
        return fingerprint;

    fingerprint.size_bytes = static_cast<uint64_t>(info.size());
    fingerprint.modified_utc_ms = info.lastModified().toMSecsSinceEpoch();
    return fingerprint;
}

bool sourceFingerprintMatches(const ProjectSource& source,
                              const SourceFingerprint& fingerprint)
{
    if (!fingerprint.exists)
        return false;
    if (source.size_bytes != fingerprint.size_bytes)
        return false;
    if (source.modified_utc_ms != 0
        && source.modified_utc_ms != fingerprint.modified_utc_ms) {
        return false;
    }
    return true;
}

void releaseSourceJob(const std::string& source_id)
{
    std::lock_guard<std::mutex> lock(g_active_source_jobs_mutex);
    g_active_source_jobs.erase(source_id);
}

void removeArtifactStoreFileIfUnused(const Project& project,
                                     const std::string& source_id,
                                     const ImportTaskResult& result)
{
    if (normaliseFormat(result.artifact_store_format) != "dlpd"
        || result.artifact_store_path.empty()) {
        return;
    }

    if (!project.findLayersBySource(source_id).empty())
        return;

    std::error_code ec;
    std::filesystem::remove(std::filesystem::path(result.artifact_store_path), ec);
}

} // namespace import_detail

// -- File-local helpers (used only by importFile / reindexLayer) ---------------
namespace {

std::string cachePathForSource(const Project& project, const std::string& source_id)
{
    namespace fs = std::filesystem;
    const fs::path manifest(project.manifestPath());
    if (manifest.empty())
        return {};
    return (manifest.parent_path() / "data" / (source_id + ".dlpd")).string();
}

bool tryAcquireSourceJob(const std::string& source_id)
{
    std::lock_guard<std::mutex> lock(g_active_source_jobs_mutex);
    return g_active_source_jobs.insert(source_id).second;
}

} // namespace

// -- Public methods ------------------------------------------------------------

ImportService::ImportService(QObject* parent) : QObject(parent) {}

ImportService::~ImportService()
{
    // Wait for any in-flight background futures before destruction so that
    // releaseSourceJob() is always called and g_active_source_jobs stays clean.
    for (auto* child : children()) {
        if (auto* w = qobject_cast<QFutureWatcherBase*>(child))
            w->waitForFinished();
    }
}

std::string ImportService::importFile(const std::string& path,
                                      const std::string& format,
                                      std::shared_ptr<Project> project,
                                      const core::SpatialRef& user_crs,
                                      bool import_hf,
                                      bool import_lf,
                                      std::vector<core::ArtifactType> wanted_modules)
{
    if (!project) {
        const std::string error_id = path.empty() ? "unknown" : path;
        emit indexingFailed(error_id, "No project loaded");
        return {};
    }

    const std::string fmt = import_detail::normaliseFormat(format);
    if (const auto pre = checkImportPreflight(path, fmt); !pre) {
        emit indexingFailed(path, pre.error);
        return {};
    }

    QFileInfo fi(QString::fromStdString(path));

    auto* src = project->findSourceByPath(path);
    if (!src)
        src = project->addSource(path, format);
    if (!src) {
        emit indexingFailed(path, "Failed to add source to project");
        return {};
    }

    if (!tryAcquireSourceJob(src->id)) {
        emit indexingFailed(src->id, "This source is already being indexed.");
        return {};
    }

    const std::string layer_label = import_detail::uniqueLayerLabel(
        *project, fi.baseName().toStdString());
    auto* layer = project->addLayer(src->id, layer_label);
    if (!layer) {
        import_detail::releaseSourceJob(src->id);
        emit indexingFailed(src->id, "Failed to add layer to project");
        return {};
    }

    const std::string layer_id   = layer->id;
    const std::string cache_path = cachePathForSource(*project, src->id);
    const ProjectSource source_snapshot = *src;

    project->markLayerIndexing(layer_id);
    emit indexingStarted(layer_id);

    auto progress_fn = [this, layer_id](float p) {
        const int pct = static_cast<int>(p * 100.f);
        QMetaObject::invokeMethod(this, [this, layer_id, pct]() {
            emit indexingProgress(layer_id, pct);
        }, Qt::QueuedConnection);
    };

    QFuture<import_detail::ImportTaskResult> future = QtConcurrent::run(
        [source_snapshot, path, format, cache_path, progress_fn]() {
            return import_detail::buildArtifactStore(
                source_snapshot, path, format, cache_path, progress_fn);
        });

    auto* watcher = new QFutureWatcher<import_detail::ImportTaskResult>(this);
    connect(watcher, &QFutureWatcher<import_detail::ImportTaskResult>::finished, this,
        [this, watcher, project, layer_id, source_id = src->id,
         user_crs, import_hf, import_lf,
         wanted_modules = std::move(wanted_modules)]() mutable {
            watcher->deleteLater();

            import_detail::ImportTaskResult result;
            try {
                result = watcher->result();
            } catch (const std::exception& e) {
                qWarning() << "[ImportService] Import failed for layer"
                           << QString::fromStdString(layer_id) << ":" << e.what();
                import_detail::releaseSourceJob(source_id);
                if (project) project->removeLayer(layer_id);
                emit indexingFailed(layer_id, std::string("Import error: ") + e.what());
                return;
            } catch (...) {
                qWarning() << "[ImportService] Unexpected error importing layer"
                           << QString::fromStdString(layer_id);
                import_detail::releaseSourceJob(source_id);
                if (project) project->removeLayer(layer_id);
                emit indexingFailed(layer_id, "Unexpected error during import");
                return;
            }

            import_detail::completeImport(this, std::move(result), project,
                                          layer_id, source_id, user_crs,
                                          import_hf, import_lf,
                                          std::move(wanted_modules));
        });
    watcher->setFuture(future);

    return layer_id;
}

std::string ImportService::reindexLayer(const std::string& path,
                                        std::shared_ptr<Project> project,
                                        const std::string& layer_id)
{
    if (!project || layer_id.empty()) {
        emit indexingFailed(layer_id, "Reindex requested with no project or layer ID");
        return {};
    }

    auto* layer = project->findLayer(layer_id);
    if (!layer) {
        emit indexingFailed(layer_id, "Layer not found: " + layer_id);
        return {};
    }

    auto* source = project->findSource(layer->source_id);
    if (!source) {
        emit indexingFailed(layer_id, "Source not found for layer: " + layer_id);
        return {};
    }

    if (!tryAcquireSourceJob(layer->source_id)) {
        emit indexingFailed(layer_id, "This source is already being indexed.");
        return {};
    }

    const std::string cache_path = cachePathForSource(*project, layer->source_id);
    const std::string fmt = FormatRegistry::instance().sniffFromPath(path);

    if (const auto pre = checkImportPreflight(path, fmt); !pre) {
        import_detail::releaseSourceJob(layer->source_id);
        emit indexingFailed(layer_id, pre.error);
        return {};
    }

    const ProjectSource source_snapshot = *source;

    project->markLayerIndexing(layer_id);
    emit indexingStarted(layer_id);

    auto progress_fn = [this, layer_id](float p) {
        const int pct = static_cast<int>(p * 100.f);
        QMetaObject::invokeMethod(this, [this, layer_id, pct]() {
            emit indexingProgress(layer_id, pct);
        }, Qt::QueuedConnection);
    };

    QFuture<import_detail::ImportTaskResult> future = QtConcurrent::run(
        [source_snapshot, path, fmt, cache_path, progress_fn]() {
            return import_detail::buildArtifactStore(
                source_snapshot, path, fmt, cache_path, progress_fn);
        });

    auto* watcher = new QFutureWatcher<import_detail::ImportTaskResult>(this);
    connect(watcher, &QFutureWatcher<import_detail::ImportTaskResult>::finished, this,
        [this, watcher, project, layer_id, source_id = layer->source_id]() {
            watcher->deleteLater();

            import_detail::ImportTaskResult result;
            try {
                result = watcher->result();
            } catch (const std::exception& e) {
                import_detail::releaseSourceJob(source_id);
                emit indexingFailed(layer_id, std::string("Import error: ") + e.what());
                return;
            } catch (...) {
                import_detail::releaseSourceJob(source_id);
                emit indexingFailed(layer_id, "Unexpected error during import");
                return;
            }

            import_detail::completeReindex(this, std::move(result), project,
                                           layer_id, source_id);
        });
    watcher->setFuture(future);

    return layer_id;
}

void ImportService::rebuildCacheIndex(const std::string& layer_id,
                                      std::shared_ptr<Project> project)
{
    if (!project || layer_id.empty()) {
        emit indexingFailed(layer_id, "rebuildCacheIndex: no project or layer ID");
        return;
    }
    auto* layer = project->findLayer(layer_id);
    if (!layer) {
        emit indexingFailed(layer_id, "rebuildCacheIndex: layer not found");
        return;
    }

    const std::string cache_path = layer->artifact_store_path;
    const std::string source_id  = layer->source_id;

    if (cache_path.empty()) {
        emit indexingFailed(layer_id, "rebuildCacheIndex: no artifact store path");
        return;
    }

    // Cancel any previous rebuild for this layer_id before starting a new one.
    m_rebuild_tokens.erase(layer_id);
    CancellationToken token;
    m_rebuild_tokens.emplace(layer_id, token);

    project->markLayerIndexing(layer_id);
    emit indexingStarted(layer_id);

    struct Result {
        core::ArtifactIndex index;
        io::FormatMeta      meta;
        std::string         error;
        bool                cancelled = false;
    };

    // Emit indexingProgress from the background thread via invokeMethod so the
    // dialog progress bars animate during a full scan (old files without footer).
    // For modern files with a footer the fast path returns immediately so no
    // progress events are needed — they will just not fire.
    QPointer<ImportService> self(this);
    auto progress_fn = [self, layer_id](float p) {
        const int pct = static_cast<int>(p * 100.f);
        QMetaObject::invokeMethod(self, [self, layer_id, pct]() {
            if (self) Q_EMIT self->indexingProgress(layer_id, pct);
        }, Qt::QueuedConnection);
    };

    auto* watcher = new QFutureWatcher<Result>(this);
    connect(watcher, &QFutureWatcher<Result>::finished, this,
            [this, watcher, project, layer_id, source_id]() {
        watcher->deleteLater();
        m_rebuild_tokens.erase(layer_id);

        const Result r = watcher->result();
        if (r.cancelled) return;  // project switched mid-scan; discard silently

        if (!r.error.empty()) {
            emit indexingFailed(layer_id, r.error);
            return;
        }

        auto* layer = project->findLayer(layer_id);
        if (!layer) {
            emit indexingFailed(layer_id, "rebuildCacheIndex: layer disappeared");
            return;
        }

        // Module-routed layers (e.g. SSS or SBP from a mixed XTF) were
        // imported with a filtered artifact_index. Re-apply that filter so
        // the rebuilt index doesn't suddenly include sibling modality types.
        core::ArtifactIndex resolved = r.index;
        const Modality mod = layer->modality;
        if (mod != Modality::Unknown && mod != Modality::Mixed && !resolved.empty()) {
            const core::ArtifactType wanted = artifactTypeForModality(mod);
            bool mixed = false;
            for (const auto& e : resolved.entries)
                if (e.type != wanted) { mixed = true; break; }
            if (mixed)
                resolved = filteredByType(resolved, wanted);
        }

        layer->artifact_index.source_id =
            resolved.source_id.empty() ? source_id : resolved.source_id;
        layer->artifact_index.entries = resolved.entries;
        layer->index_built = !layer->artifact_index.empty();

        // Back-fill metadata that may be absent from old JSON manifests.
        if (layer->sonar_name.empty() && !r.meta.sonar_name.empty())
            layer->sonar_name = r.meta.sonar_name;
        if (layer->survey_name.empty() && !r.meta.survey_name.empty())
            layer->survey_name = r.meta.survey_name;
        if (layer->vessel_name.empty() && !r.meta.vessel_name.empty())
            layer->vessel_name = r.meta.vessel_name;
        if (layer->modality == Modality::Unknown && !layer->artifact_index.empty())
            layer->modality = inferModality(layer->artifact_index);

        // Back-fill pipeline_applied for old projects that predate the field.
        // metadata() after buildIndex() has correction_flags_seen from the footer.
        if (!layer->pipeline_applied && r.meta.correction_flags_seen != 0)
            layer->pipeline_applied = true;

        project->commitLayer(layer_id);
        emit cacheIndexRebuilt(layer_id);
    });

    watcher->setFuture(QtConcurrent::run([cache_path, token, progress_fn]() -> Result {
        Result r;
        io::ParsedCacheReader reader;
        if (!reader.open(cache_path)) {
            r.error = "Cannot open cache: " + cache_path;
            return r;
        }
        r.index = reader.buildIndex(progress_fn, token.flag());
        if (token.isCancelled()) {
            r.cancelled = true;
            return r;
        }
        // metadata() after buildIndex() includes correction_flags_seen from footer.
        r.meta = reader.metadata();
        if (r.index.empty())
            r.error = "Cache index is empty: " + cache_path;
        return r;
    }));
}

void ImportService::cancelPendingRebuild()
{
    for (auto& [id, tok] : m_rebuild_tokens)
        tok.cancel();
    m_rebuild_tokens.clear();
}

} // namespace dolphin::app
