// MainWindow.cpp — constructor + service wiring only
#include "ui/mainwindow/MainWindow.h"
#include "ui/mainwindow/ProjectSessionController.h"
#include "ui/systems/ProjectEventBus.h"
#include "ui/mainwindow/MainStatusBar.h"
#include "ui/bottom/RuntimeLogBridge.h"
#include "ui/shell/Features.h"
#include "ui/features/nodegraph/NodeGraphWindow.h"
#include "ui/features/import/ImportController.h"
#include "ui/features/map/sidescan/SidescanViewController.h"
#include "ui/mainwindow/coordinators/ExportController.h"
#include "ui/mainwindow/coordinators/CorrectionBatchOperator.h"
#include "ui/mainwindow/coordinators/ToolController.h"
#include "ui/mainwindow/coordinators/ViewportCoordinator.h"
#include "ui/mainwindow/coordinators/SidescanProcessingCoordinator.h"
#include "ui/features/import/ImportProgressDialog.h"
#include "ui/features/waterfall/WaterfallWindow.h"
#include "ui/features/subbottom/SubBottomWindow.h"
#include "ui/features/map/MapView.h"
#include "ui/features/map/MapViewportHost.h"
#include "app/services/ImportService.h"
#include "ui/shared/panels/LineListPanel.h"
#include "ui/shell/AppInfo.h"
#include <QPushButton>
#include <QTimer>
#include <QSettings>
#include <QUndoStack>

namespace dolphin::ui {

static constexpr int kMinW = 1440;
static constexpr int kMinH = 900;

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    // Palette + stylesheet are applied once by AppStyle::apply() in main.cpp
    // (from the persisted theme setting) — re-applying here would force a
    // second full-widget re-polish at startup.

    setWindowTitle(tr("Dolphin Explorer"));
    setMinimumSize(kMinW, kMinH);
    setAcceptDrops(true);
    setWindowFlag(Qt::FramelessWindowHint);  // custom title bar (menubar = caption)

    m_undo_stack = new QUndoStack(this);

    setupRuntimeServices();
    // --- ProjectSessionController + LayerDisplayCoordinator -----------------
    // PSC owns: m_project, m_project_dirty, m_pending_crs, and all CRUD slots.
    // LDC owns: m_active_layer_id, navigation history.
    // Aspect files access these via currentProject() / activeLayerId() helpers.
    m_session_ctrl = new ProjectSessionController(
        m_undo_stack, m_diag_hub, m_op_mgr, m_import_service, this, this);
    m_layer_ctrl  = new LayerDisplayCoordinator(m_session_ctrl, this);
    m_sss_processing = new SidescanProcessingCoordinator(m_display_state, this);

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
        // Do NOT blank the viewport here. The project parse runs synchronously on
        // the UI thread; disabling updates (or clearing the map) before it left the
        // GL viewport blank for the whole open — the window appeared to vanish and
        // reappear. Instead, keep the existing mosaic on screen during the open and
        // let bindProjectUi's ProjectReplaced broadcast clear it AFTER the parse,
        // immediately before the new project's layers load. deactivate(false) cancels
        // in-flight builds + resets controller state without wiping the map.
        if (m_sss_ctrl) m_sss_ctrl->deactivate(false);
        if (m_import_service) m_import_service->cancelPendingRebuild();
        if (m_corr_op) m_corr_op->cancelBatch();
        // Clear the Background Tasks panel: the cancelled builds' counters/cards must
        // not carry into the next project (stale "Building map X of Y" / stuck panel
        // when switching projects back and forth).
        if (m_import_overlay) m_import_overlay->resetState();
        m_tools_apply_layers.clear();   // drop stale per-line apply tracking
        m_layer_ctrl->clearActiveLayer();
        m_layer_ctrl->clearHistory();
    });
    connect(m_session_ctrl, &ProjectSessionController::projectChanged,
            this, [this](std::shared_ptr<app::Project>) {
        if (m_display_state) m_display_state->setProject(currentProject());
        bindProjectUi();
    });
    connect(m_session_ctrl, &ProjectSessionController::firstLayerReady,
            this, [this](const std::string& first_layer_id) {
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

        // …and show the whole survey, not just the active line. For every other
        // indexed line: draw its nav track instantly from the index (zero I/O)
        // for immediate feedback, then load the full representation as a
        // non-active layer — SSS: cache-first raster (map lane, cap 2);
        // SBP: depth-coloured profile ribbon ("sbp:open" lane, cap 2 — trace
        // reads are disk-heavy, D-14 spirit). SSS/SBP parity: before this, SBP
        // lines stayed invisible until manually selected once. Lines not yet
        // indexed (footerless first open) get theirs as their reindex completes
        // (cacheLayerReady). The active line still loads first (priority).
        if (m_op_mgr) m_op_mgr->setLaneCap("sbp:open", 2);
        for (const auto& layer : proj->layers()) {
            if (!layer || layer->id == first_layer_id) continue;
            if (!layer->index_built || layer->artifact_index.empty()) continue;
            if (layer->modality == app::Modality::Sidescan && m_sss_ctrl) {
                m_sss_ctrl->showNavTrackFromIndex(layer->id, proj.get());
                if (m_import_ctrl) m_import_ctrl->onMapLoadPending();
                m_sss_ctrl->activateLayer(layer->id, proj.get(), /*as_active=*/false);
            } else if (layer->modality == app::Modality::SubBottom) {
                // showNavTrackFromIndex is modality-agnostic (reads the
                // artifact index); the ribbon build replaces the track when done.
                if (m_sss_ctrl)
                    m_sss_ctrl->showNavTrackFromIndex(layer->id, proj.get());
                buildSbpProfileMap(layer.get(), "sbp:open");
            }
        }

        // Self-heal dead placeholders: a layer persisted WITHOUT its parsed
        // data (app closed or crashed mid-import) used to sit dead in the
        // project forever, silently — "imported three lines, only one works".
        // Re-run the import for any such layer through the normal reindex
        // pipeline (progress UI, failure reporting, and the cacheLayerReady
        // map hookup all come for free). A permanently bad file fails loudly
        // into the Problems panel instead of pretending to be a layer.
        if (m_import_ctrl) {
            // A placeholder never had its CRS confirmed (that happened only in
            // the original wizard run). Recover with the source's exact CRS if
            // stored, else the survey grid — the first exact CRS among the
            // project's sources — mirroring how the wizard applies one CRS to
            // a whole batch. Without this the reindex lands on the parser's
            // "projected, unknown grid" placeholder and every trace fails nav
            // normalisation ("No valid GPS position in any trace").
            core::SpatialRef survey_crs;
            for (const auto& s : proj->sources())
                if (s.source_spatial_ref.exact) { survey_crs = s.source_spatial_ref; break; }

            for (const auto& layer : proj->layers()) {
                if (!layer) continue;
                if (layer->index_built && !layer->artifact_index.empty()) continue;
                const auto* src = proj->findSource(layer->source_id);
                if (!src || src->path.empty()) continue;
                const core::SpatialRef crs =
                    src->source_spatial_ref.exact ? src->source_spatial_ref
                                                  : survey_crs;
                appendJobMessage(tr("Recovering %1 — parsed data was missing…")
                                     .arg(QString::fromStdString(layer->label)));
                m_import_ctrl->reindexLayer(src->path, layer->id, crs);
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

    m_tool_ctrl = new ToolController(m_app_state, m_map_view, m_viewport_host, this);
    m_tool_ctrl->setButtons(
        m_cursor_btn, m_select_btn, m_zoom_btn, m_measure_btn,
        m_contact_btn, m_feat_poly_btn, m_feat_line_btn, m_feat_pen_btn);
    connect(m_tool_ctrl, &ToolController::statusMessage,
            this, [this](const QString& message) { appendJobMessage(message); });
    m_tool_ctrl->activatePan();  // sync AppState + MapView mode with the initially-checked cursor button

    m_export_ctrl = new ExportController(
        [this] { return currentProject(); }, this, m_viewport_host, this, this);
    connect(m_export_ctrl, &ExportController::statusMessage,
            this, [this](const QString& message) { appendJobMessage(message); });
    connect(m_export_ctrl, &ExportController::activityRecorded,
            this, [this](const QString& description) {
                recordActivity(ActivityKind::Export, description);
            });

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
        connect(m_event_bus, &ProjectEventBus::contactUpdated,
                m_line_list, &LineListPanel::refreshContactRow);
        connect(m_event_bus, &ProjectEventBus::featureAdded,
                m_line_list, &LineListPanel::refreshFeatures);
        connect(m_event_bus, &ProjectEventBus::featureRemoved,
                m_line_list, &LineListPanel::refreshFeatures);
        connect(m_event_bus, &ProjectEventBus::featureUpdated,
                m_line_list, &LineListPanel::refreshFeatureRow);
    }
    if (m_map_view) {
        connect(m_event_bus, &ProjectEventBus::layersReordered,
                m_map_view, &MapView::refreshLayerOrder);
        connect(m_event_bus, &ProjectEventBus::contactAdded,
                m_map_view, static_cast<void (QWidget::*)()>(&QWidget::update));
        connect(m_event_bus, &ProjectEventBus::contactRemoved,
                m_map_view, static_cast<void (QWidget::*)()>(&QWidget::update));
        connect(m_event_bus, &ProjectEventBus::contactUpdated,
                m_map_view, static_cast<void (QWidget::*)()>(&QWidget::update));
        connect(m_event_bus, &ProjectEventBus::featureAdded,
                m_map_view, static_cast<void (QWidget::*)()>(&QWidget::update));
        connect(m_event_bus, &ProjectEventBus::featureRemoved,
                m_map_view, static_cast<void (QWidget::*)()>(&QWidget::update));
        connect(m_event_bus, &ProjectEventBus::featureUpdated,
                m_map_view, static_cast<void (QWidget::*)()>(&QWidget::update));
    }
    // Inspector modality set changes when layers arrive or depart.
    connect(m_event_bus, &ProjectEventBus::layerReady,
            this, [this](app::DataLayer*) { refreshInspectorModalities(); });
    connect(m_event_bus, &ProjectEventBus::layerRemoved,
            this, [this](const std::string&) { refreshInspectorModalities(); });
    // Sidebar Recycle Bin section mirrors the project recycle bin.
    connect(m_event_bus, &ProjectEventBus::recycleBinChanged,
            this, [this]() { refreshRecycleBin(); });
    connect(m_event_bus, &ProjectEventBus::projectReplaced,
            this, [this](app::Project*) { refreshRecycleBin(); });
    // Viewer contact overlays (waterfall + SBP) — windows may not exist yet.
    // One resync covers add / remove / edit so the markers never go stale.
    {
        // Coalesced: overlay re-derivation walks all contacts (and, for
        // unlinked ones, scans loaded rows), so rapid bursts — checkbox
        // toggling, multi-delete — collapse into one resync.
        auto sync_viewer_contacts = [this]() {
            if (m_viewer_contacts_sync_pending) return;
            m_viewer_contacts_sync_pending = true;
            QTimer::singleShot(50, this, [this]() {
                m_viewer_contacts_sync_pending = false;
                if (!m_event_bus->project()) return;
                const auto& contacts = m_event_bus->project()->contacts();
                if (m_waterfall_win) m_waterfall_win->setProjectContacts(contacts);
                if (m_sbp_win)       m_sbp_win->setProjectContacts(contacts);
            });
        };
        connect(m_event_bus, &ProjectEventBus::contactAdded,
                this, [sync_viewer_contacts](const core::Contact&) { sync_viewer_contacts(); });
        connect(m_event_bus, &ProjectEventBus::contactRemoved,
                this, [sync_viewer_contacts](uint64_t) { sync_viewer_contacts(); });
        connect(m_event_bus, &ProjectEventBus::contactUpdated,
                this, [sync_viewer_contacts](uint64_t) { sync_viewer_contacts(); });
    }
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

    setupFeatureControllers();
    setupTitleBar();

    QSettings settings(AppInfo::kOrgName, AppInfo::kSettingsApp);
    restoreGeometry(settings.value("geometry").toByteArray());

    // Restore panel state from previous session.
    if (settings.contains("ui/propsOpen"))
        setPropertiesOpen(settings.value("ui/propsOpen").toBool());
    if (settings.contains("ui/propsWidth")) {
        // Migration: 260 was the default before the right tool rail was
        // retired — only honour persisted widths the user explicitly widened;
        // otherwise adopt the new, larger default (302).
        const int stored = settings.value("ui/propsWidth").toInt();
        if (stored > 260) m_props_width = stored;
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
