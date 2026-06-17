// MainWindow.cpp — constructor + service wiring only
#include "ui/mainwindow/MainWindow.h"
#include "ui/mainwindow/ProjectSessionController.h"
#include "ui/systems/ProjectEventBus.h"
#include "ui/mainwindow/MainStatusBar.h"
#include "ui/bottom/BottomDockPanel.h"
#include "ui/bottom/RuntimeLogBridge.h"
#include "ui/shell/AppStyle.h"
#include "ui/shell/Features.h"
#include "ui/features/nodegraph/NodeGraphWindow.h"
#include "ui/features/import/ImportController.h"
#include "ui/features/processing/ProcessingController.h"
#include "ui/features/map/sidescan/SidescanViewController.h"
#include "ui/mainwindow/coordinators/CorrectionBatchOperator.h"
#include "ui/mainwindow/coordinators/ProjectOperationCoordinator.h"
#include "ui/mainwindow/coordinators/ViewportCoordinator.h"
#include "ui/features/import/ImportProgressDialog.h"
#include "ui/features/waterfall/WaterfallWindow.h"
#include "ui/features/subbottom/SubBottomWindow.h"
#include "ui/shared/dialogs/SettingsDialog.h"
#include "ui/shared/widgets/LayerPickerWidget.h"
#include "ui/features/map/MapView.h"
#include "ui/features/map/MapViewportHost.h"
#include "app/services/ImportService.h"
#include "app/services/ProcessingService.h"
#include "ui/shared/panels/LineListPanel.h"
#include "ui/mainwindow/AppSettingsDialog.h"
#include "ui/shell/AppInfo.h"
#include <QApplication>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QUndoStack>

namespace dolphin::ui {

static constexpr int kMinW = 1440;
static constexpr int kMinH = 900;

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    qApp->setStyleSheet(AppStyle::sheet());

    setWindowTitle(tr("Dolphin Explorer"));
    setMinimumSize(kMinW, kMinH);
    setAcceptDrops(true);
    setWindowFlag(Qt::FramelessWindowHint);

    m_undo_stack = new QUndoStack(this);

    // Settings authority — must exist before any code that reads live settings.
    m_app_state = new AppState(this);
    m_window_registry = new WindowRegistry(this);
    m_event_bus = new ProjectEventBus(this);
    connect(m_app_state, &AppState::settingsChanged,
            this, &MainWindow::applyLiveSettings);
    // Sound velocity change: reload the waterfall so the pipeline picks up
    // the new value on its next setLayer() call.
    auto reloadWaterfall = [this]() {
        if (!m_waterfall_win || !m_waterfall_win->isVisible()) return;
        if (!currentProject() || activeLayerId().empty()) return;
        if (auto* layer = currentProject()->findLayer(activeLayerId())) {
            const auto* src = currentProject()->findSource(layer->source_id);
            m_waterfall_win->setLayer(layer, m_import_service,
                src ? src->path : std::string{},
                src ? src->size_bytes : 0);
            applyStoredNavParams(activeLayerId());
            if (layer->sss_display_state.customized)
                m_waterfall_win->applyExternalParams(layer->sss_display_state.params);
        }
    };
    connect(m_app_state, &AppState::soundVelocityChanged, this,
            [reloadWaterfall](double) { reloadWaterfall(); });
    connect(m_app_state, &AppState::autoStretchChanged, this,
            [this](bool) {
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
            });
    connect(m_op_mgr, &app::OperationManager::operationStarted, this,
            [this](uint32_t op_id, const QString& name) {
                // If it was queued, flip it to Running; otherwise create it running.
                const auto it = m_op_job_ids.find(op_id);
                if (it != m_op_job_ids.end()) m_diag_hub->startJob(it->second);
                else m_op_job_ids[op_id] = m_diag_hub->beginJob(name);
            });
    connect(m_op_mgr, &app::OperationManager::operationCompleted, this,
            [this](uint32_t op_id) {
                const auto it = m_op_job_ids.find(op_id);
                if (it != m_op_job_ids.end()) {
                    m_diag_hub->endJob(it->second);
                    m_op_job_ids.erase(it);
                }
            });
    connect(m_op_mgr, &app::OperationManager::operationFailed, this,
            [this](uint32_t op_id, const QString& error) {
                const auto it = m_op_job_ids.find(op_id);
                if (it != m_op_job_ids.end()) {
                    m_diag_hub->failJob(it->second, error);
                    m_op_job_ids.erase(it);
                }
            });
    connect(m_op_mgr, &app::OperationManager::operationCancelled, this,
            [this](uint32_t op_id) {
                const auto it = m_op_job_ids.find(op_id);
                if (it != m_op_job_ids.end()) {
                    m_diag_hub->cancelJob(it->second);
                    m_op_job_ids.erase(it);
                }
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

    // --- ProjectSessionController + LayerDisplayCoordinator -----------------
    // PSC owns: m_project, m_project_dirty, m_pending_crs, and all CRUD slots.
    // LDC owns: m_active_layer_id, navigation history.
    // Aspect files access these via currentProject() / activeLayerId() helpers.
    m_session_ctrl = new ProjectSessionController(
        m_undo_stack, m_diag_hub, m_op_mgr, m_import_service, this, this);
    m_layer_ctrl  = new LayerDisplayCoordinator(m_session_ctrl, this);

    // openProject() for cache-only files encodes the path in a "job message"
    // prefixed with __import_cache__: so MainWindow can intercept it.
    connect(m_session_ctrl, &ProjectSessionController::jobMessage,
            this, [this](const QString& msg) {
        if (msg.startsWith("__import_cache__:"))
            showImportDialog({msg.mid(17)});
        else
            appendJobMessage(msg);
    });
    connect(m_session_ctrl, &ProjectSessionController::windowTitleChanged,
            this, &MainWindow::setWindowTitle);
    connect(m_session_ctrl, &ProjectSessionController::recentProjectsChanged,
            this, [this](const QStringList& paths) {
        refreshSidebarSections(paths);
        rebuildRecentMenu();
    });
    connect(m_session_ctrl, &ProjectSessionController::projectAboutToChange,
            this, [this]() {
        if (m_viewport_host) m_viewport_host->setUpdatesEnabled(false);
        if (m_sss_ctrl) m_sss_ctrl->deactivate(true);
        if (m_import_service) m_import_service->cancelPendingRebuild();
        m_layer_ctrl->clearActiveLayer();
        m_layer_ctrl->clearHistory();
    });
    connect(m_session_ctrl, &ProjectSessionController::projectChanged,
            this, [this](std::shared_ptr<app::Project>) {
        bindProjectUi();
        // For close/failed-open: re-enable viewport immediately.
        // For successful open: firstLayerReady() re-enables after next tick.
        if (!m_session_ctrl->project())
            if (m_viewport_host) m_viewport_host->setUpdatesEnabled(true);
    });
    connect(m_session_ctrl, &ProjectSessionController::firstLayerReady,
            this, [this](const std::string& first_layer_id) {
        if (m_viewport_host) m_viewport_host->setUpdatesEnabled(true);
        const auto proj = m_session_ctrl->project();
        if (!proj) return;

        // Frame the whole survey as the lines arrive (active mosaic + every other
        // line's nav track). Without this the view stays on the active line's extent
        // — a programmatic viewport sync sets the interaction flag, so lines whose
        // extent falls outside the active line's box are left off-screen.
        if (m_map_view) m_map_view->requestFrameSurvey();

        // Load the selected/restored layer's full mosaic first (priority). Count it
        // as a pending map load so the background panel surfaces "Building map" while
        // the survey loads (each load is balanced by loadingFinished → onMapLoadDone).
        if (!first_layer_id.empty()) {
            const auto* fl = proj->findLayer(first_layer_id);
            if (m_import_ctrl && fl && fl->modality == app::Modality::Sidescan)
                m_import_ctrl->onMapLoadPending();
            onLayerSelected(first_layer_id);
        }

        // …and show the whole survey as RASTER, not just the active line. For every
        // other indexed sidescan line: draw its nav track instantly from the index
        // (zero I/O) for immediate feedback, then load its raster as a non-active
        // overview layer. The raster load is cache-first — a fresh .draster paints
        // with no ping decode (instant on reopen); a cache miss builds + caches it in
        // the background (map lane, cap 2), upgrading the track to a raster. Lines not
        // yet indexed (footerless first open) get theirs as their reindex completes
        // (cacheLayerReady). The active line still loads first (priority).
        if (m_sss_ctrl) {
            for (const auto& layer : proj->layers()) {
                if (!layer || layer->id == first_layer_id) continue;
                if (!layer->index_built || layer->artifact_index.empty()) continue;
                if (layer->modality != app::Modality::Sidescan) continue;
                m_sss_ctrl->showNavTrackFromIndex(layer->id, proj.get());
                if (m_import_ctrl) m_import_ctrl->onMapLoadPending();
                m_sss_ctrl->activateLayer(layer->id, proj.get(), /*as_active=*/false);
            }
        }
    });

    // LayerDisplayCoordinator signals.
    connect(m_layer_ctrl, &LayerDisplayCoordinator::layerActivationRequested,
            this, &MainWindow::onLayerSelected);
    connect(m_layer_ctrl, &LayerDisplayCoordinator::navigationChanged,
            this, [this](bool back, bool fwd) {
        if (m_btn_nav_back)    m_btn_nav_back->setEnabled(back);
        if (m_btn_nav_forward) m_btn_nav_forward->setEnabled(fwd);
    });
    // -----------------------------------------------------------------------

    setupCentralWidget();
    RuntimeLogBridge::instance()->replayPending();
    setupToolBar();
    setupMenuBar();
    setupStatusBar();

    // Viewport coordinator — single path for all scale/rotation commands and feedback.
    m_viewport_coord = new ViewportCoordinator(m_viewport_host, this);
    connect(m_status_bar, &MainStatusBar::scaleChangeRequested,
            m_viewport_coord, &ViewportCoordinator::requestScale);
    connect(m_status_bar, &MainStatusBar::rotationChangeRequested,
            m_viewport_coord, &ViewportCoordinator::requestRotation);
    connect(m_viewport_coord, &ViewportCoordinator::stateChanged,
            this, [this](ViewportState s) {
        m_status_bar->setViewportInfo(s.mpp, s.rot_deg);
    });

    // CRS badge → open Geodetic Settings dialog.
    connect(m_status_bar, &MainStatusBar::crsClicked,
            this, &MainWindow::onGeodeticSettings);

    // -- Project event bus — one-time wiring, survives project replace -----
    // Static components (always exist after setupCentralWidget) connect here.
    // Lazy components (waterfall, sbp, node graph) are null-guarded so these
    // connections are safe to establish even before those windows are created.
    if (m_line_list) {
        connect(m_event_bus, &ProjectEventBus::layerPending,
                m_line_list, &LineListPanel::refresh);
        connect(m_event_bus, &ProjectEventBus::layerReady,
                this, [this](app::DataLayer* l) {
                    if (m_line_list && l) m_line_list->refreshLayer(l->id);
                });
        connect(m_event_bus, &ProjectEventBus::layerRemoved,
                m_line_list, &LineListPanel::refresh);
        connect(m_event_bus, &ProjectEventBus::layersReordered,
                m_line_list, &LineListPanel::refresh);
        connect(m_event_bus, &ProjectEventBus::contactAdded,
                m_line_list, &LineListPanel::refreshContacts);
        connect(m_event_bus, &ProjectEventBus::contactRemoved,
                m_line_list, &LineListPanel::refreshContacts);
    }
    if (m_map_view) {
        connect(m_event_bus, &ProjectEventBus::layersReordered,
                m_map_view, &MapView::refreshLayerOrder);
        connect(m_event_bus, &ProjectEventBus::contactAdded,
                m_map_view, static_cast<void (QWidget::*)()>(&QWidget::update));
        connect(m_event_bus, &ProjectEventBus::contactRemoved,
                m_map_view, static_cast<void (QWidget::*)()>(&QWidget::update));
    }
    // Inspector modality set changes when layers arrive or depart.
    connect(m_event_bus, &ProjectEventBus::layerReady,
            this, [this](app::DataLayer*) { refreshInspectorModalities(); });
    connect(m_event_bus, &ProjectEventBus::layerRemoved,
            this, [this](const std::string&) { refreshInspectorModalities(); });
    // Waterfall contact overlay — window may not exist yet.
    connect(m_event_bus, &ProjectEventBus::contactAdded,
            this, [this](const core::Contact&) {
                if (m_waterfall_win && m_event_bus->project())
                    m_waterfall_win->setProjectContacts(
                        m_event_bus->project()->contacts());
            });
    connect(m_event_bus, &ProjectEventBus::contactRemoved,
            this, [this](uint64_t) {
                if (m_waterfall_win && m_event_bus->project())
                    m_waterfall_win->setProjectContacts(
                        m_event_bus->project()->contacts());
            });
    // Node graph — null-guarded; window is lazy-created.
    if constexpr (Features::kNodeGraph) {
        connect(m_event_bus, &ProjectEventBus::layerReady,
                this, [this](app::DataLayer*) {
                    if (m_node_graph_win) m_node_graph_win->refreshLayerList();
                });
        connect(m_event_bus, &ProjectEventBus::layerRemoved,
                this, [this](const std::string&) {
                    if (m_node_graph_win) m_node_graph_win->refreshLayerList();
                });
        connect(m_event_bus, &ProjectEventBus::layersReordered,
                this, [this]() {
                    if (m_node_graph_win) m_node_graph_win->refreshLayerList();
                });
    }
    // Project dirty flag — route via PSC so title stays in sync.
    connect(m_event_bus, &ProjectEventBus::projectModified,
            this, [this]() { markProjectDirty(); });
    // Route layerDataChanged through WindowRegistry so every registered viewer
    // (waterfall, SBP, SSS map) reloads the affected layer automatically.
    connect(m_event_bus, &ProjectEventBus::layerDataChanged,
            this, [this](const std::string& id) {
                m_window_registry->broadcast(ViewerRefreshReason::LayerDataChanged, id);
            });
    // ---------------------------------------------------------------------

    m_sss_ctrl = new SidescanViewController(
        m_map_view, m_import_service,
        m_status_bar->pingLabel(), m_status_bar->posLabel(), m_status_bar->depthLabel(), this);
    m_sss_ctrl->setOperationManager(m_op_mgr);  // owns per-layer map-build ops (keyed)
    connect(m_sss_ctrl, &SidescanViewController::contactPicked,
            this, &MainWindow::onContactPicked);
    connect(m_sss_ctrl, &SidescanViewController::loadingStarted, this, [this]() {
        // The controller has already flagged itself Loading; let the shared
        // indicator reflect the aggregate busy state of all viewers.
        refreshLoadingIndicator();
    });
    connect(m_sss_ctrl, &SidescanViewController::loadingFinished, this, [this]() {
        // Only clear the indicator if no other viewer (or concurrent map build)
        // is still busy — the controller's state is updated before this fires.
        refreshLoadingIndicator();
        if (m_import_ctrl)
            m_import_ctrl->onMapLoadDone();
    });
    connect(m_sss_ctrl, &SidescanViewController::mapDiagnosticsReady,
            this, &MainWindow::onMapDiagnosticsReady);

    // Register the map controller so WindowRegistry broadcasts reach it.
    // Uses m_map_view as the host widget for auto-cleanup on destroy.
    m_window_registry->registerViewer(m_map_view, m_sss_ctrl);

    // Single source of truth for correction bakes (SSS + SBP).
    // CorrectionBatchOperator owns both services and routes lifecycle events
    // through DiagnosticsHub and ExecutionProgressDialog internally.
    m_corr_op = new CorrectionBatchOperator(m_import_service, m_diag_hub, m_import_overlay, this);
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
        // Apply the persisted map sonar quality (default: CoverageOnly — a new
        // project shows coverage + nav instantly and pays no raster cost until the
        // user picks a higher tier).
        QSettings qs;
        const int saved = qs.value(SettingsDialog::kKeyMapSonarQuality,
                                   static_cast<int>(MapSonarQuality::CoverageOnly)).toInt();
        // mapSonarQualityFromInt migrates the retired "Low" tier to Medium.
        m_sss_ctrl->setMapSonarQuality(mapSonarQualityFromInt(saved));
    }
    if constexpr (Features::kImport) {
        m_import_job_mgr = new app::ImportJobManager(m_import_service, this);
        m_import_ctrl = new ExecutionController(
            m_import_job_mgr, m_import_overlay, m_layer_picker,
            this, this);
        m_import_ctrl->connectToCacheRebuilds(m_import_service);
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

    setupTitleBar();

    QSettings settings(AppInfo::kOrgName, AppInfo::kSettingsApp);
    restoreGeometry(settings.value("geometry").toByteArray());

    // Restore panel state from previous session.
    if (settings.contains("ui/propsOpen"))
        setPropertiesOpen(settings.value("ui/propsOpen").toBool());
    if (settings.contains("ui/propsWidth")) {
        m_props_width = settings.value("ui/propsWidth").toInt();
        if (m_props_panel) m_props_panel->setFixedWidth(m_props_width);
    }
    if (settings.contains("ui/toolbarVisible"))
        setRightToolBarVisible(settings.value("ui/toolbarVisible").toBool());

    // Apply all persisted settings on startup. AppState already loaded them
    // from QSettings in its constructor — no need to re-read.
    applyLiveSettings(m_app_state->current());

    bindProjectUi();
    appendJobMessage("Workstation ready.");
}

// -- Project session helpers (shorthand for aspect files) -------------------

app::Project* MainWindow::currentProject() const noexcept
{
    if (!m_session_ctrl) return nullptr;
    const auto& p = m_session_ctrl->project();
    return p ? p.get() : nullptr;
}

std::shared_ptr<app::Project> MainWindow::currentProjectPtr() const noexcept
{
    return m_session_ctrl ? m_session_ctrl->project() : nullptr;
}

bool MainWindow::isProjectDirty() const noexcept
{
    return m_session_ctrl && m_session_ctrl->isDirty();
}

void MainWindow::markProjectDirty()
{
    if (m_session_ctrl) m_session_ctrl->markDirty();
}

// -- Layer display helpers (shorthand for aspect files) ---------------------

const std::string& MainWindow::activeLayerId() const noexcept
{
    static const std::string kEmpty;
    return m_layer_ctrl ? m_layer_ctrl->activeLayerId() : kEmpty;
}

} // namespace dolphin::ui
