// ImportController.cpp — thin UI adapter over ImportJobManager.
// Queue and dispatch logic live in app::ImportJobManager.
// This file drives the progress dialog, layer picker, and status bar.
#include "ui/features/import/ImportController.h"
#include "ui/features/import/ImportProgressDialog.h"
#include "ui/shared/widgets/LayerPickerWidget.h"
#include "ui/shared/CoordFormat.h"
#include "app/layers/DataLayer.h"
#include "app/project/Project.h"

#include <QFileInfo>
#include <QMessageBox>
#include <QProgressBar>

namespace dolphin::ui {

ExecutionController::ExecutionController(app::ImportJobManager*   job_manager,
                                         ExecutionProgressDialog* dialog,
                                         LayerPickerWidget*       layer_picker,
                                         QProgressBar*            progress_bar,
                                         QWidget*                 dialog_parent,
                                         QObject*                 parent)
    : QObject(parent)
    , m_manager(job_manager)
    , m_dialog(dialog)
    , m_layer_picker(layer_picker)
    , m_progress_bar(progress_bar)
    , m_dialog_parent(dialog_parent)
{
    connect(m_manager, &app::ImportJobManager::jobStarted,
            this, &ExecutionController::onJobStarted);
    connect(m_manager, &app::ImportJobManager::jobProgress,
            this, &ExecutionController::onJobProgress);
    connect(m_manager, &app::ImportJobManager::jobCompleted,
            this, &ExecutionController::onJobCompleted);
    connect(m_manager, &app::ImportJobManager::jobFailed,
            this, &ExecutionController::onJobFailed);
    connect(m_manager, &app::ImportJobManager::batchCompleted,
            this, &ExecutionController::batchCompleted);
    connect(m_manager, &app::ImportJobManager::statusMessage,
            this, &ExecutionController::statusMessage);
}

void ExecutionController::setProject(std::shared_ptr<app::Project> project)
{
    m_project = project;
    m_manager->setProject(std::move(project));
}

void ExecutionController::importBatch(const QList<FileImportAction>& actions)
{
    if (!m_project) return;
    m_dialog->setQueueTotal(
        m_manager->pendingCount() + (m_manager->busy() ? 1 : 0)
        + static_cast<int>(
            std::count_if(actions.begin(), actions.end(), [](const FileImportAction& a) {
                return a.kind != FileImportAction::Kind::Skip
                    && a.kind != FileImportAction::Kind::ReuseExisting;
            })));
    m_manager->importBatch(actions);
}

void ExecutionController::reindexLayer(const std::string& source_path,
                                       const std::string& layer_id)
{
    m_manager->reindexLayer(source_path, layer_id);
}

void ExecutionController::onJobStarted(const std::string& layer_id,
                                       const QString& filename,
                                       const QString& format,
                                       float size_mb)
{
    emit progressChanged(0, true);
    m_dialog->addJob(layer_id, filename, format, size_mb);
    m_dialog->reanchor();
    m_dialog->raise();
    if (m_layer_picker) {
        m_layer_picker->expand();
        m_layer_picker->refresh();
    }
}

void ExecutionController::onJobProgress(const std::string& layer_id, int percent)
{
    emit progressChanged(percent, true);
    m_dialog->updateJob(layer_id, percent);
}

void ExecutionController::onJobCompleted(const std::string& layer_id)
{
    emit progressChanged(100, false);

    QString coord_sys = QStringLiteral("—");
    float   freq_khz  = 0.f;
    int     artifacts = 0;

    if (m_project) {
        if (const auto* layer = m_project->findLayer(layer_id)) {
            freq_khz  = layer->frequency_hz / 1000.f;
            artifacts = layer->bandArtifactCount();
            auto fill = [&](const core::SpatialRef& ref) {
                if (!ref.empty()) coord_sys = spatialRefDisplayName(ref);
            };
            fill(layer->source_spatial_ref);
            if (coord_sys == QStringLiteral("—")) {
                if (const auto* src = m_project->findSource(layer->source_id))
                    fill(src->source_spatial_ref);
            }
        }
    }

    // Fire importCompleted BEFORE finishJob so MainWindow can call
    // onMapLoadPending(); checkAllDone() inside finishJob then sees
    // m_pending_map_loads > 0 and withholds "All Done" until the rasteriser finishes.
    emit importCompleted(layer_id);
    m_dialog->finishJob(layer_id, artifacts, freq_khz, coord_sys);
    if (m_layer_picker)
        m_layer_picker->refresh();
}

void ExecutionController::onJobFailed(const std::string& layer_id, const QString& error)
{
    emit progressChanged(0, false);
    m_dialog->failJob(layer_id, error);
    emit importFailed(QString::fromStdString(layer_id), error);
    QMessageBox::warning(m_dialog_parent, tr("Import Failed"), error);
}

} // namespace dolphin::ui
