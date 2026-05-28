// ImportJobManager.cpp — serial import queue and dispatch.
// Queue/dispatch logic moved from ui::ExecutionController so it lives in the
// app layer, independent of any dialog or widget.
#include "app/import/ImportJobManager.h"
#include "app/services/ImportService.h"
#include "app/project/Project.h"
#include <QFileInfo>
#include <QMetaObject>

namespace dolphin::app {

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
    if (m_busy || !m_queue.empty())
        cancelQueue(tr("Import cancelled — project changed."));
    m_project = std::move(project);
}

void ImportJobManager::importBatch(const QList<FileImportAction>& actions)
{
    if (!m_project) return;

    int new_jobs = 0;
    for (const auto& action : actions) {
        if (action.kind == FileImportAction::Kind::Skip) continue;
        if (action.kind == FileImportAction::Kind::ReuseExisting) {
            m_log.record(makeEntry(ImportLogEntry::Event::Reused,
                                   action.existing_layer_id,
                                   QFileInfo(action.path).fileName().toStdString()));
            emit statusMessage(
                tr("Using existing data for %1").arg(QFileInfo(action.path).fileName()));
            emit jobCompleted(action.existing_layer_id);
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
        if (!m_busy) dispatchNext();
    } else if (!m_busy) {
        m_log.record(makeEntry(ImportLogEntry::Event::BatchDone));
        const uint32_t tok = m_epoch;
        QMetaObject::invokeMethod(this, [this, tok] {
            if (tok == m_epoch) emit batchCompleted();
        }, Qt::QueuedConnection);
    }
}

void ImportJobManager::reindexLayer(const std::string& source_path,
                                    const std::string& layer_id)
{
    QueuedJob job;
    job.kind              = FileImportAction::Kind::RebuildExisting;
    job.path              = QString::fromStdString(source_path);
    job.existing_layer_id = layer_id;
    m_queue.push_back(std::move(job));
    if (!m_busy) dispatchNext();
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

    // If the active job's primary completion has already run (m_active_layer_id
    // cleared, m_awaiting_start false) but the deferred dispatchNext() lambda
    // hasn't fired yet, that lambda is now stale and will be a no-op. Nothing
    // else will reset m_busy or emit batchCompleted, so do it here.
    if (!m_active_layer_id.empty() || m_awaiting_start)
        return;  // still in-flight; let the task settle naturally
    if (m_busy) {
        m_busy = false;
        const uint32_t tok = m_epoch;
        QMetaObject::invokeMethod(this, [this, tok] {
            if (tok == m_epoch) emit batchCompleted();
        }, Qt::QueuedConnection);
    }
}

void ImportJobManager::dispatchNext()
{
    if (m_queue.empty()) {
        m_busy             = false;
        m_awaiting_start   = false;
        m_active_layer_id.clear();
        m_log.record(makeEntry(ImportLogEntry::Event::BatchDone));
        emit batchCompleted();
        return;
    }

    m_busy             = true;
    m_awaiting_start   = true;
    m_active_job_epoch = m_epoch;  // snapshot: signals from this job are fresh
    const QueuedJob job = m_queue.front();
    m_queue.pop_front();

    if (job.kind == FileImportAction::Kind::RebuildExisting
        && !job.existing_layer_id.empty()) {
        m_log.record(makeEntry(ImportLogEntry::Event::Dispatched,
                               job.existing_layer_id,
                               QFileInfo(job.path).fileName().toStdString()));
        emit statusMessage(
            tr("Rebuilding %1…").arg(QFileInfo(job.path).fileName()));
        m_service->reindexLayer(job.path.toStdString(), m_project,
                                job.existing_layer_id);
        return;
    }

    const QString ext = QFileInfo(job.path).suffix().toLower();
    m_log.record(makeEntry(ImportLogEntry::Event::Dispatched,
                           {},
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

void ImportJobManager::onIndexingStarted(const std::string& layer_id)
{
    // Always update internal state so queue advancement works even after cancel.
    m_awaiting_start  = false;
    m_active_layer_id = layer_id;

    if (m_active_job_epoch != m_epoch) {
        m_log.record(makeEntry(ImportLogEntry::Event::Suppressed, layer_id, "started"));
        return;
    }

    QString filename;
    QString format   = QStringLiteral("XTF");
    float   size_mb  = 0.f;

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
    if (m_active_job_epoch != m_epoch) return;
    if (percent % 25 == 0) {  // log milestones only to avoid flooding the buffer
        auto e = makeEntry(ImportLogEntry::Event::Progress, layer_id);
        e.progress_pct = static_cast<uint8_t>(percent);
        m_log.record(std::move(e));
    }
    emit jobProgress(layer_id, percent);
}

void ImportJobManager::onIndexingComplete(const std::string& layer_id)
{
    if (m_active_job_epoch == m_epoch) {
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

    if (layer_id == m_active_layer_id) {
        m_active_layer_id.clear();
        // Defer so completeImport()'s remaining LF/extra emissions fully unwind
        // before the next queued job starts dispatching.
        const uint32_t tok = m_epoch;
        QMetaObject::invokeMethod(this, [this, tok] {
            if (tok == m_epoch) dispatchNext();
        }, Qt::QueuedConnection);
    }
}

void ImportJobManager::onIndexingFailed(const std::string& layer_id,
                                        const std::string& error)
{
    if (m_active_job_epoch == m_epoch) {
        m_log.record(makeEntry(ImportLogEntry::Event::Failed, layer_id, error));
        qWarning().noquote() << m_log.dump();
        emit jobFailed(layer_id, QString::fromStdString(error));
    } else {
        m_log.record(makeEntry(ImportLogEntry::Event::Suppressed, layer_id,
                               "failed:" + error));
    }

    // Advance queue if this is the active job:
    // m_awaiting_start covers synchronous failures before indexingStarted fires.
    if (m_awaiting_start || layer_id == m_active_layer_id) {
        m_awaiting_start  = false;
        m_active_layer_id.clear();
        const uint32_t tok = m_epoch;
        QMetaObject::invokeMethod(this, [this, tok] {
            if (tok == m_epoch) dispatchNext();
        }, Qt::QueuedConnection);
    }
}

} // namespace dolphin::app
