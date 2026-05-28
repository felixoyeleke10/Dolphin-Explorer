// SubBottomView.Input.cpp — mouse, wheel, and context menu event handling.

#include "ui/features/subbottom/SubBottomView.h"
#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <algorithm>

namespace dolphin::ui {

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
    emit contextMenuRequested(e->globalPos());
}

} // namespace dolphin::ui
