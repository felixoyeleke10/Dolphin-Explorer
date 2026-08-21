// MapViewPaint.Sonar.cpp — empty state, sonar images, coverage ribbons.
#include "ui/features/map/MapView.h"
#include "app/project/Project.h"
#include "core/Feature.h"
#include "core/SpatialRef.h"
#include "geo/GeoUtils.h"
#include "ui/shell/Theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <algorithm>
#include <array>
#include <cmath>

namespace {
// Semi-transparent accent for the "DOLPHIN EXPLORER" watermark — accent-hued but
// deliberately dimmer than the interactive accent to read as a background element.
const QColor kEmptyStateAccent  (0x3b, 0x7f, 0xff, 140);
// Selected-layer hull outline — full-opacity accent, not semi-transparent.
const QColor kSelectedLayerOutline(59, 127, 255, 255);
} // namespace

namespace dolphin::ui {

void MapView::paintEmptyState(QPainter& p) const
{
    // Soft accent glow behind the launcher hero (the actionable empty state —
    // logo, actions, Recent Projects — is the overlay in MapViewportHost).
    // Reads on any canvas colour in either theme; deliberately faint.
    const QRect r = rect();
    QRadialGradient glow(QPointF(r.width() / 2.0, r.height() * 0.38),
                         r.width() * 0.42);
    QColor c(kEmptyStateAccent);
    c.setAlpha(22);
    glow.setColorAt(0.0, c);
    c.setAlpha(0);
    glow.setColorAt(1.0, c);
    p.fillRect(r, glow);
}

void MapView::paintSonarLayers(QPainter& p) const
{
    // Iterate visible layers in project order.
    auto forEachVisibleLayer = [&](auto fn) {
        if (m_project) {
            for (const auto& layer : m_project->layers()) {
                if (!layer) continue;
                const auto it = m_layer_data.find(layer->id);
                if (it == m_layer_data.end() || !it->second.visible) continue;
                fn(it->second);
            }
        } else {
            for (const auto& [lid, ld] : m_layer_data)
                if (ld.visible) fn(ld);
        }
    };

    // Clip mask from the project's drawn polygons, in pixel space, built lazily
    // and cached for this paint (only when a layer requests clip-to-polygons).
    // Empty path (no polygons yet) means "don't clip" so an enabled toggle with
    // nothing drawn never blanks the whole mosaic.
    bool  clip_built = false;
    QPainterPath clip_path;
    auto sonarClipPath = [&]() -> const QPainterPath& {
        if (clip_built) return clip_path;
        clip_built = true;
        if (!m_project) return clip_path;
        const core::SpatialRef display_ref = m_project->displaySpatialRef();
        for (const auto& f : m_project->features()) {
            if (!f.visible || f.type != core::FeatureType::Polygon) continue;
            if (f.vertices.size() < 3) continue;
            QPolygonF poly;
            poly.reserve(static_cast<int>(f.vertices.size()));
            for (const auto& v : f.vertices) {
                double lon = v.lon, lat = v.lat;
                if (core::spatialRefIsProjected(f.spatial_ref)) {
                    core::NavPoint nav; nav.lat = v.lat; nav.lon = v.lon;
                    nav.valid = true; nav.spatial_ref = f.spatial_ref;
                    nav.is_projected = true;
                    core::NavPoint norm;
                    if (!geo::normalizeNavForMap(nav, display_ref, norm)) continue;
                    lon = norm.lon; lat = norm.lat;
                }
                poly.append(geoToPixel(lon, lat));
            }
            if (poly.size() >= 3) clip_path.addPolygon(poly);
        }
        return clip_path;
    };

    // Merged port+starboard swath polygon renderer.
    // show_inner=true: OddEvenFill donut (fill with nadir dead-zone hole).
    // show_inner=false: outer hull only (used for selection outline).
    auto drawMergedSwath = [&](const LayerMapData& ld, bool show_inner) {
        const auto& display_coverage =
            (!ld.show_nadir && !ld.coverage_nadir_hidden.empty())
                ? ld.coverage_nadir_hidden : ld.coverage;
        const SwathCoverage* port_cov = nullptr;
        const SwathCoverage* stbd_cov = nullptr;
        for (const auto& cov : display_coverage) {
            if      (cov.channel == core::SidescanChannel::Port)      port_cov = &cov;
            else if (cov.channel == core::SidescanChannel::Starboard) stbd_cov = &cov;
        }

        if (port_cov && stbd_cov) {
            const int count = static_cast<int>(
                std::min(port_cov->ribbons.size(), stbd_cov->ribbons.size()));
            for (int i = 0; i < count; ++i) {
                const auto& pr = port_cov->ribbons[i];
                const auto& sr = stbd_cov->ribbons[i];
                const int pn = static_cast<int>(pr.size());
                const int sn = static_cast<int>(sr.size());
                if (pn < 4 || sn < 4) continue;
                const int ph = pn / 2;
                const int sh = sn / 2;

                QPolygonF outer;
                outer.reserve((pn - ph) + (sn - sh));
                for (int j = pn - 1; j >= ph; --j)
                    outer.append(geoToPixel(pr[j].x(), pr[j].y()));
                for (int j = sh; j < sn; ++j)
                    outer.append(geoToPixel(sr[j].x(), sr[j].y()));
                if (outer.size() < 3) continue;

                if (show_inner) {
                    QPolygonF inner;
                    inner.reserve(ph + sh);
                    for (int j = ph - 1; j >= 0; --j)
                        inner.append(geoToPixel(pr[j].x(), pr[j].y()));
                    for (int j = 0; j < sh; ++j)
                        inner.append(geoToPixel(sr[j].x(), sr[j].y()));

                    if (inner.size() >= 3) {
                        QPainterPath path;
                        path.setFillRule(Qt::OddEvenFill);
                        path.addPolygon(outer);
                        path.closeSubpath();
                        path.addPolygon(inner);
                        path.closeSubpath();
                        p.drawPath(path);
                    } else {
                        p.drawPolygon(outer, Qt::OddEvenFill);
                    }
                } else {
                    p.drawPolygon(outer, Qt::OddEvenFill);
                }
            }
        } else {
            for (const auto& cov : display_coverage) {
                for (const auto& ribbon : cov.ribbons) {
                    if (ribbon.size() < 3) continue;
                    QPolygonF poly;
                    poly.reserve(static_cast<int>(ribbon.size()));
                    for (const auto& pt : ribbon)
                        poly.append(geoToPixel(pt.x(), pt.y()));
                    p.drawPolygon(poly, Qt::OddEvenFill);
                }
            }
        }
    };

    // Sonar preview images (drawn below coverage and overlays).
    {
        p.save();
        p.setRenderHint(QPainter::Antialiasing, false);
        // Mosaic rasters are frequently drawn at a different screen scale from
        // their cached grid. Bilinear image resampling avoids nearest-neighbour
        // alias bands without changing the underlying amplitude product.
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        forEachVisibleLayer([&](const LayerMapData& ld) {
            if (ld.preview_image.isNull()) return;
            p.save();
            const QPointF tl = geoToPixel(ld.lon_min, ld.lat_max);
            const QPointF br = geoToPixel(ld.lon_max, ld.lat_min);
            if (!ld.show_nadir && !ld.coverage_nadir_hidden.empty()) {
                QPainterPath footprint;
                footprint.setFillRule(Qt::WindingFill);
                for (const auto& coverage : ld.coverage_nadir_hidden) {
                    for (const auto& ribbon : coverage.ribbons) {
                        if (ribbon.size() < 3) continue;
                        QPolygonF polygon;
                        polygon.reserve(static_cast<int>(ribbon.size()));
                        for (const auto& point : ribbon)
                            polygon.append(geoToPixel(point.x(), point.y()));
                        footprint.addPolygon(polygon);
                    }
                }
                if (!footprint.isEmpty()) p.setClipPath(footprint);
            }
            // Clip this layer's mosaic to the drawn polygons (show inside) when
            // enabled and at least one polygon exists.
            if (ld.clip_polygons) {
                const QPainterPath& cp = sonarClipPath();
                if (!cp.isEmpty()) p.setClipPath(cp, Qt::IntersectClip);
            }
            // Blend mode controls how this layer composites over layers already
            // drawn beneath it (visible where surveys overlap):
            //   0 Blend    — normal opaque SourceOver. Per-layer opacity remains
            //                available explicitly; the default must not make
            //                overlaps darker than single-covered survey areas.
            //   1 Cover up — SourceOver opaque (top fully covers)
            //   2 Lighten  — keep the brighter pixel
            //   3 Darken   — keep the darker pixel
            switch (ld.blend_mode) {
                case 1: p.setCompositionMode(QPainter::CompositionMode_SourceOver);
                        p.setOpacity(1.0 * static_cast<qreal>(ld.opacity)); break;
                case 2: p.setCompositionMode(QPainter::CompositionMode_Lighten);
                        p.setOpacity(static_cast<qreal>(ld.opacity)); break;
                case 3: p.setCompositionMode(QPainter::CompositionMode_Darken);
                        p.setOpacity(static_cast<qreal>(ld.opacity)); break;
                default: p.setCompositionMode(QPainter::CompositionMode_SourceOver);
                        p.setOpacity(static_cast<qreal>(ld.opacity)); break;
            }
            p.drawImage(QRectF(tl, br), ld.preview_image);
            p.setOpacity(1.0);
            p.setCompositionMode(QPainter::CompositionMode_SourceOver);
            p.restore();
        });
        p.restore();
    }

    // SSS coverage ribbons (only for layers without a preview image).
    {
        const QColor kFill(30, 140, 255, 80);
        p.setRenderHint(QPainter::Antialiasing, false);
        forEachVisibleLayer([&](const LayerMapData& ld) {
            if (!ld.preview_image.isNull()) return;
            p.setBrush(kFill);
            p.setPen(Qt::NoPen);
            drawMergedSwath(ld, !ld.show_beams);
        });
        p.setRenderHint(QPainter::Antialiasing, true);
    }

    // Side-scan across-track rays, decimated per channel so the overlay remains
    // readable without coupling port and starboard record availability.
    {
        // Port = red; starboard = green.
        static const QColor kPort      (255,  80,  80, 90);
        static const QColor kStarboard ( 60, 200, 120, 90);
        p.setBrush(Qt::NoBrush);
        forEachVisibleLayer([&](const LayerMapData& ld) {
            if (!ld.show_beams) return;
            const size_t step = static_cast<size_t>(
                std::clamp(ld.beam_spacing, 1, 50));
            std::array<size_t, 2> channel_index{};
            for (const auto& ray : ld.beam_rays) {
                const size_t side = ray.channel == core::SidescanChannel::Port
                    ? 0u : 1u;
                const bool draw = channel_index[side] % step == 0;
                ++channel_index[side];
                if (!draw) continue;

                p.setPen(QPen(side == 0 ? kPort : kStarboard, 1.5));
                p.drawLine(geoToPixel(ray.origin.x(), ray.origin.y()),
                           geoToPixel(ray.outer.x(), ray.outer.y()));
            }
        });
    }

    // Track-only layers (SBP/MAG/MBE) have no swath hull — their selection and
    // hover feedback is the nav polyline itself, re-drawn in the outline pen.
    auto drawTrackLine = [&](const LayerMapData& ld) {
        QPointF prev;
        bool    prev_ok = false;
        for (const QPointF& gp : ld.nav_track) {
            if (std::isnan(gp.x())) { prev_ok = false; continue; }
            const QPointF cur = geoToPixel(gp.x(), gp.y());
            if (prev_ok) p.drawLine(prev, cur);
            prev    = cur;
            prev_ok = true;
        }
    };
    auto drawLayerOutline = [&](const LayerMapData& ld) {
        if (!ld.coverage.empty()) drawMergedSwath(ld, false);
        else if (!ld.nav_track.empty()) drawTrackLine(ld);
    };

    // Selected layers — unified outer hull outline (or the track line itself).
    if (!m_selected_layer_ids.empty()) {
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(kSelectedLayerOutline, 2.0));
        for (const auto& sel_id : m_selected_layer_ids) {
            const auto it = m_layer_data.find(sel_id);
            if (it == m_layer_data.end() || !it->second.visible) continue;
            drawLayerOutline(it->second);
        }
        p.setRenderHint(QPainter::Antialiasing, true);
    }

    // Hover highlight ("Highlight items under cursor", Map tab) — soft accent
    // outline on the layer under the cursor; skipped when already selected.
    if (m_hover_highlight && !m_hover_layer_id.empty()
            && !m_selected_layer_ids.count(m_hover_layer_id)) {
        const auto it = m_layer_data.find(m_hover_layer_id);
        if (it != m_layer_data.end() && it->second.visible) {
            p.setRenderHint(QPainter::Antialiasing, false);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor(90, 170, 255, 170), 1.0, Qt::DashLine));
            drawLayerOutline(it->second);
            p.setRenderHint(QPainter::Antialiasing, true);
        }
    }
}

} // namespace dolphin::ui
