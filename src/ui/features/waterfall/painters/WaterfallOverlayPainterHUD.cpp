// WaterfallOverlayPainterHUD.cpp — pure-geometry HUD overlays
//
// paintRuler()      — ping-index labels on the left depth strip
// paintCrosshair()  — configurable cursor crosshair lines
// paintRangeGrid()  — vertical lines at constant slant-range intervals
// paintEmptyState() — centred placeholder text when no data is loaded

#include "ui/features/waterfall/painters/WaterfallOverlayPainter.h"

#include "ui/shell/Theme.h"

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPen>

#include <algorithm>
#include <cmath>

namespace dolphin::ui {  

// ─────────────────────────────────────────────────────────────────────────────
//  Ruler — ping-index labels on the left depth strip
// ─────────────────────────────────────────────────────────────────────────────

void WaterfallOverlayPainter::paintRuler(
    QPainter&                        p,
    const WfLayout&                  lay,
    const WaterfallScrollController& scroll,
    int                              row_count)
{
    const int img_top = kWfScaleBarH;
    const int img_bot = lay.widget_h;

    p.fillRect(0, img_top, kWfRulerW, img_bot - img_top, QColor(Theme::kBgElevated));
    p.setPen(QColor(Theme::kBorder));
    p.drawLine(kWfRulerW, img_top, kWfRulerW, img_bot);

    p.setFont(QFont("Segoe UI", 7));
    p.setPen(QColor(Theme::kTextMuted));

    const int rh         = lay.row_height;
    const int label_step = qMax(50, (img_bot - img_top) / 6);
    for (int y = img_top; y < img_bot; y += label_step) {
        const int row = scroll.scrollRow() + (y - img_top) / rh;
        if (row < row_count)
            p.drawText(kWfLabelPadX, y + 10, QString::number(row));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Crosshair — configurable cursor lines
// ─────────────────────────────────────────────────────────────────────────────

void WaterfallOverlayPainter::paintCrosshair(
    QPainter&              p,
    const WfLayout&        lay,
    int                    cursor_x,
    int                    cursor_y,
    const WfOverlayParams& overlay)
{
    if (!overlay.xhair_show) return;

    const int img_top = kWfScaleBarH;
    const int img_bot = kWfScaleBarH + lay.img_h;
    if (cursor_x < kWfRulerW || cursor_y < img_top || cursor_y >= img_bot)
        return;

    QColor c = overlay.xhair_color;
    c.setAlphaF(overlay.xhair_opacity / 100.0);

    p.save();
    p.setPen(QPen(c, overlay.xhair_width, overlay.xhair_style));

    // Vertical line — full waterfall height
    p.drawLine(cursor_x, img_top, cursor_x, img_bot);

    // Horizontal line — from ruler edge to widget edge
    // Use a slightly lower opacity for horizontal (traditional SSS display style)
    QColor ch = overlay.xhair_color;
    ch.setAlphaF(overlay.xhair_opacity * 0.75 / 100.0);
    p.setPen(QPen(ch, overlay.xhair_width, overlay.xhair_style));
    p.drawLine(kWfRulerW, cursor_y, lay.widget_w, cursor_y);

    p.restore();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Range grid — vertical lines at constant slant-range intervals
//
//  The scale bar already shows the range mapping at the top.  This grid draws
//  lines across the image at auto or user-specified metre intervals.
// ─────────────────────────────────────────────────────────────────────────────

void WaterfallOverlayPainter::paintRangeGrid(
    QPainter&                        p,
    const std::vector<PingRow>&      rows,
    const WfLayout&                  lay,
    const WaterfallScrollController& scroll,
    const WfOverlayParams&           overlay)
{
    if (!overlay.grid_show || rows.empty()) return;

    // Determine the maximum slant range available in the current window.
    // Use the samples_per_ping and sound velocity (or fall back to a nominal).
    // We need to know the range-per-pixel mapping from the nadir outward.
    // The scale bar (paintScaleBar) already computes this; replicate it here.

    // Find a representative row to get samples-per-ping and range scale
    const int  scroll_row   = scroll.scrollRow();
    const int  n_rows       = static_cast<int>(rows.size());

    // Locate the first visible row with a valid slant range
    const PingRow* rep = nullptr;
    for (int i = scroll_row; i < n_rows; ++i) {
        if (rows[i].slant_range_m > 0.f) {
            rep = &rows[i]; break;
        }
    }
    if (!rep) {
        // Fall back: scan all rows
        for (const auto& r : rows) {
            if (r.slant_range_m > 0.f) { rep = &r; break; }
        }
    }
    if (!rep) return;

    const float max_range_m = rep->slant_range_m;
    if (max_range_m <= 0.f) return;

    // Pixel extent of the half-swath (nadir to edge), accounting for h-zoom/pan
    const float half_w_px = static_cast<float>(lay.widget_w - kWfRulerW) / 2.0f;
    if (half_w_px <= 2.f) return;

    // metres per pixel
    const float mpp = max_range_m / half_w_px;
    if (mpp <= 0.f) return;

    // Determine interval in metres
    float interval_m = overlay.grid_interval_m;
    if (interval_m <= 0.f) {
        // Auto: pick a nice interval giving roughly 4–8 gridlines per side
        const float nice[] = { 1.f, 2.f, 5.f, 10.f, 20.f, 25.f, 50.f, 100.f, 200.f, 500.f };
        interval_m = nice[std::size(nice) - 1];
        for (float ni : nice) {
            if (max_range_m / ni <= 6.f) { interval_m = ni; break; }
        }
    }

    const int img_top = kWfScaleBarH;
    const int img_bot = kWfScaleBarH + lay.img_h;
    const int nadir_x = lay.nadir_x;

    QColor gc = overlay.grid_color;
    gc.setAlphaF(overlay.grid_opacity / 100.0);

    p.save();
    p.setPen(QPen(gc, 1, Qt::SolidLine));
    p.setFont(QFont("Segoe UI", 7));

    const float px_per_interval = interval_m / mpp;
    if (px_per_interval < 2.f) { p.restore(); return; }

    // Draw lines on both port and starboard sides of nadir
    for (float range = interval_m; range < max_range_m + interval_m * 0.5f;
         range += interval_m)
    {
        const int dx = static_cast<int>(range / mpp + 0.5f);

        // Port side (left of nadir)
        const int xp = nadir_x - dx;
        if (xp >= kWfRulerW && xp < lay.widget_w) {
            p.drawLine(xp, img_top, xp, img_bot);
            // Label at top
            const QString lbl = QString("%1 m").arg(static_cast<int>(range));
            p.drawText(xp + 2, img_top + 12, lbl);
        }

        // Starboard side (right of nadir)
        const int xs = nadir_x + dx;
        if (xs >= kWfRulerW && xs < lay.widget_w) {
            p.drawLine(xs, img_top, xs, img_bot);
            const QString lbl = QString("%1 m").arg(static_cast<int>(range));
            p.drawText(xs + 2, img_top + 12, lbl);
        }
    }

    p.restore();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Empty state — centred placeholder text when no data is loaded
// ─────────────────────────────────────────────────────────────────────────────

void WaterfallOverlayPainter::paintEmptyState(
    QPainter&    p,
    const QRect& widget_rect,
    bool         empty)
{
    if (!empty) return;
    p.setPen(QColor(Theme::kTextDim));
    p.setFont(QFont("Segoe UI", 12));
    p.drawText(widget_rect, Qt::AlignCenter,
               "No layer loaded\nOpen a project and import a survey file.");
}

} // namespace dolphin::ui
