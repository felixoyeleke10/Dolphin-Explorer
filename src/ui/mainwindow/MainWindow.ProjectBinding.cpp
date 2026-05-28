// MainWindow.ProjectBinding.cpp — bindProjectUi and window title sync.
#include "ui/mainwindow/MainWindow.h"
#include "ui/mainwindow/ProjectEventBus.h"
#include "ui/shell/Features.h"
#include "ui/features/datalibrary/DataLibraryWindow.h"
#include "ui/features/import/ImportController.h"
#include "ui/features/processing/ProcessingController.h"
#include "ui/features/map/sidescan/SidescanViewController.h"
#include "ui/features/contacts/ContactListPanel.h"
#include "ui/features/geodesy/GeodesyPanel.h"
#include "ui/shared/panels/LineListPanel.h"
#include "ui/mainwindow/panels/InspectorPanel.h"
#include "ui/shared/widgets/LayerPickerWidget.h"
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
    m_project_dirty = false;
    m_pending_sbp_builds.clear();
    m_layer_nav_params.clear();
    m_layer_wf_params.clear();
    if (m_undo_stack) m_undo_stack->clear();
    updateActionStates();
    setWindowTitleFromProject();

    // Notify all registered viewers that the project was replaced so they
    // clear any layer data from the previous project.
    m_window_registry->broadcast(ViewerRefreshReason::ProjectReplaced);

    if (m_act_open_folder) {
        const bool has_folder = m_project
            && !m_project->isTempProject()
            && !m_project->manifestPath().empty();
        m_act_open_folder->setEnabled(has_folder);
    }

    app::Project* raw = m_project.get();

    if (m_map_view)
        m_map_view->setProject(raw);

    if (m_viewport_host && !raw)
        m_viewport_host->clearScene();

    if (m_viewport_host)
        m_viewport_host->setShowImportHint(!raw || raw->layers().empty());

    // broadcast(ProjectReplaced) above already calls deactivate(true) on the
    // SSS controller via onViewerRefresh — no explicit deactivate needed here.

    if (m_inspector)
        m_inspector->showEmpty();
    refreshInspectorModalities();

    if constexpr (Features::kImport)
        if (m_import_ctrl)
            m_import_ctrl->setProject(m_project);

    if constexpr (Features::kProcessing)
        if (m_proc_ctrl)
            m_proc_ctrl->setProject(m_project);

    if constexpr (Features::kContacts)
        if (m_contact_list)
            m_contact_list->setProject(raw);

    if constexpr (Features::kDataLibrary)
        if (m_data_library_win)
            m_data_library_win->setProject(raw);

    if (m_line_list)
        m_line_list->setProject(raw);

    if (m_bottom_panel) {
        QString term_dir;
        if (m_project && !m_project->manifestPath().empty()) {
            term_dir = QFileInfo(
                QString::fromStdString(m_project->manifestPath()))
                .absolutePath();
        }
        if (!term_dir.isEmpty())
            m_bottom_panel->setWorkingDirectory(term_dir);
    }

    if (m_geodesy_panel)
        m_geodesy_panel->refresh(raw, m_pending_crs);

    if (m_processing_win)
        m_processing_win->setProject(m_project, m_processing_service, m_sss_ctrl);

    if constexpr (Features::kNodeGraph) {
        if (m_node_graph_win) {
            app::DataLayer* active_layer = nullptr;
            if (raw && !m_active_layer_id.empty())
                active_layer = raw->findLayer(m_active_layer_id);
            m_node_graph_win->setLayer(active_layer, raw);
        }
    }

    // Wire the event bus to the new project. All component subscriptions were
    // established once in the constructor; no per-project reconnection needed.
    m_event_bus->setProject(m_project.get());

    updateContextInfo();
}

void MainWindow::setWindowTitleFromProject()
{
    if (!m_project) { setWindowTitle(tr("Dolphin Explorer")); return; }

    QString title;
    if (m_project_dirty) title += QStringLiteral("• ");
    title += QString::fromStdString(m_project->name());
    if (m_project->isTempProject()) title += tr(" (unsaved)");
    title += " — Dolphin Explorer";
    setWindowTitle(title);
}

} // namespace dolphin::ui
