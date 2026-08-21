// MainWindow.Runtime.cpp - process-wide state and service initialization.
#include "ui/mainwindow/MainWindow.h"

#include "ui/bottom/RuntimeLogBridge.h"
#include "ui/features/map/MapView.h"
#include "ui/features/map/MapViewportHost.h"
#include "ui/features/map/sidescan/SidescanViewController.h"
#include "ui/features/import/ImportProgressDialog.h"
#include "ui/features/waterfall/WaterfallWindow.h"
#include "ui/mainwindow/rightpanel/RightPanelHost.h"
#include "ui/shared/panels/LineListPanel.h"
#include "ui/shell/Features.h"
#include "app/services/ImportService.h"
#include "app/services/ProcessingService.h"

#include <QAction>

namespace dolphin::ui {

void MainWindow::setupRuntimeServices()
{
    // Settings authority — must exist before any code that reads live settings.
    m_app_state = new AppState(this);
    m_window_registry = new WindowRegistry(this);
    m_event_bus = new ProjectEventBus(this);

    // Display-state authority — owns per-view state (map preview quality, …),
    // bridges global defaults from AppState, and is the single bus for per-layer
    // display changes via displayStateChanged(layer_id, aspect).
    m_display_state = new DisplayStateManager(m_app_state, this);
    m_display_state->loadPersistentState();
    connect(m_display_state, &DisplayStateManager::displayStateChanged, this,
            [this](const QString& layer_id, DisplayAspect aspect) {
        if (aspect == DisplayAspect::MapQuality) {
            if (m_sss_ctrl) m_sss_ctrl->setMapSonarQuality(m_display_state->mapQuality());
            const int cur = static_cast<int>(m_display_state->mapQuality());
            for (int i = 0; i < static_cast<int>(m_act_map_quality.size()); ++i)
                if (m_act_map_quality[i]) m_act_map_quality[i]->setChecked(i == cur);
        }
        // Global SSS palette change (empty layer_id): keep every visible control
        // and renderer on the same palette name/look.
        if (aspect == DisplayAspect::Palette && layer_id.isEmpty()) {
            const int pal = m_display_state->mapPalette();
            if (m_modal_host)    m_modal_host->setPalette(pal);
            if (m_waterfall_win) m_waterfall_win->setPalette(pal);
            if (m_sss_ctrl)      m_sss_ctrl->setPaletteIndex(pal);
            if (m_viewport_host) m_viewport_host->setSonarPalette(pal);
        }
        // A per-layer display change (palette/gain/visibility/nav) means the project
        // look differs from disk — mark it dirty so it's saved.
        if (!layer_id.isEmpty())
            markProjectDirty();
        // Per-layer visibility: fan out to every widget that renders or lists
        // the layer. The model write lives in DisplayStateManager::
        // setLayerVisible — this is the ONLY sync point (undo/redo replays
        // through the same setter and lands here too).
        if (aspect == DisplayAspect::Visibility && !layer_id.isEmpty()) {
            const std::string lid = layer_id.toStdString();
            const auto* l = currentProject() ? currentProject()->findLayer(lid)
                                             : nullptr;
            const bool v = l && l->visible;
            if (m_viewport_host) m_viewport_host->setLayerVisible(lid, v);
            else if (m_map_view) m_map_view->setLayerVisible(lid, v);
            if (m_line_list)     m_line_list->setLayerVisibility(lid, v);

            // Checkbox-on means materialize this layer; selection is independent.
            // Keep overview lines background/Low and promote only the selected line.
            if (v && l && l->modality == app::Modality::Sidescan && m_sss_ctrl) {
                m_sss_ctrl->showNavTrackFromIndex(lid, currentProject());
                m_sss_ctrl->activateLayer(lid, currentProject(),
                                          lid == activeLayerId(),
                                          /*cache_only=*/false);
            }
        }
        // Per-layer map compositing (transparency + blend mode): fan out to the
        // map viewport (2D mosaic + 3D drape/curtain for opacity; 2D-only for
        // blend). Model writes live in DisplayStateManager::setLayerOpacity /
        // setLayerBlendMode, both of which emit this aspect.
        if (aspect == DisplayAspect::Opacity && !layer_id.isEmpty()) {
            const std::string lid = layer_id.toStdString();
            const auto* l = currentProject() ? currentProject()->findLayer(lid)
                                             : nullptr;
            if (l && m_viewport_host) {
                m_viewport_host->setLayerOpacity(lid, l->map_opacity);
                m_viewport_host->setLayerBlendMode(lid, l->map_blend_mode);
                m_viewport_host->setLayerClipPolygons(lid, l->map_clip_polygons);
                m_viewport_host->setLayerShowBeams(lid, l->map_show_beams);
                m_viewport_host->setLayerBeamSpacing(lid, l->map_beam_spacing);
            }
        }
        // SBP palette change → recolour the 3D curtains (shader uniform, free).
        if (aspect == DisplayAspect::Palette && !layer_id.isEmpty()
                && currentProject() && m_viewport_host) {
            if (const auto* l = currentProject()->findLayer(layer_id.toStdString());
                    l && l->modality == app::Modality::SubBottom)
                m_viewport_host->setSbpCurtainPalette(
                    l->sbp_palette >= 0 ? l->sbp_palette : 0);
        }
        // Keep the left Views panel mirroring the authority (palette/quality
        // changes made anywhere: status bar, menu, inspector, Views itself).
        if (aspect == DisplayAspect::MapQuality || aspect == DisplayAspect::Palette)
            refreshViewsPanel();
    });
    connect(m_app_state, &AppState::settingsChanged,
            this, &MainWindow::applyLiveSettings);
    // Sound velocity change: reload the waterfall so the pipeline picks up
    // the new value on its next setLayer() call.
    auto reloadWaterfall = [this]() {
        if (!m_waterfall_win || !m_waterfall_win->isVisible()) return;
        if (!currentProject() || activeLayerId().empty()) return;
        if (auto* layer = currentProject()->findLayer(activeLayerId())) {
            const auto* src = currentProject()->findSource(layer->source_id);
            m_waterfall_win->setLayer(layer,
                src ? src->path : std::string{},
                src ? src->size_bytes : 0);
            applyStoredNavParams(activeLayerId());
        }
    };
    connect(m_app_state, &AppState::soundVelocityChanged, this,
            [reloadWaterfall](double) { reloadWaterfall(); });
    connect(m_app_state, &AppState::autoStretchChanged, this,
            [this](bool enabled) {
                if (m_sss_ctrl)
                    m_sss_ctrl->setAutoStretchEnabled(enabled);
                if (m_waterfall_win && m_waterfall_win->isVisible())
                    m_waterfall_win->invalidateProcessedCache();
            });
    // Default palette change: sync waterfall and inspector to the new default.
    connect(m_app_state, &AppState::defaultPaletteChanged,
            this, &MainWindow::onPaletteChanged);

    // Hub must exist before setupCentralWidget (which builds BottomDockPanel).
    m_diag_hub = new DiagnosticsHub(this);

    // PSC must be created after m_undo_stack, m_diag_hub, m_op_mgr, m_import_service.
    // It is wired to MainWindow signals below, after services are ready.
    {
        auto* runtime_logs = RuntimeLogBridge::instance();
        connect(runtime_logs, &RuntimeLogBridge::messageLogged,
                this, [this](int type, const QString& message) {
            if (!m_diag_hub || message.isEmpty()) return;

            const auto qt_type = static_cast<QtMsgType>(type);
            const QString prefix =
                qt_type == QtDebugMsg    ? QStringLiteral("[DEBUG] ") :
                qt_type == QtInfoMsg     ? QStringLiteral("[INFO] ")  :
                qt_type == QtWarningMsg  ? QStringLiteral("[WARN] ")  :
                qt_type == QtCriticalMsg ? QStringLiteral("[ERROR] ") :
                                           QStringLiteral("[FATAL] ");

            m_diag_hub->logOutput(prefix + message);

            if (qt_type == QtWarningMsg || qt_type == QtCriticalMsg || qt_type == QtFatalMsg) {
                const auto severity = qt_type == QtWarningMsg
                    ? DiagnosticsHub::Severity::Warning
                    : DiagnosticsHub::Severity::Error;
                m_diag_hub->postProblem(message, severity, QStringLiteral("runtime"));
            }
        });
    }

    // Central operation manager — bridges lifecycle signals to DiagnosticsHub
    // so any subsystem using m_op_mgr->run<T>() gets automatic job tracking.
    m_op_mgr = new app::OperationManager(this);
    connect(m_op_mgr, &app::OperationManager::operationQueued, this,
            [this](uint32_t op_id, const QString& name) {
                // Submitted but parked behind a lane cap — show as Queued, not Running.
                m_op_job_ids[op_id] = m_diag_hub->beginJob(
                    name, {}, 0, {}, 0.f, DiagnosticsHub::JobStatus::Queued);
                if (m_import_overlay)
                    m_import_overlay->addJob("op:" + std::to_string(op_id),
                                             name, "RUN", 0.f, false);
            });
    connect(m_op_mgr, &app::OperationManager::operationStarted, this,
            [this](uint32_t op_id, const QString& name) {
                // If it was queued, flip it to Running; otherwise create it running.
                const auto it = m_op_job_ids.find(op_id);
                if (it != m_op_job_ids.end()) m_diag_hub->startJob(it->second);
                else m_op_job_ids[op_id] = m_diag_hub->beginJob(name);
                if (m_import_overlay) {
                    const std::string row_id = "op:" + std::to_string(op_id);
                    m_import_overlay->addJob(row_id, name, "RUN", 0.f, false);
                    m_import_overlay->updateJob(row_id, 0, tr("Running"));
                }
            });
    connect(m_op_mgr, &app::OperationManager::operationCompleted, this,
            [this](uint32_t op_id) {
                const auto it = m_op_job_ids.find(op_id);
                if (it != m_op_job_ids.end()) {
                    m_diag_hub->endJob(it->second);
                    m_op_job_ids.erase(it);
                }
                if (m_import_overlay)
                    m_import_overlay->finishJob("op:" + std::to_string(op_id),
                                                tr("Completed"));
            });
    connect(m_op_mgr, &app::OperationManager::operationFailed, this,
            [this](uint32_t op_id, const QString& error) {
                const auto it = m_op_job_ids.find(op_id);
                if (it != m_op_job_ids.end()) {
                    m_diag_hub->failJob(it->second, error);
                    m_op_job_ids.erase(it);
                }
                if (m_import_overlay)
                    m_import_overlay->failJob("op:" + std::to_string(op_id), error);
            });
    connect(m_op_mgr, &app::OperationManager::operationCancelled, this,
            [this](uint32_t op_id) {
                const auto it = m_op_job_ids.find(op_id);
                if (it != m_op_job_ids.end()) {
                    m_diag_hub->cancelJob(it->second);
                    m_op_job_ids.erase(it);
                }
                if (m_import_overlay)
                    m_import_overlay->cancelJob("op:" + std::to_string(op_id));
            });

    // Route AppState notifications → output log and (for warnings/errors) DiagnosticsHub.
    connect(m_app_state, &AppState::notificationPosted,
            this, [this](const Notification& n) {
                appendJobMessage(QString::fromStdString(n.message));
                if (n.severity == NotificationSeverity::Warning
                        || n.severity == NotificationSeverity::Error) {
                    const auto sev = n.severity == NotificationSeverity::Error
                        ? DiagnosticsHub::Severity::Error
                        : DiagnosticsHub::Severity::Warning;
                    m_diag_hub->postProblem(
                        QString::fromStdString(n.message), sev,
                        QString::fromStdString(n.source));
                }
            });

    // PSC is constructed after import_service is available (see below).
    if constexpr (Features::kImport) {
        m_import_service = new app::ImportService(this);

        // Route ImportService indexing lifecycle → DiagnosticsHub structured jobs
        // so the bottom-panel Jobs tab tracks every file import automatically.
        // Cache-index rebuilds (project open) share the same signals but must not
        // show the import popup — isRebuildingLayer() distinguishes them.
        connect(m_import_service, &app::ImportService::indexingStarted, this,
                [this](const std::string& layer_id) {
                    if (m_import_service->isRebuildingLayer(layer_id)) return;
                    QString label = tr("Importing…");
                    if (currentProject()) {
                        if (const auto* layer = currentProject()->findLayer(layer_id))
                            label = tr("Importing %1")
                                .arg(QString::fromStdString(layer->label));
                    }
                    const uint32_t jid = m_diag_hub->beginJob(
                        label, QString::fromStdString(layer_id));
                    m_import_job_ids[layer_id] = jid;
                });
        connect(m_import_service, &app::ImportService::indexingProgress, this,
                [this](const std::string& layer_id, int percent) {
                    const auto it = m_import_job_ids.find(layer_id);
                    if (it != m_import_job_ids.end())
                        m_diag_hub->updateJob(it->second, {}, percent / 100.f);
                });
        connect(m_import_service, &app::ImportService::indexingComplete, this,
                [this](const std::string& layer_id) {
                    const auto it = m_import_job_ids.find(layer_id);
                    if (it != m_import_job_ids.end()) {
                        m_diag_hub->endJob(it->second, tr("Ready"));
                        m_import_job_ids.erase(it);
                    }
                    if (currentProject()) {
                        if (const auto* layer = currentProject()->findLayer(layer_id))
                            recordActivity(ActivityKind::Import,
                                tr("Imported: %1")
                                    .arg(QString::fromStdString(layer->label)));
                    }
                });
        connect(m_import_service, &app::ImportService::indexingFailed, this,
                [this](const std::string& layer_id, const std::string& error) {
                    const auto it = m_import_job_ids.find(layer_id);
                    if (it != m_import_job_ids.end()) {
                        m_diag_hub->failJob(it->second,
                            QString::fromStdString(error));
                        m_import_job_ids.erase(it);
                    } else {
                        // No import job registered — this was a cache rebuild failure.
                        // Report through diagnostics without a modal popup.
                        m_diag_hub->postProblem(
                            tr("Could not load cached data for layer %1: %2")
                                .arg(QString::fromStdString(layer_id),
                                     QString::fromStdString(error)),
                            DiagnosticsHub::Severity::Warning, "cache-rebuild");
                        return;
                    }
                });

    }

    if constexpr (Features::kProcessing)
        m_processing_service = new app::ProcessingService(this);

}

} // namespace dolphin::ui
