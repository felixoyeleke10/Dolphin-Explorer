// MapViewInput.cpp — resize and pointer/wheel event handlers for MapView

#include "ui/features/map/MapView.h"
#include "geo/GeoUtils.h"

#include <QContextMenuEvent>
#include <QCursor>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPixmap>
#include <QResizeEvent>
#include <QString>
#include <QToolTip>
#include <QWheelEvent>
#include <QtGlobal>   // qQNaN
#include <algorithm>  // std::clamp, std::find
#include <cmath>

namespace dolphin::ui {

namespace {

// Magnifier cursor for the Zoom tool, built once from the toolbar glyph.
QCursor zoomCursor()
{
    static const QCursor cur = [] {
        QPixmap pm(QStringLiteral(":/icons/zoom.svg"));
        if (!pm.isNull()) {
            pm = pm.scaled(20, 20, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            // Hotspot on the lens centre (the glyph's handle points down-right).
            return QCursor(pm, 8, 8);
        }
        return QCursor(Qt::CrossCursor);
    }();
    return cur;
}

// The idle cursor each input mode shows. Single source of truth — used by
// setInputMode and by the release handlers, so finishing a click/drag never
// leaves a tool with the wrong cursor.
QCursor cursorForMode(MapView::MapInputMode mode)
{
    switch (mode) {
    case MapView::ModeSelect:
    case MapView::ModeMeasure:
    case MapView::ModePickContact:
    case MapView::ModeDrawFeature: return QCursor(Qt::CrossCursor);
    case MapView::ModeZoom:        return zoomCursor();
    default:                       return QCursor(Qt::ArrowCursor);  // ModePan: arrow at rest, hand appears on drag
    }
}

} // namespace

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

    // Drop any in-progress feature draft when the tool changes.
    m_feature_pts_geo.clear();
    m_feature_drawing = false;
    m_feature_pen_down = false;

    setCursor(cursorForMode(mode));
    // Feature drawing needs keyboard focus for Enter/Esc/Backspace;
    // measuring needs it for Esc-to-clear.
    if (mode == ModeDrawFeature || mode == ModeMeasure)
        setFocus(Qt::OtherFocusReason);
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
    m_frame_survey_pending = false;
    update();
    emit viewportChanged(viewportMetresPerPixel(), m_rotation_deg);
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
    // Middle-button drag pans in EVERY mode — measuring or drawing across more
    // than one screen must not require switching back to the Cursor tool.
    if (event->button() == Qt::MiddleButton) {
        m_dragging      = true;
        m_rubberbanding = false;
        m_drag_moved    = false;
        m_drag_start    = event->pos();
        setCursor(Qt::ClosedHandCursor);
        return;
    }

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

    // Feature-draw mode.
    if (m_input_mode == ModeDrawFeature && event->button() == Qt::LeftButton) {
        m_feature_cursor_px = event->pos();
        if (m_feature_kind == 3) {
            // Pen: begin a freehand stroke; points are appended on drag.
            m_feature_pts_geo.clear();
            m_feature_pts_geo.push_back(pixelToGeo(event->pos()));
            m_feature_drawing  = true;
            m_feature_pen_down = true;
        } else {
            // Polygon / line: each click adds a vertex.
            m_feature_pts_geo.push_back(pixelToGeo(event->pos()));
            m_feature_drawing = true;
        }
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
    // End a middle-button pan; restore the cursor the active mode expects.
    if (event->button() == Qt::MiddleButton) {
        m_dragging   = false;
        m_drag_moved = false;
        setCursor(cursorForMode(m_input_mode));
        return;
    }

    // Pen freehand: releasing the button finishes the stroke.
    if (m_input_mode == ModeDrawFeature && m_feature_pen_down
            && event->button() == Qt::LeftButton) {
        commitFeatureDraft();
        return;
    }

    if (event->button() == Qt::LeftButton) {
        const bool was_click    = !m_drag_moved;
        const bool was_rubber   = m_rubberbanding && m_drag_moved;

        m_dragging      = false;
        m_rubberbanding = false;
        m_drag_moved    = false;

        // Restore the mode's idle cursor (the press may have shown ClosedHand).
        // Must be mode-aware: Measure/Pick/Draw keep their crosshair — resetting
        // to OpenHand here used to lose the crosshair after every tool click.
        setCursor(cursorForMode(m_input_mode));

        // Short click = hit-test select. Shared by Pan and Select: the Select
        // tool must select on click too, not only via the rubber band.
        auto clickSelect = [&]() {
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
        };

        if (m_input_mode == ModeSelect) {
            if (was_rubber) {
                const QRect rect = QRect(m_drag_start, event->pos()).normalized();
                const auto ids   = layersInRect(rect);
                setSelectedLayers(ids);
                emit layersSelected(ids);
                update();
            } else if (was_click) {
                clickSelect();
            }
        } else {
            // ModePan — short click = hit test
            if (was_click) clickSelect();
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
                m_frame_survey_pending = false;
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

    if (m_input_mode == ModeDrawFeature && m_feature_drawing) {
        m_feature_cursor_px = event->pos();
        // Pen: while dragging, append freehand points (throttled by distance).
        if (m_feature_pen_down && (event->buttons() & Qt::LeftButton)
                && !m_feature_pts_geo.empty()) {
            const QPointF last_px = geoToPixel(m_feature_pts_geo.back().x(),
                                               m_feature_pts_geo.back().y());
            if ((event->pos() - last_px.toPoint()).manhattanLength() >= 4)
                m_feature_pts_geo.push_back(pixelToGeo(event->pos()));
        }
        update();
    }

    // Map-tab view options: hover tooltip / highlight (throttled inside).
    updateHoverState(event->pos(), event->globalPosition().toPoint());

    QPointF geo = pixelToGeo(event->pos());
    emit cursorMoved(geo.x(), geo.y());
}

void MapView::wheelEvent(QWheelEvent* event)
{
    m_user_interacted = true;
    m_frame_survey_pending = false;
    const double factor  = (event->angleDelta().y() > 0) ? 1.15 : (1.0 / 1.15);
    const double newZoom = std::clamp(m_zoom * factor, 1e-6, 1e8);

    QPointF cen(width() / 2.0, height() / 2.0);
    QPointF rel = event->position() - cen - m_origin;
    m_origin -= rel * (newZoom / m_zoom - 1.0);
    m_zoom    = newZoom;

    update();
    emit viewportChanged(viewportMetresPerPixel(), m_rotation_deg);
}

void MapView::leaveEvent(QEvent*)
{
    // Clear hover-driven visuals so nothing sticks to the last cursor position.
    m_hover_test_px = QPoint(-9999, -9999);
    if (!m_hover_layer_id.empty()) {
        m_hover_layer_id.clear();
        if (m_hover_highlight) update();
    }
    QToolTip::hideText();
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
    // Polygon/line — double-click finishes and commits the shape. (Pen commits on
    // mouse release, so it ignores double-click.)
    if (m_input_mode == ModeDrawFeature && m_feature_kind != 3
            && event->button() == Qt::LeftButton) {
        commitFeatureDraft();
        return;
    }

    // Pan mode — double-click a layer's coverage to open its viewer.
    if (m_input_mode == ModePan && event->button() == Qt::LeftButton) {
        const std::string hit = hitTestLayer(event->pos());
        if (!hit.empty()) {
            emit layerActivated(hit);
            event->accept();
            return;
        }
    }
    QWidget::mouseDoubleClickEvent(event);
}

void MapView::keyPressEvent(QKeyEvent* event)
{
    // Measure: Esc clears the measurement (parity with right-click/double-click).
    if (m_input_mode == ModeMeasure && event->key() == Qt::Key_Escape
            && !m_measure_pts_geo.empty()) {
        m_measure_pts_geo.clear();
        m_measure_pts_px.clear();
        m_measure_seg_dist.clear();
        m_measure_live_dist = 0.0;
        emit measurementUpdated(-1.0);
        update();
        return;
    }

    if (m_input_mode == ModeDrawFeature && m_feature_drawing) {
        switch (event->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            commitFeatureDraft();
            return;
        case Qt::Key_Escape:
            cancelFeatureDraft();
            return;
        case Qt::Key_Backspace:
            if (!m_feature_pts_geo.empty()) {
                m_feature_pts_geo.pop_back();
                if (m_feature_pts_geo.empty()) m_feature_drawing = false;
                update();
            }
            return;
        default:
            break;
        }
    }
    QWidget::keyPressEvent(event);
}

void MapView::contextMenuEvent(QContextMenuEvent* event)
{
    if (m_input_mode == ModeZoom) { event->accept(); return; }
    // While drawing a feature, right-click cancels the in-progress draft instead
    // of opening the layer context menu.
    if (m_input_mode == ModeDrawFeature && m_feature_drawing) {
        cancelFeatureDraft();
        event->accept();
        return;
    }
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
