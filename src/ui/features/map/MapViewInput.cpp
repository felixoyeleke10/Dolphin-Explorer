// MapViewInput.cpp — resize and pointer/wheel event handlers for MapView

#include "ui/features/map/MapView.h"
#include "ui/features/map/MapTypes.h"
#include "app/project/Project.h"
#include "geo/GeoUtils.h"

#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QPolygonF>
#include <QRect>
#include <QResizeEvent>
#include <QString>
#include <QWheelEvent>
#include <QtGlobal>   // qQNaN
#include <algorithm>  // std::clamp, std::find
#include <cmath>

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  Input mode
// -----------------------------------------------------------------------------

void MapView::setInputMode(MapInputMode mode)
{
    m_input_mode    = mode;
    m_rubberbanding = false;
    m_drag_moved    = false;
    m_dragging      = false;

    if (!m_measure_pts_geo.empty()) {
        m_measure_pts_geo.clear();
        m_measure_pts_px.clear();
        m_measure_seg_dist.clear();
        m_measure_live_dist = 0.0;
        emit measurementUpdated(-1.0);
    }

    switch (mode) {
    case ModeSelect:
        setCursor(Qt::CrossCursor);
        break;
    case ModeZoom:
        setCursor(Qt::SizeAllCursor);
        break;
    case ModeMeasure:
    case ModePickContact:
        setCursor(Qt::CrossCursor);
        break;
    default:
        setCursor(Qt::OpenHandCursor);
        break;
    }
    update();
}

void MapView::zoomAtPoint(QPointF pos, double factor)
{
    const double newZoom = std::clamp(m_zoom * factor, 1e-6, 1e8);
    const QPointF cen(width() / 2.0, height() / 2.0);
    const QPointF rel = pos - cen - m_origin;
    m_origin -= rel * (newZoom / m_zoom - 1.0);
    m_zoom    = newZoom;
    m_user_interacted = true;
    update();
    emit viewportChanged(viewportMetresPerPixel(), m_rotation_deg);
}

// -----------------------------------------------------------------------------
//  Hit-testing helpers
// -----------------------------------------------------------------------------

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

    auto testLayer = [&](const std::string& id, const LayerMapData& ld) {
        for (const auto& cov : ld.coverage) {
            for (const auto& ribbon : cov.ribbons) {
                if (ribbon.size() < 3) continue;
                QPolygonF poly;
                poly.reserve(static_cast<int>(ribbon.size()));
                for (const auto& pt : ribbon)
                    poly.append(geoToPixel(pt.x(), pt.y()));
                if (poly.boundingRect().intersects(rectF)) {
                    result.push_back(id);
                    return;
                }
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

// -----------------------------------------------------------------------------
//  Mouse events
// -----------------------------------------------------------------------------

void MapView::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    if (m_needs_fit && e->size().width() > 0 && e->size().height() > 0) {
        m_needs_fit = false;
        fitToData();
    }
    update();
}

void MapView::mousePressEvent(QMouseEvent* event)
{
    // Zoom mode: zoom in/out immediately on press; no drag.
    if (m_input_mode == ModeZoom) {
        if (event->button() == Qt::LeftButton)
            zoomAtPoint(event->pos(), 2.0);
        else if (event->button() == Qt::RightButton)
            zoomAtPoint(event->pos(), 0.5);
        return;
    }

    // Measure mode — each left-click adds an anchor point.
    if (m_input_mode == ModeMeasure && event->button() == Qt::LeftButton) {
        const QPointF geo = pixelToGeo(event->pos());
        if (!m_measure_pts_geo.empty()) {
            const QPointF& last = m_measure_pts_geo.back();
            const double dx = geo.x() - last.x();
            const double dy = geo.y() - last.y();
            double dist;
            if (m_is_projected) {
                dist = std::sqrt(dx * dx + dy * dy);
            } else {
                dist = geo::haversineMetres(last.y(), last.x(), geo.y(), geo.x());
            }
            m_measure_seg_dist.push_back(dist);
        }
        m_measure_pts_geo.push_back(geo);
        m_measure_pts_px.push_back(event->pos());
        m_measure_cursor_px  = event->pos();
        m_measure_live_dist  = 0.0;

        double total = 0.0;
        for (double d : m_measure_seg_dist) total += d;
        emit measurementUpdated(total);
        update();
        return;
    }

    // Pick-contact mode: emit position; stay in contact-pick mode (sticky tool).
    if (m_input_mode == ModePickContact && event->button() == Qt::LeftButton) {
        const QPointF geo = pixelToGeo(event->pos());
        emit contactPickedOnMap(geo.x(), geo.y());
        return;
    }

    if (event->button() == Qt::LeftButton) {
        m_dragging      = true;
        m_drag_moved    = false;
        m_drag_start    = event->pos();

        if (m_input_mode == ModeSelect) {
            m_rubberbanding  = true;
            m_rubberband_end = event->pos();
            setCursor(Qt::CrossCursor);
        } else {
            setCursor(Qt::ClosedHandCursor);
        }
    }
}

void MapView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        const bool was_click    = !m_drag_moved;
        const bool was_rubber   = m_rubberbanding && m_drag_moved;

        m_dragging      = false;
        m_rubberbanding = false;
        m_drag_moved    = false;

        setCursor(m_input_mode == ModeSelect ? Qt::CrossCursor : Qt::OpenHandCursor);

        if (m_input_mode == ModeSelect) {
            if (was_rubber) {
                const QRect rect = QRect(m_drag_start, event->pos()).normalized();
                const auto ids   = layersInRect(rect);
                setSelectedLayers(ids);
                emit layersSelected(ids);
                update();
            }
        } else {
            // ModePan — short click = hit test
            if (was_click) {
                const std::string hit  = hitTestLayer(event->pos());
                const bool        ctrl = event->modifiers() & Qt::ControlModifier;

                if (!hit.empty()) {
                    if (ctrl) {
                        // Toggle this layer in the current selection set.
                        std::vector<std::string> ids(m_selected_layer_ids.begin(),
                                                     m_selected_layer_ids.end());
                        auto it = std::find(ids.begin(), ids.end(), hit);
                        if (it != ids.end()) ids.erase(it);
                        else                 ids.push_back(hit);
                        setSelectedLayers(ids);
                        emit layersSelected(ids);
                    } else {
                        setSelectedLayers({hit});
                        emit layerClicked(hit);
                    }
                } else if (!ctrl) {
                    // Click on empty space without Ctrl → clear selection.
                    setSelectedLayers({});
                    emit layersSelected({});
                }
                update();
            }
        }
    }
}

void MapView::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging) {
        const QPoint delta = event->pos() - m_drag_start;
        if (!m_drag_moved && (std::abs(delta.x()) > 4 || std::abs(delta.y()) > 4))
            m_drag_moved = true;

        if (m_drag_moved) {
            if (m_rubberbanding) {
                m_rubberband_end = event->pos();
                update();
            } else {
                m_user_interacted = true;
                m_origin    += event->pos() - m_drag_start;
                m_drag_start = event->pos();
                update();
            }
        }
    }

    if (m_input_mode == ModeMeasure && !m_measure_pts_geo.empty()) {
        m_measure_cursor_px = event->pos();
        const QPointF geo   = pixelToGeo(event->pos());
        const QPointF& last = m_measure_pts_geo.back();
        const double dx = geo.x() - last.x();
        const double dy = geo.y() - last.y();
        if (m_is_projected) {
            m_measure_live_dist = std::sqrt(dx * dx + dy * dy);
        } else {
            m_measure_live_dist = geo::haversineMetres(last.y(), last.x(), geo.y(), geo.x());
        }
        double total = m_measure_live_dist;
        for (double d : m_measure_seg_dist) total += d;
        emit measurementUpdated(total);
        update();
    }

    QPointF geo = pixelToGeo(event->pos());
    emit cursorMoved(geo.x(), geo.y());
}

void MapView::wheelEvent(QWheelEvent* event)
{
    m_user_interacted = true;
    const double factor  = (event->angleDelta().y() > 0) ? 1.15 : (1.0 / 1.15);
    const double newZoom = std::clamp(m_zoom * factor, 1e-6, 1e8);

    QPointF cen(width() / 2.0, height() / 2.0);
    QPointF rel = event->position() - cen - m_origin;
    m_origin -= rel * (newZoom / m_zoom - 1.0);
    m_zoom    = newZoom;

    update();
}

void MapView::leaveEvent(QEvent*)
{
    emit cursorMoved(qQNaN(), qQNaN());
}

void MapView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (m_input_mode == ModeMeasure && event->button() == Qt::LeftButton) {
        m_measure_pts_geo.clear();
        m_measure_pts_px.clear();
        m_measure_seg_dist.clear();
        m_measure_live_dist = 0.0;
        emit measurementUpdated(-1.0);
        update();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void MapView::contextMenuEvent(QContextMenuEvent* event)
{
    if (m_input_mode == ModeZoom) { event->accept(); return; }
    if (m_input_mode == ModeMeasure) {
        if (!m_measure_pts_geo.empty()) {
            m_measure_pts_geo.clear();
            m_measure_pts_px.clear();
            m_measure_seg_dist.clear();
            m_measure_live_dist = 0.0;
            emit measurementUpdated(-1.0);
            update();
        }
        event->accept();
        return;
    }
    emit contextMenuRequested(event->globalPos());
}

} // namespace dolphin::ui
