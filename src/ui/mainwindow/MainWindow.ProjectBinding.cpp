// MainWindow.ProjectBinding.cpp — bindProjectUi: notifies all subsystems when
// the live project changes. Called from PSC's projectChanged signal handler.
#include "ui/mainwindow/MainWindow.h"
#include "ui/mainwindow/MainStatusBar.h"
#include "ui/systems/ProjectEventBus.h"
#include "ui/shell/Features.h"
#include "ui/features/datalibrary/DataLibraryWindow.h"
#include "ui/features/import/ImportController.h"
#include "ui/mainwindow/coordinators/ProjectOperationCoordinator.h"
#include "ui/features/processing/ProcessingController.h"
#include "ui/features/map/sidescan/SidescanViewController.h"
#include "ui/features/geodesy/GeodesyPanel.h"
#include "ui/shared/panels/LineListPanel.h"
#include "ui/mainwindow/panels/InspectorPanel.h"
#include "ui/mainwindow/rightpanel/RightPanelHost.h"
#include "ui/features/map/MapView.h"
#include "ui/features/map/MapViewportHost.h"
#include "ui/features/nodegraph/NodeGraphWindow.h"
#include "ui/features/processing/ProcessingWindow.h"
#include "ui/bottom/BottomDockPanel.h"
#include "app/project/Project.h"
#include "app/layers/DataLayer.h"

#include <QFileInfo>
#include <QStackedWidget>
#include <QUndoStack>

namespace dolphin::ui {

void MainWindow::bindProjectUi()
{
    // Abandon any background ops from the previous project (SBP profile builds,
    // etc.) so their completion handlers no-op against the new project.
    if (m_op_mgr) m_op_mgr->cancelAll();
    if (m_undo_stack) m_undo_stack->clear();
    updateActionStates();

    // Per-project UI state that would otherwise leak across a switch:
    // the History tab's activity journal, the Problems panel (its entries are
    // keyed by the OLD project's layer ids and would never clear), and the
    // lazy metadata inspector windows (they hold raw Project*/DataLayer*
    // pointers into the previous project — closing is the only safe reset).
    m_activity_log.clear();
    rebuildHistoryList();   // History tab may be showing — don't leave stale rows
    if (m_diag_hub) m_diag_hub->clearProblems();
    if (m_metadata_win)     m_metadata_win->close();
    if (m_sbp_metadata_win) m_sbp_metadata_win->close();
    // Window title is maintained by PSC; no setWindowTitle call needed here.

    // Notify all registered viewers that the project was replaced so they
    // clear any layer data from the previous project.
    m_window_registry->broadcast(ViewerRefreshReason::ProjectReplaced);

    app::Project* raw = currentProject();

    if (m_act_open_folder) {
        const bool has_folder = raw
            && !raw->isTempProject()
            && !raw->manifestPath().empty();
        m_act_open_folder->setEnabled(has_folder);
    }

    if (m_map_view)
        m_map_view->setProject(raw);

    // Status-bar CRS is set by updateContextInfo() at the end of this method
    // (it shows the project's working/survey CRS, refreshed on every layer change).

    // Clear BOTH views on every project change — close AND project→project
    // switch. The 2D view was already reset by setProject above, but the 3D
    // scene (curtains / drapes / terrain) is only ever populated additively;
    // without this, the old project's geometry stays in the 3D viewport under
    // the new project's data ("old survey leaking into the new one").
    if (m_viewport_host)
        m_viewport_host->clearScene();

    if (m_viewport_host)
        m_viewport_host->setShowImportHint(!raw || raw->layers().empty());

    // Load the new project's stored draping surface. clearScene above dropped
    // whatever terrain was loaded, so always start from a clean slate.
    {
        const QString stored = raw
            ? QString::fromStdString(raw->drapingSurface()) : QString{};
        m_loaded_draping.clear();
        if (m_viewport_host && !stored.isEmpty() && QFileInfo::exists(stored)) {
            m_viewport_host->loadTerrainPath(stored);
            m_loaded_draping = stored;
        }
    }
    refreshViewsPanel();

    // broadcast(ProjectReplaced) above already calls deactivate(true) on the
    // SSS controller via onViewerRefresh — no explicit deactivate needed here.

    if (m_inspector)    m_inspector->showEmpty();
    if (m_modal_host)   m_modal_host->clearLayer();
    refreshInspectorModalities();

    const auto proj_ptr = currentProjectPtr();

    if constexpr (Features::kImport)
        if (m_import_ctrl)
            m_import_ctrl->setProject(proj_ptr);

    if constexpr (Features::kProcessing)
        if (m_proc_ctrl)
            m_proc_ctrl->setProject(proj_ptr);

    if constexpr (Features::kDataLibrary)
        if (m_data_library_win)
            m_data_library_win->setProject(raw);

    if (m_line_list)
        m_line_list->setProject(raw);

    if (m_bottom_panel) {
        QString term_dir;
        if (raw && !raw->manifestPath().empty()) {
            term_dir = QFileInfo(
                QString::fromStdString(raw->manifestPath()))
                .absolutePath();
        }
        if (!term_dir.isEmpty())
            m_bottom_panel->setWorkingDirectory(term_dir);
    }

    if (m_geodesy_panel)
        m_geodesy_panel->refresh(raw, m_session_ctrl->pendingCrs());

    if (m_processing_win)
        m_processing_win->setProject(proj_ptr, m_processing_service);

    if constexpr (Features::kNodeGraph) {
        if (m_node_graph_win) {
            app::DataLayer* active_layer = nullptr;
            if (raw && !activeLayerId().empty())
                active_layer = raw->findLayer(activeLayerId());
            m_node_graph_win->setLayer(active_layer, raw);
        }
    }

    // Wire the event bus to the new project. All component subscriptions were
    // established once in the constructor; no per-project reconnection needed.
    m_event_bus->setProject(raw);

    if (m_op_coord)
        m_op_coord->setProject(proj_ptr);

    // Seed the activity history when a real (saved) project is opened, so the
    // History tab always reflects at least the open — loading an existing project
    // records nothing else (only imports/edits do).
    if (raw && !raw->isTempProject() && !raw->name().empty())
        recordActivity(ActivityKind::GroupChange,
                       tr("Opened project %1").arg(QString::fromStdString(raw->name())));

    updateContextInfo();
}

} // namespace dolphin::ui
