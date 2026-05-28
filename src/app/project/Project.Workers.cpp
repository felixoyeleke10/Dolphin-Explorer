// Project.Workers.cpp — Worker CRUD methods.
#include "app/project/Project.h"
#include <algorithm>

namespace dolphin::app {

workers::Worker* Project::addWorker(workers::Worker worker)
{
    if (worker.id.empty())    worker.id    = generateId("worker");
    if (worker.label.empty()) worker.label = worker.id;
    m_workers.push_back(std::move(worker));
    emit modified();
    return &m_workers.back();
}

workers::Worker* Project::findWorker(const std::string& id)
{
    for (auto& w : m_workers) if (w.id == id) return &w;
    return nullptr;
}

const workers::Worker* Project::findWorker(const std::string& id) const
{
    for (const auto& w : m_workers) if (w.id == id) return &w;
    return nullptr;
}

void Project::removeWorker(const std::string& id)
{
    auto it = std::remove_if(m_workers.begin(), m_workers.end(),
        [&](const workers::Worker& w){ return w.id == id; });
    if (it == m_workers.end()) return;
    m_workers.erase(it, m_workers.end());
    emit modified();
}

} // namespace dolphin::app
