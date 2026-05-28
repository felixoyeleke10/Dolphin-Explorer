// MainWindow.NodeGraphCoordinator.cpp — NodeGraphWindow lifetime and wiring.
#include "ui/mainwindow/MainWindow.h"
#include "ui/features/nodegraph/NodeGraphWindow.h"
#include "ui/features/processing/ProcessingController.h"
#include "ui/shell/Features.h"
#include "app/project/Project.h"
#include "app/layers/DataLayer.h"

#include <QDateTime>
#include <QDir>
#include <QStandardPaths>

namespace dolphin::ui {

void MainWindow::onNodeGraph()
{
    if constexpr (!Features::kNodeGraph) return;

    if (!m_project) {
        const QString ts = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss");
        const QString session_name = "Session_" + ts;
        const QString root_dir =
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
            + "/projects/" + session_name;
        QDir().mkpath(root_dir);
        const QString proj_path = root_dir + "/" + session_name + ".dlp";

        m_project = app::Project::create(
            session_name.toStdString(), proj_path.toStdString());
        if (m_project)
            m_project->setTempProject(true);
        bindProjectUi();
        appendJobMessage(tr("Created a temporary project for the node graph."));
    }

    if (!m_node_graph_win) {
        m_node_graph_win = new NodeGraphWindow(this);

        connect(m_node_graph_win, &NodeGraphWindow::graphModified, this, [this]() {
            m_project_dirty = true;
            setWindowTitleFromProject();
        });

        connect(m_node_graph_win, &NodeGraphWindow::runRequested,
                this, &MainWindow::onRunSelectedLayer);

        connect(m_node_graph_win, &NodeGraphWindow::importRequested,
                this, &MainWindow::onImportFile);

        connect(m_node_graph_win, &NodeGraphWindow::layerSelectionRequested,
                this, &MainWindow::onLayerSelected);
    }

    // Bind to the currently-active layer (may be null — that's fine)
    if (m_project && !m_active_layer_id.empty()) {
        auto* layer = m_project->findLayer(m_active_layer_id);
        m_node_graph_win->setLayer(layer, m_project.get());
    } else {
        m_node_graph_win->setLayer(nullptr, m_project.get());
    }

    m_node_graph_win->show();
    m_node_graph_win->raise();
    m_node_graph_win->activateWindow();
}

} // namespace dolphin::ui
