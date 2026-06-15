// ImportController.cpp — thin UI adapter over ImportJobManager.
// Queue and dispatch logic live in app::ImportJobManager.
// This file drives the progress dialog, layer picker, and status bar.
#include "ui/features/import/ImportController.h"
#include "ui/features/import/ImportProgressDialog.h"
#include "ui/shared/widgets/LayerPickerWidget.h"
#include "ui/shared/CoordFormat.h"
#include "app/layers/DataLayer.h"
#include "app/project/Project.h"
#include "app/services/ImportService.h"

#include <QFileInfo>
#include <QMessageBox>

namespace dolphin::ui {

ExecutionController::ExecutionController(app::ImportJobManager*   job_manager,
                                         ExecutionProgressDialog* dialog,
                                         LayerPickerWidget*       layer_picker,
                                         QWidget*                 dialog_parent,
                                         QObject*                 parent)
    : QObject(parent)
    , m_manager(job_manager)
    , m_dialog(dialog)
    , m_layer_picker(layer_picker)
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
            this, [this](app::ImportJobManager::BatchSummary s) {
                emit batchCompleted(s);
            });
    connect(m_manager, &app::ImportJobManager::statusMessage,
            this, &ExecutionController::statusMessage);
}

void ExecutionController::setProject(std::shared_ptr<app::Project> project)
{
    m_project = project;
    m_manager->setProject(std::move(project));
}

void ExecutionController::connectToCacheRebuilds(app::ImportService* service)
{
    if (!service) return;
    connect(service, &app::ImportService::indexingStarted,
            this, [this, service](const std::string& layer_id) {
                onCacheRebuildStarted(service, layer_id);
            });
    connect(service, &app::ImportService::indexingProgress,
            this, &ExecutionController::onCacheRebuildProgress);
    connect(service, &app::ImportService::cacheIndexRebuilt,
            this, &ExecutionController::onCacheRebuilt);
    connect(service, &app::ImportService::indexingFailed,
            this, &ExecutionController::onCacheRebuildFailed);
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

void ExecutionController::onMapLoadPending()
{
    if (m_dialog)
        m_dialog->onMapLoadPending();
}

void ExecutionController::onMapLoadDone()
{
    if (m_dialog)
        m_dialog->onMapLoadDone();
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

void ExecutionController::onCacheRebuildStarted(app::ImportService* service,
                                                const std::string& layer_id)
{
    if (!service || !service->isRebuildingLayer(layer_id)) return;
    m_cache_rebuild_jobs.insert(layer_id);

    QString filename = QString::fromStdString(layer_id);
    QString format = QStringLiteral("DLPD");
    float size_mb = 0.f;

    if (m_project) {
        if (const auto* layer = m_project->findLayer(layer_id)) {
            filename = QString::fromStdString(layer->label);
            if (!layer->artifact_store_format.empty())
                format = QString::fromStdString(layer->artifact_store_format).toUpper();
            if (!layer->artifact_store_path.empty()) {
                QFileInfo fi(QString::fromStdString(layer->artifact_store_path));
                if (fi.exists())
                    size_mb = static_cast<float>(fi.size()) / (1024.f * 1024.f);
            }
        }
    }

    emit progressChanged(0, true);
    if (m_dialog) {
        m_dialog->addJob(layer_id, filename, format, size_mb);
        m_dialog->raise();
    }
}

void ExecutionController::onCacheRebuildProgress(const std::string& layer_id, int percent)
{
    if (!m_cache_rebuild_jobs.count(layer_id)) return;
    emit progressChanged(percent, true);
    if (m_dialog)
        m_dialog->updateJob(layer_id, percent);
}

void ExecutionController::onCacheRebuilt(const std::string& layer_id)
{
    if (!m_cache_rebuild_jobs.erase(layer_id)) return;

    emit progressChanged(100, false);
    emit cacheLayerReady(layer_id);

    QString result = tr("Cached data loaded");
    if (m_project) {
        if (const auto* layer = m_project->findLayer(layer_id)) {
            const int artifacts = layer->bandArtifactCount();
            result = artifacts >= 1000
                ? tr("%1k artifacts cached").arg(artifacts / 1000.0, 0, 'f', 1)
                : tr("%1 artifacts cached").arg(artifacts);
        }
    }
    if (m_dialog)
        m_dialog->finishJob(layer_id, result);
    if (m_layer_picker)
        m_layer_picker->refresh();
}

void ExecutionController::onCacheRebuildFailed(const std::string& layer_id,
                                               const std::string& error)
{
    if (!m_cache_rebuild_jobs.erase(layer_id)) return;

    emit progressChanged(0, false);
    const QString qerror = QString::fromStdString(error);
    if (m_dialog)
        m_dialog->failJob(layer_id, qerror);
    emit cacheLayerFailed(QString::fromStdString(layer_id), qerror);
}

} // namespace dolphin::ui
