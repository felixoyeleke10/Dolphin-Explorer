// ImportJobManager.cpp — concurrent import queue; parallelism scales to CPU cores.
#include "app/import/ImportJobManager.h"
#include "app/services/ImportService.h"
#include "app/project/Project.h"
#include <QFileInfo>
#include <QMetaObject>
#include <QSet>
#include <QString>
#include <algorithm>

namespace dolphin::app {

int ImportJobManager::maxConcurrentJobs()
{
    // D-14: each parse/decode is heavy in both CPU and disk bandwidth. Two
    // concurrent jobs preserve headroom for the map, viewers, and UI thread.
    return 2;
}

int ImportJobManager::effectiveQueuedJobCount(
    const QList<FileImportAction>& actions)
{
    QSet<QString> seen_paths;
    int count = 0;
    for (const auto& action : actions) {
        if (!action.path.isEmpty()) {
            const QString norm = QFileInfo(action.path).canonicalFilePath();
            const QString key = norm.isEmpty() ? action.path : norm;
            if (seen_paths.contains(key)) continue;
            seen_paths.insert(key);
        }
        if (action.kind == FileImportAction::Kind::Skip
            || action.kind == FileImportAction::Kind::ReuseExisting) {
            continue;
        }
        ++count;
    }
    return count;
}

ImportJobManager::ImportJobManager(ImportService* service, QObject* parent)
    : QObject(parent)
    , m_service(service)
{
    ImportLog::registerForCrashTrace(&m_log);
    connect(m_service, &ImportService::indexingStarted,
            this, &ImportJobManager::onIndexingStarted);
    connect(m_service, &ImportService::indexingProgress,
            this, &ImportJobManager::onIndexingProgress);
    connect(m_service, &ImportService::indexingComplete,
            this, &ImportJobManager::onIndexingComplete);
    connect(m_service, &ImportService::indexingFailed,
            this, &ImportJobManager::onIndexingFailed);
}

ImportJobManager::~ImportJobManager()
{
    ImportLog::registerForCrashTrace(nullptr);
}

void ImportJobManager::setProject(std::shared_ptr<Project> project)
{
    if (busy())
        cancelQueue(tr("Import cancelled — project changed."));
    m_project = std::move(project);
}

void ImportJobManager::importBatch(const QList<FileImportAction>& actions)
{
    if (!m_project) return;

    // Deduplicate — identical normalized paths in one batch are processed once.
    QSet<QString> seen_paths;
    int new_jobs = 0;

    for (const auto& action : actions) {
        if (!action.path.isEmpty()) {
            const QString norm = QFileInfo(action.path).canonicalFilePath();
            const QString key  = norm.isEmpty() ? action.path : norm;
            if (seen_paths.contains(key)) {
                m_log.record(makeEntry(ImportLogEntry::Event::Suppressed, {},
                    QFileInfo(action.path).fileName().toStdString() + " (duplicate in batch)"));
                continue;
            }
            seen_paths.insert(key);
        }
        if (action.kind == FileImportAction::Kind::Skip) continue;
        if (action.kind == FileImportAction::Kind::ReuseExisting) {
            m_log.record(makeEntry(ImportLogEntry::Event::Reused,
                                   action.existing_layer_id,
                                   QFileInfo(action.path).fileName().toStdString()));
            emit statusMessage(
                tr("Using existing data for %1").arg(QFileInfo(action.path).fileName()));
            emit jobCompleted(action.existing_layer_id);
            ++m_summary.reused;
            continue;
        }
        QueuedJob job;
        job.kind               = action.kind;
        job.band_choice        = action.band_choice;
        job.path               = action.path;
        job.existing_layer_id  = action.existing_layer_id;
        job.existing_source_id = action.existing_source_id;
        job.source_crs         = action.source_crs;
        job.module_filter      = action.module_filter;
        m_queue.push_back(std::move(job));
        ++new_jobs;
    }

    if (new_jobs > 0) {
        dispatchNext();  // fills up to maxConcurrentJobs() slots
    } else if (m_active_count == 0) {
        // All actions were reuse/skip and nothing is running — batch is done.
        m_log.record(makeEntry(ImportLogEntry::Event::BatchDone));
        BatchSummary summary = m_summary;
        m_summary = {};
        const uint32_t tok = m_epoch;
        QMetaObject::invokeMethod(this, [this, tok, s = summary] {
            if (tok == m_epoch) emit batchCompleted(s);
        }, Qt::QueuedConnection);
    }
}

void ImportJobManager::reindexLayer(const std::string& source_path,
                                    const std::string& layer_id,
                                    const core::SpatialRef& user_crs)
{
    QueuedJob job;
    job.kind              = FileImportAction::Kind::RebuildExisting;
    job.path              = QString::fromStdString(source_path);
    job.existing_layer_id = layer_id;
    job.source_crs        = user_crs;
    m_queue.push_back(std::move(job));
    dispatchNext();
}

ImportLogEntry ImportJobManager::makeEntry(ImportLogEntry::Event event,
                                           const std::string& layer_id,
                                           const std::string& detail) const
{
    ImportLogEntry e;
    e.event            = event;
    e.epoch            = m_epoch;
    e.active_job_epoch = m_active_job_epoch;
    e.queue_depth      = static_cast<uint16_t>(m_queue.size());
    e.layer_id         = layer_id;
    e.detail           = detail;
    return e;
}

void ImportJobManager::cancelQueue(const QString& reason)
{
    ++m_epoch;  // invalidate all pending deferred lambdas
    m_log.record(makeEntry(ImportLogEntry::Event::Cancelled, {}, reason.toStdString()));
    for (const auto& job : m_queue)
        emit statusMessage(tr("Cancelled: %1").arg(QFileInfo(job.path).fileName()));
    m_queue.clear();
    emit statusMessage(reason);

    // If jobs are still in-flight, let them settle naturally.  Each
    // completion/failure handler decrements m_active_count; when it reaches 0
    // with an empty queue, dispatchNext() will emit batchCompleted.
    if (m_active_count > 0 || m_awaiting_start_count > 0) return;

    BatchSummary summary = m_summary;
    m_summary = {};
    const uint32_t tok = m_epoch;
    QMetaObject::invokeMethod(this, [this, tok, s = summary] {
        if (tok == m_epoch) emit batchCompleted(s);
    }, Qt::QueuedConnection);
}

void ImportJobManager::dispatchNext()
{
    const int cap = maxConcurrentJobs();
    while (m_active_count < cap && !m_queue.empty()) {
        QueuedJob job = m_queue.front();
        m_queue.pop_front();

        // Verify the source file still exists before a rebuild dispatch.
        if (job.kind == FileImportAction::Kind::RebuildExisting
            && !job.path.isEmpty()
            && !QFileInfo::exists(job.path)) {
            const std::string fname = QFileInfo(job.path).fileName().toStdString();
            m_log.record(makeEntry(ImportLogEntry::Event::Failed,
                                   job.existing_layer_id,
                                   fname + ": source not found"));
            emit statusMessage(
                tr("Source not found: %1").arg(QFileInfo(job.path).fileName()));
            emit jobFailed(job.existing_layer_id,
                           tr("Source file not found: %1").arg(job.path));
            ++m_summary.failed;
            continue;  // don't hold a concurrency slot; try next job
        }

        ++m_active_count;
        ++m_awaiting_start_count;
        m_active_job_epoch = m_epoch;

        if (job.kind == FileImportAction::Kind::RebuildExisting
            && !job.existing_layer_id.empty()) {
            // Layer ID is known at dispatch time — pre-insert so onIndexingComplete
            // can distinguish RebuildExisting from ImportNew in the summary.
            m_active_jobs[job.existing_layer_id] = {
                FileImportAction::Kind::RebuildExisting, false, m_epoch};
            m_log.record(makeEntry(ImportLogEntry::Event::Dispatched,
                                   job.existing_layer_id,
                                   QFileInfo(job.path).fileName().toStdString()));
            emit statusMessage(
                tr("Rebuilding %1…").arg(QFileInfo(job.path).fileName()));
            m_service->reindexLayer(job.path.toStdString(), m_project,
                                    job.existing_layer_id, job.source_crs);
        } else {
            // ImportNew: layer ID is not known until onIndexingStarted fires.
            const QString ext = QFileInfo(job.path).suffix().toLower();
            m_log.record(makeEntry(ImportLogEntry::Event::Dispatched, {},
                                   QFileInfo(job.path).fileName().toStdString()));
            emit statusMessage(
                tr("Importing %1 as %2").arg(QFileInfo(job.path).fileName(),
                                             ext.toUpper()));
            using BC = FileImportAction::BandChoice;
            const bool want_hf = (job.band_choice != BC::LFOnly);
            const bool want_lf = (job.band_choice != BC::HFOnly);
            m_service->importFile(job.path.toStdString(), ext.toStdString(),
                                  m_project, job.source_crs,
                                  want_hf, want_lf, job.module_filter);
        }
    }

    // Queue drained and no jobs running — batch is complete.
    if (m_active_count == 0 && m_queue.empty()) {
        m_log.record(makeEntry(ImportLogEntry::Event::BatchDone));
        BatchSummary summary = m_summary;
        m_summary = {};
        emit batchCompleted(summary);
    }
}

void ImportJobManager::onIndexingStarted(const std::string& layer_id)
{
    // rebuildCacheIndex shares indexingStarted but is not a queued import job.
    // It has no matching dispatchNext() call, so it must not touch m_active_count
    // or m_active_jobs — those only track jobs that went through importBatch().
    if (m_service->isRebuildingLayer(layer_id)) return;

    // Always update internal state so queue advancement is consistent after cancel.
    if (m_awaiting_start_count > 0) --m_awaiting_start_count;

    auto it = m_active_jobs.find(layer_id);
    if (it == m_active_jobs.end()) {
        // ImportNew: layer ID wasn't known at dispatch — add it now.
        m_active_jobs[layer_id] = {
            FileImportAction::Kind::ImportNew, true, m_active_job_epoch};
    } else {
        it->second.started = true;
    }

    const auto active = m_active_jobs.find(layer_id);
    if (active == m_active_jobs.end() || active->second.epoch != m_epoch) {
        m_log.record(makeEntry(ImportLogEntry::Event::Suppressed, layer_id, "started"));
        return;
    }

    QString filename;
    QString format  = QStringLiteral("XTF");
    float   size_mb = 0.f;

    if (m_project) {
        if (const auto* layer = m_project->findLayer(layer_id)) {
            if (const auto* src = m_project->findSource(layer->source_id)) {
                QFileInfo fi(QString::fromStdString(src->path));
                filename = fi.fileName();
                format   = QString::fromStdString(src->format).toUpper();
                size_mb  = static_cast<float>(fi.size()) / (1024.f * 1024.f);
            }
            if (filename.isEmpty())
                filename = QString::fromStdString(layer->label);
        }
    }

    m_log.record(makeEntry(ImportLogEntry::Event::Started, layer_id,
                           filename.toStdString()));
    emit statusMessage(tr("Parsing %1…").arg(filename));
    emit jobStarted(layer_id, filename, format, size_mb);
}

void ImportJobManager::onIndexingProgress(const std::string& layer_id, int percent)
{
    const auto active = m_active_jobs.find(layer_id);
    if (active != m_active_jobs.end() && active->second.epoch != m_epoch) return;
    if (percent % 25 == 0) {  // log milestones only to avoid flooding the buffer
        auto e = makeEntry(ImportLogEntry::Event::Progress, layer_id);
        e.progress_pct = static_cast<uint8_t>(percent);
        m_log.record(std::move(e));
    }
    emit jobProgress(layer_id, percent);
}

void ImportJobManager::onIndexingComplete(const std::string& layer_id)
{
    const auto active = m_active_jobs.find(layer_id);
    const bool live = active != m_active_jobs.end()
                   && active->second.epoch == m_epoch;

    if (live) {
        m_log.record(makeEntry(ImportLogEntry::Event::Completed, layer_id));
        if (m_project) {
            if (const auto* layer = m_project->findLayer(layer_id)) {
                emit statusMessage(
                    tr("Indexed %1 — %2 artifacts")
                        .arg(QString::fromStdString(layer->label))
                        .arg(layer->bandArtifactCount()));
            }
        }
        emit jobCompleted(layer_id);
    } else {
        m_log.record(makeEntry(ImportLogEntry::Event::Suppressed, layer_id, "completed"));
    }

    // Unconditional state update — keeps counts correct even after a cancel.
    auto it = m_active_jobs.find(layer_id);
    if (it != m_active_jobs.end()) {
        if (live) {
            if (it->second.kind == FileImportAction::Kind::RebuildExisting)
                ++m_summary.rebuilt;
            else
                ++m_summary.imported;
        }
        m_active_jobs.erase(it);
        --m_active_count;
        // Defer so the service's remaining per-job emissions fully unwind first.
        const uint32_t tok = m_epoch;
        QMetaObject::invokeMethod(this, [this, tok] {
            if (tok == m_epoch) dispatchNext();
        }, Qt::QueuedConnection);
    }
}

void ImportJobManager::onIndexingFailed(const std::string& layer_id,
                                        const std::string& error)
{
    const auto active = m_active_jobs.find(layer_id);
    const bool live = active != m_active_jobs.end()
        ? active->second.epoch == m_epoch
        : m_active_job_epoch == m_epoch;

    if (live) {
        m_log.record(makeEntry(ImportLogEntry::Event::Failed, layer_id, error));
        qWarning().noquote() << m_log.dump();
        emit jobFailed(layer_id, QString::fromStdString(error));
    } else {
        m_log.record(makeEntry(ImportLogEntry::Event::Suppressed, layer_id,
                               "failed:" + error));
    }

    // Update tracking.  m_awaiting_start_count must be decremented if
    // indexingStarted never fired for this job (either because it's an ImportNew
    // that failed synchronously, or a RebuildExisting that failed before started).
    //
    // rebuildCacheIndex failures reach here with an empty m_active_jobs entry
    // (onIndexingStarted now early-returns for rebuilds) and m_awaiting_start_count==0.
    // Don't decrement m_active_count for those — no dispatchNext() slot was taken.
    auto it = m_active_jobs.find(layer_id);
    const bool is_tracked = (it != m_active_jobs.end()) || (m_awaiting_start_count > 0);

    if (it == m_active_jobs.end()) {
        // ImportNew sync fail — not yet in m_active_jobs.
        if (m_awaiting_start_count > 0) --m_awaiting_start_count;
    } else {
        if (!it->second.started && m_awaiting_start_count > 0)
            --m_awaiting_start_count;  // RebuildExisting sync fail — started never fired
        m_active_jobs.erase(it);
    }

    if (live) ++m_summary.failed;
    if (!is_tracked) return;  // cache rebuild failure — no concurrency slot was held

    --m_active_count;

    const uint32_t tok = m_epoch;
    QMetaObject::invokeMethod(this, [this, tok] {
        if (tok == m_epoch) dispatchNext();
    }, Qt::QueuedConnection);
}

} // namespace dolphin::app
