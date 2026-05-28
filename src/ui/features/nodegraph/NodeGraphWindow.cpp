// NodeGraphWindow.cpp — layer binding, run/selection slots, close.
//
// Layout / palette build  → NodeGraphWindow.Layout.cpp
// Palette drag & drop     → NodeGraphWindow.PaletteDrag.cpp
#include "ui/features/nodegraph/NodeGraphWindow.h"
#include "ui/features/nodegraph/NodeGraphView.h"
#include "ui/features/nodegraph/NodeInspectorPanel.h"
#include "ui/features/nodegraph/NodePaletteView.h"
#include "app/layers/DataLayer.h"
#include "app/project/Project.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QSignalBlocker>
#include <QToolButton>

namespace dolphin::ui {

void NodeGraphWindow::setLayer(dolphin::app::DataLayer* layer,
                               dolphin::app::Project* project)
{
    cancelPaletteDrag();
    m_project = project;
    m_layer   = layer;
    m_graph   = nullptr;

    // ── Layer combo (run target) ─────────────────────────────────────────────
    if (m_layer_combo) {
        QSignalBlocker blocker(m_layer_combo);
        m_layer_combo->clear();

        if (project) {
            m_layer_combo->addItem(tr("(no layer selected)"), QString{});
            for (const auto& candidate : project->layers()) {
                const QString lbl = candidate->label.empty()
                    ? QString::fromStdString(candidate->id)
                    : QString::fromStdString(candidate->label);
                m_layer_combo->addItem(lbl, QString::fromStdString(candidate->id));
            }
            m_layer_combo->setEnabled(true);

            if (layer) {
                const int idx = m_layer_combo->findData(QString::fromStdString(layer->id));
                if (idx >= 0) m_layer_combo->setCurrentIndex(idx);
            } else {
                m_layer_combo->setCurrentIndex(0);
            }
        } else {
            m_layer_combo->addItem(tr("No project"));
            m_layer_combo->setEnabled(false);
        }
    }

    // ── Worker tabs ──────────────────────────────────────────────────────────
    rebuildWorkerTabs();

    // ── No project ───────────────────────────────────────────────────────────
    if (!project || !m_graph) {
        m_run_btn->setEnabled(false);
        if (m_palette) {
            m_palette->setPlacementEnabled(false);
            m_palette->setToolTip(tr("Create or open a project first"));
        }
        if (m_canvas)  m_canvas->setGraph(nullptr);
        if (m_inspector) {
            m_inspector->showStatus(
                tr("No project"),
                tr("Create or open a project to build a processing graph."));
        }
        return;
    }

    // ── Ready ────────────────────────────────────────────────────────────────
    m_run_btn->setEnabled(layer != nullptr);
    if (m_palette) {
        m_palette->setPlacementEnabled(true);
        m_palette->setToolTip(tr("Drag into the graph or double-click to add"));
    }
    if (m_canvas)
        m_canvas->setGraph(m_graph);

    if (m_inspector)
        m_inspector->showStatus(tr("Graph ready"),
            tr("Drag nodes from the palette or right-click to add."));
}

void NodeGraphWindow::refreshLayerList()
{
    if (!m_layer_combo || !m_project) return;

    QSignalBlocker blocker(m_layer_combo);
    m_layer_combo->clear();
    m_layer_combo->addItem(tr("(no layer selected)"), QString{});
    for (const auto& candidate : m_project->layers()) {
        const QString lbl = candidate->label.empty()
            ? QString::fromStdString(candidate->id)
            : QString::fromStdString(candidate->label);
        m_layer_combo->addItem(lbl, QString::fromStdString(candidate->id));
    }
    m_layer_combo->setEnabled(true);

    if (m_layer) {
        const int idx = m_layer_combo->findData(QString::fromStdString(m_layer->id));
        m_layer_combo->setCurrentIndex(idx >= 0 ? idx : 0);
    } else {
        m_layer_combo->setCurrentIndex(0);
    }
}

void NodeGraphWindow::onLayerComboChanged(int index)
{
    if (!m_layer_combo || index < 0 || !m_project) return;

    const QString layer_id = m_layer_combo->itemData(index).toString();
    m_layer = layer_id.isEmpty() ? nullptr : m_project->findLayer(layer_id.toStdString());
    m_run_btn->setEnabled(m_layer != nullptr);

    if (m_layer)
        emit layerSelectionRequested(m_layer->id);
}

void NodeGraphWindow::onWorkerTabChanged(int index)
{
    if (!m_project || index < 0) return;

    m_active_worker_tab = index;
    auto& workers = m_project->workers();
    m_graph = workers.empty()
        ? &m_project->processing_graph
        : (index < (int)workers.size() ? &workers[index].graph : nullptr);

    if (m_canvas)    m_canvas->setGraph(m_graph);
    if (m_inspector) m_inspector->clearNode();
}

void NodeGraphWindow::onNodeSelected(const std::string& node_id)
{
    if (!m_graph || node_id.empty()) {
        m_inspector->clearNode();
        return;
    }
    m_inspector->showNode(m_graph, node_id);
}

void NodeGraphWindow::onGraphModified()
{
    if (m_canvas) m_canvas->update();
    emit graphModified();
}

void NodeGraphWindow::onRun()
{
    emit runRequested();
}

void NodeGraphWindow::closeEvent(QCloseEvent* ev)
{
    cancelPaletteDrag();
    hide();
    ev->ignore();
}

} // namespace dolphin::ui
