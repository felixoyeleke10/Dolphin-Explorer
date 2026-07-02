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
    }
    update();
}

void SubBottomView::setFeatureTool(int tool)
{
    m_feature_tool = tool;
    m_feature_pts.clear();
    m_feature_px.clear();
    if (tool != 0) {
        m_contact_tool = 0;
        setFocus(Qt::OtherFocusReason);   // receive Enter/Esc/Backspace
    }
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

void SubBottomView::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (m_feature_tool != 0 && e->button() == Qt::LeftButton) {
        const bool   polygon = (m_feature_tool == 1);
        const size_t min_pts = polygon ? 3u : 2u;
        if (m_feature_pts.size() >= min_pts)
            emit featureDrawn(m_feature_pts, polygon, m_feature_proj);
        m_feature_pts.clear();
        m_feature_px.clear();
        update();
        e->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(e);
}

void SubBottomView::keyPressEvent(QKeyEvent* e)
{
    if (m_feature_tool != 0 && !m_feature_pts.empty()) {
        switch (e->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter: {
            const bool   polygon = (m_feature_tool == 1);
            const size_t min_pts = polygon ? 3u : 2u;
            if (m_feature_pts.size() >= min_pts)
                emit featureDrawn(m_feature_pts, polygon, m_feature_proj);
            m_feature_pts.clear();
            m_feature_px.clear();
            update();
            return;
        }
        case Qt::Key_Escape:
            m_feature_pts.clear();
            m_feature_px.clear();
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
        update();
        e->accept();
        return;
    }
    emit contextMenuRequested(e->globalPos());
}

} // namespace dolphin::ui
