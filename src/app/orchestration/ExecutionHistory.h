#pragma once

#include <string>
#include <vector>

#include "app/orchestration/WorkerRunRecord.h"

namespace dolphin::app::orchestration {

class ExecutionHistory {
public:
    void record(WorkerRunRecord record);
    const std::vector<WorkerRunRecord>& all() const { return m_records; }
    std::vector<WorkerRunRecord> forWorker(const std::string& worker_id) const;
    void clear();

private:
    std::vector<WorkerRunRecord> m_records;
};

} // namespace dolphin::app::orchestration
