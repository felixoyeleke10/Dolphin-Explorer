#pragma once
#include "app/tasks/CancellationToken.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace dolphin::app {

// Lightweight registry of named background tasks (UI-thread only).
// Provides a single place to query how many tasks are in-flight and cancel them all.
class TaskRegistry {
public:
    // Register an active task. Replaces any previous entry with the same name.
    void registerTask(std::string name, CancellationToken token) {
        m_tasks.insert_or_assign(std::move(name), std::move(token));
    }

    // Remove a completed task by name.
    void completeTask(const std::string& name) {
        m_tasks.erase(name);
    }

    // Cancel and clear all registered tasks.
    void cancelAll() {
        for (auto& [name, token] : m_tasks)
            token.cancel();
        m_tasks.clear();
    }

    int activeCount() const { return static_cast<int>(m_tasks.size()); }

    std::vector<std::string> activeNames() const {
        std::vector<std::string> out;
        out.reserve(m_tasks.size());
        for (const auto& [k, v] : m_tasks)
            out.push_back(k);
        return out;
    }

private:
    std::unordered_map<std::string, CancellationToken> m_tasks;
};

} // namespace dolphin::app
