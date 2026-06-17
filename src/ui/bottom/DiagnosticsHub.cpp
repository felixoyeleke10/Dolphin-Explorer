#include "ui/bottom/DiagnosticsHub.h"

namespace dolphin::ui {

DiagnosticsHub::DiagnosticsHub(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<DiagnosticsHub::Problem>();
}

// -- Problems ------------------------------------------------------------------

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

// -- Output log ----------------------------------------------------------------

void DiagnosticsHub::logOutput(const QString& msg)
{
    if (m_output.size() >= kMaxOutputEntries)
        m_output.removeFirst();
    m_output.append({msg, QDateTime::currentDateTime()});
    emit outputLogged(msg);
}

// -- Batches -------------------------------------------------------------------

uint32_t DiagnosticsHub::beginBatch(const QString& name, int total)
{
    Batch b;
    b.id      = m_next_id++;
    b.name    = name;
    b.total   = total;
    b.started = QDateTime::currentDateTime();
    m_batches.prepend(b);
    emit batchChanged(b.id);
    return b.id;
}

void DiagnosticsHub::updateBatch(uint32_t batch_id, int done, int succeeded)
{
    if (auto* b = findBatchMut(batch_id)) {
        b->done      = done;
        b->succeeded = succeeded;
        emit batchChanged(batch_id);
    }
}

void DiagnosticsHub::finishBatch(uint32_t batch_id, int succeeded, int total)
{
    if (auto* b = findBatchMut(batch_id)) {
        b->done      = total;
        b->succeeded = succeeded;
        b->state     = (succeeded == total) ? BatchState::Completed
                     : (succeeded > 0)      ? BatchState::CompletedWithErrors
                     :                        BatchState::Failed;
        b->ended     = QDateTime::currentDateTime();
        emit batchChanged(batch_id);
    }
}

void DiagnosticsHub::failBatch(uint32_t batch_id, const QString& error)
{
    if (auto* b = findBatchMut(batch_id)) {
        b->state = BatchState::Failed;
        b->ended = QDateTime::currentDateTime();
        if (!error.isEmpty()) b->name = error;
        emit batchChanged(batch_id);
    }
}

void DiagnosticsHub::cancelBatch(uint32_t batch_id)
{
    if (auto* b = findBatchMut(batch_id)) {
        b->state = BatchState::Cancelled;
        b->ended = QDateTime::currentDateTime();
        emit batchChanged(batch_id);
    }
}

int DiagnosticsHub::activeBatchCount() const
{
    int n = 0;
    for (const auto& b : m_batches)
        if (b.state == BatchState::Running) ++n;
    return n;
}

const DiagnosticsHub::Batch* DiagnosticsHub::findBatch(uint32_t id) const
{
    for (const auto& b : m_batches)
        if (b.id == id) return &b;
    return nullptr;
}

DiagnosticsHub::Batch* DiagnosticsHub::findBatchMut(uint32_t id)
{
    for (auto& b : m_batches)
        if (b.id == id) return &b;
    return nullptr;
}

// -- Jobs ----------------------------------------------------------------------

uint32_t DiagnosticsHub::beginJob(const QString& name, const QString& layer_id,
                                   uint32_t batch_id, const QString& format_badge,
                                   float size_mb, JobStatus initial)
{
    Job j;
    j.id           = m_next_id++;
    j.name         = name;
    j.layer_id     = layer_id;
    j.batch_id     = batch_id;
    j.format_badge = format_badge;
    j.size_mb      = size_mb;
    j.status       = initial;
    // For a Running job this is the start time; for a Queued job it is the enqueue
    // time, refreshed to the real start time by startJob().
    j.started      = QDateTime::currentDateTime();
    m_jobs.prepend(j);  // newest first
    pruneJobs();
    emit jobChanged(j.id);
    return j.id;
}

void DiagnosticsHub::startJob(uint32_t id)
{
    if (auto* j = findJob(id)) {
        if (j->status != JobStatus::Queued) return;
        j->status  = JobStatus::Running;
        j->started = QDateTime::currentDateTime();
        emit jobChanged(id);
    }
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
        if (j.status == JobStatus::Running || j.status == JobStatus::Queued) ++n;
    return n;
}

DiagnosticsHub::Job* DiagnosticsHub::findJob(uint32_t id)
{
    for (auto& j : m_jobs)
        if (j.id == id) return &j;
    return nullptr;
}

void DiagnosticsHub::pruneJobs()
{
    int over = static_cast<int>(m_jobs.size()) - kMaxJobs;
    if (over <= 0) return;
    // Remove oldest finished jobs (from the back; the list is newest-first). Keep
    // Running/Queued jobs regardless so in-flight work is never dropped.
    for (int i = static_cast<int>(m_jobs.size()) - 1; i >= 0 && over > 0; --i) {
        const auto s = m_jobs[i].status;
        if (s == JobStatus::Completed || s == JobStatus::Failed
                || s == JobStatus::Cancelled) {
            m_jobs.removeAt(i);
            --over;
        }
    }
}

} // namespace dolphin::ui
