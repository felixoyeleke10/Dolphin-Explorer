#pragma once
#include "app/import/ImportAction.h"
#include "app/import/ImportLog.h"
#include <QList>
#include <QObject>
#include <QString>
#include <deque>
#include <memory>
#include <string>

namespace dolphin::app {
class ImportService;
class Project;

// Owns the import job queue and dispatch loop.
// One job runs at a time; jobs are dispatched serially to prevent concurrent I/O.
// UI consumers should connect to the signals below and drive their own widgets.
class ImportJobManager : public QObject {
    Q_OBJECT
public:
    explicit ImportJobManager(ImportService* service, QObject* parent = nullptr);
    ~ImportJobManager() override;

    void setProject(std::shared_ptr<Project> project);
    void importBatch(const QList<FileImportAction>& actions);
    void reindexLayer(const std::string& source_path, const std::string& layer_id);

    int pendingCount() const { return static_cast<int>(m_queue.size()); }
    bool busy() const { return m_busy; }

    const ImportLog& importLog() const { return m_log; }

signals:
    // Fired when a background parse task starts for a layer.
    void jobStarted(std::string layer_id, QString filename, QString format, float size_mb);
    void jobProgress(std::string layer_id, int percent);
    void jobCompleted(std::string layer_id);
    void jobFailed(std::string layer_id, QString error);
    // Fired once when all queued jobs finish (including reuse/skip-only batches).
    void batchCompleted();
    void statusMessage(QString message);

private slots:
    void onIndexingStarted(const std::string& layer_id);
    void onIndexingProgress(const std::string& layer_id, int percent);
    void onIndexingComplete(const std::string& layer_id);
    void onIndexingFailed(const std::string& layer_id, const std::string& error);

private:
    struct QueuedJob {
        FileImportAction::Kind       kind        = FileImportAction::Kind::ImportNew;
        FileImportAction::BandChoice band_choice = FileImportAction::BandChoice::Both;
        QString                      path;
        std::string                  existing_layer_id;
        std::string                  existing_source_id;
        core::SpatialRef             source_crs;
        std::vector<core::ArtifactType> module_filter;
    };

    void dispatchNext();
    // Clears the pending queue and invalidates deferred dispatch lambdas.
    // Does NOT abort the in-flight task — it will still settle and emit
    // batchCompleted when its completion/failure slot runs.
    void cancelQueue(const QString& reason);

    // Builds a log entry pre-filled with the current epoch/queue snapshot.
    ImportLogEntry makeEntry(ImportLogEntry::Event event,
                             const std::string& layer_id = {},
                             const std::string& detail   = {}) const;

    ImportService*           m_service;
    std::shared_ptr<Project> m_project;
    std::deque<QueuedJob>    m_queue;
    bool                     m_busy = false;
    // True from dispatchNext() until onIndexingStarted() fires — covers the
    // window where importFile() can fail synchronously before indexingStarted.
    bool                     m_awaiting_start  = false;
    // Primary layer ID of the currently dispatched job. Set by onIndexingStarted,
    // cleared when that layer completes or fails. Only this layer's completion
    // advances the queue — multi-layer jobs (LF split, mixed-modality extras)
    // emit jobCompleted but do not trigger dispatchNext().
    std::string              m_active_layer_id;
    // Monotonically incremented by cancelQueue() only.
    // Captured by value in every deferred lambda — stale post-cancel lambdas
    // see a mismatch and become no-ops.
    uint32_t                 m_epoch           = 0;
    // Snapshot of m_epoch taken in dispatchNext() when a job is dispatched.
    // All four service-event handlers compare against m_epoch: if they differ,
    // a cancel happened after this job started → suppress UI-facing signals
    // (jobStarted / jobProgress / jobCompleted / jobFailed) while still
    // performing internal state transitions so queue advancement works.
    uint32_t                 m_active_job_epoch = 0;

    ImportLog                m_log;
};

} // namespace dolphin::app
