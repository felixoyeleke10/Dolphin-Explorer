// WaterfallOverlayPainterAmplitude.cpp — amplitude bar overlay
//
// paintAmplitudeBar() — per-column mean amplitude line chart at the bottom edge.
// Receives a precomputed WaterfallAmpProfile so this TU is pure painting.

#include "ui/features/waterfall/painters/WaterfallOverlayPainter.h"
#include "ui/features/waterfall/processing/WaterfallAmpProfile.h"

#include "ui/shell/Theme.h"

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QVector>

namespace {
// Amplitude line colors — port and starboard channel identity.
// Slightly lighter than the scale-bar strip colors to suit the anti-aliased line.
const QColor kPortAmpColor(0xc8, 0x50, 0x38);
const QColor kStbdAmpColor(0x48, 0x6c, 0xd8);
} // namespace

namespace dolphin::ui {

void WaterfallOverlayPainter::paintAmplitudeBar(
    QPainter&                        p,
    const WaterfallAmpProfile&       profile,
    const WfLayout&                  lay,
    const WaterfallScrollController& scroll)
{
    const int w     = lay.widget_w;
    const int h     = lay.widget_h;
    const int nadir = lay.nadir_x;
    const int bar_y = h - kWfAmpBarH;
    const int bar_h = kWfAmpBarH;

    // ── Background ────────────────────────────────────────────────────────────
    p.fillRect(0,     bar_y, nadir,    bar_h, QColor(Theme::kBg));
    p.fillRect(nadir, bar_y, w - nadir, bar_h, QColor(Theme::kBg));

    // Top separator
    p.setPen(QColor(Theme::kBorder));
    p.drawLine(0, bar_y, w, bar_y);

    // Nadir divider
    p.setPen(QPen(QColor(Theme::kTextMuted), 1));
    p.drawLine(nadir, bar_y + 1, nadir, h - 1);

    // Channel labels
    p.setFont(QFont("Segoe UI", 7, QFont::Medium));
    p.setPen(QColor(Theme::kTextMuted));
    p.drawText(4,     h - 3, "PORT");
    p.drawText(w - 34, h - 3, "STBD");

    if (profile.ns == 0) return;

    const int   ns     = profile.ns;
    const float h_zoom = scroll.hZoom();
    const int   h_pan  = scroll.hPan();
    const int   port_w = nadir;
    const int   stbd_w = w - nadir;

    const float z_port = (h_zoom > 0.f) ? h_zoom : (port_w > 0 ? float(port_w) / ns : 1.f);
    const float z_stbd = (h_zoom > 0.f) ? h_zoom : (stbd_w > 0 ? float(stbd_w) / ns : 1.f);

    // Chart area — line sits within bar, with small vertical margins.
    const float chart_bot = static_cast<float>(h - 16);
    const float chart_top = static_cast<float>(bar_y + 3);
    const float chart_h   = chart_bot - chart_top;
    if (chart_h <= 0.f) return;

    p.setRenderHint(QPainter::Antialiasing);

    // ── Port line ─────────────────────────────────────────────────────────────
    {
        QVector<QPointF> pts;
        pts.reserve(nadir);
        for (int x = 0; x < nadir; ++x) {
            const int si = static_cast<int>(h_pan + (nadir - 1 - x) / z_port);
            if (si < 0 || si >= ns) continue;
            const float y = chart_bot - profile.port_mean[si] / 65535.f * chart_h;
            pts.append({double(x), double(y)});
        }
        if (pts.size() > 1) {
            p.setPen(QPen(kPortAmpColor, 1.5f));
            p.drawPolyline(pts.data(), pts.size());
        }
    }

    // ── Starboard line ────────────────────────────────────────────────────────
    {
        QVector<QPointF> pts;
        pts.reserve(w - nadir);
        for (int x = nadir; x < w; ++x) {
            const int si = static_cast<int>(h_pan + (x - nadir) / z_stbd);
            if (si < 0 || si >= ns) continue;
            const float y = chart_bot - profile.stbd_mean[si] / 65535.f * chart_h;
            pts.append({double(x), double(y)});
        }
        if (pts.size() > 1) {
            p.setPen(QPen(kStbdAmpColor, 1.5f));
            p.drawPolyline(pts.data(), pts.size());
        }
    }

    p.setRenderHint(QPainter::Antialiasing, false);
}

} // namespace dolphin::ui
