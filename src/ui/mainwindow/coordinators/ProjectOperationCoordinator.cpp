#include "ui/mainwindow/coordinators/ProjectOperationCoordinator.h"
#include "ui/features/processing/ProcessingController.h"
#include "ui/mainwindow/coordinators/CorrectionBatchOperator.h"
#include "ui/systems/ProjectEventBus.h"
#include "app/project/Project.h"
#include "app/layers/DataLayer.h"
#include "app/workers/Worker.h"
#include "app/contracts/ContractEnvelope.h"
#include "app/contracts/ContractTypes.h"
#include "core/SidescanPing.h"
#include "core/SubBottomTrace.h"

#include <functional>
#include <string>

namespace dolphin::ui {

ProjectOperationCoordinator::ProjectOperationCoordinator(ProjectEventBus* event_bus,
                                                         QObject*         parent)
    : QObject(parent)
    , m_event_bus(event_bus)
{}

void ProjectOperationCoordinator::setProject(std::shared_ptr<app::Project> project)
{
    m_project = std::move(project);
}

void ProjectOperationCoordinator::connectToProcessing(ProcessingController* ctrl)
{
    if (!ctrl) return;
    connect(ctrl, &ProcessingController::processingPersisted,
            this, [this](const std::string& id,
                         const std::string& proc_path,
                         const core::ArtifactIndex& proc_index,
                         bool slant_range_corrected,
                         uint32_t baked_correction_flags) {
                onProcessingPersisted(id, proc_path, proc_index,
                                      slant_range_corrected,
                                      baked_correction_flags);
            });
}

void ProjectOperationCoordinator::connectToCorrections(CorrectionBatchOperator* corr_op)
{
    if (!corr_op) return;
    connect(corr_op, &CorrectionBatchOperator::correctionPersisted,
            this, [this](const std::string& layer_id,
                         const std::string& new_path,
                         const core::ArtifactIndex& new_index,
                         uint32_t baked_correction_flags) {
                onCorrectionPersisted(layer_id, new_path, new_index,
                                      baked_correction_flags);
            });
}

// ─── processing completion ────────────────────────────────────────────────────

void ProjectOperationCoordinator::onProcessingPersisted(
    const std::string& layer_id,
    const std::string& proc_path,
    const core::ArtifactIndex& proc_index,
    bool slant_range_corrected,
    uint32_t baked_correction_flags)
{
    if (!m_project) return;

    auto* layer = m_project->findLayer(layer_id);
    if (!layer) return;

    // ── 1. Commit result into DataLayer ──────────────────────────────────────
    if (layer->source_artifact_store_path.empty())
        layer->source_artifact_store_path = layer->artifact_store_path;
    layer->artifact_store_path    = proc_path;  // may be a per-layer sidecar
    layer->artifact_store_format  = "dlpd";
    layer->artifact_index         = proc_index;
    layer->pipeline_applied       = true;
    layer->processing_origin      = app::ProcessingOrigin::NodeGraph;
    layer->slant_range_corrected  = slant_range_corrected;
    layer->baked_correction_flags = baked_correction_flags;

    // ── 2. Compute graph hash for cache invalidation ──────────────────────────
    const std::string graph_json = layer->uses_project_graph
        ? m_project->processing_graph.toJson()
        : layer->node_graph.toJson();
    layer->applied_graph_json = graph_json;
    const std::string graph_hash =
        std::to_string(std::hash<std::string>{}(graph_json));

    // ── 3. Update worker status and hashes ────────────────────────────────────
    auto* worker = m_project->findWorker(layer_id);
    if (!worker) {
        app::workers::Worker w(layer_id);
        w.graph = layer->uses_project_graph
            ? m_project->processing_graph : layer->node_graph;
        worker = m_project->addWorker(std::move(w));
    }
    if (worker) {
        worker->markCompleted();
        worker->last_graph_hash = graph_hash;
    }

    // ── 4. Publish ProcessedLayer contract ────────────────────────────────────
    {
        app::contracts::ContractEnvelope env;
        env.id                 = layer_id + "_proc";
        env.binding_key        = layer_id;
        env.type               = app::contracts::ContractType::ProcessedLayer;
        env.producer_worker_id = layer_id;

        app::contracts::ProcessedLayer pl;
        pl.layer_id              = layer_id;
        pl.source_id             = layer->source_id;
        pl.artifact_store_path   = proc_path;
        pl.artifact_store_format = "dlpd";
        pl.artifact_index        = proc_index;
        pl.graph_hash            = graph_hash;
        pl.modality              = layer->modality;
        env.payload = std::move(pl);
        m_project->contractStore().publish(layer_id, {std::move(env)});
    }

    // ── 5. Mark project dirty, persist through the session authority, notify ──
    if (m_event_bus) m_event_bus->postProjectModified();
    emit persistenceRequested();
    if (m_event_bus) m_event_bus->postLayerDataChanged(layer_id);
}

// ─── correction completion ────────────────────────────────────────────────────

void ProjectOperationCoordinator::onCorrectionPersisted(
    const std::string& layer_id,
    const std::string& new_path,
    const core::ArtifactIndex& new_index,
    uint32_t baked_correction_flags)
{
    if (!m_project) return;

    auto* layer = m_project->findLayer(layer_id);
    if (!layer) return;

    // ── 1. Commit result into DataLayer ──────────────────────────────────────
    if (layer->source_artifact_store_path.empty())
        layer->source_artifact_store_path = layer->artifact_store_path;
    layer->artifact_store_path   = new_path;
    layer->artifact_store_format = "dlpd";
    layer->artifact_index        = new_index;
    layer->pipeline_applied      = true;
    layer->processing_origin     = app::ProcessingOrigin::Waterfall;
    layer->applied_graph_json.clear();
    layer->baked_correction_flags = baked_correction_flags;

    // ── 2. Mark worker dirty so the next graph run re-executes with new input ─
    if (auto* worker = m_project->findWorker(layer_id))
        worker->markDirty();

    // ── 3. Publish CorrectionApplied contract ─────────────────────────────────
    {
        app::contracts::ContractEnvelope env;
        env.id                 = layer_id + "_corr";
        env.binding_key        = layer_id;
        env.type               = app::contracts::ContractType::CorrectionApplied;
        env.producer_worker_id = layer_id;

        app::contracts::CorrectionApplied ca;
        ca.layer_id            = layer_id;
        ca.artifact_store_path = new_path;
        ca.artifact_index      = new_index;
        env.payload = std::move(ca);
        m_project->contractStore().publish(layer_id, {std::move(env)});
    }

    // ── 4. Mark project dirty, persist through the session authority, notify ──
    if (m_event_bus) m_event_bus->postProjectModified();
    emit persistenceRequested();
    if (m_event_bus) m_event_bus->postLayerDataChanged(layer_id);
}

} // namespace dolphin::ui
