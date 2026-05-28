#pragma once

#include "app/workers/WorkerAdapter.h"

namespace dolphin::app::workers {

class QcWorkerAdapter : public IWorkerAdapter {
public:
    std::string adapterId() const override { return "qc"; }

    pipeline::ArtifactBuffer buildSource(
        const std::vector<contracts::ContractEnvelope>& inputs,
        const WorkerExecutionContext& context) const override;

    std::vector<contracts::ContractEnvelope> collectOutputs(
        const pipeline::ArtifactBuffer& graph_output,
        const pipeline::GraphJob& job,
        const WorkerExecutionContext& context) const override;
};

} // namespace dolphin::app::workers
