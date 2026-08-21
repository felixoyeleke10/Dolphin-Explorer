// SubBottomView.Input.cpp — mouse, wheel, and context menu event handling.

#include "ui/features/subbottom/SubBottomView.h"
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <algorithm>

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  Annotation tools
// -----------------------------------------------------------------------------

void SubBottomView::setContactTool(int tool)
{
    m_contact_tool = tool;
    if (tool != 0) {
        m_feature_tool = 0;
        m_feature_pts.clear();
        m_feature_px.clear();
        m_feature_pen_down = false;
    }
    update();
}

void SubBottomView::setFeatureTool(int tool)
{
    m_feature_tool = tool;
    m_feature_pts.clear();
    m_feature_px.clear();
    m_feature_pen_down = false;
    if (tool != 0) {
        m_contact_tool = 0;
        setFocus(Qt::OtherFocusReason);   // receive Enter/Esc/Backspace
    }
    update();
}

void SubBottomView::setExternalContacts(std::vector<ContactMark> marks)
{
    m_external_contacts = std::move(marks);
    update();
}

bool SubBottomView::traceGeoAt(QPoint pos, int& trace_idx, float& depth_s,
                               double& lat, double& lon, bool& is_projected) const
{
    if (m_traces.empty() || m_px_per_trace <= 0) return false;
    const int col = pos.x() / m_px_per_trace;
    const int ti  = m_first_trace + col;
    if (ti < 0 || ti >= traceCount()) return false;

    const auto& trace = m_traces[ti];
    const int si = (m_px_per_sample > 0.f)
        ? static_cast<int>(static_cast<float>(pos.y()) / m_px_per_sample) : 0;
    trace_idx    = ti;
    depth_s      = (trace.sample_rate_hz > 0.f && si >= 0)
                 ? static_cast<float>(si) / trace.sample_rate_hz : -1.f;
    lat          = trace.nav.lat;
    lon          = trace.nav.lon;
    is_projected = trace.nav.is_projected;
    return true;
}

void SubBottomView::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) { e->ignore(); return; }

    int ti; float depth_s; double lat, lon; bool proj;

    if (m_contact_tool == 1 && traceGeoAt(e->pos(), ti, depth_s, lat, lon, proj)) {
        if (lat != 0.0 || lon != 0.0)
            emit contactPicked(ti, depth_s, lat, lon, proj);
        e->accept();
        return;
    }

    if (m_feature_tool != 0 && traceGeoAt(e->pos(), ti, depth_s, lat, lon, proj)) {
        if (lat != 0.0 || lon != 0.0) {
            if (m_feature_tool == 3) {   // pen: begin a freehand stroke
                m_feature_pts.clear();
                m_feature_px.clear();
                m_feature_pen_down = true;
            }
            m_feature_pts.push_back(QPointF(lon, lat));   // (lon,lat)
            m_feature_px.push_back(e->pos());
            m_feature_proj = proj;
            update();
        }
        e->accept();
        return;
    }
    e->ignore();
}

void SubBottomView::mouseReleaseEvent(QMouseEvent* e)
{
    if (m_feature_pen_down && e->button() == Qt::LeftButton) {
        m_feature_pen_down = false;
        if (m_feature_pts.size() >= 2)
            emit featureDrawn(m_feature_pts, /*polygon=*/false, m_feature_proj);
        m_feature_pts.clear();
        m_feature_px.clear();
        update();
        e->accept();
        return;
    }
    QWidget::mouseReleaseEvent(e);
}

void SubBottomView::commitFeatureDraft()
{
    // A double-click commit arrives as press (adds a vertex) + dblclick, leaving
    // a near-duplicate final vertex (Qt allows a few px of slop between the two
    // presses). Strip trailing near-coincident vertices for the click tools —
    // pen points are legitimately close together and never commit this way.
    if (m_feature_tool != 3) {
        while (m_feature_px.size() >= 2
               && (m_feature_px.back() - m_feature_px[m_feature_px.size() - 2])
                      .manhattanLength() < 6) {
            m_feature_pts.pop_back();
            m_feature_px.pop_back();
        }
    }
    const bool   polygon = (m_feature_tool == 1);
    const size_t min_pts = polygon ? 3u : 2u;
    if (m_feature_pts.size() >= min_pts)
        emit featureDrawn(m_feature_pts, polygon, m_feature_proj);
    m_feature_pts.clear();
    m_feature_px.clear();
    update();
}

void SubBottomView::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (m_feature_tool != 0 && m_feature_tool != 3 && e->button() == Qt::LeftButton) {
        commitFeatureDraft();
        e->accept();
        return;
    }

    // No annotation tool active: double-click a contact marker → open its editor.
    if (e->button() == Qt::LeftButton && m_contact_tool == 0 && m_feature_tool == 0) {
        uint64_t best_id   = 0;
        int      best_dist = 12;   // hit radius (px) around the diamond marker
        for (const ContactMark& m : m_external_contacts) {
            if (m.id == 0) continue;
            QPoint px;
            if (!contactMarkPixelPos(m, px)) continue;
            const int d = (e->pos() - px).manhattanLength();
            if (d < best_dist) { best_dist = d; best_id = m.id; }
        }
        if (best_id != 0) {
            emit contactEditRequested(best_id);
            e->accept();
            return;
        }
    }
    QWidget::mouseDoubleClickEvent(e);
}

void SubBottomView::keyPressEvent(QKeyEvent* e)
{
    if (m_feature_tool != 0 && !m_feature_pts.empty()) {
        switch (e->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            commitFeatureDraft();
            return;
        case Qt::Key_Escape:
            m_feature_pts.clear();
            m_feature_px.clear();
            m_feature_pen_down = false;   // cancel an in-progress pen stroke too
            update();
            return;
        case Qt::Key_Backspace:
            m_feature_pts.pop_back();
            m_feature_px.pop_back();
            update();
            return;
        default:
            break;
        }
    }
    QWidget::keyPressEvent(e);
}

void SubBottomView::mouseMoveEvent(QMouseEvent* e)
{
    m_cursor_x = e->pos().x();
    m_cursor_y = e->pos().y();
    update();

    // Pen freehand: append points while dragging (throttled to ~4 px).
    if (m_feature_pen_down && (e->buttons() & Qt::LeftButton)) {
        const bool far = m_feature_px.empty()
            || (e->pos() - m_feature_px.back()).manhattanLength() >= 4;
        int ti2; float depth2; double lat2, lon2; bool proj2;
        if (far && traceGeoAt(e->pos(), ti2, depth2, lat2, lon2, proj2)
            && (lat2 != 0.0 || lon2 != 0.0)) {
            m_feature_pts.push_back(QPointF(lon2, lat2));
            m_feature_px.push_back(e->pos());
            m_feature_proj = proj2;
        }
    }

    if (m_traces.empty() || m_px_per_trace <= 0) return;

    const int col = m_cursor_x / m_px_per_trace;
    const int ti  = m_first_trace + col;
    if (ti < 0 || ti >= traceCount()) return;

    const auto& trace = m_traces[ti];
    const int si = (m_px_per_sample > 0.f)
        ? static_cast<int>(static_cast<float>(m_cursor_y) / m_px_per_sample)
        : 0;

    const float depth_s = (trace.sample_rate_hz > 0.f && si >= 0)
        ? static_cast<float>(si) / trace.sample_rate_hz
        : -1.f;

    emit cursorMoved(ti, depth_s, trace.nav.lat, trace.nav.lon, trace.nav.is_projected);
}

void SubBottomView::wheelEvent(QWheelEvent* e)
{
    const int angle = e->angleDelta().y();
    if (angle == 0) { e->ignore(); return; }

    if (e->modifiers() & Qt::ControlModifier) {
        // Ctrl+scroll: zoom trace width (±1 px per notch)
        const int delta = (angle > 0) ? 1 : -1;
        setPxPerTrace(m_px_per_trace + delta);
        emit scrollChanged(m_first_trace, traceCount(), visibleTraceCount());
        e->accept();
        return;
    }

    if (e->modifiers() & Qt::ShiftModifier) {
        // Shift+scroll: zoom depth scale (×1.25 or ÷1.25 per notch)
        const float factor = (angle > 0) ? 1.25f : 0.8f;
        setPxPerSample(m_px_per_sample * factor);
        e->accept();
        return;
    }

    // Plain scroll: horizontal (along-track) pan
    const int vis  = std::max(1, visibleTraceCount());
    const int step = std::max(1, vis / 4);
    const int dir  = (angle > 0) ? -step : step;
    scrollToTrace(m_first_trace + dir);
    emit scrollChanged(m_first_trace, traceCount(), visibleTraceCount());
    e->accept();
}

void SubBottomView::leaveEvent(QEvent*)
{
    m_cursor_x = -1;
    m_cursor_y = -1;
    update();
    emit cursorLeft();
}

void SubBottomView::contextMenuEvent(QContextMenuEvent* e)
{
    // While drawing a feature, right-click cancels the in-progress draft.
    if (m_feature_tool != 0 && !m_feature_pts.empty()) {
        m_feature_pts.clear();
        m_feature_px.clear();
        m_feature_pen_down = false;   // cancel an in-progress pen stroke too
        update();
        e->accept();
        return;
    }
    uint64_t contact_id = 0;
    int best_dist = 12;
    for (const ContactMark& mark : m_external_contacts) {
        if (mark.id == 0) continue;
        QPoint px;
        if (!contactMarkPixelPos(mark, px)) continue;
        const int d = (e->pos() - px).manhattanLength();
        if (d < best_dist) { best_dist = d; contact_id = mark.id; }
    }
    emit contextMenuRequested(e->globalPos(), contact_id);
}

} // namespace dolphin::ui
