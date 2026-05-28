// WaterfallScrollSync.cpp — scroll coordination, status bar, and command handling

#include "ui/features/waterfall/WaterfallWindow.h"
#include "ui/features/waterfall/WaterfallView.h"
#include "ui/shared/CoordFormat.h"
#include "ui/shared/widgets/CommandBar.h"

#include <cmath>
#include <QLabel>
#include <QLineEdit>
#include <QScrollBar>
#include <QTimer>

namespace dolphin::ui {

// ─────────────────────────────────────────────────────────────────────────────
//  Scroll helpers
// ─────────────────────────────────────────────────────────────────────────────

int WaterfallWindow::estimatedTotalRows() const
{
    if (m_total_ssc_entries <= 0) return 1;
    // If we have a measured ratio, use it.  Otherwise fall back to 1:1
    // (one index entry per row) which is always safe.
    const float epr = (m_entries_per_row > 1.01f) ? m_entries_per_row : 1.0f;
    return qMax(m_view ? m_view->rowCount() : 1,
                static_cast<int>(m_total_ssc_entries / epr));
}

void WaterfallWindow::onViewScrollChanged(int scroll_row, int total_rows, int visible_rows)
{
    // The external scrollbar covers the full estimated survey extent, not just
    // the currently-loaded window.
    const int estimated_total = estimatedTotalRows();
    const int abs_row         = m_window_first_row + scroll_row;

    m_vscroll->blockSignals(true);
    m_vscroll->setRange(0, std::max(0, estimated_total - visible_rows));
    m_vscroll->setPageStep(visible_rows);
    m_vscroll->setValue(abs_row);
    m_vscroll->blockSignals(false);

    Q_UNUSED(total_rows)
}

void WaterfallWindow::onScrollBeyondBounds(int direction)
{
    if (!m_layer) return;

    const int step            = m_window_size / 4;
    const int cur_abs         = m_window_first_row;
    const int estimated_total = estimatedTotalRows();

    int target;
    if (direction < 0) {
        if (cur_abs == 0) return;
        target = qMax(0, cur_abs - step);
    } else {
        const int window_end = cur_abs + m_view->rowCount();
        if (window_end >= estimated_total) return;
        target = qMin(estimated_total - 1, window_end + step);
    }

    // Route through the debounce timer so that rapid wheel events at a window
    // boundary collapse into a single load instead of queuing multiple jobs.
    m_pending_abs_row = target;
    m_scroll_debounce->start();
}

void WaterfallWindow::onScrollbarMoved(int abs_row)
{
    // During a thumb drag, only buffer the target position.  Apply on sliderReleased.
    if (m_scrollbar_dragging) {
        m_pending_abs_row = abs_row;
        return;
    }

    const int local_row = abs_row - m_window_first_row;
    if (local_row >= 0 && local_row < m_view->rowCount()) {
        // Inside loaded window: scroll instantly, no disk I/O needed.
        m_scroll_debounce->stop();
        m_pending_abs_row = -1;
        m_view->scrollToRow(local_row);
    } else {
        // Out-of-window: debounce so rapid key-repeats don't flood the thread pool.
        m_pending_abs_row = abs_row;
        m_scroll_debounce->start();   // restarts the 120 ms countdown
    }
}

void WaterfallWindow::onScrollDebounce()
{
    if (m_pending_abs_row >= 0)
        loadWindow(m_pending_abs_row);
    m_pending_abs_row = -1;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Status and command slots
// ─────────────────────────────────────────────────────────────────────────────

void WaterfallWindow::onModeChanged(int mode)
{
    m_active_mode = mode;
    switch (mode) {
    case ModeNavigate: m_status_left->setText(tr("Navigate"));                                    break;
    case ModeFix:      m_status_left->setText(tr("Fix — click to place markers"));           break;
    case ModeReview:   m_status_left->setText(tr("Review — scroll to inspect data"));        break;
    case ModeAnalyze:  m_status_left->setText(tr("Analyze — adjust image parameters"));      break;
    }
}

void WaterfallWindow::onCursorMoved(int /*ping_idx*/,
                                    float range_port_m, float range_stbd_m,
                                    float depth_port_m, float depth_stbd_m,
                                    double lat, double lon, bool is_projected)
{
    const bool  is_port = range_port_m > 0.f;
    const float range   = is_port ? range_port_m : range_stbd_m;
    const float depth   = is_port ? depth_port_m : depth_stbd_m;
    const QString side  = is_port ? tr("Port") : tr("Stbd");

    if (range <= 0.f) {
        m_status_right->clear();
        emit cursorUpdated(0.f, QString{}, 0.0, 0.0, false);
        return;
    }

    QString text = QString("%1  %2 m").arg(side).arg(range, 0, 'f', 1);
    if (depth > 0.f)
        text += QString("  ·  depth %1 m").arg(depth, 0, 'f', 1);

    const bool has_nav = (lat != 0.0 || lon != 0.0);
    if (has_nav)
        text += QStringLiteral("  ·  ") + formatPosition(lat, lon, is_projected);

    m_status_right->setText(text);

    emit cursorUpdated(range, side, lat, lon, is_projected);
}

} // namespace dolphin::ui
