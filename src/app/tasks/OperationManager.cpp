#include "app/tasks/OperationManager.h"

#include <vector>

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

void OperationManager::cancelByKey(const std::string& key)
{
    const auto it = m_keyed.find(key);
    if (it != m_keyed.end()) cancel(it->second);
}

void OperationManager::cancelByPrefix(const std::string& prefix)
{
    // Collect first — cancel() leaves m_keyed intact (the watcher erases it later),
    // but collecting keeps this robust regardless of cancel()'s internals.
    std::vector<uint32_t> ids;
    for (const auto& [k, id] : m_keyed)
        if (k.rfind(prefix, 0) == 0) ids.push_back(id);
    for (uint32_t id : ids) cancel(id);
}

void OperationManager::cancelAll()
{
    for (auto& [id, entry] : m_entries)
        entry.token.cancel();
    for (auto& [name, tok] : m_external)
        tok.cancel();
    m_external.clear();
    // Queued (never-launched) ops have no watcher to clean them up, so drop them
    // here. Running ops keep their m_entries record — their watchers still fire
    // and clean up via finishOp.
    for (auto& [ln, L] : m_lanes) {
        for (auto& p : L.queue) {
            m_entries.erase(p.op_id);
            if (!p.key.empty()) {
                const auto it = m_keyed.find(p.key);
                if (it != m_keyed.end() && it->second == p.op_id) m_keyed.erase(it);
            }
            emit operationCancelled(p.op_id);
            if (p.on_finally) p.on_finally();
        }
        L.queue.clear();
        // Do NOT reset L.running: already-launched ops keep their watchers and each
        // decrements via finishOp when it finishes (even when cancelled). Zeroing it
        // would let a freshly-submitted op exceed the cap while cancelled-but-still-
        // running jobs drain.
    }
}

void OperationManager::finishOp(uint32_t op_id, const std::string& key, const std::string& lane)
{
    m_entries.erase(op_id);
    if (!key.empty()) {
        const auto it = m_keyed.find(key);
        if (it != m_keyed.end() && it->second == op_id) m_keyed.erase(it);
    }
    if (!lane.empty()) {
        Lane& L = laneFor(lane);
        if (L.running > 0) --L.running;
        pumpLane(lane);
    }
}

void OperationManager::pumpLane(const std::string& lane)
{
    Lane& L = laneFor(lane);
    while (L.running < L.cap && !L.queue.empty()) {
        Pending p = std::move(L.queue.front());
        L.queue.pop_front();
        if (p.token.isCancelled()) {
            // Superseded / cancelled while waiting — never launch it.
            m_entries.erase(p.op_id);
            if (!p.key.empty()) {
                const auto it = m_keyed.find(p.key);
                if (it != m_keyed.end() && it->second == p.op_id) m_keyed.erase(it);
            }
            emit operationCancelled(p.op_id);
            if (p.on_finally) p.on_finally();
            continue;
        }
        ++L.running;
        // Now actually launching — emit started (it was reported as queued before).
        emit operationStarted(p.op_id, p.name);
        p.start();
    }
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
