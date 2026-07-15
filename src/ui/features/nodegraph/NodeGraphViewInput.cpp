// NodeGraphViewInput.cpp — mouse, keyboard, wheel, resize and context menu events.
#include "ui/features/nodegraph/NodeGraphView.h"
#include "pipeline/NodeGraph.h"
#include "pipeline/NodeRegistry.h"

#include <QContextMenuEvent>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMap>
#include <QMenu>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  Mouse events
// -----------------------------------------------------------------------------

void NodeGraphView::mousePressEvent(QMouseEvent* ev)
{
    if (!m_graph) return;
    setFocus();
    const QPointF wp = ev->position();
    const QPointF cp = widgetToCanvas(wp);
    const bool ctrl  = ev->modifiers() & Qt::ControlModifier;

    if (m_place_preview) {
        if (ev->button() == Qt::LeftButton)  { const auto t = m_place_type_id; clearPlacementPreview(); addNodeAt(t, widgetToCanvas(wp)); return; }
        if (ev->button() == Qt::RightButton) { clearPlacementPreview(); return; }
    }

    if (ev->button() == Qt::MiddleButton ||
        (ev->button() == Qt::LeftButton && (ev->modifiers() & Qt::AltModifier))) {
        m_pan_drag = true; m_pan_start = ev->pos(); m_pan_origin = m_pan;
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    if (ev->button() == Qt::LeftButton) {
        // Output port → begin edge drag
        std::string port_node;
        if (hitOutPort(wp, port_node)) {
            m_edge_drag = true; m_edge_from = port_node; m_edge_to_port = 0; m_edge_cur_w = wp;
            return;
        }

        // Input port — if occupied: disconnect and reroute from source
        {
            std::string in_id; int in_port = 0;
            if (hitInPort(wp, in_id, in_port)) {
                const auto& edges = m_graph->edges();
                auto it = std::find_if(edges.begin(), edges.end(),
                    [&](const pipeline::Edge& e){ return e.to_node == in_id && e.to_port == in_port; });
                if (it != edges.end()) {
                    const std::string src = it->from_node;
                    m_graph->removeEdgeToPort(in_id, in_port);
                    emit graphModified();
                    m_edge_drag = true; m_edge_from = src; m_edge_to_port = 0; m_edge_cur_w = wp;
                    update(); return;
                }
            }
        }

        // Node hit
        const std::string hit = hitNode(wp);
        if (!hit.empty()) {
            if (ctrl) {
                // Toggle in selection
                if (m_sel_nodes.count(hit)) { m_sel_nodes.erase(hit); if (m_sel_node == hit) { m_sel_node = m_sel_nodes.empty() ? "" : *m_sel_nodes.begin(); emit nodeSelected(m_sel_node); } }
                else                        { m_sel_nodes.insert(hit); m_sel_node = hit; emit nodeSelected(hit); }
            } else {
                if (!m_sel_nodes.count(hit)) { m_sel_nodes = {hit}; m_sel_node = hit; emit nodeSelected(hit); }
                // else keep multi-selection intact so user can drag all
            }
            // Begin drag — record origins for all selected nodes
            m_node_drag = true; m_drag_start_c = cp;
            m_drag_origins.clear();
            for (const auto& sid : m_sel_nodes) {
                auto [ox, oy] = m_graph->nodePosition(sid);
                m_drag_origins[sid] = {ox, oy};
            }
            update(); return;
        }

        // Group header hit
        const std::string ghit = hitGroup(cp);
        if (!ghit.empty()) {
            m_group_drag = true; m_dragged_group = ghit; m_group_drag_start_c = cp;
            auto* grp = m_graph->findGroup(ghit);
            m_group_node_origins.clear();
            if (grp) for (const auto& nid : grp->node_ids) {
                auto [ox, oy] = m_graph->nodePosition(nid);
                m_group_node_origins[nid] = {ox, oy};
            }
            return;
        }

        // Empty canvas — clear selection, start rubber band
        if (!ctrl) { m_sel_node.clear(); m_sel_nodes.clear(); emit nodeSelected({}); }
        m_rubber_active  = true;
        m_rubber_start_w = wp;
        m_rubber_rect_w  = QRectF(wp, QSizeF(0, 0));
        update();
    }
}

void NodeGraphView::mouseMoveEvent(QMouseEvent* ev)
{
    const QPointF wp = ev->position();
    const QPointF cp = widgetToCanvas(wp);

    if (m_place_preview) { m_place_widget = wp; update(); return; }
    if (m_pan_drag)      { m_pan = m_pan_origin + (wp - m_pan_start); update(); return; }

    if (m_node_drag) {
        const QPointF d = cp - m_drag_start_c;
        for (const auto& [sid, origin] : m_drag_origins)
            m_graph->setNodePosition(sid, origin.first + (float)d.x(), origin.second + (float)d.y());
        update(); return;
    }

    if (m_group_drag) {
        const QPointF d = cp - m_group_drag_start_c;
        for (const auto& [nid, origin] : m_group_node_origins)
            m_graph->setNodePosition(nid, origin.first + (float)d.x(), origin.second + (float)d.y());
        update(); return;
    }

    if (m_edge_drag) {
        m_edge_cur_w = wp;
        std::string in_node;
        int _p = 0; if (hitInPort(wp, in_node, _p) && in_node != m_edge_from) { m_hov_port_node = in_node; m_hov_port_out = false; }
        else m_hov_port_node.clear();
        update(); return;
    }

    if (m_rubber_active) {
        m_rubber_rect_w = QRectF(m_rubber_start_w, wp).normalized();
        update(); return;
    }

    const std::string phn = m_hov_node, php = m_hov_port_node, phg = m_hov_group;
    const int phe = m_hov_edge;
    m_hov_node.clear(); m_hov_port_node.clear(); m_hov_edge = -1; m_hov_group.clear();

    std::string port_node; int dummy_port = 0;
    if (hitOutPort(wp, port_node))               { m_hov_port_node = port_node; m_hov_port_out = true;  setCursor(Qt::CrossCursor); }
    else if (hitInPort(wp, port_node, dummy_port)){ m_hov_port_node = port_node; m_hov_port_out = false; setCursor(Qt::CrossCursor); }
    else {
        const std::string hov = hitNode(wp);
        if (!hov.empty())               { m_hov_node = hov; setCursor(Qt::SizeAllCursor); }
        else {
            const std::string ghov = hitGroup(cp);
            if (!ghov.empty())          { m_hov_group = ghov; setCursor(Qt::SizeAllCursor); }
            else { int ei; if (hitEdge(wp, ei)) m_hov_edge = ei; setCursor(Qt::ArrowCursor); }
        }
    }

    if (m_hov_node != phn || m_hov_port_node != php || m_hov_edge != phe || m_hov_group != phg) update();
}

void NodeGraphView::mouseReleaseEvent(QMouseEvent* ev)
{
    if (m_pan_drag && (ev->button() == Qt::MiddleButton || ev->button() == Qt::LeftButton))
        { m_pan_drag = false; setCursor(Qt::ArrowCursor); return; }

    if (ev->button() == Qt::LeftButton) {
        if (m_node_drag)  { m_node_drag = false; m_drag_origins.clear(); emit graphModified(); }
        if (m_group_drag) { m_group_drag = false; m_dragged_group.clear(); m_group_node_origins.clear(); emit graphModified(); }

        if (m_edge_drag) {
            const QPointF wp = ev->position();
            std::string in_node; int in_port = 0;
            if (hitInPort(wp, in_node, in_port) && in_node != m_edge_from)
                if (m_graph->addEdge(m_edge_from, in_node, in_port)) emit graphModified();
            m_edge_drag = false; m_edge_from.clear(); m_edge_to_port = 0; m_hov_port_node.clear();
            update();
        }

        if (m_rubber_active) {
            m_rubber_active = false;
            // Select all nodes whose widget-rect intersects the rubber band
            const QRectF rr = m_rubber_rect_w;
            const bool ctrl = ev->modifiers() & Qt::ControlModifier;
            if (!ctrl) m_sel_nodes.clear();
            for (const auto& n : m_graph->nodes()) {
                const QPointF tl = canvasToWidget(nodeRect(n->instance_id).topLeft());
                const QRectF nw(tl, QSizeF(kNodeW * m_zoom, kNodeH * m_zoom));
                if (rr.intersects(nw)) m_sel_nodes.insert(n->instance_id);
            }
            m_sel_node = m_sel_nodes.empty() ? "" : *m_sel_nodes.begin();
            emit nodeSelected(m_sel_node);
            update();
        }
    }
}

void NodeGraphView::wheelEvent(QWheelEvent* ev)
{
    const double factor = (ev->angleDelta().y() > 0) ? 1.15 : 1.0 / 1.15;
    const QPointF pivot = ev->position();
    m_pan  = pivot + (m_pan - pivot) * factor;
    m_zoom = std::clamp(m_zoom * factor, 0.12, 3.0);
    update(); ev->accept();
}

void NodeGraphView::keyPressEvent(QKeyEvent* ev)
{
    if (m_place_preview && ev->key() == Qt::Key_Escape) { clearPlacementPreview(); return; }

    const bool ctrl = ev->modifiers() & Qt::ControlModifier;

    if (ev->key() == Qt::Key_Escape) { m_sel_nodes.clear(); m_sel_node.clear(); emit nodeSelected({}); update(); return; }
    if ((ev->key() == Qt::Key_Delete || ev->key() == Qt::Key_Backspace) && !m_sel_nodes.empty()) { deleteSelected(); return; }
    if (ev->key() == Qt::Key_F)  { frameAll(); return; }
    if (ev->key() == Qt::Key_G && !ctrl && m_sel_nodes.size() >= 2) { groupSelected(); return; }
    if (ctrl && ev->key() == Qt::Key_A && m_graph) {
        m_sel_nodes.clear();
        for (const auto& n : m_graph->nodes()) m_sel_nodes.insert(n->instance_id);
        if (!m_sel_nodes.empty()) m_sel_node = *m_sel_nodes.begin();
        emit nodeSelected(m_sel_node);
        update(); return;
    }
    if (ctrl && (ev->key() == Qt::Key_Equal || ev->key() == Qt::Key_Plus))  { zoomBy(1.25, QPointF(width()/2.0, height()/2.0)); return; }
    if (ctrl && ev->key() == Qt::Key_Minus)  { zoomBy(1.0/1.25, QPointF(width()/2.0, height()/2.0)); return; }
    if (ctrl && ev->key() == Qt::Key_0)      { m_zoom = 1.0; m_pan = QPointF(width()/2.0, height()/2.0); update(); return; }

    QWidget::keyPressEvent(ev);
}

void NodeGraphView::resizeEvent(QResizeEvent* ev)
{
    const QPointF old_c(ev->oldSize().width()  / 2.0, ev->oldSize().height() / 2.0);
    const QPointF new_c(ev->size().width()     / 2.0, ev->size().height()    / 2.0);
    m_pan += (new_c - old_c);
    QWidget::resizeEvent(ev);
}

void NodeGraphView::contextMenuEvent(QContextMenuEvent* ev)
{
    if (!m_graph) return;
    if (m_place_preview) { clearPlacementPreview(); return; }

    const QPointF wp = ev->pos();
    const QPointF cp = widgetToCanvas(wp);
    QMenu menu(this);

    // Group header hit
    const std::string ghit = hitGroup(cp);
    if (!ghit.empty()) {
        m_hov_group = ghit;
        auto* grp = m_graph->findGroup(ghit);
        menu.addAction(tr("Ungroup"), [this, ghit]() {
            m_graph->removeGroup(ghit); emit graphModified(); update();
        });
        if (grp) {
            menu.addAction(tr("Rename…"), [this, grp]() {
                bool accepted = false;
                const QString current = QString::fromStdString(grp->label);
                const QString label = QInputDialog::getText(
                    this, tr("Rename Group"), tr("Group name:"),
                    QLineEdit::Normal, current, &accepted).trimmed();
                if (!accepted || label.isEmpty() || label == current) return;
                grp->label = label.toStdString();
                emit graphModified();
                update();
            });
        }
        menu.exec(ev->globalPos()); return;
    }

    const std::string hit = hitNode(wp);
    if (!hit.empty()) {
        if (!m_sel_nodes.count(hit)) { m_sel_nodes = {hit}; m_sel_node = hit; emit nodeSelected(hit); update(); }
        menu.addAction(tr("Duplicate"), [this, hit, cp]() {
            auto src = m_graph->findNode(hit);
            if (!src) return;
            addNodeAt(src->typeId(), QPointF(cp.x() + kNodeW + 20, cp.y()));
        });
        if (m_sel_nodes.size() >= 2)
            menu.addAction(tr("Group Selection  (%1 nodes)").arg(m_sel_nodes.size()),
                           this, &NodeGraphView::groupSelected);
        menu.addSeparator();
        menu.addAction(tr("Delete"), this, &NodeGraphView::deleteSelected);
        menu.exec(ev->globalPos()); return;
    }

    int edge_i;
    if (hitEdge(wp, edge_i)) {
        const std::string from = m_graph->edges()[edge_i].from_node;
        const std::string to   = m_graph->edges()[edge_i].to_node;
        menu.addAction(tr("Remove Connection"), [this, from, to]() {
            m_graph->removeEdge(from, to); emit graphModified(); update();
        });
        menu.exec(ev->globalPos()); return;
    }

    // Canvas right-click — group action if selection exists
    if (m_sel_nodes.size() >= 2)
        menu.addAction(tr("Group Selection  (%1 nodes)").arg(m_sel_nodes.size()),
                       this, &NodeGraphView::groupSelected);

    auto* sub = menu.addMenu(tr("Add Node"));
    QMap<QString, QMenu*> cat_menus;
    for (const auto& type_id : pipeline::NodeRegistry::instance().allTypeIds()) {
        auto proto = pipeline::NodeRegistry::instance().create(type_id);
        if (!proto) continue;
        const QString cat = QString::fromStdString(proto->schema().category);
        if (!cat_menus.contains(cat))
            cat_menus[cat] = sub->addMenu(cat);
        cat_menus[cat]->addAction(QString::fromStdString(proto->label()),
                                  [this, type_id, cp]() { addNodeAt(type_id, cp); });
    }
    menu.exec(ev->globalPos());
}

} // namespace dolphin::ui
