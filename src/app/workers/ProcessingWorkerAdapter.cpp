#include "app/workers/ProcessingWorkerAdapter.h"

namespace dolphin::app::workers {

pipeline::ArtifactBuffer ProcessingWorkerAdapter::buildSource(
    const std::vector<contracts::ContractEnvelope>& /*inputs*/,
    const WorkerExecutionContext& /*context*/) const
{
    return {};
}

std::vector<contracts::ContractEnvelope> ProcessingWorkerAdapter::collectOutputs(
    const pipeline::ArtifactBuffer& /*graph_output*/,
    const pipeline::GraphJob& /*job*/,
    const WorkerExecutionContext& /*context*/) const
{
    return {};
}

} // namespace dolphin::app::workers
