#pragma once
#include "app/import/ImportAction.h"   // FileImportAction lives in the app layer
#include "app/import/ImportJobManager.h"
#include <QList>
#include <QObject>
#include <QString>
#include <memory>
#include <string>

class QProgressBar;
class QWidget;

namespace dolphin::app {
class Project;
}

namespace dolphin::ui {
class ExecutionProgressDialog;
class LayerPickerWidget;

// Bring FileImportAction into the ui namespace so existing call sites are unchanged.
using FileImportAction = app::FileImportAction;

// -- ExecutionController -------------------------------------------------------
// Thin UI adapter over ImportJobManager.
// Drives ExecutionProgressDialog, LayerPickerWidget, and the status bar in
// response to job events from the manager. Queue and dispatch logic live in
// ImportJobManager (app layer).
class ExecutionController : public QObject {
    Q_OBJECT
public:
    ExecutionController(app::ImportJobManager*   job_manager,
                        ExecutionProgressDialog* dialog,
                        LayerPickerWidget*       layer_picker,
                        QProgressBar*            progress_bar,
                        QWidget*                 dialog_parent,
                        QObject*                 parent = nullptr);

    void setProject(std::shared_ptr<app::Project> project);

    void importBatch(const QList<FileImportAction>& actions);
    void reindexLayer(const std::string& source_path, const std::string& layer_id);

signals:
    void importCompleted(const std::string& layer_id);
    void importFailed(const QString& layer_id, const QString& error);
    void statusMessage(const QString& message);
    void progressChanged(int percent, bool visible);
    void batchCompleted(app::ImportJobManager::BatchSummary summary);

private:
    void onJobStarted(const std::string& layer_id, const QString& filename,
                      const QString& format, float size_mb);
    void onJobProgress(const std::string& layer_id, int percent);
    void onJobCompleted(const std::string& layer_id);
    void onJobFailed(const std::string& layer_id, const QString& error);

    app::ImportJobManager*        m_manager;
    ExecutionProgressDialog*      m_dialog;
    LayerPickerWidget*            m_layer_picker;
    QProgressBar*                 m_progress_bar;
    QWidget*                      m_dialog_parent;
    std::shared_ptr<app::Project> m_project;
};

} // namespace dolphin::ui
