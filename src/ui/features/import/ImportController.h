#pragma once
#include "app/import/ImportAction.h"   // FileImportAction lives in the app layer
#include "app/import/ImportJobManager.h"
#include <QList>
#include <QObject>
#include <QString>
#include <memory>
#include <string>
#include <unordered_set>

class QWidget;

namespace dolphin::app {
class ImportService;
class Project;
}

namespace dolphin::ui {
class ExecutionProgressDialog;

// Bring FileImportAction into the ui namespace so existing call sites are unchanged.
using FileImportAction = app::FileImportAction;

// -- ExecutionController -------------------------------------------------------
// Thin UI adapter over ImportJobManager.
// Drives ExecutionProgressDialog and the status bar in response to job events
// from the manager. Queue and dispatch logic live in ImportJobManager (app
// layer).
class ExecutionController : public QObject {
    Q_OBJECT
public:
    ExecutionController(app::ImportJobManager*   job_manager,
                        ExecutionProgressDialog* dialog,
                        QWidget*                 dialog_parent,
                        QObject*                 parent = nullptr);

    void setProject(std::shared_ptr<app::Project> project);
    void connectToCacheRebuilds(app::ImportService* service);

    void importBatch(const QList<FileImportAction>& actions);
    void reindexLayer(const std::string& source_path, const std::string& layer_id,
                      const core::SpatialRef& user_crs = {});

    // True while import/reindex jobs are running or queued — used by the
    // main-window close guard so parses aren't silently abandoned.
    bool importsBusy() const;

    void onMapLoadPending(uint64_t task_id, const QString& layer_name = {});
    void onMapLoadProgress(int percent);
    void onMapLoadDone(uint64_t task_id);

signals:
    void importCompleted(const std::string& layer_id);
    void importFailed(const QString& layer_id, const QString& error);
    void cacheLayerReady(const std::string& layer_id);
    void cacheLayerFailed(const QString& layer_id, const QString& error);
    void statusMessage(const QString& message);
    void progressChanged(int percent, bool visible);
    void batchCompleted(app::ImportJobManager::BatchSummary summary);

private:
    void onJobStarted(const std::string& layer_id, const QString& filename,
                      const QString& format, float size_mb);
    void onJobProgress(const std::string& layer_id, int percent);
    void onJobCompleted(const std::string& layer_id);
    void onJobFailed(const std::string& layer_id, const QString& error);
    void onCacheRebuildStarted(app::ImportService* service, const std::string& layer_id);
    void onCacheRebuildProgress(const std::string& layer_id, int percent);
    void onCacheRebuilt(const std::string& layer_id);
    void onCacheRebuildFailed(const std::string& layer_id, const std::string& error);

    app::ImportJobManager*        m_manager;
    ExecutionProgressDialog*      m_dialog;
    QWidget*                      m_dialog_parent;
    std::shared_ptr<app::Project> m_project;
    std::unordered_set<std::string> m_cache_rebuild_jobs;
};

} // namespace dolphin::ui
