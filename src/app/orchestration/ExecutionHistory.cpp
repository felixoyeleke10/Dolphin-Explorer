#include "app/orchestration/ExecutionHistory.h"

namespace dolphin::app::orchestration {

void ExecutionHistory::record(WorkerRunRecord record)
{
    m_records.push_back(std::move(record));
}

std::vector<WorkerRunRecord> ExecutionHistory::forWorker(const std::string& worker_id) const
{
    std::vector<WorkerRunRecord> result;
    for (const auto& record : m_records) {
        if (record.worker_id == worker_id)
            result.push_back(record);
    }
    return result;
}

void ExecutionHistory::clear()
{
    m_records.clear();
}

} // namespace dolphin::app::orchestration
