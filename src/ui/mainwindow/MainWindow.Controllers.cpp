// MainWindow.Controllers.cpp - feature-controller construction and wiring.
#include "ui/mainwindow/MainWindow.h"

#include "ui/features/import/ImportController.h"
#include "ui/features/import/ImportProgressDialog.h"
#include "ui/features/map/MapView.h"
#include "ui/features/map/sidescan/SidescanViewController.h"
#include "ui/features/processing/ProcessingController.h"
#include "ui/mainwindow/MainStatusBar.h"
#include "ui/mainwindow/ProjectSessionController.h"
#include "ui/mainwindow/coordinators/CorrectionBatchOperator.h"
#include "ui/mainwindow/coordinators/ProjectOperationCoordinator.h"
#include "ui/shared/panels/LineListPanel.h"
#include "ui/systems/ProjectEventBus.h"
#include "ui/systems/WindowRegistry.h"
#include "ui/shell/Features.h"

namespace dolphin::ui {

void MainWindow::setupFeatureControllers()
{
    // Only the live coordinate (posLabel) is shown in the status bar. The old
    // ping/track-length and cursor-depth readouts were noise (and floated, overlapping
    // the project name), so they're gone — pass nullptr; the controller null-checks both.
    m_sss_ctrl = new SidescanViewController(
        m_map_view,
        /*status_ping=*/nullptr, m_status_bar->posLabel(), /*status_depth=*/nullptr, this);
    m_sss_ctrl->setOperationManager(m_op_mgr);  // owns per-layer map-build ops (keyed)
    connect(m_sss_ctrl, &SidescanViewController::contactPicked,
            this, &MainWindow::onContactPicked);
    connect(m_sss_ctrl, &SidescanViewController::loadingStarted, this, [this]() {
        // A sidescan map build reports real 0→100 progress (loadingProgress), so mark
        // it progress-driven up front — otherwise refreshLoadingIndicator would flash
        // the indeterminate spinner before the first progress tick (the whole show on
        // a fast/cached load). The determinate bar appears with the first tick.
        m_map_progress_active = true;
        refreshLoadingIndicator();
    });
    connect(m_sss_ctrl, &SidescanViewController::loadingFinished, this, [this]() {
        // Only clear the indicator if no other viewer (or concurrent map build)
        // is still busy — the controller's state is updated before this fires.
        refreshLoadingIndicator();
        if (m_import_ctrl)
            m_import_ctrl->onMapLoadDone();
    });
    // Determinate progress for the active layer's map build (0→100): fills the
    // status-bar bar from start to end instead of the indeterminate bounce.
    connect(m_sss_ctrl, &SidescanViewController::loadingProgress, this, [this](int pct) {
        m_map_progress_active = true;
        if (m_status_bar) {
            m_status_bar->setProgress(pct, true);
            // In-window background-task feedback (no top-level popup → no blink on the
            // frameless window). Cleared by refreshLoadingIndicator when idle.
            m_status_bar->setBusyText(tr("Building map…"));
        }
    });
    // Per-line progress for a bottom-bar Apply batch: update that line's dialog card
    // with a readable phase (the map build reports ~5–55 decode, ~66 corrections,
    // ~80 georeference, ~98 raster).
    connect(m_sss_ctrl, &SidescanViewController::prebuildTierProgress, this,
            [this](const std::string& layer_id, int pct) {
        if (!m_import_overlay) return;
        const auto it = m_tools_apply_layers.find(layer_id);
        if (it == m_tools_apply_layers.end()) return;
        const QString& tools = it->second;   // "TVG, ARC, ARN" (empty = display only)
        const QString phase =
            pct < 55  ? tr("Reading pings…")        :
            pct < 70  ? (tools.isEmpty() ? tr("Applying corrections…")
                                         : tr("Applying %1…").arg(tools)) :
            pct < 85  ? tr("Georeferencing…")       :
            pct < 100 ? tr("Building mosaic…")      :
                        tr("Finishing…");
        m_import_overlay->updateJob(layer_id, pct,
                                    QStringLiteral("%1  %2%").arg(phase).arg(pct));
    });
    connect(m_sss_ctrl, &SidescanViewController::prebuildTierFinished, this,
            [this](const std::string& layer_id, MapSonarQuality) {
        refreshLoadingIndicator();
        if (m_import_overlay && m_tools_apply_layers.erase(layer_id) > 0) {
            m_import_overlay->finishJob(layer_id, tr("Corrections applied"));
        }
    });
    connect(m_sss_ctrl, &SidescanViewController::mapDiagnosticsReady,
            this, &MainWindow::onMapDiagnosticsReady);

    // Register the map controller so WindowRegistry broadcasts reach it.
    // Uses m_map_view as the host widget for auto-cleanup on destroy.
    m_window_registry->registerViewer(m_map_view, m_sss_ctrl);

    // Single source of truth for correction bakes (SSS + SBP).
    // CorrectionBatchOperator owns both services and routes lifecycle events
    // through DiagnosticsHub and ExecutionProgressDialog internally.
    m_corr_op = new CorrectionBatchOperator(m_diag_hub, m_import_overlay, this);
    // correctionPersisted: layer mutation, contract, save, and viewer reload
    // are handled entirely by ProjectOperationCoordinator (wired below).
    // MainWindow only emits the user-visible log message.
    connect(m_corr_op, &CorrectionBatchOperator::correctionPersisted,
            this, [this](const std::string& layer_id,
                         const std::string& /*new_path*/,
                         const core::ArtifactIndex& /*new_index*/) {
                const auto* layer = currentProject() ? currentProject()->findLayer(layer_id) : nullptr;
                if (layer)
                    appendJobMessage(tr("Corrections baked into %1")
                        .arg(QString::fromStdString(layer->label)));
            });
    connect(m_corr_op, &CorrectionBatchOperator::correctionSkipped,
            this, [this](const std::string& layer_id) {
                const auto* layer = currentProject() ? currentProject()->findLayer(layer_id) : nullptr;
                if (layer)
                    appendJobMessage(tr("Corrections already baked — skipped %1")
                        .arg(QString::fromStdString(layer->label)));
            });
    connect(m_corr_op, &CorrectionBatchOperator::correctionFailed,
            this, [this](const std::string& /*layer_id*/, const QString& error) {
                appendJobMessage(tr("Correction bake failed: %1").arg(error));
            });
    connect(m_corr_op, &CorrectionBatchOperator::batchComplete,
            this, [this](int succeeded, int total) {
                appendJobMessage(tr("Correction bake complete: %1/%2 lines")
                    .arg(succeeded).arg(total));
            });

    {
        // Apply the persisted map sonar quality from the display-state authority
        // (loaded in the ctor; default CoverageOnly — coverage + nav instantly, no
        // raster cost until the user picks a higher tier).
        m_sss_ctrl->setMapSonarQuality(m_display_state->mapQuality());
        // Seed the manager's global map palette from the controller's computed initial
        // (the controller keeps the app-default-name fallback for a fresh install); the
        // manager owns it from here on.
        m_display_state->initMapPalette(m_sss_ctrl->paletteIndex());
    }
    if constexpr (Features::kImport) {
        m_import_job_mgr = new app::ImportJobManager(m_import_service, this);
        m_import_ctrl = new ExecutionController(
            m_import_job_mgr, m_import_overlay, this, this);
        m_import_ctrl->connectToCacheRebuilds(m_import_service);
        // Project switch / new / close must warn about in-flight imports the
        // same way app close does — otherwise a switch abandons them silently.
        m_session_ctrl->setImportsBusyCheck(
            [this]() { return m_import_ctrl && m_import_ctrl->importsBusy(); });
        // Escape hatch for that dialog: drop queued files (the in-flight
        // decode settles on its own; ensureImportsIdle waits for it, bounded).
        m_session_ctrl->setImportsCancelRequest([this]() {
            if (m_import_job_mgr)
                m_import_job_mgr->cancelQueue(tr("Imports cancelled by operator"));
        });
        connect(m_import_ctrl, &ExecutionController::importFailed,
                this, [this](const QString& layer_id, const QString& error) {
                    m_diag_hub->postProblem(
                        tr("Import failed: %1").arg(error),
                        DiagnosticsHub::Severity::Error, layer_id);
                });
        connect(m_import_ctrl, &ExecutionController::importCompleted,
                this, [this](const std::string& layer_id) {
                    // Only Sidescan layers trigger a background map-build task.
                    // For all other modalities onLayerSelected returns synchronously
                    // and loadingFinished never fires, so don't call onMapLoadPending.
                    const auto* layer = currentProject() ? currentProject()->findLayer(layer_id) : nullptr;
                    const bool  needs_map_build = layer
                        && layer->modality == app::Modality::Sidescan;
                    if (needs_map_build)
                        m_import_ctrl->onMapLoadPending();
                    // Evict stale map data so activateLayer() does a fresh load.
                    if (m_sss_ctrl)
                        m_sss_ctrl->unloadLayer(layer_id);
                    // For SBP layers, clear the stale map profile so onLayerSelected
                    // rebuilds it. On fresh import m_map_view has no data for this
                    // layer yet so this is a no-op; on reindex it forces a rebuild.
                    if (layer && layer->modality == app::Modality::SubBottom && m_map_view)
                        m_map_view->removeLayerData(layer_id);
                    onLayerSelected(layer_id);
                    // Broadcast to all open viewers (handles the reindex case where a
                    // viewer already showing this layer holds stale pre-reindex data).
                    m_event_bus->postLayerDataChanged(layer_id);
                    // Modality and index are finalised by the time importCompleted
                    // fires — rebuild the tree so the layer lands in the right group,
                    // and refresh the inspector's modality set so the right panel
                    // shows the correct sections for the newly indexed layer.
                    if (m_line_list) m_line_list->refresh();
                    refreshInspectorModalities();
                });
        connect(m_import_ctrl, &ExecutionController::cacheLayerReady,
                this, [this](const std::string& layer_id) {
                    if (!currentProject()) return;
                    auto* layer = currentProject()->findLayer(layer_id);
                    if (!layer || !layer->index_built) return;

                    using M = app::Modality;
                    const bool needs_map_build =
                        layer->modality == M::Sidescan && m_sss_ctrl;

                    // Lazy: a freshly-reindexed layer only loads into the map when it
                    // IS the active selection (or when nothing is selected yet — then
                    // it becomes the selection). Other reindexed layers stay indexed
                    // but unloaded, so opening a project with N lines doesn't rebuild
                    // N mosaics — only the one the user is looking at.
                    if (activeLayerId().empty()) {
                        if (needs_map_build) m_import_ctrl->onMapLoadPending();
                        onLayerSelected(layer_id);
                    } else if (layer_id == activeLayerId()) {
                        if (needs_map_build) {
                            m_import_ctrl->onMapLoadPending();
                            m_sss_ctrl->activateLayer(layer_id, currentProject());
                        } else if (m_map_view) {
                            m_map_view->setActiveLayer(layer_id);
                            m_map_view->setNavTrackVisible(layer_id, true);
                        }
                    } else if (needs_map_build) {
                        // Reindexed non-active sidescan layer: instant nav-track
                        // overview, then load its raster as a non-active overview
                        // layer (cache-first; builds + caches on miss) so the whole
                        // survey shows as raster, not just the active line.
                        m_sss_ctrl->showNavTrackFromIndex(layer_id, currentProject());
                        m_sss_ctrl->activateLayer(layer_id, currentProject(),
                                                  /*as_active=*/false);
                    } else if (layer->modality == M::SubBottom) {
                        // SSS/SBP parity: a reindexed non-active SBP line shows on
                        // the map too — instant track, then the profile ribbon.
                        if (m_sss_ctrl)
                            m_sss_ctrl->showNavTrackFromIndex(layer_id, currentProject());
                        if (m_op_mgr) m_op_mgr->setLaneCap("sbp:open", 2);
                        buildSbpProfileMap(layer, "sbp:open");
                    }
                    // Refresh the tree so the reindexed layer shows ready.
                    if (m_line_list) m_line_list->refresh();

                    markProjectDirty();
                });
        connect(m_import_ctrl, &ExecutionController::statusMessage,
                this, &MainWindow::appendJobMessage);
        connect(m_import_ctrl, &ExecutionController::progressChanged,
                this, [this](int percent, bool visible) {
                    m_status_bar->setProgress(percent, visible);
                });
    }
    if constexpr (Features::kProcessing) {
        m_proc_ctrl = new ProcessingController(m_processing_service, this);
        connect(m_proc_ctrl, &ProcessingController::statusMessage,
                this, &MainWindow::appendJobMessage);
        connect(m_proc_ctrl, &ProcessingController::progressChanged,
                this, [this](int percent, bool visible) {
                    m_status_bar->setProgress(percent, visible);
                });
        // Feed processing jobs into the overlay dialog and DiagnosticsHub.
        connect(m_proc_ctrl, &ProcessingController::layerRunStarted,
                this, [this](const std::string& id, const QString& label) {
                    m_import_job_ids[id] = m_diag_hub->beginJob(
                        tr("Processing: %1").arg(label), QString::fromStdString(id));
                    m_import_overlay->addJob(id, label, "RUN", 0.f);
                });
        connect(m_proc_ctrl, &ProcessingController::layerRunFinished,
                this, [this](const std::string& id, const QString& summary) {
                    const auto it = m_import_job_ids.find(id);
                    if (it != m_import_job_ids.end()) {
                        m_diag_hub->endJob(it->second, summary);
                        m_import_job_ids.erase(it);
                    }
                    m_import_overlay->finishJob(id, summary);
                    // Viewer reload happens in processingPersisted (after index is updated).
                    // If no data was written (empty buffer) the .dlpd is unchanged — no reload needed.
                });
        connect(m_proc_ctrl, &ProcessingController::layerRunFailed,
                this, [this](const std::string& id, const QString& error) {
                    const auto it = m_import_job_ids.find(id);
                    if (it != m_import_job_ids.end()) {
                        m_diag_hub->failJob(it->second, error);
                        m_import_job_ids.erase(it);
                    }
                    m_import_overlay->failJob(id, error);
                    m_diag_hub->postProblem(
                        tr("Processing failed: %1").arg(error),
                        DiagnosticsHub::Severity::Error, QString::fromStdString(id));
                });
        // processingPersisted: layer mutation, contract, save, and viewer reload
        // are handled entirely by ProjectOperationCoordinator (wired below).
        // Nothing left for MainWindow to do here.
    }

    // ProjectOperationCoordinator — bridges service completion into the Worker
    // registry, ContractStore, event bus, and project persistence.
    m_op_coord = new ProjectOperationCoordinator(m_event_bus, this);
    m_op_coord->connectToProcessing(m_proc_ctrl);
    m_op_coord->connectToCorrections(m_corr_op);
    connect(m_op_coord, &ProjectOperationCoordinator::persistenceRequested,
            m_session_ctrl, &ProjectSessionController::autoSave);

}

} // namespace dolphin::ui
