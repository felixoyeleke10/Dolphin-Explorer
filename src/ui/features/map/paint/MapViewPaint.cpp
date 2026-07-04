// MapViewPaint.cpp — paintEvent orchestrator.
//
// Draw phases are implemented in split files:
//   MapViewPaint.Sonar.cpp      — background/grid, sonar images, coverage ribbons
//   MapViewPaint.NavTrack.cpp   — graticule, nav track
//   MapViewPaint.Contacts.cpp   — contact diamonds (geo space only)
//   MapViewPaint.Overlays.cpp   — measure overlay, scale bar, status badges
//
// All HUD elements (rubber-band, north arrow, badges) are drawn AFTER p.restore()
// so they are always in screen space regardless of map rotation.
#include "ui/features/map/MapView.h"

#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>

namespace dolphin::ui {

void MapView::paintEvent(QPaintEvent*)
{
    ensureCombined();

    QPainter p(this);
    p.setRenderHints(QPainter::Antialiasing |
                     QPainter::TextAntialiasing |
                     QPainter::SmoothPixmapTransform);

    p.fillRect(rect(), m_map_bg_color);

    // Save screen-space state, then optionally rotate the canvas.
    // p.restore() below returns to screen space for HUD drawing.
    p.save();
    if (m_rotation_deg != 0.0) {
        p.translate(width() / 2.0, height() / 2.0);
        p.rotate(m_rotation_deg);
        p.translate(-width() / 2.0, -height() / 2.0);
    }

    if (m_layer_data.empty() && m_nav_track.empty()) {
        if (m_show_grid) paintGraticule(p);
        paintEmptyState(p);
        paintFeatures      (p);
    } else {
        paintSonarLayers(p);
        if (m_show_grid) paintGraticule(p);
        paintNavTrack      (p);
        paintContacts      (p);
        paintFeatures      (p);
        paintMeasureOverlay(p);
    }

    // Screen-space HUD phase — restore removes the canvas rotation.
    p.restore();

    // Rubber-band selection rect: uses raw widget-pixel coordinates so it must
    // be drawn here in screen space, not in the rotated canvas above.
    if (m_rubberbanding && m_drag_moved) {
        const QRect rr = QRect(m_drag_start, m_rubberband_end).normalized();
        p.setPen(QPen(QColor(59, 127, 255, 200), 1, Qt::DashLine));
        p.setBrush(QColor(59, 127, 255, 30));
        p.drawRect(rr);
    }

    paintScaleAndBadges(p);
}


} // namespace dolphin::ui
