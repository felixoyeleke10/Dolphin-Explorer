#include "app/workers/ReportWorkerAdapter.h"

namespace dolphin::app::workers {

pipeline::ArtifactBuffer ReportWorkerAdapter::buildSource(
    const std::vector<contracts::ContractEnvelope>& /*inputs*/,
    const WorkerExecutionContext& /*context*/) const
{
    return {};
}

std::vector<contracts::ContractEnvelope> ReportWorkerAdapter::collectOutputs(
    const pipeline::ArtifactBuffer& /*graph_output*/,
    const pipeline::GraphJob& /*job*/,
    const WorkerExecutionContext& /*context*/) const
{
    return {};
}

} // namespace dolphin::app::workers
