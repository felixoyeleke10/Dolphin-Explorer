#pragma once

#include <memory>
#include <string>
#include <vector>

#include "app/workers/WorkerTypes.h"
#include "pipeline/NodeGraph.h"

namespace dolphin::app::workers {

class IWorkerAdapter;

class Worker {
public:
    Worker();
    explicit Worker(std::string worker_id, std::string worker_label = {});

    bool isRunnable() const;
    void markDirty();
    void markCompleted();

    std::string               id;
    std::string               label;
    WorkerKind                kind = WorkerKind::Custom;
    pipeline::NodeGraph       graph;
    std::vector<WorkerPort>   inputs;
    std::vector<WorkerPort>   outputs;
    std::vector<std::string>  depends_on;
    ExecutionPolicy           policy;
    WorkerStatus              status = WorkerStatus::Idle;
    std::shared_ptr<IWorkerAdapter> adapter;

    std::string last_input_hash;
    std::string last_graph_hash;
    std::string last_output_hash;
};

} // namespace dolphin::app::workers
