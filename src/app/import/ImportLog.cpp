#include "app/import/ImportLog.h"
#include <QStringList>

namespace dolphin::app {

// Registered instance for crash-trace access. Written from main thread only;
// read from terminate handler — atomic for safe cross-thread visibility.
static std::atomic<const ImportLog*> g_crash_log{nullptr};

static const char* eventLabel(ImportLogEntry::Event e)
{
    switch (e) {
    case ImportLogEntry::Event::Dispatched:  return "DISPATCH";
    case ImportLogEntry::Event::Reused:      return "REUSED  ";
    case ImportLogEntry::Event::Started:     return "STARTED ";
    case ImportLogEntry::Event::Progress:    return "PROGRESS";
    case ImportLogEntry::Event::Completed:   return "COMPLETE";
    case ImportLogEntry::Event::Failed:      return "FAILED  ";
    case ImportLogEntry::Event::Cancelled:   return "CANCEL  ";
    case ImportLogEntry::Event::BatchDone:   return "BATCH_OK";
    case ImportLogEntry::Event::Suppressed:  return "SUPPRESS";
    }
    return "?       ";
}

void ImportLog::record(ImportLogEntry entry)
{
    entry.when = std::chrono::steady_clock::now();
    m_buf[m_write % kCapacity] = std::move(entry);
    ++m_write;
}

QString ImportLog::dump() const
{
    const std::size_t count = std::min(m_write, kCapacity);
    if (count == 0)
        return QStringLiteral("ImportLog: no events recorded.");

    QStringList lines;
    lines.reserve(static_cast<int>(count) + 1);
    lines << QStringLiteral("ImportLog — last %1 event(s):").arg(count);

    const auto now = std::chrono::steady_clock::now();
    const std::size_t start = (m_write > kCapacity) ? (m_write % kCapacity) : 0;

    for (std::size_t i = 0; i < count; ++i) {
        const ImportLogEntry& e = m_buf[(start + i) % kCapacity];

        const auto age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - e.when).count();

        QString line = QStringLiteral("[-%1ms] %2 epoch=%3/%4 q=%5")
            .arg(age_ms)
            .arg(QLatin1String(eventLabel(e.event)))
            .arg(e.active_job_epoch)
            .arg(e.epoch)
            .arg(e.queue_depth);

        if (!e.layer_id.empty())
            line += QStringLiteral(" layer=%1").arg(QString::fromStdString(e.layer_id));
        if (e.event == ImportLogEntry::Event::Progress)
            line += QStringLiteral(" %1%%").arg(e.progress_pct);
        if (!e.detail.empty())
            line += QStringLiteral(" \"%1\"").arg(QString::fromStdString(e.detail));

        lines << line;
    }
    return lines.join(QLatin1Char('\n'));
}

void ImportLog::registerForCrashTrace(const ImportLog* log) noexcept
{
    g_crash_log.store(log, std::memory_order_relaxed);
}

void ImportLog::dumpCrashTrace(FILE* file) noexcept
{
    const ImportLog* log = g_crash_log.load(std::memory_order_relaxed);
    if (!log || !file) return;

    const std::size_t count = std::min(log->m_write, kCapacity);
    fprintf(file, "=== ImportLog crash trace — %zu event(s) ===\n", count);

    const auto now = std::chrono::steady_clock::now();
    const std::size_t start = (log->m_write > kCapacity) ? (log->m_write % kCapacity) : 0;

    for (std::size_t i = 0; i < count; ++i) {
        const ImportLogEntry& e = log->m_buf[(start + i) % kCapacity];
        const long long age_ms = static_cast<long long>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now - e.when).count());

        fprintf(file, "[-%lldms] %s epoch=%u/%u q=%u",
                age_ms, eventLabel(e.event),
                e.active_job_epoch, e.epoch, e.queue_depth);

        if (!e.layer_id.empty())
            fprintf(file, " layer=%s", e.layer_id.c_str());
        if (e.event == ImportLogEntry::Event::Progress)
            fprintf(file, " %u%%", static_cast<unsigned>(e.progress_pct));
        if (!e.detail.empty())
            fprintf(file, " \"%s\"", e.detail.c_str());

        fprintf(file, "\n");
    }
    fflush(file);
}

} // namespace dolphin::app
