// MainWindow.NodeGraphCoordinator.cpp — NodeGraphWindow lifetime and wiring.
#include "ui/mainwindow/MainWindow.h"
#include "ui/features/nodegraph/NodeGraphWindow.h"
#include "ui/features/processing/ProcessingController.h"
#include "ui/shell/Features.h"
#include "app/project/Project.h"
#include "app/layers/DataLayer.h"
#include "app/workers/Worker.h"
#include "app/artifacts/BaselineArtifactResolver.h"

#include <QDateTime>
#include <QDir>
#include <QStandardPaths>

namespace dolphin::ui {

void MainWindow::onNodeGraph()
{
    if constexpr (!Features::kNodeGraph) return;

    if (!currentProject()) {
        const QString ts = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss");
        const QString session_name = "Session_" + ts;
        const QString root_dir =
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
            + "/projects/" + session_name;
        QDir().mkpath(root_dir);
        const QString proj_path = root_dir + "/" + session_name + ".dlp";

        m_session_ctrl->adoptNewProject(app::Project::create(
            session_name.toStdString(), proj_path.toStdString()));
        if (currentProject())
            currentProject()->setTempProject(true);
        bindProjectUi();
        appendJobMessage(tr("Created a temporary project for the node graph."));
    }

    if (!m_node_graph_win) {
        m_node_graph_win = new NodeGraphWindow(this);

        connect(m_node_graph_win, &NodeGraphWindow::graphModified, this, [this]() {
            if (!currentProject()) return;
            markProjectDirty();
            m_session_ctrl->autoSave();
        });

        connect(m_node_graph_win, &NodeGraphWindow::runRequested,
                this, &MainWindow::onRunSelectedLayer);

        connect(m_node_graph_win, &NodeGraphWindow::revertRequested,
                this, &MainWindow::onRevertProcessedLayer);

        connect(m_node_graph_win, &NodeGraphWindow::importRequested,
                this, &MainWindow::onImportFile);

        connect(m_node_graph_win, &NodeGraphWindow::layerSelectionRequested,
                this, &MainWindow::onLayerSelected);
    }

    // Bind to the currently-active layer (may be null — that's fine)
    if (currentProject() && !activeLayerId().empty()) {
        auto* layer = currentProject()->findLayer(activeLayerId());
        m_node_graph_win->setLayer(layer, currentProject());
    } else {
        m_node_graph_win->setLayer(nullptr, currentProject());
    }

    m_node_graph_win->show();
    m_node_graph_win->raise();
    m_node_graph_win->activateWindow();
}

void MainWindow::onRevertProcessedLayer(const std::string& layer_id)
{
    if (!currentProject()) return;
    auto* layer = currentProject()->findLayer(layer_id);
    if (!layer || layer->source_artifact_store_path.empty()
        || layer->source_artifact_store_path == layer->artifact_store_path)
        return;

    const auto baseline = app::resolveBaselineArtifact(*layer);
    if (!baseline) {
        appendJobMessage(tr("Cannot revert %1: the imported artifact is unavailable.")
            .arg(QString::fromStdString(layer->label)));
        return;
    }
    layer->artifact_store_path = baseline.path;
    layer->artifact_store_format = baseline.format;
    layer->artifact_index = baseline.index;
    layer->pipeline_applied = false;
    layer->processing_origin = app::ProcessingOrigin::None;
    layer->applied_graph_json.clear();
    layer->baked_correction_flags = 0;
    layer->slant_range_corrected = false;
    if (m_display_state) {
        if (layer->modality == app::Modality::Sidescan) {
            const auto appearance_only = withoutSidescanProcessing(
                layer->sss_display_state.params);
            m_display_state->setLayerSssDisplay(layer_id, appearance_only);
        } else if (layer->modality == app::Modality::SubBottom) {
            m_display_state->clearLayerSbpProcessing(layer_id);
        }
    }
    if (auto* worker = currentProject()->findWorker(layer_id)) worker->markDirty();

    markProjectDirty();
    m_session_ctrl->autoSave();
    if (m_event_bus) m_event_bus->postLayerDataChanged(layer_id);
    if (m_node_graph_win) m_node_graph_win->setLayer(layer, currentProject());
    rebuildHistoryList();
    appendJobMessage(tr("Reverted %1 to its preserved imported data.")
        .arg(QString::fromStdString(layer->label)));
}

} // namespace dolphin::ui
