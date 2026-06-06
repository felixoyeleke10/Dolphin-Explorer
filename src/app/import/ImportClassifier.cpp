#include "app/import/ImportClassifier.h"
#include "app/project/Project.h"
#include "app/layers/DataLayer.h"
#include "io/cache/ParsedCache.h"
#include <QString>

namespace dolphin::app {

namespace {

std::string normaliseFormat(std::string fmt)
{
    for (auto& c : fmt)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return fmt;
}

bool cacheIsValid(const DataLayer* layer)
{
    if (!layer || layer->artifact_store_path.empty()) return false;
    const std::string fmt = normaliseFormat(layer->artifact_store_format);
    if (fmt != "dlpd" && fmt != "dpcache") return false;
    return io::parsedCacheIsValid(layer->artifact_store_path);
}

// Among layers that share a source, pick the "best" one to reuse or rebuild:
// prefer processed (pipeline_applied) over raw, and indexed over placeholder.
const DataLayer* bestLayer(const std::vector<DataLayer*>& layers)
{
    const DataLayer* best = nullptr;
    for (const auto* l : layers) {
        if (!l) continue;
        if (!best) { best = l; continue; }
        // Prefer layers that have already been through the pipeline.
        if (l->pipeline_applied && !best->pipeline_applied) { best = l; continue; }
        if (!l->pipeline_applied && best->pipeline_applied) continue;
        // Among equals, prefer the one with a built index.
        if (l->index_built && !best->index_built) best = l;
    }
    return best;
}

} // namespace

FileImportAction classifyImportAction(const QString& path, const Project* project)
{
    FileImportAction action;
    action.path = path;
    action.kind = FileImportAction::Kind::ImportNew;

    if (!project) return action;

    auto* source = project->findSourceByPath(path.toStdString());
    if (!source) return action;

    const auto layers = project->findLayersBySource(source->id);
    if (layers.empty()) return action;

    // Check each layer for a valid DLPD cache.
    std::vector<DataLayer*> valid_cache_layers;
    for (auto* l : layers) {
        if (cacheIsValid(l)) valid_cache_layers.push_back(l);
    }

    if (!valid_cache_layers.empty()) {
        const DataLayer* chosen = bestLayer(valid_cache_layers);
        action.kind               = FileImportAction::Kind::ReuseExisting;
        action.existing_layer_id  = chosen->id;
        action.existing_source_id = source->id;
    } else {
        // Source known but cache missing or stale — re-parse required.
        const DataLayer* chosen = bestLayer(layers);
        action.kind               = FileImportAction::Kind::RebuildExisting;
        action.existing_layer_id  = chosen ? chosen->id : std::string{};
        action.existing_source_id = source->id;
    }
    return action;
}

} // namespace dolphin::app
