#pragma once
#include <atomic>
#include <memory>

namespace dolphin::app {

// Shared cancellation handle — copyable, lightweight, thread-safe.
// Copies share the same underlying flag: cancel() on any copy signals all.
// reset() replaces the flag so old copies remain cancelled; they will see
// isCancelled() == true while the new token starts fresh (false).
struct CancellationToken {
    CancellationToken()
        : m_flag(std::make_shared<std::atomic<bool>>(false)) {}

    void cancel()      const { m_flag->store(true,  std::memory_order_relaxed); }
    bool isCancelled() const { return m_flag->load(std::memory_order_relaxed); }

    // Raw flag pointer for passing to code in layers that cannot include this header
    // (e.g. io/ layer passing into ParsedCacheReader::buildIndex).
    const std::atomic<bool>* flag() const { return m_flag.get(); }

    // Swap in a new flag.  All existing copies still hold the old (cancelled) flag.
    void reset() { m_flag = std::make_shared<std::atomic<bool>>(false); }

    CancellationToken(const CancellationToken&)            = default;
    CancellationToken& operator=(const CancellationToken&) = default;

private:
    std::shared_ptr<std::atomic<bool>> m_flag;
};

} // namespace dolphin::app
