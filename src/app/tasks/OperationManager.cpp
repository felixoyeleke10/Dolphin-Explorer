#include "app/tasks/OperationManager.h"

namespace dolphin::app {

OperationManager::OperationManager(QObject* parent) : QObject(parent) {}

OperationManager::~OperationManager()
{
    // Signal all background tasks to stop so they exit early rather than
    // writing results back to a destroyed object.
    cancelAll();
}

void OperationManager::cancel(uint32_t op_id)
{
    const auto it = m_entries.find(op_id);
    if (it == m_entries.end()) return;
    it->second.token.cancel();
    // The watcher's finished() lambda checks isCancelled() and emits
    // operationCancelled when the background work eventually finishes.
}

void OperationManager::cancelAll()
{
    for (auto& [id, entry] : m_entries)
        entry.token.cancel();
    for (auto& [name, tok] : m_external)
        tok.cancel();
    m_external.clear();
    // Don't clear m_entries — watchers still need to fire to clean up.
}

void OperationManager::registerExternal(const std::string& name, CancellationToken token)
{
    m_external.insert_or_assign(name, std::move(token));
}

void OperationManager::unregisterExternal(const std::string& name)
{
    m_external.erase(name);
}

} // namespace dolphin::app
