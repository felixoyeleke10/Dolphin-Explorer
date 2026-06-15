#include "app/workers/ProcessingWorkerAdapter.h"
#include "app/project/Project.h"
#include "app/layers/DataLayer.h"
#include "app/contracts/ContractEnvelope.h"
#include "app/contracts/ContractTypes.h"
#include "io/cache/ParsedCache.h"

#include <filesystem>

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
    std::string write_path = base_path;
    {
        io::ParsedCacheReader check;
        if (check.open(base_path)) {
            const auto fi = check.buildIndex();
            if (layer->artifact_index.entries.size() < fi.entries.size()) {
                namespace fs = std::filesystem;
                const fs::path p(base_path);
                write_path = (p.parent_path()
                    / (p.stem().string() + "_" + context.worker->id + ".dlpd")).string();
            }
        }
    }

    io::FormatMeta meta;
    {
        io::ParsedCacheReader mr;
        if (mr.open(base_path)) meta = mr.metadata();
    }

    core::ArtifactIndex out_index;
    if (!io::writeArtifactBufferToCache(write_path, graph_output, meta, out_index))
        return {};

    out_index.source_id = layer->artifact_index.source_id;

    contracts::ContractEnvelope env;
    env.id                 = context.worker->id + "_proc";
    env.binding_key        = context.worker->id;
    env.type               = contracts::ContractType::ProcessedSidescanLayer;
    env.producer_worker_id = context.worker->id;

    contracts::ProcessedSidescanLayer pl;
    pl.layer_id              = context.worker->id;
    pl.source_id             = layer->source_id;
    pl.artifact_store_path   = write_path;
    pl.artifact_store_format = "dlpd";
    pl.artifact_index        = std::move(out_index);
    env.payload = std::move(pl);
    return {std::move(env)};
}

} // namespace dolphin::app::workers
