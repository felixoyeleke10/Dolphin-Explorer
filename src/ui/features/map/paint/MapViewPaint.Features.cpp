// MapViewPaint.Features.cpp — feature shapes (polygons/polylines) + draw draft.
//
// Features are SHAPE annotations, distinct from contact point picks. Committed
// features are read from the project; the in-progress draft is the live polygon/
// polyline being drawn in ModeDrawFeature.
#include "ui/features/map/MapView.h"
#include "app/project/Project.h"
#include "core/Feature.h"
#include "core/SpatialRef.h"
#include "geo/GeoUtils.h"

#include <QPainter>
#include <QPolygonF>
#include <vector>

namespace {
// Feature palette — teal/cyan to read distinctly from the golden contact diamonds.
const QColor kFeatureLine     ( 60, 200, 200, 220);
const QColor kFeatureFill     ( 60, 200, 200,  45);
const QColor kFeatureSelLine  (120, 240, 255, 255);
const QColor kFeatureSelFill  (120, 240, 255,  70);
const QColor kFeatureVertex   ( 60, 200, 200, 240);
const QColor kFeatureVtxBorder(  0,   0,   0, 150);
const QColor kFeatureLabel    (235, 255, 255, 220);
const QColor kFeatureLabelShad (  0,   0,   0, 160);
// Draft (in-progress) — brighter, dashed live segment.
const QColor kDraftLine       (120, 240, 255, 230);
const QColor kDraftLineLive   (120, 240, 255, 150);
const QColor kDraftFill       (120, 240, 255,  40);
} // namespace

namespace dolphin::ui {

void MapView::paintFeatures(QPainter& p) const
{
    p.setRenderHint(QPainter::Antialiasing, true);

    // -- Committed features ----------------------------------------------------
    if (m_project) {
        const core::SpatialRef display_ref = m_project->displaySpatialRef();

        QFont font("Segoe UI", 8);
        p.setFont(font);

        for (const auto& f : m_project->features()) {
            if (f.vertices.size() < 2) continue;

            QPolygonF poly;
            poly.reserve(static_cast<int>(f.vertices.size()));
            bool any_visible = false;
            for (const auto& v : f.vertices) {
                double disp_lon = v.lon, disp_lat = v.lat;
                if (core::spatialRefIsProjected(f.spatial_ref)) {
                    core::NavPoint nav;
                    nav.lat = v.lat; nav.lon = v.lon; nav.valid = true;
                    nav.spatial_ref  = f.spatial_ref;
                    nav.is_projected = true;
                    core::NavPoint norm;
                    if (!geo::normalizeNavForMap(nav, display_ref, norm)) continue;
                    disp_lon = norm.lon; disp_lat = norm.lat;
                }
                const QPointF px = geoToPixel(disp_lon, disp_lat);
                poly.append(px);
                if (px.x() >= -50 && px.x() <= width() + 50 &&
                    px.y() >= -50 && px.y() <= height() + 50)
                    any_visible = true;
            }
            if (poly.size() < 2 || !any_visible) continue;

            const bool   selected = (f.id == m_selected_feature_id);
            const QColor line     = selected ? kFeatureSelLine : kFeatureLine;
            const bool   polygon  = (f.type == core::FeatureType::Polygon);

            if (polygon) {
                p.setPen(QPen(line, selected ? 2.0 : 1.5));
                p.setBrush(selected ? kFeatureSelFill : kFeatureFill);
                p.drawPolygon(poly);
            } else {
                p.setPen(QPen(line, selected ? 2.0 : 1.5));
                p.setBrush(Qt::NoBrush);
                p.drawPolyline(poly);
            }

            // Label near the first vertex.
            if (!f.label.empty()) {
                const QPointF& a = poly.front();
                const QString lbl = QString::fromStdString(f.label);
                const int tx = static_cast<int>(a.x()) + 6;
                const int ty = static_cast<int>(a.y()) - 6;
                p.setPen(kFeatureLabelShad);
                p.drawText(tx + 1, ty + 1, lbl);
                p.setPen(kFeatureLabel);
                p.drawText(tx, ty, lbl);
            }
        }
    }

    // -- In-progress draft -----------------------------------------------------
    if (m_input_mode == ModeDrawFeature && m_feature_drawing &&
        !m_feature_pts_geo.empty()) {

        QPolygonF draft;
        draft.reserve(static_cast<int>(m_feature_pts_geo.size()));
        for (const auto& g : m_feature_pts_geo)
            draft.append(geoToPixel(g.x(), g.y()));

        // Confirmed chain (solid).
        if (draft.size() >= 2) {
            p.setPen(QPen(kDraftLine, 1.5));
            p.setBrush(Qt::NoBrush);
            p.drawPolyline(draft);
        }

        // Polygon: translucent fill preview once it encloses area.
        if (m_feature_polygon && draft.size() >= 3) {
            QPolygonF closed = draft;
            closed.append(QPointF(m_feature_cursor_px));
            p.setPen(Qt::NoPen);
            p.setBrush(kDraftFill);
            p.drawPolygon(closed);
        }

        // Live segment from the last confirmed vertex to the cursor (dashed).
        p.setPen(QPen(kDraftLineLive, 1.5, Qt::DashLine));
        p.drawLine(draft.back(), QPointF(m_feature_cursor_px));

        // Polygon: dashed closing segment back to the first vertex.
        if (m_feature_polygon && draft.size() >= 2)
            p.drawLine(QPointF(m_feature_cursor_px), draft.front());

        // Vertex dots.
        p.setPen(QPen(kFeatureVtxBorder, 1));
        p.setBrush(kFeatureVertex);
        for (const QPointF& pt : draft)
            p.drawEllipse(pt, 3.5, 3.5);
    }
}

} // namespace dolphin::ui
