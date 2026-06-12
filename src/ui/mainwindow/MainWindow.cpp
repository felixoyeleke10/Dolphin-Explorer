// MainWindow.cpp — constructor + service wiring only
#include "ui/mainwindow/MainWindow.h"
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
#include "app/corrections/SidescanCorrectionService.h"
#include "app/corrections/SubBottomCorrectionService.h"
#include "ui/features/import/ImportProgressDialog.h"
#include "ui/features/waterfall/WaterfallWindow.h"
#include "ui/features/subbottom/SubBottomWindow.h"
#include "ui/shared/dialogs/SettingsDialog.h"
#include "ui/shared/widgets/LayerPickerWidget.h"
#include "ui/features/map/MapView.h"
#include "app/services/ImportService.h"
#include "app/services/ProcessingService.h"
#include "ui/shared/panels/LineListPanel.h"
#include "ui/mainwindow/AppSettingsDialog.h"
#include "ui/shell/AppInfo.h"
#include <QApplication>
#include <QProgressBar>
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
        if (!m_project || m_active_layer_id.empty()) return;
        if (auto* layer = m_project->findLayer(m_active_layer_id)) {
            const auto* src = m_project->findSource(layer->source_id);
            m_waterfall_win->setLayer(layer, m_import_service,
                src ? src->path : std::string{},
                src ? src->size_bytes : 0);
            applyStoredNavParams(m_active_layer_id);
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
    connect(m_op_mgr, &app::OperationManager::operationStarted, this,
            [this](uint32_t op_id, const QString& name) {
                m_op_job_ids[op_id] = m_diag_hub->beginJob(name);
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
                    if (m_project) {
                        if (const auto* layer = m_project->findLayer(layer_id))
                            label = tr("Importing %1")
                                .arg(QString::fromStdString(layer->label));
                    }
                    const uint32_t jid = m_diag_hub->beginJob(
                        label, QString::fromStdString(layer_id));
                    m_import_job_ids[layer_id] = jid;
                    taskBegin(QStringLiteral("import:") + QString::fromStdString(layer_id), label);
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
                    taskDone(QStringLiteral("import:") + QString::fromStdString(layer_id));
                    if (m_project) {
                        if (const auto* layer = m_project->findLayer(layer_id))
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
                    taskFail(QStringLiteral("import:") + QString::fromStdString(layer_id),
                             QString::fromStdString(error));
                });

        // Background cache-index rebuild completed (deferred from project open).
        // Activate the layer in the map and — if nothing is selected yet — select it.
        connect(m_import_service, &app::ImportService::cacheIndexRebuilt, this,
                [this](const std::string& layer_id) {
                    if (!m_project) return;
                    auto* layer = m_project->findLayer(layer_id);
                    if (!layer || !layer->index_built) return;

                    // Push nav track and swath preview into the map.
                    using M = app::Modality;
                    if (layer->modality == M::Sidescan && m_sss_ctrl)
                        m_sss_ctrl->activateLayer(layer_id, m_project.get());
                    else if (m_map_view)
                        m_map_view->setActiveLayer(layer_id);

                    // If no layer is selected yet (project just opened), select this one.
                    if (m_active_layer_id.empty())
                        onLayerSelected(layer_id);

                    m_project_dirty = true;
                    setWindowTitleFromProject();
                });
    }

    if constexpr (Features::kProcessing)
        m_processing_service = new app::ProcessingService(this);

    setupCentralWidget();
    RuntimeLogBridge::instance()->replayPending();
    setupToolBar();
    setupMenuBar();
    setupStatusBar();

    // Scale spin box → map zoom: both m_status_bar and m_map_view exist after this point.
    connect(m_status_bar, &MainStatusBar::scaleChangeRequested,
            this, [this](double mpp) {
        if (m_map_view) m_map_view->setZoomFromMpp(mpp);
    });

    // Rotation spin box → map bearing.
    connect(m_status_bar, &MainStatusBar::rotationChangeRequested,
            this, [this](double deg) {
        if (m_map_view) m_map_view->setRotationDeg(deg);
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
    // Project dirty flag.
    connect(m_event_bus, &ProjectEventBus::projectModified,
            this, [this]() {
                m_project_dirty = true;
                setWindowTitleFromProject();
            });
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
    connect(m_sss_ctrl, &SidescanViewController::contactPicked,
            this, &MainWindow::onContactPicked);
    connect(m_sss_ctrl, &SidescanViewController::loadingStarted, this, [this]() {
        m_status_bar->setProgressIndeterminate();
    });
    connect(m_sss_ctrl, &SidescanViewController::loadingFinished, this, [this]() {
        m_status_bar->hideProgress();
        m_import_overlay->onMapLoadDone();
    });
    connect(m_sss_ctrl, &SidescanViewController::mapDiagnosticsReady,
            this, &MainWindow::onMapDiagnosticsReady);

    // Register the map controller so WindowRegistry broadcasts reach it.
    // Uses m_map_view as the host widget for auto-cleanup on destroy.
    m_window_registry->registerViewer(m_map_view, m_sss_ctrl);

    // Bakes processing corrections (TVG, ARC, AGC) into .dlpd amplitude data.
    // Wired to the waterfall Apply buttons in onWaterfallOpen().
    m_correction_svc = new app::SidescanCorrectionService(m_import_service, this);
    connect(m_correction_svc, &app::SidescanCorrectionService::applyStarted,
            this, [this](const std::string& layer_id) {
                auto* layer = m_project ? m_project->findLayer(layer_id) : nullptr;
                const QString label = layer
                    ? tr("Baking corrections into %1…").arg(QString::fromStdString(layer->label))
                    : tr("Baking sidescan corrections…");
                taskBegin(QStringLiteral("correction:") + QString::fromStdString(layer_id), label);
            });
    connect(m_correction_svc, &app::SidescanCorrectionService::correctionsPersisted,
            this, [this](const std::string& layer_id,
                         const std::string& new_path,
                         const core::ArtifactIndex& new_index) {
                auto* layer = m_project ? m_project->findLayer(layer_id) : nullptr;
                if (!layer) return;
                layer->artifact_store_path   = new_path;
                layer->artifact_store_format = "dlpd";
                layer->artifact_index        = new_index;
                m_project_dirty = true;
                setWindowTitleFromProject();
                if (m_sss_ctrl) m_sss_ctrl->reloadLayer(layer_id);
                appendJobMessage(
                    tr("Corrections baked into %1")
                        .arg(QString::fromStdString(layer->label)));
                taskDone(QStringLiteral("correction:") + QString::fromStdString(layer_id));
            });
    connect(m_correction_svc, &app::SidescanCorrectionService::applyFailed,
            this, [this](const std::string& layer_id, const std::string& error) {
                appendJobMessage(tr("Correction bake failed: %1")
                    .arg(QString::fromStdString(error)));
                taskFail(QStringLiteral("correction:") + QString::fromStdString(layer_id),
                         QString::fromStdString(error));
            });

    // Bakes SBP processing corrections into .dlpd float samples.
    // Wired to the SBP Apply buttons in onSubBottomOpen().
    m_sbp_correction_svc = new app::SubBottomCorrectionService(m_import_service, this);
    connect(m_sbp_correction_svc, &app::SubBottomCorrectionService::applyStarted,
            this, [this](const std::string& layer_id) {
                auto* layer = m_project ? m_project->findLayer(layer_id) : nullptr;
                const QString label = layer
                    ? tr("Baking corrections into %1…").arg(QString::fromStdString(layer->label))
                    : tr("Baking sub-bottom corrections…");
                taskBegin(QStringLiteral("sbp_correction:") + QString::fromStdString(layer_id), label);
            });
    connect(m_sbp_correction_svc, &app::SubBottomCorrectionService::correctionsPersisted,
            this, [this](const std::string& layer_id,
                         const std::string& new_path,
                         const core::ArtifactIndex& new_index) {
                auto* layer = m_project ? m_project->findLayer(layer_id) : nullptr;
                if (!layer) return;
                layer->artifact_store_path   = new_path;
                layer->artifact_store_format = "dlpd";
                layer->artifact_index        = new_index;
                m_project_dirty = true;
                setWindowTitleFromProject();
                if (m_sbp_win && m_sbp_win->currentLayerId() == layer_id)
                    m_sbp_win->reloadCurrentLayer();
                appendJobMessage(
                    tr("Corrections baked into %1")
                        .arg(QString::fromStdString(layer->label)));
                taskDone(QStringLiteral("sbp_correction:") + QString::fromStdString(layer_id));
            });
    connect(m_sbp_correction_svc, &app::SubBottomCorrectionService::applyFailed,
            this, [this](const std::string& layer_id, const std::string& error) {
                appendJobMessage(tr("SBP correction bake failed: %1")
                    .arg(QString::fromStdString(error)));
                taskFail(QStringLiteral("sbp_correction:") + QString::fromStdString(layer_id),
                         QString::fromStdString(error));
            });

    {
        // Apply the persisted map sonar quality (default: CoverageOnly).
        QSettings qs;
        const int saved = qs.value(SettingsDialog::kKeyMapSonarQuality,
                                   static_cast<int>(MapSonarQuality::Low)).toInt();
        m_sss_ctrl->setMapSonarQuality(static_cast<MapSonarQuality>(saved));
    }
    if constexpr (Features::kImport) {
        m_import_job_mgr = new app::ImportJobManager(m_import_service, this);
        m_import_ctrl = new ExecutionController(
            m_import_job_mgr, m_import_overlay, m_layer_picker,
            m_status_bar->progressBar(), this, this);
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
                    const auto* layer = m_project ? m_project->findLayer(layer_id) : nullptr;
                    const bool  needs_map_build = layer
                        && layer->modality == app::Modality::Sidescan;
                    if (needs_map_build)
                        m_import_overlay->onMapLoadPending();
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
                    taskBegin(QStringLiteral("proc:") + QString::fromStdString(id),
                              tr("Processing: %1").arg(label));
                });
        connect(m_proc_ctrl, &ProcessingController::layerRunFinished,
                this, [this](const std::string& id, const QString& summary) {
                    const auto it = m_import_job_ids.find(id);
                    if (it != m_import_job_ids.end()) {
                        m_diag_hub->endJob(it->second, summary);
                        m_import_job_ids.erase(it);
                    }
                    m_import_overlay->finishJob(id, summary);
                    taskDone(QStringLiteral("proc:") + QString::fromStdString(id));
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
                    taskFail(QStringLiteral("proc:") + QString::fromStdString(id), error);
                });
        connect(m_proc_ctrl, &ProcessingController::processingPersisted,
                this, [this](const std::string& id,
                             const std::string& /*proc_path*/,
                             const core::ArtifactIndex& proc_index,
                             bool slant_range_corrected) {
                    auto* layer = m_project ? m_project->findLayer(id) : nullptr;
                    if (!layer) return;
                    // The .dlpd was overwritten in-place; only the index changes.
                    layer->artifact_index         = proc_index;
                    layer->pipeline_applied       = true;
                    layer->slant_range_corrected  = slant_range_corrected;
                    m_project_dirty = true;
                    setWindowTitleFromProject();
                    // Reload viewers from the now-updated .dlpd.
                    m_event_bus->postLayerDataChanged(id);
                });
    }

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


} // namespace dolphin::ui
