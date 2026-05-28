#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <QString>

namespace dolphin::app {

struct ImportLogEntry {
    enum class Event : uint8_t {
        Dispatched,   // dispatchNext() picked a job from the queue
        Reused,       // ReuseExisting — forwarded as jobCompleted with no parse
        Started,      // indexingStarted signal received and forwarded
        Progress,     // progress milestone (0 / 25 / 50 / 75 / 100 %)
        Completed,    // indexingComplete for primary layer, forwarded
        Failed,       // indexingFailed, forwarded
        Cancelled,    // cancelQueue() cleared the queue
        BatchDone,    // batchCompleted emitted
        Suppressed,   // non-progress service event dropped due to stale epoch
    };

    std::chrono::steady_clock::time_point when;   // set by ImportLog::record()
    Event       event            = Event::Dispatched;
    uint8_t     progress_pct     = 0;             // Progress events only
    uint32_t    epoch            = 0;             // m_epoch at record time
    uint32_t    active_job_epoch = 0;             // m_active_job_epoch at record time
    uint16_t    queue_depth      = 0;             // m_queue.size() at record time
    std::string layer_id;
    std::string detail;   // filename for Dispatched/Reused; error for Failed; reason for Cancelled
};

// Fixed-capacity ring buffer. Written only from the main thread; no locking needed.
class ImportLog {
public:
    static constexpr std::size_t kCapacity = 256;

    void    record(ImportLogEntry entry);

    // Human-readable dump using Qt types. Safe on the main thread.
    // Entries are printed oldest-first with milliseconds-ago offsets.
    QString dump() const;

    // Register / deregister this instance for crash-trace access.
    // Call from ImportJobManager ctor/dtor. Thread-safe.
    static void registerForCrashTrace(const ImportLog* log) noexcept;

    // Write a raw crash trace to file without Qt heap allocation.
    // Safe to call from std::terminate handlers and signal handlers.
    static void dumpCrashTrace(FILE* file) noexcept;

private:
    std::array<ImportLogEntry, kCapacity> m_buf{};
    std::size_t m_write = 0;   // raw (not wrapped) counter of total records written
};

} // namespace dolphin::app
