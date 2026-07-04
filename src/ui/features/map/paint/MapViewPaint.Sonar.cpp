// MapViewPaint.Sonar.cpp — empty state, sonar images, coverage ribbons.
#include "ui/features/map/MapView.h"
#include "app/project/Project.h"
#include "ui/shell/Theme.h"

#include <QPainter>
#include <QPainterPath>
#include <algorithm>

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
    const QRect r = rect();

    const int cx = r.width() / 2;
    const int cy = r.height() / 2;

    // Watermark only — the actionable empty state (Recent Projects +
    // Import Files) is the launcher overlay in MapViewportHost, drawn on top.
    QFont kfont("Segoe UI", 7);
    kfont.setLetterSpacing(QFont::AbsoluteSpacing, 2.4);
    p.setFont(kfont);
    p.setPen(kEmptyStateAccent);
    p.drawText(QRect(cx - 200, cy - 150, 400, 18),
               Qt::AlignHCenter | Qt::AlignVCenter,
               tr("DOLPHIN EXPLORER"));
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

    // Merged port+starboard swath polygon renderer.
    // show_inner=true: OddEvenFill donut (fill with nadir dead-zone hole).
    // show_inner=false: outer hull only (used for selection outline).
    auto drawMergedSwath = [&](const LayerMapData& ld, bool show_inner) {
        const SwathCoverage* port_cov = nullptr;
        const SwathCoverage* stbd_cov = nullptr;
        for (const auto& cov : ld.coverage) {
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
            for (const auto& cov : ld.coverage) {
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
        p.setRenderHint(QPainter::Antialiasing, false);
        forEachVisibleLayer([&](const LayerMapData& ld) {
            if (ld.preview_image.isNull()) return;
            const QPointF tl = geoToPixel(ld.lon_min, ld.lat_max);
            const QPointF br = geoToPixel(ld.lon_max, ld.lat_min);
            p.setOpacity(0.88);
            p.drawImage(QRectF(tl, br), ld.preview_image);
            p.setOpacity(1.0);
        });
        p.setRenderHint(QPainter::Antialiasing, true);
    }

    // SSS coverage ribbons (only for layers without a preview image).
    {
        const QColor kFill(30, 140, 255, 80);
        p.setRenderHint(QPainter::Antialiasing, false);
        forEachVisibleLayer([&](const LayerMapData& ld) {
            if (!ld.preview_image.isNull()) return;
            p.setBrush(kFill);
            p.setPen(Qt::NoPen);
            drawMergedSwath(ld, true);
        });
        p.setRenderHint(QPainter::Antialiasing, true);
    }

    // Selected layers — unified outer hull outline.
    if (!m_selected_layer_ids.empty()) {
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(kSelectedLayerOutline, 2.0));
        for (const auto& sel_id : m_selected_layer_ids) {
            const auto it = m_layer_data.find(sel_id);
            if (it == m_layer_data.end() || !it->second.visible) continue;
            drawMergedSwath(it->second, false);
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
            drawMergedSwath(it->second, false);
            p.setRenderHint(QPainter::Antialiasing, true);
        }
    }
}

} // namespace dolphin::ui
