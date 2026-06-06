#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "app/workers/WorkerTypes.h"
#include "pipeline/GraphJob.h"

namespace dolphin::app::execution {

struct WorkerRunRecord {
    std::string                   worker_id;
    workers::WorkerStatus         status = workers::WorkerStatus::Idle;
    bool                          cached = false;
    int64_t                       started_utc_ms = 0;
    int64_t                       ended_utc_ms = 0;
    std::string                   input_hash;
    std::string                   graph_hash;
    std::string                   output_hash;
    std::vector<std::string>      output_contract_ids;
    std::string                   error;
    std::optional<pipeline::GraphJob> graph_job;

    int64_t durationMs() const { return ended_utc_ms - started_utc_ms; }
};

} // namespace dolphin::app::execution
