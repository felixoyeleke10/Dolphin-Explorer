// WaterfallScrollSync.cpp — scroll coordination, status bar, and command handling

#include "ui/features/waterfall/WaterfallWindow.h"
#include "ui/features/waterfall/WaterfallView.h"
#include "ui/features/waterfall/WaterfallQcStrip.h"
#include "app/layers/DataLayer.h"
#include "ui/shared/CoordFormat.h"
#include "ui/shared/widgets/CommandBar.h"
#include "geo/GeoUtils.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <QLabel>
#include <QLineEdit>
#include <QScrollBar>
#include <QTimer>

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  QC range tracking
// -----------------------------------------------------------------------------

void WaterfallWindow::markRowsAsViewed(int abs_first, int abs_last)
{
    if (abs_first >= abs_last) return;
    // Skip fraction update while the entry count is not yet known — avoids
    // emitting a bogus 100% when estimatedTotalRows() falls back to 1.
    if (m_total_ssc_entries <= 0) return;

    m_viewed_ranges.push_back({ abs_first, abs_last });

    // Merge overlapping / adjacent intervals (keep sorted).
    // Skip the full sort when the new range arrives in order (the common path
    // during forward scrolling) — the vector is already sorted in that case.
    if (m_viewed_ranges.size() > 1) {
        const auto& prev = m_viewed_ranges[m_viewed_ranges.size() - 2];
        if (abs_first < prev.first)
            std::sort(m_viewed_ranges.begin(), m_viewed_ranges.end());
    }
    std::vector<std::pair<int,int>> merged;
    merged.reserve(m_viewed_ranges.size());
    for (const auto& iv : m_viewed_ranges) {
        if (!merged.empty() && iv.first <= merged.back().second)
            merged.back().second = std::max(merged.back().second, iv.second);
        else
            merged.push_back(iv);
    }
    m_viewed_ranges = std::move(merged);

    // Recompute fraction.
    const int total = estimatedTotalRows();
    if (total <= 0) return;

    int64_t viewed = 0;
    for (const auto& [a, b] : m_viewed_ranges)
        viewed += static_cast<int64_t>(b) - a;
    const float new_frac = std::clamp(
        static_cast<float>(viewed) / static_cast<float>(total), 0.f, 1.f);

    if (new_frac - m_qc_fraction >= 0.005f || m_qc_fraction - new_frac >= 0.005f) {
        m_qc_fraction = new_frac;
        if (m_layer) {
            m_layer->qc_viewed_fraction = m_qc_fraction;   // keep DataLayer in sync live
            emit qcViewedFractionChanged(m_layer->id, m_qc_fraction);
        }
    }
}

// -----------------------------------------------------------------------------
//  Scroll helpers
// -----------------------------------------------------------------------------

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
    const int abs_first       = m_window_first_row + scroll_row;

    m_vscroll->blockSignals(true);
    m_vscroll->setRange(0, std::max(0, estimated_total - visible_rows));
    m_vscroll->setPageStep(visible_rows);
    m_vscroll->setValue(abs_first);
    m_vscroll->blockSignals(false);

    // Mark the rows currently on screen as viewed and update the QC strip.
    const int abs_last = std::min(abs_first + visible_rows, estimated_total);
    markRowsAsViewed(abs_first, abs_last);

    if (m_qc_strip)
        m_qc_strip->setData(estimated_total, m_viewed_ranges,
                            abs_first, visible_rows, m_qc_fraction);

    Q_UNUSED(total_rows)
}

void WaterfallWindow::onScrollBeyondBounds(int direction)
{
    if (!m_layer || !m_view) return;

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
    if (!m_view) return;

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

// -----------------------------------------------------------------------------
//  Status and command slots
// -----------------------------------------------------------------------------

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

    bool display_projected = is_projected;
    double display_lat = lat;
    double display_lon = lon;
    if (!is_projected && core::spatialRefIsProjected(m_hover_spatial_ref)) {
        double northing = 0.0;
        double easting = 0.0;
        if (geo::latLonToProjected(
                lat, lon, m_hover_spatial_ref, northing, easting)) {
            display_lat = northing;
            display_lon = easting;
            display_projected = true;
        }
    }

    const bool has_nav = (display_lat != 0.0 || display_lon != 0.0);
    if (has_nav)
        text += QStringLiteral("  ·  ")
            + formatPosition(display_lat, display_lon, display_projected);

    m_status_right->setText(text);

    emit cursorUpdated(
        range, side, display_lat, display_lon, display_projected);
}

} // namespace dolphin::ui
