#include "app/workers/Worker.h"

namespace dolphin::app::workers {

namespace {

std::string defaultLabelFromId(const std::string& id)
{
    return id.empty() ? "Worker" : id;
}

} // namespace

std::string workerStatusName(WorkerStatus status)
{
    switch (status) {
    case WorkerStatus::Idle:      return "idle";
    case WorkerStatus::Dirty:     return "dirty";
    case WorkerStatus::Running:   return "running";
    case WorkerStatus::Cached:    return "cached";
    case WorkerStatus::Completed: return "completed";
    case WorkerStatus::Failed:    return "failed";
    case WorkerStatus::Blocked:   return "blocked";
    }
    return "unknown";
}

Worker::Worker() = default;

Worker::Worker(std::string worker_id, std::string worker_label)
    : id(std::move(worker_id))
    , label(worker_label.empty() ? defaultLabelFromId(id) : std::move(worker_label))
{
}

bool Worker::isRunnable() const
{
    return status != WorkerStatus::Running && status != WorkerStatus::Blocked;
}

void Worker::markDirty()
{
    status = WorkerStatus::Dirty;
}

void Worker::markCompleted()
{
    status = WorkerStatus::Completed;
}

} // namespace dolphin::app::workers
