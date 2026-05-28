#pragma once

#include <memory>
#include <string>
#include <vector>

#include "app/contracts/ContractEnvelope.h"
#include "pipeline/GraphJob.h"
#include "pipeline/IProcessingNode.h"

namespace dolphin::app {
class Project;
namespace workers { class Worker; }
}

namespace dolphin::app::workers {

struct WorkerExecutionContext {
    Project* project = nullptr;
    Worker*  worker = nullptr;
};

class IWorkerAdapter {
public:
    virtual ~IWorkerAdapter() = default;

    virtual std::string adapterId() const = 0;

    virtual pipeline::ArtifactBuffer buildSource(
        const std::vector<contracts::ContractEnvelope>& inputs,
        const WorkerExecutionContext& context) const = 0;

    virtual std::vector<contracts::ContractEnvelope> collectOutputs(
        const pipeline::ArtifactBuffer& graph_output,
        const pipeline::GraphJob& job,
        const WorkerExecutionContext& context) const = 0;
};

using WorkerAdapterPtr = std::shared_ptr<IWorkerAdapter>;

} // namespace dolphin::app::workers
