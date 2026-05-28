// NodeGraphViewHitTest.cpp — hit testing, type colours, group bounds.
#include "ui/features/nodegraph/NodeGraphView.h"
#include "pipeline/NodeGraph.h"
#include "pipeline/NodeRegistry.h"
#include "ui/shell/Theme.h"

#include <algorithm>
#include <cmath>

namespace dolphin::ui {

// ─────────────────────────────────────────────────────────────────────────────
//  Hit testing
// ─────────────────────────────────────────────────────────────────────────────

std::string NodeGraphView::hitNode(QPointF wp) const
{
    if (!m_graph) return {};
    const auto& nodes = m_graph->nodes();
    for (int i = (int)nodes.size() - 1; i >= 0; --i) {
        const QPointF tl = canvasToWidget(nodeRect(nodes[i]->instance_id).topLeft());
        const QRectF rw(tl, QSizeF(kNodeW * m_zoom, kNodeH * m_zoom));
        if (rw.contains(wp))
            return nodes[i]->instance_id;
    }
    return {};
}

bool NodeGraphView::hitOutPort(QPointF wp, std::string& out_id) const
{
    if (!m_graph) return false;
    const double hit_r = std::max(10.0, kPortHit * m_zoom);
    for (const auto& n : m_graph->nodes()) {
        const QPointF d = wp - canvasToWidget(outPortPos(n->instance_id));
        if (d.x()*d.x() + d.y()*d.y() < hit_r * hit_r) {
            out_id = n->instance_id; return true;
        }
    }
    return false;
}

bool NodeGraphView::hitInPort(QPointF wp, std::string& out_id, int& out_port) const
{
    if (!m_graph) return false;
    const double hit_r = std::max(10.0, kPortHit * m_zoom);
    for (const auto& n : m_graph->nodes()) {
        const int ports = n->inputCount();
        for (int p = 0; p < ports; ++p) {
            const QPointF d = wp - canvasToWidget(inPortPos(n->instance_id, p));
            if (d.x()*d.x() + d.y()*d.y() < hit_r * hit_r) {
                out_id = n->instance_id; out_port = p; return true;
            }
        }
    }
    return false;
}

bool NodeGraphView::hitEdge(QPointF wp, int& edge_idx) const
{
    if (!m_graph) return false;
    const double hit_r = std::max(6.0, 7.0 * m_zoom);
    const auto& edges = m_graph->edges();
    for (int i = 0; i < (int)edges.size(); ++i) {
        if (!m_graph->findNode(edges[i].from_node) ||
            !m_graph->findNode(edges[i].to_node)) continue;
        const QPointF from = canvasToWidget(outPortPos(edges[i].from_node));
        const QPointF to   = canvasToWidget(inPortPos(edges[i].to_node, edges[i].to_port));
        const double dx = std::max(std::abs(to.x() - from.x()) * 0.5, 60.0 * m_zoom);
        const QPointF c1(from.x() + dx, from.y());
        const QPointF c2(to.x()   - dx, to.y());
        for (int t = 0; t <= 30; ++t) {
            const double u = t / 30.0, v = 1.0 - u;
            const QPointF pt = v*v*v*from + 3*v*v*u*c1 + 3*v*u*u*c2 + u*u*u*to;
            const QPointF d  = pt - wp;
            if (d.x()*d.x() + d.y()*d.y() < hit_r * hit_r) { edge_idx = i; return true; }
        }
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────────────────────────

QColor NodeGraphView::typeColor(const std::string& type_id) const
{
    auto proto = pipeline::NodeRegistry::instance().create(type_id);
    const std::string cat = proto ? proto->schema().category : "";
    if (cat == "DataIn")      return QColor(Theme::kNodeColorSource);
    if (cat == "Correction")  return QColor(Theme::kNodeColorCorrection);
    if (cat == "Filter")      return QColor(Theme::kNodeColorFilter);
    if (cat == "Enhancement") return QColor(Theme::kNodeColorEnhancement);
    if (cat == "Analysis")    return QColor(Theme::kNodeColorAnalysis);
    if (cat == "Merge")       return QColor(Theme::kNodeColorMerge);
    if (cat == "Output")      return QColor(Theme::kNodeColorOutput);
    return QColor(Theme::kNodeColorUnknown);
}

bool NodeGraphView::isConnected(const std::string& id, bool output) const
{
    if (!m_graph) return false;
    for (const auto& e : m_graph->edges()) {
        if (output  && e.from_node == id) return true;
        if (!output && e.to_node   == id) return true;
    }
    return false;
}

QRectF NodeGraphView::groupBounds(const pipeline::NodeGroup& g) const
{
    if (g.node_ids.empty()) return {};
    constexpr float pad = 14.f;
    float mnx = 1e9f, mny = 1e9f, mxx = -1e9f, mxy = -1e9f;
    for (const auto& nid : g.node_ids) {
        const QRectF r = nodeRect(nid);
        mnx = std::min(mnx, (float)r.left());
        mny = std::min(mny, (float)r.top());
        mxx = std::max(mxx, (float)r.right());
        mxy = std::max(mxy, (float)r.bottom());
    }
    return QRectF(mnx - pad, mny - 20.f - pad, (mxx - mnx) + pad * 2, (mxy - mny) + 20.f + pad * 2);
}

std::string NodeGraphView::hitGroup(QPointF cp) const
{
    if (!m_graph) return {};
    for (const auto& g : m_graph->groups()) {
        const QRectF hdr = groupBounds(g);
        if (!hdr.isValid()) continue;
        // Only hit the header strip (top 20 canvas units)
        const QRectF hdr_strip(hdr.left(), hdr.top(), hdr.width(), 20.f);
        if (hdr_strip.contains(cp)) return g.id;
    }
    return {};
}

} // namespace dolphin::ui
