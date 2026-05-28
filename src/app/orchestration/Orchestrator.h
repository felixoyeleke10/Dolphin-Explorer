#pragma once

#include <string>
#include <vector>

#include "app/orchestration/WorkerRunRecord.h"

namespace dolphin::app {
class Project;
}

namespace dolphin::app::workers {
class Worker;
}

namespace dolphin::app::orchestration {

class Orchestrator {
public:
    std::vector<WorkerRunRecord> run(Project& project,
                                     const std::vector<std::string>& requested_worker_ids);

    std::vector<workers::Worker*> buildExecutionPlan(
        Project& project,
        const std::vector<std::string>& requested_worker_ids) const;

    void markDirty(Project& project) const;
    bool dependenciesSatisfied(const Project& project,
                               const workers::Worker& worker) const;

private:
    WorkerRunRecord executeWorker(Project& project, workers::Worker& worker) const;
};

} // namespace dolphin::app::orchestration
