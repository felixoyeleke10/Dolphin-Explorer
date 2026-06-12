// MapViewPaint.cpp — paintEvent orchestrator.
//
// Draw phases are implemented in split files:
//   MapViewPaint.Sonar.cpp      — background/grid, sonar images, coverage ribbons
//   MapViewPaint.NavTrack.cpp   — graticule, nav track
//   MapViewPaint.Contacts.cpp   — contacts, rubber-band selection rect
//   MapViewPaint.Overlays.cpp   — measure overlay, scale bar, status badges
#include "ui/features/map/MapView.h"

#include <QPainter>

namespace dolphin::ui {

void MapView::paintEvent(QPaintEvent*)
{
    ensureCombined();

    QPainter p(this);
    p.setRenderHints(QPainter::Antialiasing |
                     QPainter::TextAntialiasing |
                     QPainter::SmoothPixmapTransform);

    p.fillRect(rect(), m_map_bg_color);

    // Rotate canvas around viewport centre when bearing is non-zero.
    if (m_rotation_deg != 0.0) {
        p.translate(width() / 2.0, height() / 2.0);
        p.rotate(m_rotation_deg);
        p.translate(-width() / 2.0, -height() / 2.0);
    }

    if (m_layer_data.empty() && m_nav_track.empty()) {
        if (m_show_grid) paintGraticule(p);
        paintEmptyState(p);
        return;
    }

    paintSonarLayers(p);
    if (m_show_grid) paintGraticule(p);
    paintNavTrack      (p);
    paintProfileTracks (p);
    paintContacts      (p);
    paintMeasureOverlay(p);
    paintScaleAndBadges(p);
}


} // namespace dolphin::ui
