// Feature-selection and draft lifecycle helpers for MapView.

#include "ui/features/map/MapView.h"
#include "ui/features/map/MapLongitude.h"

#include <cstddef>

namespace dolphin::ui {

void MapView::setFeatureDrawKind(int kind)
{
    m_feature_kind = kind;
}

void MapView::setSelectedFeature(uint64_t id)
{
    if (m_selected_feature_id == id) return;
    m_selected_feature_id = id;
    update();
}

void MapView::commitFeatureDraft()
{
    // A double-click commit arrives as press (adds a vertex) + dblclick (commits),
    // leaving a duplicate final vertex — and Qt allows a few px of slop between
    // the two presses, so compare in pixel space. Click tools only: pen points
    // are legitimately close together and never commit via double-click.
    if (m_feature_kind != 3) {
        while (m_feature_pts_geo.size() >= 2) {
            const auto& p1 = m_feature_pts_geo[m_feature_pts_geo.size() - 1];
            const auto& p2 = m_feature_pts_geo[m_feature_pts_geo.size() - 2];
            const QPointF d = geoToPixel(p1.x(), p1.y()) - geoToPixel(p2.x(), p2.y());
            if (d.manhattanLength() >= 6.0) break;
            m_feature_pts_geo.pop_back();
        }
    }

    // Polygon (kind 1) needs >=3 vertices to enclose area; line/pen need >=2.
    const bool        polygon = (m_feature_kind == 1);
    const std::size_t min_pts = polygon ? 3u : 2u;
    if (m_feature_pts_geo.size() >= min_pts) {
        auto output_points = m_feature_pts_geo;
        if (!m_is_projected) {
            for (QPointF& point : output_points)
                point.setX(maplongitude::canonical(point.x()));
        }
        emit featureDrawn(output_points, polygon);
    }
    m_feature_pts_geo.clear();
    m_feature_drawing = false;
    m_feature_pen_down = false;
    update();
}

void MapView::cancelFeatureDraft()
{
    m_feature_pts_geo.clear();
    m_feature_drawing = false;
    m_feature_pen_down = false;
    update();
}

} // namespace dolphin::ui
