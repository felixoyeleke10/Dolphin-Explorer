// WaterfallOverlayPainter.cpp — data-derived waterfall overlays
//
// paintSeabedOverlay() — cyan polyline traced from seabed picks
// paintScaleBar()      — range tick labels along the bottom strip
//
// Pure-geometry HUD elements → WaterfallOverlayPainterHUD.cpp

#include "ui/features/waterfall/painters/WaterfallOverlayPainter.h"
#include "ui/features/waterfall/rendering/WaterfallRangeGeometry.h"
#include "ui/shared/contacts/ContactSymbols.h"
#include "ui/shell/Theme.h"

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPen>
#include <QVector>

#include <algorithm>
#include <cmath>

namespace {
// Port = warm red-orange, starboard = cool blue-violet.
// These are the canonical data-channel identity colors used on the scale bar.
const QColor kPortChannelColor(0xc0, 0x45, 0x30);
const QColor kStbdChannelColor(0x40, 0x60, 0xc8);
const QColor kSeabedTrackColor (0, 220, 255, 210);   // cyan   — high confidence
const QColor kSeabedTrackMedium(255, 190, 0, 220);   // amber  — medium confidence
const QColor kSeabedTrackLow   (255, 72, 72, 230);   // red    — low confidence
const QColor kSeabedTrackDim   (160, 100, 255, 175); // violet — tracker extrapolation / gap-fill
// Contact diamond halo (dark, widens the visible diamond edge)
const QColor kContactHalo     (0, 0, 0, 100);
} // namespace

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  Seabed overlay — cyan polyline on both port and starboard halves
// -----------------------------------------------------------------------------

void WaterfallOverlayPainter::paintSeabedOverlay(
    QPainter&                        p,
    const std::vector<PingRow>&      rows,
    const WfLayout&                  lay,
    const WaterfallScrollController& scroll,
    bool                             src_enabled)
{
    if (rows.empty()) return;

    const int   rh      = lay.row_height;
    const int   vis_end = qMin(scroll.scrollRow() + lay.img_h / rh,
                               static_cast<int>(rows.size()));
    const float h_zoom  = scroll.hZoom();
    const int   h_pan   = scroll.hPan();
    const int   nadir   = lay.nadir_x;
    const int   w       = lay.widget_w;

    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setClipRect(kWfRulerW, kWfScaleBarH, lay.widget_w - kWfRulerW, lay.img_h);

    // Solid cyan: true detections and manual picks.
    // Dashed dim: gap-filled / interpolated sections (detected=false but range_m > 0).
    const QPen pen_solid(kSeabedTrackColor, 1.5f, Qt::SolidLine);
    const QPen pen_mid  (kSeabedTrackMedium, 1.5f, Qt::SolidLine);
    const QPen pen_low  (kSeabedTrackLow, 1.7f, Qt::SolidLine);
    const QPen pen_dash (kSeabedTrackDim,   1.3f, Qt::DashLine);

    // Per-side polyline buffer that tracks its own pen type so the two sides
    // can switch pens independently without corrupting each other's draw calls.
    struct PenBuf {
        QVector<QPoint> pts;
        int style = 0;
    };
    PenBuf stbd_buf, port_buf;

    auto flush = [&](PenBuf& buf) {
        if (buf.pts.size() > 1) {
            switch (buf.style) {
            case 0:  p.setPen(pen_solid); break;
            case 1:  p.setPen(pen_mid);   break;
            case 2:  p.setPen(pen_low);   break;
            default: p.setPen(pen_dash);  break;
            }
            p.drawPolyline(buf.pts.data(), buf.pts.size());
        }
        buf.pts.clear();
    };

    // Add a point to a buffer.  When the pen type changes, flush the current
    // segment first and re-seed from the last point for visual continuity.
    auto addPt = [&](PenBuf& buf, QPoint pt, int style) {
        if (!buf.pts.isEmpty() && buf.style != style) {
            const QPoint bridge = buf.pts.last();
            flush(buf);
            buf.style = style;
            buf.pts.append(bridge);
        } else if (buf.pts.isEmpty()) {
            buf.style = style;
        }
        buf.pts.append(pt);
    };

    for (int row = scroll.scrollRow(); row < vis_end; ++row) {
        const PingRow& pr = rows[row];
        const int   y      = kWfScaleBarH + (row - scroll.scrollRow()) * rh + rh / 2;
        const int   stbd_w = w - nadir;
        const int   port_w = nadir - kWfRulerW;

        const int ns_s = static_cast<int>(pr.stbd.size());
        const auto& stbd_pick = waterfallSeabedForSide(
            pr, core::SidescanChannel::Starboard);
        if (ns_s > 1 && std::isfinite(stbd_pick.range_m)
                && stbd_pick.range_m > 0.f) {
            const int style = stbd_pick.is_manual || stbd_pick.confidence >= 0.66f
                ? 0 : (stbd_pick.confidence >= 0.38f ? 1 : 2);
            const float z   = (h_zoom > 0.f) ? h_zoom
                            : (stbd_w > 0 ? float(stbd_w) / ns_s : 1.f);
            const float distance = waterfallPixelDistanceForRange(
                pr, core::SidescanChannel::Starboard,
                {stbd_pick.range_m, waterfallSeabedDomainForSide(
                    pr, core::SidescanChannel::Starboard)},
                src_enabled, h_pan, z);
            const int xs = distance >= 0.f
                ? nadir + static_cast<int>(std::lround(distance)) : INT_MIN;
            if (xs >= nadir && xs < w) addPt(stbd_buf, {xs, y}, style);
            else                       flush(stbd_buf);
        } else flush(stbd_buf);

        const int ns_p = static_cast<int>(pr.port.size());
        const auto& port_pick = waterfallSeabedForSide(
            pr, core::SidescanChannel::Port);
        if (ns_p > 1 && std::isfinite(port_pick.range_m)
                && port_pick.range_m > 0.f) {
            const int style = port_pick.is_manual || port_pick.confidence >= 0.66f
                ? 0 : (port_pick.confidence >= 0.38f ? 1 : 2);
            const float z   = (h_zoom > 0.f) ? h_zoom
                            : (port_w > 0 ? float(port_w) / ns_p : 1.f);
            const float distance = waterfallPixelDistanceForRange(
                pr, core::SidescanChannel::Port,
                {port_pick.range_m, waterfallSeabedDomainForSide(
                    pr, core::SidescanChannel::Port)},
                src_enabled, h_pan, z);
            const int xp = distance >= 0.f
                ? nadir - 1 - static_cast<int>(std::lround(distance)) : INT_MIN;
            if (xp >= kWfRulerW && xp < nadir) addPt(port_buf, {xp, y}, style);
            else                                flush(port_buf);
        } else flush(port_buf);
    }
    flush(stbd_buf);
    flush(port_buf);
    p.restore();
}

// -----------------------------------------------------------------------------
//  Contact overlay — project-selected marker at each picked contact position
// -----------------------------------------------------------------------------

bool WaterfallOverlayPainter::contactPixelPos(
    const WfContact&               c,
    const std::vector<PingRow>&    rows,
    const WfLayout&                lay,
    const WaterfallScrollController& scroll,
    QPoint&                        out_px)
{
    if (rows.empty()) return false;

    const int   rh      = lay.row_height;
    const int   nadir   = lay.nadir_x;
    const int   w       = lay.widget_w;
    const int   img_h   = lay.img_h;
    const int   port_w  = nadir - kWfRulerW;
    const int   stbd_w  = w - nadir;
    const float h_zoom  = scroll.hZoom();
    const int   h_pan   = scroll.hPan();
    const int   first   = scroll.scrollRow();
    const int   last    = first + img_h / rh;

    if (c.row_idx < first || c.row_idx >= last) return false;
    if (c.row_idx >= static_cast<int>(rows.size())) return false;

    const PingRow& pr = rows[c.row_idx];
    if (pr.slant_range_m <= 0.f) return false;

    const int y = kWfScaleBarH + (c.row_idx - first) * rh + rh / 2;

    // Map range_m → sample index → pixel x
    int x = -1;
    if (c.ch == core::SidescanChannel::Port && !pr.port.empty()) {
        const int   ns = static_cast<int>(pr.port.size());
        const float z  = (h_zoom > 0.f) ? h_zoom
                       : (port_w > 0 ? float(port_w) / ns : 1.f);
        const int   si = static_cast<int>(c.range_m / pr.slant_range_m * (ns - 1));
        x = sampleToPixelPort(si, z, h_pan, nadir);
    } else if (c.ch == core::SidescanChannel::Starboard && !pr.stbd.empty()) {
        const int   ns = static_cast<int>(pr.stbd.size());
        const float z  = (h_zoom > 0.f) ? h_zoom
                       : (stbd_w > 0 ? float(stbd_w) / ns : 1.f);
        const int   si = static_cast<int>(c.range_m / pr.slant_range_m * (ns - 1));
        x = sampleToPixelStbd(si, z, h_pan, nadir);
    }

    if (x < kWfRulerW || x >= w) return false;
    out_px = QPoint(x, y);
    return true;
}

void WaterfallOverlayPainter::paintContactOverlay(
    QPainter&                      p,
    const std::vector<WfContact>&  contacts,
    const std::vector<PingRow>&    rows,
    const WfLayout&                lay,
    const WaterfallScrollController& scroll,
    QColor fill)
{
    if (contacts.empty() || rows.empty()) return;

    p.save();
    p.setRenderHint(QPainter::Antialiasing);

    static constexpr int kR = 6;

    for (const WfContact& c : contacts) {
        QPoint px;
        if (!contactPixelPos(c, rows, lay, scroll, px)) continue;
        const QColor marker_fill = c.color_rgb != 0
            ? QColor::fromRgba(c.color_rgb) : fill;
        const QString symbol = QString::fromStdString(c.symbol);
        const QPainterPath halo = contactSymbolPath(symbol, kR + 1).translated(px);
        const QPainterPath marker = contactSymbolPath(symbol, kR).translated(px);

        p.setBrush(kContactHalo);
        p.setPen(Qt::NoPen);
        p.drawPath(halo);

        p.setBrush(marker_fill);
        p.setPen(QPen(marker_fill.darker(150), 1));
        p.drawPath(marker);
    }

    p.restore();
}

// -----------------------------------------------------------------------------
//  Scale bar — two-section channel bar with range ticks
//
//  Layout (kWfScaleBarH = 22 px):
//    +--------------------------------------------------------------+
//    |  PORT ←   75    50    25  ┊  25    50    75   → STBD        |
//    +--------------------------------------------------------------+
//  The centre divider at nadir_x separates port (left) and starboard (right).
//  Channel labels are anchored to the outer edges; range numbers increase
//  outward from nadir, matching the convention in SonarWiz / Hypack / EdgeTech.
// -----------------------------------------------------------------------------

void WaterfallOverlayPainter::paintScaleBar(
    QPainter&                        p,
    const std::vector<PingRow>&      rows,
    const WfLayout&                  lay,
    const WaterfallScrollController& scroll,
    int                              samples_per_ping)
{
    struct PainterStateScope {
        explicit PainterStateScope(QPainter& painter) : painter(painter) { painter.save(); }
        ~PainterStateScope() { painter.restore(); }

        QPainter& painter;
    };
    PainterStateScope painter_scope(p);

    const int w     = lay.widget_w;
    const int nadir = lay.nadir_x;
    const int sb_y  = 0;                // bar now at the top
    const int sb_h  = kWfScaleBarH;

    // -- Background ------------------------------------------------------------
    p.fillRect(0,         sb_y, kWfRulerW,            sb_h, QColor(Theme::kBgElevated));
    p.fillRect(kWfRulerW, sb_y, nadir - kWfRulerW,    sb_h, QColor(Theme::kBgElevated));
    p.fillRect(nadir,     sb_y, w - nadir,             sb_h, QColor(Theme::kBgElevated));

    // -- Channel colour strips — 3 px band at the very top of the bar ---------
    // Port: warm red-orange   Starboard: cool blue-violet
    p.fillRect(kWfRulerW, sb_y + 1, nadir - kWfRulerW, 3, kPortChannelColor);
    p.fillRect(nadir,     sb_y + 1, w - nadir,          3, kStbdChannelColor);

    // -- Nadir divider — bright vertical line between port and starboard -------
    p.setPen(QPen(QColor(Theme::kTextMuted), 1));
    p.drawLine(nadir, sb_y + 1, nadir, sb_h - 1);

    // Bottom separator — divides the bar from the waterfall image below
    p.setPen(QColor(Theme::kBorder));
    p.drawLine(0, sb_h - 1, w, sb_h - 1);

    // -- Channel labels --------------------------------------------------------
    // "PORT" anchored near the ruler edge; "STBD" near the right edge.
    const int label_y = sb_y + 14;
    p.setFont(QFont("Segoe UI", 7, QFont::Medium));
    p.setPen(QColor(Theme::kTextSubtle));
    p.drawText(kWfRulerW + 4, label_y, "PORT");
    p.drawText(w - 34,        label_y, "STBD");

    if (rows.empty()) return;

    // -- Range data ------------------------------------------------------------
    const int rh       = lay.row_height;
    const int vis_last = qMin(scroll.scrollRow() + lay.img_h / rh,
                              static_cast<int>(rows.size()));
    float max_r = 0.f;
    for (int ri = scroll.scrollRow(); ri < vis_last; ++ri)
        if (std::isfinite(rows[ri].slant_range_m))
            max_r = std::max(max_r, rows[ri].slant_range_m);
    if (max_r == 0.f)
        for (const auto& row : rows)
            if (row.slant_range_m > 0.f && std::isfinite(row.slant_range_m))
                { max_r = row.slant_range_m; break; }
    if (max_r == 0.f) return;

    const int   port_w = nadir - kWfRulerW;
    const int   stbd_w = w - nadir;
    const int   ns     = samples_per_ping;

    const float z_port = (scroll.hZoom() > 0.f) ? scroll.hZoom()
                       : (port_w > 0 && ns > 0 ? float(port_w) / ns : 1.f);
    const float z_stbd = (scroll.hZoom() > 0.f) ? scroll.hZoom()
                       : (stbd_w > 0 && ns > 0 ? float(stbd_w) / ns : 1.f);

    const int far_port_si = ns > 0
        ? qMin(ns - 1, static_cast<int>((port_w - 1) / z_port) + scroll.hPan()) : 0;
    const int far_stbd_si = ns > 0
        ? qMin(ns - 1, static_cast<int>((stbd_w - 1) / z_stbd) + scroll.hPan()) : 0;
    const float vis_max_r =
        float(qMax(far_port_si, far_stbd_si)) / float(qMax(1, ns - 1)) * max_r;

    const float tick_step = vis_max_r > 500.f ? 100.f
                          : (vis_max_r > 200.f ? 50.f : 25.f);

    // -- Tick marks + range numbers --------------------------------------------
    // Small tick notch at the top of the bar, number below it.
    // Skip r=0 for numbers (nadir is self-evident from the divider line).
    p.setFont(QFont("Segoe UI", 7));
    const int tick_top = sb_y + 5;
    const int tick_bot = sb_y + 10;
    const int num_y    = sb_y + 14;

    for (float r = tick_step; r <= vis_max_r + 0.5f; r += tick_step) {
        const int si = ns > 1 ? static_cast<int>(r / max_r * (ns - 1)) : 0;
        const QString label = QString::number(int(r));

        // Port side — ticks go left from nadir, label right-aligned at tick
        const int px = nadir - static_cast<int>((si - scroll.hPan()) * z_port);
        if (px >= kWfRulerW + 2 && px < nadir - 4) {
            p.setPen(QColor(Theme::kTextDisabled));
            p.drawLine(px, tick_top, px, tick_bot);
            p.setPen(QColor(Theme::kTextMuted));
            p.drawText(px - 13, num_y, label);
        }

        // Starboard side — ticks go right from nadir, label left-aligned at tick
        const int sx = nadir + static_cast<int>((si - scroll.hPan()) * z_stbd);
        if (sx > nadir + 4 && sx < w - 2) {
            p.setPen(QColor(Theme::kTextDisabled));
            p.drawLine(sx, tick_top, sx, tick_bot);
            p.setPen(QColor(Theme::kTextMuted));
            p.drawText(sx + 2, num_y, label);
        }
    }
}

} // namespace dolphin::ui
