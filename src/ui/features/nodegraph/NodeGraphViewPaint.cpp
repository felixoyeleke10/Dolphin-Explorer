// NodeGraphViewPaint.cpp — all paint methods for the node graph canvas.
#include "ui/features/nodegraph/NodeGraphView.h"
#include "pipeline/NodeGraph.h"
#include "pipeline/NodeRegistry.h"
#include "ui/shell/Theme.h"

#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

namespace {
// Group panel tints (very translucent, used as fill only)
const QColor kGroupFillNormal (255, 255, 255,  4);
const QColor kGroupFillHover  (255, 255, 255,  6);
// Node drop-shadow (dark, varying opacity)
const QColor kNodeShadow      (  0,   0,   0, 55);
const QColor kNodePreviewShadow(  0,   0,   0, 40);
// Placement-preview node body — semi-transparent to hint it's not committed
const QColor kPreviewBody     ( 28,  28,  30, 210);
// Rubber-band fill — accent hue at very low opacity
const QColor kRubberbandFill  ( 10, 132, 255,  18);
} // namespace

namespace dolphin::ui {

void NodeGraphView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    paintBackground(p);

    if (!m_graph) {
        p.setPen(QColor(Theme::kTextDim));
        QFont f; f.setPixelSize(13);
        p.setFont(f);
        p.drawText(rect(), Qt::AlignCenter,
                   tr("Select a layer to edit its processing graph"));
        return;
    }

    paintGroups(p);
    paintEdges(p);
    paintEdgePreview(p);
    paintNodes(p);
    paintRubberBand(p);
    paintPlacementPreview(p);
}

void NodeGraphView::paintBackground(QPainter& p)
{
    p.fillRect(rect(), QColor(Theme::kBgPanel));
    const double gs = 24.0 * m_zoom;
    const double ox = std::fmod(m_pan.x(), gs);
    const double oy = std::fmod(m_pan.y(), gs);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(Theme::kBgCard));
    for (double x = ox; x < width();  x += gs)
        for (double y = oy; y < height(); y += gs)
            p.drawEllipse(QRectF(x - 1.0, y - 1.0, 2.0, 2.0));
}

void NodeGraphView::paintEdges(QPainter& p)
{
    const auto& edges = m_graph->edges();
    for (int i = 0; i < (int)edges.size(); ++i) {
        if (!m_graph->findNode(edges[i].from_node) ||
            !m_graph->findNode(edges[i].to_node)) continue;
        paintBezier(p,
                    canvasToWidget(outPortPos(edges[i].from_node)),
                    canvasToWidget(inPortPos(edges[i].to_node, edges[i].to_port)),
                    i == m_hov_edge);
    }
}

void NodeGraphView::paintBezier(QPainter& p, QPointF from, QPointF to,
                                  bool selected, float alpha)
{
    const double dx = std::max(std::abs(to.x() - from.x()) * 0.5, 60.0 * m_zoom);
    const QPointF c1(from.x() + dx, from.y());
    const QPointF c2(to.x()   - dx, to.y());

    QPainterPath path;
    path.moveTo(from);
    path.cubicTo(c1, c2, to);

    QColor col = selected ? QColor(Theme::kAccent) : QColor(Theme::kBorderMenu);
    col.setAlphaF(alpha);
    p.setPen(QPen(col, selected ? 2.0 : 1.4, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
}

void NodeGraphView::paintEdgePreview(QPainter& p)
{
    if (!m_edge_drag || m_edge_from.empty()) return;
    paintBezier(p, canvasToWidget(outPortPos(m_edge_from)), m_edge_cur_w, true, 0.5f);
}

void NodeGraphView::paintGroups(QPainter& p)
{
    if (!m_graph) return;
    for (const auto& g : m_graph->groups()) {
        const QRectF cb = groupBounds(g);
        if (!cb.isValid()) continue;

        // Canvas → widget
        const QPointF tl = canvasToWidget(cb.topLeft());
        const QRectF rw(tl, QSizeF(cb.width() * m_zoom, cb.height() * m_zoom));
        const bool hov = (g.id == m_hov_group);

        // Fill
        p.setPen(Qt::NoPen);
        p.setBrush(hov ? kGroupFillHover : kGroupFillNormal);
        p.drawRoundedRect(rw, 6, 6);

        // Border
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(hov ? Theme::kBorderMenu : Theme::kBorder), 1.0, Qt::DashLine));
        p.drawRoundedRect(rw, 6, 6);

        // Label strip at top
        if (m_zoom > 0.35) {
            const float hdr_h = 18.f * (float)m_zoom;
            QFont f; f.setPixelSize(std::max(8, (int)(10.f * m_zoom))); f.setWeight(QFont::Medium);
            p.setFont(f);
            p.setPen(QColor(Theme::kTextMuted));
            p.drawText(QRectF(rw.left() + 8 * m_zoom, rw.top() + 1,
                              rw.width() - 16 * m_zoom, hdr_h),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       QString::fromStdString(g.label));
        }
    }
}

void NodeGraphView::paintRubberBand(QPainter& p)
{
    if (!m_rubber_active) return;
    p.setPen(QPen(QColor(Theme::kAccent), 1.0, Qt::DashLine));
    p.setBrush(kRubberbandFill);
    p.drawRect(m_rubber_rect_w);
}

void NodeGraphView::paintNodes(QPainter& p)
{
    for (const auto& n : m_graph->nodes())
        paintNode(p, n->instance_id,
                  m_sel_nodes.count(n->instance_id) > 0,
                  n->instance_id == m_hov_node);
}

void NodeGraphView::paintPlacementPreview(QPainter& p)
{
    if (!m_place_preview || m_place_type_id.empty())
        return;

    auto proto = pipeline::NodeRegistry::instance().create(m_place_type_id);
    const QString label = proto
        ? QString::fromStdString(proto->label())
        : QString::fromStdString(m_place_type_id);
    const QColor accent = typeColor(m_place_type_id);
    const float barW = 3.f * (float)m_zoom;

    const QSizeF size(kNodeW * m_zoom, kNodeH * m_zoom);
    const QRectF rw(m_place_widget.x() - size.width() / 2.0,
                    m_place_widget.y() - size.height() / 2.0,
                    size.width(), size.height());
    const float cr = kRadius * (float)m_zoom;

    // Shadow
    p.setPen(Qt::NoPen);
    p.setBrush(kNodePreviewShadow);
    p.drawRoundedRect(rw.adjusted(1, 3, 1, 3), cr, cr);

    // Body (semi-transparent)
    p.setBrush(kPreviewBody);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(rw, cr, cr);

    // Accent bar
    {
        QPainterPath body_path; body_path.addRoundedRect(rw, cr, cr);
        QPainterPath bar_path;  bar_path.addRect(QRectF(rw.left(), rw.top(), barW, rw.height()));
        p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 180));
        p.setPen(Qt::NoPen);
        p.drawPath(body_path.intersected(bar_path));
    }

    // Dashed border
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(Theme::kAccent), 1.2, Qt::DashLine));
    p.drawRoundedRect(rw, cr, cr);

    // Label
    if (m_zoom > 0.3) {
        QFont lf; lf.setPixelSize(std::max(8, (int)(10.f * m_zoom))); lf.setWeight(QFont::Medium);
        p.setFont(lf);
        p.setPen(QColor(Theme::kTextSecond));
        p.drawText(QRectF(rw.left() + barW + 6.f * (float)m_zoom, rw.top(),
                          rw.width() - barW - 10.f * (float)m_zoom, rw.height()),
                   Qt::AlignLeft | Qt::AlignVCenter, label);
    }
}

void NodeGraphView::paintNode(QPainter& p, const std::string& id, bool sel, bool hov)
{
    auto node_ptr = m_graph->findNode(id);
    if (!node_ptr) return;
    auto* node = node_ptr.get();

    const pipeline::NodeSchema schema = node->schema();
    const QPointF tl = canvasToWidget(nodeRect(id).topLeft());
    const QSizeF  sz(kNodeW * m_zoom, kNodeH * m_zoom);
    const QRectF  rw(tl, sz);
    const float   cr     = kRadius * (float)m_zoom;
    const float   barW   = 3.f * (float)m_zoom;
    const QColor  accent = typeColor(node->typeId());

    // -- Shadow ----------------------------------------------------------------
    p.setPen(Qt::NoPen);
    p.setBrush(kNodeShadow);
    p.drawRoundedRect(rw.adjusted(1, 3, 1, 3), cr, cr);

    // -- Body ------------------------------------------------------------------
    p.setBrush(hov ? QColor(Theme::kBgHover) : QColor(Theme::kBgElevated));
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(rw, cr, cr);

    // -- Left accent bar (3 px, clipped to rounded rect) -----------------------
    {
        QPainterPath body_path; body_path.addRoundedRect(rw, cr, cr);
        QPainterPath bar_path;  bar_path.addRect(QRectF(rw.left(), rw.top(), barW, rw.height()));
        p.setBrush(accent);
        p.setPen(Qt::NoPen);
        p.drawPath(body_path.intersected(bar_path));
    }

    // -- Border ----------------------------------------------------------------
    p.setBrush(Qt::NoBrush);
    if (sel)       p.setPen(QPen(QColor(Theme::kAccent), 1.5));
    else if (hov)  p.setPen(QPen(QColor(Theme::kBorderMenu), 1.0));
    else           p.setPen(QPen(QColor(Theme::kBorder), 1.0));
    p.drawRoundedRect(rw, cr, cr);

    // -- Label (single line, vertically centred) -------------------------------
    if (m_zoom > 0.3) {
        const float tx = rw.left() + barW + 6.f * (float)m_zoom;
        const float tw = rw.width() - barW - 10.f * (float)m_zoom;
        QFont lf; lf.setPixelSize(std::max(8, (int)(10.f * m_zoom))); lf.setWeight(QFont::Medium);
        p.setFont(lf);
        p.setPen(QColor(Theme::kTextSecond));
        p.drawText(QRectF(tx, rw.top(), tw, rw.height()),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QString::fromStdString(node->label()));
    }

    // -- Ports -----------------------------------------------------------------
    paintPort(p, canvasToWidget(outPortPos(id)), true,
              m_hov_port_node == id && m_hov_port_out, isConnected(id, true));

    const int in_ports = node->inputCount();
    for (int port = 0; port < in_ports; ++port) {
        const bool conn = std::any_of(m_graph->edges().begin(), m_graph->edges().end(),
            [&](const pipeline::Edge& e){ return e.to_node == id && e.to_port == port; });
        paintPort(p, canvasToWidget(inPortPos(id, port)), false,
                  m_hov_port_node == id && !m_hov_port_out, conn);
    }
}

void NodeGraphView::paintPort(QPainter& p, QPointF wc, bool is_output,
                               bool hovered, bool connected)
{
    const float r = (hovered ? kPortR + 1.5f : kPortR) * (float)m_zoom;

    const QColor fill   = hovered   ? QColor(Theme::kAccentHover)
                        : connected  ? QColor(Theme::kAccent)
                        :              QColor(Theme::kBgCard);
    const QColor border = hovered   ? QColor(Theme::kAccentHover)
                        : connected  ? QColor(Theme::kAccentSoft)
                        :              QColor(Theme::kBorderMenu);

    p.setPen(QPen(border, 1.2));
    p.setBrush(fill);
    p.drawEllipse(wc, (double)r, (double)r);
}

} // namespace dolphin::ui
