// Hover and layer hit-testing helpers for MapView.

#include "ui/features/map/MapView.h"
#include "app/layers/DataLayer.h"
#include "app/project/Project.h"

#include <QPolygonF>
#include <QToolTip>

#include <algorithm>
#include <cmath>

namespace dolphin::ui {

void MapView::setHoverTooltipsEnabled(bool on)
{
    m_hover_tooltips = on;
    if (!on) QToolTip::hideText();
}

void MapView::setHoverHighlightEnabled(bool on)
{
    m_hover_highlight = on;
    if (!on && !m_hover_layer_id.empty()) {
        m_hover_layer_id.clear();
        update();
    }
}

// Hit-test the layer under the cursor for hover tooltip/highlight. Throttled
// by movement distance — hitTestLayer walks ribbon polygons, so re-testing on
// every 1-px move would be wasteful on large projects.
void MapView::updateHoverState(QPoint pos, QPoint global_pos)
{
    if (!m_hover_tooltips && !m_hover_highlight) return;
    if (m_input_mode != ModePan && m_input_mode != ModeSelect) return;
    if ((pos - m_hover_test_px).manhattanLength() < 6) return;
    m_hover_test_px = pos;

    const std::string hit = hitTestLayer(pos);
    if (hit != m_hover_layer_id) {
        m_hover_layer_id = hit;
        if (m_hover_highlight) update();

        if (m_hover_tooltips) {
            QString label;
            if (!hit.empty() && m_project) {
                if (const auto* layer = m_project->findLayer(hit))
                    label = QString::fromStdString(layer->label.empty() ? layer->id
                                                                        : layer->label);
            }
            if (label.isEmpty()) QToolTip::hideText();
            else                 QToolTip::showText(global_pos, label, this);
        }
    }
}

std::string MapView::hitTestLayer(QPoint px) const
{
    const QPointF pxF(px);

    auto testLayer = [&](const LayerMapData& ld) -> bool {
        for (const auto& cov : ld.coverage) {
            for (const auto& ribbon : cov.ribbons) {
                if (ribbon.size() < 3) continue;
                QPolygonF poly;
                poly.reserve(static_cast<int>(ribbon.size()));
                for (const auto& pt : ribbon)
                    poly.append(geoToPixel(pt.x(), pt.y()));
                if (poly.containsPoint(pxF, Qt::OddEvenFill))
                    return true;
            }
        }
        // Track-only layers (SBP/MAG/MBE — no swath coverage): hit when the
        // cursor is within a few px of the nav line, so click-select, hover,
        // and tooltips work on them exactly like on SSS swaths.
        if (ld.coverage.empty() && !ld.nav_track.empty()) {
            // Cheap bbox pre-reject (inflated by the pick tolerance).
            constexpr double kTolPx = 6.0;
            const QPointF a = geoToPixel(ld.lon_min, ld.lat_min);
            const QPointF b = geoToPixel(ld.lon_max, ld.lat_max);
            if (!QRectF(a, b).normalized()
                     .adjusted(-kTolPx, -kTolPx, kTolPx, kTolPx).contains(pxF))
                return false;

            const double tol2 = kTolPx * kTolPx;
            QPointF prev;
            bool    prev_ok = false;
            for (const QPointF& gp : ld.nav_track) {
                if (std::isnan(gp.x())) { prev_ok = false; continue; }
                const QPointF cur = geoToPixel(gp.x(), gp.y());
                if (prev_ok) {
                    // Point-to-segment squared distance.
                    const QPointF d  = cur - prev;
                    const double  l2 = d.x() * d.x() + d.y() * d.y();
                    double t = 0.0;
                    if (l2 > 0.0)
                        t = std::clamp((QPointF::dotProduct(pxF - prev, d)) / l2,
                                       0.0, 1.0);
                    const QPointF proj = prev + t * d;
                    const QPointF e    = pxF - proj;
                    if (e.x() * e.x() + e.y() * e.y() <= tol2)
                        return true;
                }
                prev    = cur;
                prev_ok = true;
            }
        }
        return false;
    };

    // Walk project order in reverse (topmost drawn layer gets first hit).
    if (m_project) {
        const auto& layers = m_project->layers();
        for (int i = static_cast<int>(layers.size()) - 1; i >= 0; --i) {
            if (!layers[i]) continue;
            const auto it = m_layer_data.find(layers[i]->id);
            if (it == m_layer_data.end() || !it->second.visible) continue;
            if (testLayer(it->second))
                return layers[i]->id;
        }
    } else {
        for (const auto& [lid, ld] : m_layer_data)
            if (ld.visible && testLayer(ld)) return lid;
    }
    return {};
}

std::vector<std::string> MapView::layersInRect(QRect px_rect) const
{
    std::vector<std::string> result;
    const QRectF rectF(px_rect);

    const QPolygonF rect_poly(rectF);

    auto testLayer = [&](const std::string& id, const LayerMapData& ld) {
        for (const auto& cov : ld.coverage) {
            for (const auto& ribbon : cov.ribbons) {
                if (ribbon.size() < 3) continue;
                QPolygonF poly;
                poly.reserve(static_cast<int>(ribbon.size()));
                for (const auto& pt : ribbon)
                    poly.append(geoToPixel(pt.x(), pt.y()));
                // Cheap bbox pre-reject, then exact polygon∩rect — a diagonal
                // line's bbox can cover the band without the ribbon touching it.
                if (poly.boundingRect().intersects(rectF)
                        && poly.intersects(rect_poly)) {
                    result.push_back(id);
                    return;
                }
            }
        }
        // Track-only layers (SBP/MAG/MBE): rubber-band selects the line when
        // any track segment crosses the rect (endpoint containment plus a
        // sampled-segment check covers thin diagonal crossings).
        if (ld.coverage.empty() && !ld.nav_track.empty()) {
            QPointF prev;
            bool    prev_ok = false;
            for (const QPointF& gp : ld.nav_track) {
                if (std::isnan(gp.x())) { prev_ok = false; continue; }
                const QPointF cur = geoToPixel(gp.x(), gp.y());
                if (rectF.contains(cur)) { result.push_back(id); return; }
                if (prev_ok && QRectF(prev, cur).normalized().intersects(rectF)) {
                    // Segment bbox touches the rect: test the segment against
                    // the rect polygon (exact enough at nav-point density).
                    if (rect_poly.containsPoint((prev + cur) / 2.0, Qt::OddEvenFill)
                            || rectF.contains(prev) || rectF.contains(cur)) {
                        result.push_back(id);
                        return;
                    }
                }
                prev    = cur;
                prev_ok = true;
            }
        }
    };

    if (m_project) {
        for (const auto& layer : m_project->layers()) {
            if (!layer) continue;
            const auto it = m_layer_data.find(layer->id);
            if (it == m_layer_data.end() || !it->second.visible) continue;
            testLayer(layer->id, it->second);
        }
    } else {
        for (const auto& [lid, ld] : m_layer_data)
            if (ld.visible) testLayer(lid, ld);
    }
    return result;
}

} // namespace dolphin::ui
