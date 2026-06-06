// NodeGraphViewGeometry.cpp — coordinate transforms, node geometry, auto-layout, zoom/frame.
#include "ui/features/nodegraph/NodeGraphView.h"
#include "pipeline/NodeGraph.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  Coordinate transforms
// -----------------------------------------------------------------------------

QPointF NodeGraphView::canvasToWidget(QPointF cp) const
{
    return QPointF(cp.x() * m_zoom + m_pan.x(), cp.y() * m_zoom + m_pan.y());
}

QPointF NodeGraphView::widgetToCanvas(QPointF wp) const
{
    return QPointF((wp.x() - m_pan.x()) / m_zoom, (wp.y() - m_pan.y()) / m_zoom);
}

// -----------------------------------------------------------------------------
//  Geometry
// -----------------------------------------------------------------------------

QRectF NodeGraphView::nodeRect(const std::string& id) const
{
    auto [x, y] = m_graph->nodePosition(id);
    return QRectF(x, y, kNodeW, kNodeH);
}

QPointF NodeGraphView::outPortPos(const std::string& id) const
{
    const QRectF r = nodeRect(id);
    return QPointF(r.right(), r.center().y());
}

QPointF NodeGraphView::inPortPos(const std::string& id, int port) const
{
    const QRectF r = nodeRect(id);
    const auto node = m_graph ? m_graph->findNode(id) : nullptr;
    const int n = node ? node->inputCount() : 1;
    if (n <= 1) return QPointF(r.left(), r.center().y());
    // Distribute N ports evenly along the left edge: slot i at (i+1)/(n+1) fraction
    const float frac = (float)(port + 1) / (float)(n + 1);
    return QPointF(r.left(), r.top() + r.height() * frac);
}

// -----------------------------------------------------------------------------
//  Auto-layout / frame
// -----------------------------------------------------------------------------

void NodeGraphView::computeAutoLayout()
{
    if (!m_graph || m_graph->nodes().empty()) return;
    const auto& nodes = m_graph->nodes();
    const auto& edges = m_graph->edges();

    std::map<std::string, int> col;
    for (const auto& n : nodes) col[n->instance_id] = 0;

    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& e : edges) {
            const int nc = col[e.from_node] + 1;
            if (nc > col[e.to_node]) { col[e.to_node] = nc; changed = true; }
        }
    }

    std::map<int, std::vector<std::string>> by_col;
    for (const auto& n : nodes) by_col[col[n->instance_id]].push_back(n->instance_id);

    constexpr float col_gap = kNodeW + 80.f;
    constexpr float row_gap = kNodeH + 36.f;

    for (auto& [c, ids] : by_col)
        for (int r = 0; r < (int)ids.size(); ++r)
            m_graph->setNodePosition(ids[r], 60.f + c * col_gap, 60.f + r * row_gap);
}

void NodeGraphView::autoLayout()
{
    computeAutoLayout();
    frameAll();
    emit graphModified();
}

void NodeGraphView::frameAll()
{
    if (!m_graph || m_graph->nodes().empty()) {
        m_pan = QPointF(width() / 2.0, height() / 2.0); m_zoom = 1.0;
        update(); return;
    }
    double mnx = 1e18, mxx = -1e18, mny = 1e18, mxy = -1e18;
    for (const auto& n : m_graph->nodes()) {
        const QRectF r = nodeRect(n->instance_id);
        mnx = std::min(mnx, r.left());  mxx = std::max(mxx, r.right());
        mny = std::min(mny, r.top());   mxy = std::max(mxy, r.bottom());
    }
    constexpr double pad = 60.0;
    m_zoom = std::clamp(std::min(width()  / (mxx - mnx + pad * 2),
                                 height() / (mxy - mny + pad * 2)), 0.2, 1.5);
    m_pan.setX(width()  / 2.0 - m_zoom * (mnx + (mxx - mnx) / 2.0));
    m_pan.setY(height() / 2.0 - m_zoom * (mny + (mxy - mny) / 2.0));
    update();
}

void NodeGraphView::zoomBy(double factor, QPointF pivot_w)
{
    const double new_zoom = std::clamp(m_zoom * factor, 0.12, 4.0);
    const double actual   = new_zoom / m_zoom;
    m_pan  = pivot_w + (m_pan - pivot_w) * actual;
    m_zoom = new_zoom;
    update();
}

} // namespace dolphin::ui
