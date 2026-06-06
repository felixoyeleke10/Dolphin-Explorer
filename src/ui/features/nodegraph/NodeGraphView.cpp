// NodeGraphView.cpp — construction, graph ownership, and node add/delete/group actions.
// Companion files: NodeGraphViewGeometry.cpp, NodeGraphViewHitTest.cpp,
//                  NodeGraphViewPaint.cpp, NodeGraphViewInput.cpp
#include "ui/features/nodegraph/NodeGraphView.h"
#include "pipeline/NodeGraph.h"
#include "pipeline/NodeRegistry.h"

#include <QUuid>
#include <algorithm>

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  Construction
// -----------------------------------------------------------------------------

NodeGraphView::NodeGraphView(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(300, 200);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void NodeGraphView::setGraph(pipeline::NodeGraph* graph)
{
    m_graph     = graph;
    m_sel_node.clear();
    m_sel_nodes.clear();
    m_hov_node.clear();
    m_hov_group.clear();
    m_edge_drag = false;
    m_node_drag = false;
    m_rubber_active = false;
    m_group_drag = false;
    m_place_preview = false;
    m_place_type_id.clear();

    if (graph && !graph->nodes().empty()) {
        bool any_unpositioned = false;
        for (const auto& n : graph->nodes())
            if (!graph->hasNodePosition(n->instance_id)) { any_unpositioned = true; break; }
        if (any_unpositioned) computeAutoLayout();
        frameAll();
    } else {
        m_pan  = QPointF(width() / 2.0, height() / 2.0);
        m_zoom = 1.0;
    }

    update();
    emit nodeSelected({});
}

// -----------------------------------------------------------------------------
//  Add / delete
// -----------------------------------------------------------------------------

void NodeGraphView::addNodeAt(const std::string& type_id, QPointF canvas_pos)
{
    if (!m_graph) return;
    auto node = pipeline::NodeRegistry::instance().create(type_id);
    if (!node) return;

    node->instance_id = type_id + "_" +
        QUuid::createUuid().toString(QUuid::WithoutBraces).left(8).toStdString();
    for (auto& [k, p] : node->schema().params)
        node->params[k] = p.default_value;

    m_graph->addNode(node);
    m_graph->setNodePosition(node->instance_id,
                              (float)canvas_pos.x() - kNodeW / 2.f,
                              (float)canvas_pos.y() - kNodeH / 2.f);
    m_sel_node = node->instance_id;
    emit nodeSelected(m_sel_node);
    emit graphModified();
    update();
}

void NodeGraphView::addNodeAtWidget(const std::string& type_id, QPointF widget_pos)
{
    addNodeAt(type_id, widgetToCanvas(widget_pos));
}

void NodeGraphView::setPlacementPreview(const std::string& type_id, QPointF widget_pos)
{
    if (!m_graph || type_id.empty()) {
        clearPlacementPreview();
        return;
    }

    m_place_preview = true;
    m_place_type_id = type_id;
    m_place_widget = widget_pos;
    setCursor(Qt::CrossCursor);
    update();
}

void NodeGraphView::clearPlacementPreview()
{
    if (!m_place_preview && m_place_type_id.empty())
        return;

    m_place_preview = false;
    m_place_type_id.clear();
    unsetCursor();
    update();
}

void NodeGraphView::addNode(const std::string& type_id)
{
    const QPointF center = widgetToCanvas(QPointF(width() / 2.0, height() / 2.0));
    addNodeAt(type_id, center);
}

void NodeGraphView::deleteSelected()
{
    if (!m_graph) return;

    // Delete all selected nodes
    for (const auto& id : m_sel_nodes)
        m_graph->removeNode(id);

    // Remove from any groups
    for (auto& g : m_graph->groups())
        g.node_ids.erase(std::remove_if(g.node_ids.begin(), g.node_ids.end(),
            [&](const std::string& nid){ return m_sel_nodes.count(nid); }),
            g.node_ids.end());

    m_sel_node.clear();
    m_sel_nodes.clear();
    emit nodeSelected({});
    emit graphModified();
    update();
}

void NodeGraphView::groupSelected()
{
    if (!m_graph || m_sel_nodes.size() < 2) return;
    pipeline::NodeGroup g;
    g.label = "Group";
    g.node_ids.assign(m_sel_nodes.begin(), m_sel_nodes.end());
    m_graph->addGroup(std::move(g));
    emit graphModified();
    update();
}

void NodeGraphView::ungroupSelected()
{
    if (!m_graph || m_hov_group.empty()) return;
    m_graph->removeGroup(m_hov_group);
    m_hov_group.clear();
    emit graphModified();
    update();
}

} // namespace dolphin::ui
