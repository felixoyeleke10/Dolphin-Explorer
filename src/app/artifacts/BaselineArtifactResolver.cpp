#include "app/artifacts/BaselineArtifactResolver.h"

#include "app/layers/DataLayer.h"
#include "io/cache/ParsedCache.h"

namespace dolphin::app {

BaselineArtifact resolveBaselineArtifact(const DataLayer& layer)
{
    BaselineArtifact result;
    const bool separate = !layer.source_artifact_store_path.empty()
        && layer.source_artifact_store_path != layer.artifact_store_path;
    result.path = separate ? layer.source_artifact_store_path
                           : layer.artifact_store_path;
    result.format = separate ? "dlpd" : layer.artifact_store_format;
    if (result.path.empty()) {
        result.error = "Layer has no imported artifact store";
        return result;
    }
    if (!separate) {
        result.index = layer.artifact_index;
    } else {
        io::ParsedCacheReader reader;
        if (!reader.open(result.path)) {
            result.error = "The imported baseline is unavailable or invalid";
            return result;
        }
        result.index = reader.quickIndex();
    }
    // For the currently active artifact, preserve ProcessingService's async
    // failure contract: index_built is the admission check and the worker
    // reports a missing/corrupt store.  A separately opened baseline has no
    // such prior validation, so an empty quick index is a resolver failure.
    if (separate && result.index.empty()) {
        result.error = "The imported baseline index is unavailable or invalid";
        return result;
    }
    result.index.source_id = layer.source_id;
    return result;
}

} // namespace dolphin::app
