#include "app/project/ProjectRecovery.h"

#include "app/project/Project.h"

#include <unordered_set>
#include <utility>

namespace dolphin::app {

std::vector<MissingArtifactRecovery>
planMissingArtifactRecovery(Project& project)
{
    std::vector<MissingArtifactRecovery> result;
    std::unordered_set<std::string> queued_sources;

    for (const auto& layer : project.layers()) {
        if (!layer) continue;
        if (layer->index_built && !layer->artifact_index.empty()) continue;

        const auto* source = project.findSource(layer->source_id);
        if (!source || source->path.empty()) continue;
        if (!queued_sources.insert(layer->source_id).second) continue;

        core::SpatialRef crs;
        if (source->source_spatial_ref.exact) {
            crs = source->source_spatial_ref;
        } else {
            for (const auto* sibling : project.findLayersBySource(layer->source_id)) {
                if (sibling && sibling->source_spatial_ref.exact) {
                    crs = sibling->source_spatial_ref;
                    break;
                }
            }
        }

        result.push_back({source->path, layer->id, layer->label, std::move(crs)});
    }
    return result;
}

} // namespace dolphin::app
