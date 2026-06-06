// WaterfallQcStrip.cpp — QC progress strip beside the sidescan waterfall.

#include "ui/features/waterfall/WaterfallQcStrip.h"

#include <QPainter>
#include <algorithm>

namespace dolphin::ui {

namespace {

static constexpr int kStripW = 14;

// Colours — chosen to be readable against a dark waterfall background.
static const QColor kBg        { 0x11, 0x11, 0x13 };        // near-black background
static const QColor kViewed    { 0x1e, 0x46, 0x22 };        // dark forest green fill
static const QColor kViewedEdge{ 0x43, 0xa0, 0x47 };        // brighter green border tick
static const QColor kCursor    { 0x76, 0xc4, 0x42 };        // vivid green — current viewport
static const QColor kCursorFill{ 0x76, 0xc4, 0x42, 0x30 };  // translucent fill between edges

} // namespace

WaterfallQcStrip::WaterfallQcStrip(QWidget* parent)
    : QWidget(parent)
{
    setFixedWidth(kStripW);
    setToolTip(tr("QC progress: green = viewed, dark = not yet seen.\n"
                  "Bright lines mark the current viewport."));
}

void WaterfallQcStrip::setData(int total_rows,
                               const std::vector<std::pair<int,int>>& viewed_ranges,
                               int cur_abs_first,
                               int cur_visible,
                               float fraction)
{
    m_total_rows  = total_rows;
    m_viewed      = viewed_ranges;
    m_cur_first   = cur_abs_first;
    m_cur_visible = cur_visible;
    m_fraction    = fraction;

    // Keep the tooltip percentage in sync
    const int pct = static_cast<int>(fraction * 100.f + 0.5f);
    setToolTip(tr("QC progress: %1% viewed\n"
                  "Green = seen · Dark = not yet viewed\n"
                  "Bright lines mark the current viewport.").arg(pct));

    update();
}

void WaterfallQcStrip::reset()
{
    m_total_rows  = 0;
    m_viewed.clear();
    m_cur_first   = 0;
    m_cur_visible = 0;
    m_fraction    = 0.f;
    setToolTip(tr("QC progress: no data loaded"));
    update();
}

void WaterfallQcStrip::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const int W = width();
    const int H = height();

    p.fillRect(0, 0, W, H, kBg);

    if (m_total_rows <= 0 || H <= 0) return;

    const float scale = static_cast<float>(H) / m_total_rows;

    // -- 1. Viewed segments ----------------------------------------------------
    for (const auto& [a, b] : m_viewed) {
        const int y0 = std::clamp(static_cast<int>(a * scale), 0, H - 1);
        const int y1 = std::clamp(static_cast<int>(b * scale), y0 + 1, H);
        if (y1 <= y0) continue;

        // Fill
        p.fillRect(0, y0, W, y1 - y0, kViewed);

        // Top + bottom edge ticks (1px each)
        p.fillRect(0, y0, W, 1, kViewedEdge);
        if (y1 > y0 + 1)
            p.fillRect(0, y1 - 1, W, 1, kViewedEdge);
    }

    // -- 2. Current viewport ---------------------------------------------------
    if (m_cur_visible > 0) {
        const int cy0 = std::clamp(static_cast<int>(m_cur_first * scale), 0, H - 1);
        const int cy1 = std::clamp(
            static_cast<int>((m_cur_first + m_cur_visible) * scale), cy0 + 1, H);

        // Translucent fill inside viewport span
        if (cy1 > cy0 + 4)
            p.fillRect(0, cy0 + 2, W, cy1 - cy0 - 4, kCursorFill);

        // Solid edge lines (2px each)
        p.fillRect(0, cy0,     W, 2, kCursor);
        if (cy1 - cy0 > 2)
            p.fillRect(0, cy1 - 2, W, 2, kCursor);
    }
}

} // namespace dolphin::ui
