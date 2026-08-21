#pragma once
#include "app/import/ImportAction.h"
#include "app/import/ImportLog.h"
#include <QList>
#include <QObject>
#include <QString>
#include <deque>
#include <map>
#include <memory>
#include <string>

namespace dolphin::app {
class ImportService;
class Project;

// Owns the import job queue and dispatch loop.
// Up to maxConcurrentJobs() jobs run in parallel; each is driven by ImportService
// signals.  UI consumers connect to the signals below and drive their own widgets.
class ImportJobManager : public QObject {
    Q_OBJECT
public:
    // Per-batch outcome counts emitted with batchCompleted.
    struct BatchSummary {
        int imported = 0;   // ImportNew jobs that finished successfully
        int rebuilt  = 0;   // RebuildExisting jobs that finished successfully
        int reused   = 0;   // ReuseExisting — forwarded without re-parsing
        int failed   = 0;   // jobs that failed or whose source was not found
    };

    // Max heavy import/decode jobs run in parallel (locked decision D-14).
    // The queue holds the remainder so UI/render work keeps CPU and I/O headroom.
    static int maxConcurrentJobs();
    // Number of rows this action set can actually dispatch after applying the
    // manager's skip/reuse and same-path deduplication rules.
    static int effectiveQueuedJobCount(const QList<FileImportAction>& actions);

    explicit ImportJobManager(ImportService* service, QObject* parent = nullptr);
    ~ImportJobManager() override;

    void setProject(std::shared_ptr<Project> project);
    void importBatch(const QList<FileImportAction>& actions);
    // user_crs: confirmed source CRS to re-apply (empty = preserve/detect).
    void reindexLayer(const std::string& source_path, const std::string& layer_id,
                      const core::SpatialRef& user_crs = {});

    int  pendingCount() const { return static_cast<int>(m_queue.size()); }
    bool busy()         const { return m_active_count > 0 || !m_queue.empty(); }

    // Clears the pending queue and invalidates deferred dispatch lambdas.
    // Does NOT abort in-flight tasks — they still settle and collectively emit
    // batchCompleted once all active jobs finish. Public so the shell's
    // "Cancel Imports" escape hatch (project switch / app exit while imports
    // run) can drop the queue and then wait for the in-flight decode.
    void cancelQueue(const QString& reason);

    const ImportLog& importLog() const { return m_log; }

signals:
    // Fired when a background parse task starts for a layer.
    void jobStarted(std::string layer_id, QString filename, QString format, float size_mb);
    void jobProgress(std::string layer_id, int percent);
    void jobCompleted(std::string layer_id);
    void jobFailed(std::string layer_id, QString error);
    // Fired once when all queued jobs finish (including reuse/skip-only batches).
    void batchCompleted(BatchSummary summary);
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

    // Builds a log entry pre-filled with the current epoch/queue snapshot.
    ImportLogEntry makeEntry(ImportLogEntry::Event event,
                             const std::string& layer_id = {},
                             const std::string& detail   = {}) const;

    ImportService*           m_service;
    std::shared_ptr<Project> m_project;
    std::deque<QueuedJob>    m_queue;

    // Concurrency tracking — replaces the old m_busy/m_awaiting_start/m_active_layer_id
    // scalar fields.  All are manipulated on the main thread only.
    int                      m_active_count         = 0;  // jobs dispatched but not yet done
    int                      m_awaiting_start_count = 0;  // dispatched but indexingStarted not yet received
    struct ActiveJob {
        FileImportAction::Kind kind    = FileImportAction::Kind::ImportNew;
        bool                   started = false;  // true once indexingStarted has fired
        uint32_t               epoch   = 0;      // project generation at dispatch
    };
    // Maps layer_id → ActiveJob for all in-flight jobs.
    // RebuildExisting entries are inserted at dispatch (layer_id is known then);
    // ImportNew entries are inserted in onIndexingStarted (layer_id only known then).
    std::map<std::string, ActiveJob> m_active_jobs;

    // Monotonically incremented by cancelQueue() only.
    // Captured by value in every deferred lambda — stale post-cancel lambdas
    // see a mismatch and become no-ops.
    uint32_t                 m_epoch           = 0;
    // Snapshot used only while ImportService synchronously emits the initial
    // started/failure signal. Long-lived jobs carry their own epoch in
    // ActiveJob, so dispatching a newer job cannot revalidate an older one.
    uint32_t                 m_active_job_epoch = 0;

    // Accumulated per-batch outcome counts — reset when batchCompleted fires.
    BatchSummary             m_summary;

    ImportLog                m_log;
};

} // namespace dolphin::app
