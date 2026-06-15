// MapViewPaint.Contacts.cpp — contact diamonds and rubber-band selection rect.
#include "ui/features/map/MapView.h"
#include "app/project/Project.h"
#include "core/Contact.h"
#include "core/SpatialRef.h"
#include "geo/GeoUtils.h"
#include "ui/shell/Theme.h"

#include <QPainter>

namespace {
// Contact diamond palette
const QColor kContactFill       (255, 200,  40, 220);
const QColor kContactSelFill    (255,  80,  80, 255);
const QColor kContactOutline    (  0,   0,   0, 160);
const QColor kContactLabel      (255, 255, 255, 210);
const QColor kContactLabelShadow(  0,   0,   0, 160);
// Rubber-band selection
const QColor kRubberbandStroke  ( 59, 127, 255, 200);
const QColor kRubberbandFill    ( 59, 127, 255,  30);
} // namespace

namespace dolphin::ui {

void MapView::paintContacts(QPainter& p) const
{
    // -- Contacts --------------------------------------------------------------
    if (m_project) {
        constexpr int kRadius    = 5;
        constexpr int kSelRadius = 7;

        QFont font("Segoe UI", 8);
        p.setFont(font);

        const core::SpatialRef display_ref = m_project->displaySpatialRef();

        for (const auto& c : m_project->contacts()) {
            if (c.lat == 0.0 && c.lon == 0.0) continue;

            double disp_lon = c.lon, disp_lat = c.lat;
            if (core::spatialRefIsProjected(c.spatial_ref)) {
                core::NavPoint nav;
                nav.lat = c.lat; nav.lon = c.lon; nav.valid = true;
                nav.spatial_ref  = c.spatial_ref;
                nav.is_projected = true;
                core::NavPoint norm;
                if (!geo::normalizeNavForMap(nav, display_ref, norm)) continue;
                disp_lon = norm.lon; disp_lat = norm.lat;
            }

            const QPointF px = geoToPixel(disp_lon, disp_lat);
            if (px.x() < -50 || px.x() > width() + 50 ||
                px.y() < -50 || px.y() > height() + 50)
                continue;

            const bool   selected = (c.id == m_selected_contact_id);
            const int    r        = selected ? kSelRadius    : kRadius;
            const QColor fill     = selected ? kContactSelFill : kContactFill;

            QPolygonF diamond;
            diamond << QPointF(px.x(),     px.y() - r)
                    << QPointF(px.x() + r, px.y()    )
                    << QPointF(px.x(),     px.y() + r)
                    << QPointF(px.x() - r, px.y()    );

            p.setPen(QPen(kContactOutline, selected ? 1.5 : 1.0));
            p.setBrush(fill);
            p.drawPolygon(diamond);

            const QString lbl = QString::fromStdString(c.label);
            const int tx = static_cast<int>(px.x()) + r + 3;
            const int ty = static_cast<int>(px.y()) - 6;
            p.setPen(kContactLabelShadow);
            p.drawText(tx + 1, ty + 1, lbl);
            p.setPen(kContactLabel);
            p.drawText(tx, ty, lbl);
        }
    }

    // Rubber-band is drawn in screen space (after painter rotation is restored)
    // — see MapViewPaint.cpp paintEvent, HUD phase.
}

} // namespace dolphin::ui
