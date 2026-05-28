#include "ui/bottom/DiagnosticsHub.h"

namespace dolphin::ui {

DiagnosticsHub::DiagnosticsHub(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<DiagnosticsHub::Problem>();
}

// ── Problems ──────────────────────────────────────────────────────────────────

void DiagnosticsHub::postProblem(const QString& msg, Severity sev,
                                 const QString& layer_id)
{
    Problem p;
    p.severity  = sev;
    p.message   = msg;
    p.layer_id  = layer_id;
    p.timestamp = QDateTime::currentDateTime();
    m_problems.append(p);
    emit problemPosted(p);
}

void DiagnosticsHub::clearProblems(const QString& layer_id)
{
    if (layer_id.isEmpty()) {
        m_problems.clear();
    } else {
        m_problems.erase(
            std::remove_if(m_problems.begin(), m_problems.end(),
                [&](const Problem& p) { return p.layer_id == layer_id; }),
            m_problems.end());
    }
    emit problemsCleared(layer_id);
}

int DiagnosticsHub::errorCount() const
{
    int n = 0;
    for (const auto& p : m_problems)
        if (p.severity == Severity::Error) ++n;
    return n;
}

// ── Output log ────────────────────────────────────────────────────────────────

void DiagnosticsHub::logOutput(const QString& msg)
{
    if (m_output.size() >= kMaxOutputEntries)
        m_output.removeFirst();
    m_output.append({msg, QDateTime::currentDateTime()});
    emit outputLogged(msg);
}

// ── Jobs ──────────────────────────────────────────────────────────────────────

uint32_t DiagnosticsHub::beginJob(const QString& name, const QString& layer_id)
{
    Job j;
    j.id       = m_next_id++;
    j.name     = name;
    j.layer_id = layer_id;
    j.status   = JobStatus::Running;
    j.started  = QDateTime::currentDateTime();
    m_jobs.prepend(j);  // newest first
    emit jobChanged(j.id);
    return j.id;
}

void DiagnosticsHub::updateJob(uint32_t id, const QString& detail, float progress)
{
    if (auto* j = findJob(id)) {
        j->detail   = detail;
        j->progress = progress;
        emit jobChanged(id);
    }
}

void DiagnosticsHub::endJob(uint32_t id, const QString& summary)
{
    if (auto* j = findJob(id)) {
        j->status = JobStatus::Completed;
        j->detail = summary;
        j->ended  = QDateTime::currentDateTime();
        emit jobChanged(id);
    }
}

void DiagnosticsHub::failJob(uint32_t id, const QString& error)
{
    if (auto* j = findJob(id)) {
        j->status = JobStatus::Failed;
        j->detail = error;
        j->ended  = QDateTime::currentDateTime();
        emit jobChanged(id);
    }
}

void DiagnosticsHub::cancelJob(uint32_t id)
{
    if (auto* j = findJob(id)) {
        j->status = JobStatus::Cancelled;
        j->ended  = QDateTime::currentDateTime();
        emit jobChanged(id);
    }
}

int DiagnosticsHub::activeJobCount() const
{
    int n = 0;
    for (const auto& j : m_jobs)
        if (j.status == JobStatus::Running) ++n;
    return n;
}

DiagnosticsHub::Job* DiagnosticsHub::findJob(uint32_t id)
{
    for (auto& j : m_jobs)
        if (j.id == id) return &j;
    return nullptr;
}

} // namespace dolphin::ui
