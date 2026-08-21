#pragma once
#include <QObject>
#include <QString>
#include <memory>
#include <string>
#include "core/ArtifactIndex.h"

namespace dolphin::app {
class DataLayer;
class ProcessingService;
class Project;
}

namespace dolphin::ui {

// Owns the pipeline execution flow: forwards runLayer/runAll to
// ProcessingService and translates its signals into status messages.
// MainWindow drives it by calling runLayer()/runAll() and listening to
// statusMessage().
class ProcessingController : public QObject {
    Q_OBJECT
public:
    explicit ProcessingController(app::ProcessingService* processing_service,
                                  QObject*               parent = nullptr);

    // Called from MainWindow::bindProjectUi whenever the active project changes.
    void setProject(std::shared_ptr<app::Project> project);

    // Run the pipeline on a single layer.
    void runLayer(app::DataLayer* layer, const std::string& source_path);

    // Run the pipeline on all indexed layers in the current project.
    void runAll();

signals:
    // Human-readable status update for the main window's status bar.
    void statusMessage(const QString& message);
    // Progress bar control: percent in [0,100], visible=true while running.
    void progressChanged(int percent, bool visible);

    // Per-layer signals for the execution progress dialog.
    void layerRunStarted (const std::string& layer_id, const QString& label);
    void layerRunFinished(const std::string& layer_id, const QString& summary);
    void layerRunFailed  (const std::string& layer_id, const QString& error);

    // Emitted after a successful run when processed data has been persisted.
    // Consumers should redirect the layer's artifact store to proc_path so
    // the next load reads the processed output instead of the raw cache.
    void processingPersisted(const std::string& layer_id,
                             const std::string& proc_path,
                             const core::ArtifactIndex& proc_index,
                             bool slant_range_corrected,
                             uint32_t baked_correction_flags);

private slots:
    void onRunStarted(const std::string& layer_id);
    void onRunComplete(const std::string& layer_id, const std::string& summary);
    void onRunFailed(const std::string& layer_id, const std::string& error);
    void onBatchProgress(int done, int total);

private:
    app::ProcessingService*       m_processing_service;
    std::shared_ptr<app::Project> m_project;
};

} // namespace dolphin::ui
