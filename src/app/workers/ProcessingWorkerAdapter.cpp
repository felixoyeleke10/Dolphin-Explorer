#include "app/workers/ProcessingWorkerAdapter.h"
#include "app/artifacts/ArtifactSidecar.h"
#include "app/project/Project.h"
#include "app/layers/DataLayer.h"
#include "app/contracts/ContractEnvelope.h"
#include "app/contracts/ContractTypes.h"
#include "io/cache/ParsedCache.h"

namespace dolphin::app::workers {

pipeline::ArtifactBuffer ProcessingWorkerAdapter::buildSource(
    const std::vector<contracts::ContractEnvelope>& /*inputs*/,
    const WorkerExecutionContext& context) const
{
    if (!context.project || !context.worker)
        return {};

    const auto* layer = context.project->findLayer(context.worker->id);
    if (!layer || !layer->index_built || layer->artifact_store_path.empty())
        return {};

    io::ParsedCacheReader reader;
    if (!reader.open(layer->artifact_store_path))
        return {};

    pipeline::ArtifactBuffer buffer;
    for (const auto& entry : layer->artifact_index.entries) {
        if (auto art = reader.readArtifact(entry))
            buffer.push_back(std::move(*art));
    }
    return buffer;
}

std::vector<contracts::ContractEnvelope> ProcessingWorkerAdapter::collectOutputs(
    const pipeline::ArtifactBuffer& graph_output,
    const pipeline::GraphJob& /*job*/,
    const WorkerExecutionContext& context) const
{
    if (!context.project || !context.worker || graph_output.empty())
        return {};

    auto* layer = context.project->findLayer(context.worker->id);
    if (!layer)
        return {};

    const std::string base_path = layer->artifact_store_path;

    io::FormatMeta meta;
    {
        io::ParsedCacheReader mr;
        if (mr.open(base_path)) meta = mr.metadata();
    }

    // Always write to a per-layer sidecar — never overwrite the original parsed
    // store (D-04). "Already our sidecar" = formal role marker (preferred) or the
    // legacy "_<layerId>" filename suffix; re-runs overwrite that sidecar in place.
    const std::string write_path = dolphin::app::sidecarArtifactPath(
        base_path, context.worker->id, meta.artifact_role);
    meta.artifact_role = io::kArtifactRoleSidecar;  // formal marker on the output

    core::ArtifactIndex out_index;
    if (!io::writeArtifactBufferToCache(write_path, graph_output, meta, out_index))
        return {};

    out_index.source_id = layer->artifact_index.source_id;

    contracts::ContractEnvelope env;
    env.id                 = context.worker->id + "_proc";
    env.binding_key        = context.worker->id;
    env.type               = contracts::ContractType::ProcessedLayer;
    env.producer_worker_id = context.worker->id;

    contracts::ProcessedLayer pl;
    pl.layer_id              = context.worker->id;
    pl.source_id             = layer->source_id;
    pl.artifact_store_path   = write_path;
    pl.artifact_store_format = "dlpd";
    pl.artifact_index        = std::move(out_index);
    pl.modality              = layer->modality;
    env.payload = std::move(pl);
    return {std::move(env)};
}

} // namespace dolphin::app::workers
