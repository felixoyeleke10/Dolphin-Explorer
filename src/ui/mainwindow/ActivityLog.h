#pragma once
#include <QString>
#include <QDateTime>
#include <vector>

namespace dolphin::ui {

enum class ActivityKind {
    Import        = 0,
    Processing    = 1,
    Palette       = 2,
    DisplayParams = 3,
    NavCorrection = 4,
    Visibility    = 5,
    CrsChange     = 6,
    TagChange     = 7,
    GroupChange   = 8,
    Export        = 9,
    ContactPick   = 10,
};

struct ActivityEntry {
    ActivityKind kind;
    QString      description;
    QDateTime    timestamp;
};

// Lightweight in-process activity journal; not a QObject.
// MainWindow owns one and exposes recordActivity() as a thin facade so
// aspect files can call it without knowing about ActivityLog directly.
class ActivityLog {
public:
    static constexpr int kMaxEntries = 1000;

    void record(ActivityKind kind, const QString& description)
    {
        m_entries.insert(m_entries.begin(),
            {kind, description, QDateTime::currentDateTime()});
        if (static_cast<int>(m_entries.size()) > kMaxEntries)
            m_entries.resize(kMaxEntries);
    }

    const std::vector<ActivityEntry>& entries() const { return m_entries; }
    bool empty() const { return m_entries.empty(); }
    void clear()       { m_entries.clear(); }

private:
    std::vector<ActivityEntry> m_entries;
};

} // namespace dolphin::ui
