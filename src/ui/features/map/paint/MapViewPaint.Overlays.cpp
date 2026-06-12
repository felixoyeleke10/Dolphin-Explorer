// MapViewPaint.Overlays.cpp — measure overlay, scale bar, status badges.
#include "ui/features/map/MapView.h"

#include <QPainter>
#include <QPolygonF>
#include <algorithm>
#include <cmath>

namespace {

// Status badge palette — semi-transparent so the map image composites through.
const QColor kBadgeInfoBg   (0x11, 0x11, 0x18, 200);  // info: dark panel
const QColor kBadgeInfoFg   (0x3b, 0x7f, 0xff, 200);  // info: accent blue
const QColor kBadgeWarnBg   (0x33, 0x22, 0x00, 200);  // warning / caution: dark amber
const QColor kBadgeWarnFg   (0xff, 0xcc, 0x00, 220);  // warning: yellow
const QColor kBadgeCautionFg(0xff, 0xaa, 0x00, 220);  // caution: amber (preview reduced)
const QColor kBadgeDangerBg (0x44, 0x18, 0x00, 210);  // danger: dark orange-red
const QColor kBadgeDangerFg (0xff, 0x77, 0x00, 230);  // danger: orange (bad nav)

// Measure overlay — golden polyline + lettered anchor dots
const QColor kMeasureLine      (255, 220,  50, 210);
const QColor kMeasureDot       (255, 220,  50, 240);
const QColor kMeasureLineDash  (255, 220,  50, 140);   // live (uncommitted) segment
const QColor kMeasureDotOutline(  0,   0,   0, 130);   // dot border
const QColor kMeasureLabelShad (  0,   0,   0, 180);   // label drop-shadow
// Measure info box
const QColor kMeasureBoxBg     ( 15,  15,  25, 215);
const QColor kMeasureBoxBorder (255, 220,  50,  70);
const QColor kMeasureBoxText   (210, 210, 210, 220);
const QColor kMeasureDivider   (255, 220,  50,  55);
const QColor kMeasureTotal     (255, 220,  50, 235);
} // namespace

namespace dolphin::ui {

void MapView::paintMeasureOverlay(QPainter& p) const
{
    if (m_input_mode != ModeMeasure || m_measure_pts_geo.empty()) return;

    p.setRenderHint(QPainter::Antialiasing, true);


    // Committed segments — solid golden line
    if (m_measure_pts_px.size() >= 2) {
        p.setPen(QPen(kMeasureLine, 1.5));
        for (int i = 1; i < static_cast<int>(m_measure_pts_px.size()); ++i)
            p.drawLine(m_measure_pts_px[i - 1], m_measure_pts_px[i]);
    }

    // Live segment (dashed) from last confirmed point to cursor
    {
        p.setPen(QPen(kMeasureLineDash, 1.5, Qt::DashLine));
        p.drawLine(m_measure_pts_px.back(), m_measure_cursor_px);
    }

    // Lettered dot at each confirmed anchor: A, B, C, …
    QFont dotFont("Segoe UI", 8, QFont::Bold);
    p.setFont(dotFont);
    const QFontMetrics dotFm(dotFont);
    for (int i = 0; i < static_cast<int>(m_measure_pts_px.size()); ++i) {
        const QPoint& pt = m_measure_pts_px[i];
        p.setBrush(kMeasureDot);
        p.setPen(QPen(kMeasureDotOutline, 1));
        p.drawEllipse(pt, 4, 4);

        const QString lbl = i < 26
            ? QString(QChar('A' + i))
            : QString(QChar('A' + i / 26 - 1)) + QString(QChar('A' + i % 26));
        const int lw = dotFm.horizontalAdvance(lbl);
        p.setPen(kMeasureLabelShad);
        p.drawText(pt.x() - lw / 2 + 1, pt.y() - 7, lbl);
        p.setPen(kMeasureDot);
        p.drawText(pt.x() - lw / 2, pt.y() - 8, lbl);
    }

    // Floating overlay box (top-right corner)
    if (!m_measure_seg_dist.empty() || m_measure_live_dist > 0.0) {
        QFont rowFont ("Segoe UI", 9);
        QFont boldFont("Segoe UI", 9, QFont::DemiBold);
        const QFontMetrics rfm(rowFont);
        const QFontMetrics bfm(boldFont);
        const int lineH = rfm.height() + 3;
        const int pad   = 8;

        QStringList rows;
        double committed = 0.0;
        for (int i = 0; i < static_cast<int>(m_measure_seg_dist.size()); ++i) {
            const double d = m_measure_seg_dist[i];
            committed += d;
            const QString seg = i < 25
                ? QString("%1-%2").arg(QChar('A' + i)).arg(QChar('A' + i + 1))
                : QString("seg %1").arg(i + 1);
            const QString val = d >= 1000.0
                ? QString("%1 km").arg(d / 1000.0, 0, 'f', 3)
                : QString("%1 m") .arg(d,           0, 'f', 1);
            rows.append(seg + ":  " + val);
        }

        const int liveIdx = static_cast<int>(m_measure_seg_dist.size());
        if (m_measure_live_dist > 0.0) {
            const QString seg = liveIdx < 25
                ? QString("%1-%2").arg(QChar('A' + liveIdx)).arg(QChar('A' + liveIdx + 1))
                : QString("seg %1").arg(liveIdx + 1);
            const QString val = m_measure_live_dist >= 1000.0
                ? QString("%1 km").arg(m_measure_live_dist / 1000.0, 0, 'f', 3)
                : QString("%1 m") .arg(m_measure_live_dist,           0, 'f', 1);
            rows.append(seg + ":  " + val + " …");
        }

        const double total = committed + m_measure_live_dist;
        const QString totalStr = total >= 1000.0
            ? QString("%1 km").arg(total / 1000.0, 0, 'f', 3)
            : QString("%1 m") .arg(total,           0, 'f', 1);
        const QString totalRow = "Total:  " + totalStr;

        int maxW = bfm.horizontalAdvance(totalRow);
        for (const QString& r : rows)
            maxW = std::max(maxW, rfm.horizontalAdvance(r));

        const int nRows = static_cast<int>(rows.size());
        const int boxH  = pad * 2 + nRows * lineH + (nRows > 0 ? 6 : 0) + lineH;
        const int boxW  = maxW + pad * 2;
        const int margin = 12;
        const QRect box(width() - boxW - margin, margin, boxW, boxH);

        p.setBrush(kMeasureBoxBg);
        p.setPen(QPen(kMeasureBoxBorder, 1));
        p.drawRoundedRect(box, 6, 6);

        int y = box.top() + pad + rfm.ascent();
        p.setFont(rowFont);
        p.setPen(kMeasureBoxText);
        for (const QString& r : rows) {
            p.drawText(box.left() + pad, y, r);
            y += lineH;
        }

        if (nRows > 0) {
            p.setPen(kMeasureDivider);
            p.drawLine(box.left() + pad, y + 1, box.right() - pad, y + 1);
            y += 5;
        }

        p.setFont(boldFont);
        p.setPen(kMeasureTotal);
        p.drawText(box.left() + pad, y + bfm.ascent() - rfm.ascent(), totalRow);
    }
}

void MapView::paintScaleAndBadges(QPainter& p) const
{
    // -- North arrow (top-right, fixed north-up) ------------------------------
    {
        p.setRenderHint(QPainter::Antialiasing, true);
        constexpr int kR = 13, kMargin = 22, kCY = 30;
        const int kCX = width() - kMargin;

        p.setBrush(QColor(0x11, 0x11, 0x18, 180));
        p.setPen(QPen(QColor(255, 255, 255, 30), 1));
        p.drawEllipse(QPoint(kCX, kCY), kR, kR);

        const QPolygonF north_needle{
            QPointF(kCX,     kCY - kR + 2),
            QPointF(kCX + 5, kCY + 3),
            QPointF(kCX - 5, kCY + 3),
        };
        p.setBrush(QColor(255, 255, 255, 220));
        p.setPen(Qt::NoPen);
        p.drawPolygon(north_needle);

        const QPolygonF south_needle{
            QPointF(kCX,     kCY + kR - 2),
            QPointF(kCX + 5, kCY + 3),
            QPointF(kCX - 5, kCY + 3),
        };
        p.setBrush(QColor(0x8b, 0x1a, 0x1a, 200));
        p.setPen(Qt::NoPen);
        p.drawPolygon(south_needle);

        QFont nf("Segoe UI", 7, QFont::Bold);
        p.setFont(nf);
        p.setPen(QColor(200, 200, 200, 210));
        const int nw = QFontMetrics(nf).horizontalAdvance("N");
        p.drawText(kCX - nw / 2, kCY - kR - 1, "N");
    }

    // -- Status badges (top-right) ---------------------------------------------
    {
        bool any_reduced   = false;
        int  bad_nav_total = 0;
        int  total_nav     = 0;
        int  gap_total     = 0;
        int  spike_total   = 0;
        for (const auto& [lid, ld] : m_layer_data) {
            if (!ld.visible) continue;
            if (ld.preview_reduced) any_reduced = true;
            bad_nav_total += static_cast<int>(ld.nav_stats.invalid_nav);
            bad_nav_total += static_cast<int>(ld.track_stats.invalid_nav
                                            + ld.track_stats.zero_coords);
            total_nav     += static_cast<int>(ld.nav_stats.total_pings);
            total_nav     += static_cast<int>(ld.track_stats.total_traces);
            gap_total     += static_cast<int>(ld.nav_stats.time_gaps);
            spike_total   += static_cast<int>(ld.nav_stats.nav_spikes);
        }

        QFont bf("Segoe UI", 8);
        p.setFont(bf);
        const QFontMetrics fm(bf);

        auto drawBadge = [&](int& badge_y, const QString& text, QColor bg, QColor fg) {
            const int text_w = fm.horizontalAdvance(text);
            const int pad_x  = 8;
            const int pad_y  = 4;
            const int bw     = text_w + pad_x * 2;
            const int bh     = fm.height() + pad_y * 2;
            const int bx     = width() - bw - 10;
            const int by     = badge_y;
            badge_y += bh + 4;

            p.setPen(Qt::NoPen);
            p.setBrush(bg);
            p.drawRoundedRect(bx, by, bw, bh, 3, 3);
            p.setPen(fg);
            p.drawText(QRect(bx + pad_x, by + pad_y, text_w, fm.height()),
                       Qt::AlignLeft | Qt::AlignVCenter, text);
        };

        int badge_y = 10;

        if (any_reduced)
            drawBadge(badge_y, tr("preview reduced"), kBadgeWarnBg, kBadgeCautionFg);

        // Only show if >= 5% of pings have bad nav; GPS dropout at line
        // edges is normal and not worth alarming on.
        const bool show_bad_nav = bad_nav_total > 0
            && (total_nav == 0 || bad_nav_total * 100 / total_nav >= 5);
        if (show_bad_nav)
            drawBadge(badge_y, tr("%1 bad nav").arg(bad_nav_total),
                      kBadgeDangerBg, kBadgeDangerFg);

        if (gap_total > 0)
            drawBadge(badge_y,
                      gap_total == 1 ? tr("1 survey gap") : tr("%1 survey gaps").arg(gap_total),
                      kBadgeWarnBg, kBadgeWarnFg);

        if (spike_total > 0)
            drawBadge(badge_y,
                      spike_total == 1 ? tr("1 GPS spike") : tr("%1 GPS spikes").arg(spike_total),
                      kBadgeWarnBg, kBadgeWarnFg);
    }
}

} // namespace dolphin::ui
