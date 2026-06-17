#pragma once

#include <string>

#include "app/contracts/ContractTypes.h"

namespace dolphin::app::workers {

enum class WorkerKind {
    Processing,
    QC,
    Reporting,
    Custom,
};

enum class WorkerStatus {
    Idle,
    Dirty,
    Running,
    Cached,
    Completed,
    Failed,
    Blocked,
};

struct WorkerPort {
    std::string              name;
    contracts::ContractType  type = contracts::ContractType::ProcessedLayer;
    std::string              binding_key;
    bool                     required = true;
};

struct ExecutionPolicy {
    bool allow_parallel = true;
    bool cache_enabled = true;
    bool run_on_dirty_only = true;
    int  priority = 0;
};

std::string workerStatusName(WorkerStatus status);

} // namespace dolphin::app::workers
