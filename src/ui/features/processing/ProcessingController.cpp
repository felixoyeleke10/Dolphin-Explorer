#include "ui/features/processing/ProcessingController.h"
#include "app/layers/DataLayer.h"
#include "app/services/ProcessingService.h"
#include "app/project/Project.h"

namespace dolphin::ui {

ProcessingController::ProcessingController(app::ProcessingService* processing_service,
                                           QObject*               parent)
    : QObject(parent)
    , m_processing_service(processing_service)
{
    connect(m_processing_service, &app::ProcessingService::runStarted,
            this, &ProcessingController::onRunStarted);
    connect(m_processing_service, &app::ProcessingService::runComplete,
            this, &ProcessingController::onRunComplete);
    connect(m_processing_service, &app::ProcessingService::runFailed,
            this, &ProcessingController::onRunFailed);
    connect(m_processing_service, &app::ProcessingService::batchProgress,
            this, &ProcessingController::onBatchProgress);
    connect(m_processing_service, &app::ProcessingService::runPersisted,
            this, [this](const std::string& id,
                         const std::string& proc_path,
                         const core::ArtifactIndex& proc_index,
                         bool slant_range_corrected,
                         uint32_t baked_correction_flags) {
                emit processingPersisted(id, proc_path, proc_index,
                                         slant_range_corrected,
                                         baked_correction_flags);
            });
}

void ProcessingController::setProject(std::shared_ptr<app::Project> project)
{
    m_project = std::move(project);
}

void ProcessingController::runLayer(app::DataLayer* layer, const std::string& source_path)
{
    if (!m_project || !layer)
        return;
    m_processing_service->runLayer(*m_project, layer, source_path);
}

void ProcessingController::runAll()
{
    if (!m_project)
        return;
    m_processing_service->runAll(*m_project);
}

void ProcessingController::onRunStarted(const std::string& layer_id)
{
    auto* layer = m_project ? m_project->findLayer(layer_id) : nullptr;
    const QString label = layer ? QString::fromStdString(layer->label)
                                : QString::fromStdString(layer_id);
    emit statusMessage("Running pipeline on " + label + "...");
    emit progressChanged(0, true);
    emit layerRunStarted(layer_id, label);
}

void ProcessingController::onRunComplete(const std::string& layer_id,
                                         const std::string& summary)
{
    auto* layer = m_project ? m_project->findLayer(layer_id) : nullptr;
    const QString label = layer ? QString::fromStdString(layer->label)
                                : QString::fromStdString(layer_id);
    const QString qs = QString::fromStdString(summary);
    emit statusMessage("Pipeline complete for " + label + ": " + qs);
    emit progressChanged(100, false);
    emit layerRunFinished(layer_id, qs.isEmpty() ? tr("Complete") : qs);
}

void ProcessingController::onRunFailed(const std::string& layer_id,
                                       const std::string& error)
{
    auto* layer = m_project ? m_project->findLayer(layer_id) : nullptr;
    const QString label = layer ? QString::fromStdString(layer->label)
                                : QString::fromStdString(layer_id);
    const QString qe = QString::fromStdString(error);
    emit statusMessage("Pipeline failed for " + label + ": " + qe);
    emit progressChanged(0, false);
    emit layerRunFailed(layer_id, qe);
}

void ProcessingController::onBatchProgress(int done, int total)
{
    const int pct = (total > 0) ? (done * 100 / total) : 0;
    emit progressChanged(pct, true);
    emit statusMessage(QString("Processing: %1 / %2 layers").arg(done).arg(total));
}

} // namespace dolphin::ui
