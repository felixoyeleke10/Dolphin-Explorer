#pragma once
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
#include <cstdint>

namespace dolphin::ui {

// Central in-process bus for Problems, Output log, and Job lifecycle events.
// All methods must be called from the main thread.
// Consumers connect to signals; producers call the write methods directly.
class DiagnosticsHub : public QObject {
    Q_OBJECT
public:
    explicit DiagnosticsHub(QObject* parent = nullptr);

    // -- Severity ---------------------------���------------------------���---------
    enum class Severity { Info, Warning, Error };

    // -- Problems --------------------------------------------------------------
    struct Problem {
        Severity  severity   = Severity::Warning;
        QString   message;
        QString   layer_id;   // empty = global
        QDateTime timestamp;
    };

    void postProblem(const QString& msg,
                     Severity       sev      = Severity::Warning,
                     const QString& layer_id = {});

    // Remove all problems for a layer (or all if layer_id is empty).
    void clearProblems(const QString& layer_id = {});

    const QList<Problem>& problems() const { return m_problems; }
    int totalProblemCount() const { return m_problems.size(); }
    int errorCount() const;

    // -- Output log ------------------------------------------------------------
    struct OutputEntry {
        QString   msg;
        QDateTime timestamp;
    };

    void logOutput(const QString& msg);
    const QList<OutputEntry>& outputs() const { return m_output; }

    // -- Jobs ------------------------------------------------------------------
    enum class JobStatus { Running, Completed, Failed, Cancelled };

    struct Job {
        uint32_t  id         = 0;
        QString   name;
        QString   layer_id;
        JobStatus status     = JobStatus::Running;
        float     progress   = -1.f;  // -1 = indeterminate
        QString   detail;
        QDateTime started;
        QDateTime ended;
    };

    uint32_t beginJob(const QString& name, const QString& layer_id = {});
    void     updateJob(uint32_t id, const QString& detail, float progress = -1.f);
    void     endJob(uint32_t id, const QString& summary = {});
    void     failJob(uint32_t id, const QString& error = {});
    void     cancelJob(uint32_t id);

    int activeJobCount() const;
    const QList<Job>& jobs() const { return m_jobs; }

signals:
    void problemPosted(const dolphin::ui::DiagnosticsHub::Problem& p);
    void problemsCleared(const QString& layer_id);
    void outputLogged(const QString& msg);
    void jobChanged(uint32_t id);

private:
    Job* findJob(uint32_t id);

    static constexpr int kMaxOutputEntries = 2000;

    QList<Problem>     m_problems;
    QList<OutputEntry> m_output;
    QList<Job>         m_jobs;
    uint32_t           m_next_id = 1;
};

} // namespace dolphin::ui

Q_DECLARE_METATYPE(dolphin::ui::DiagnosticsHub::Problem)
