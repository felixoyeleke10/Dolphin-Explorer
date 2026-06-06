// MapView3D.Input.cpp — mouse and wheel event handlers.

#include "ui/features/map/MapView3D.h"

#include <QContextMenuEvent>
#include <QEvent>
#include <QMouseEvent>
#include <QWheelEvent>

#include <cmath>

namespace dolphin::ui {

// -- Tool mode -----------------------------------------------------------------

static Qt::CursorShape cursorForMode(int mode)
{
    switch (mode) {
    case 1:  return Qt::ArrowCursor;    // Select
    case 3:  return Qt::CrossCursor;    // Measure
    case 4:  return Qt::CrossCursor;    // ContactPick
    default: return Qt::OpenHandCursor; // Pan / Zoom
    }
}

void MapView3D::setToolMode(int mode)
{
    m_tool_mode = mode;
    if (!m_panning && !m_orbiting)
        setCursor(cursorForMode(mode));
}

// -- Mouse handlers ------------------------------------------------------------

void MapView3D::mousePressEvent(QMouseEvent* ev)
{
    if (ev->button() == Qt::LeftButton) {
        // Left drag = pan, matching 2D map view muscle memory.
        m_panning     = true;
        m_pan_moved   = false;
        m_pan_start   = ev->pos();
        m_pan_target0 = m_camera.target;
        setCursor(Qt::SizeAllCursor);
    } else if (ev->button() == Qt::RightButton) {
        // Right drag = orbit.  Snap the pivot to the ground point under the cursor
        // so the scene feels "attached" to what the user clicked rather than spinning
        // around the global origin.
        QPointF geo;
        if (groundHit(ev->pos(), geo)) {
            const QVector3D new_target = toLocal(geo.x(), geo.y(), 0.0);
            const float new_dist = (m_camera.position() - new_target).length();
            if (new_dist > 1.f && new_dist < m_scene_radius * 10.f) {
                m_camera.target   = new_target;
                m_camera.distance = new_dist;
                m_camera.near_z   = m_camera.distance * 0.0002f;
                m_camera.far_z    = m_camera.distance * 200.f;
            }
        }
        m_orbiting    = true;
        m_orbit_moved = false;
        m_had_orbit   = false;
        m_drag_start  = ev->pos();
        m_drag_yaw0   = m_camera.yaw;
        m_drag_pitch0 = m_camera.pitch;
        setCursor(Qt::ClosedHandCursor);
    }
}

void MapView3D::mouseMoveEvent(QMouseEvent* ev)
{
    if (m_panning) {
        const QPoint d = ev->pos() - m_pan_start;
        if (!m_pan_moved && (std::abs(d.x()) > 3 || std::abs(d.y()) > 3)) {
            m_pan_moved = true;
            m_camera_user_moved = true;
        }
        const float scale = m_camera.distance / float(height()) * 2.f;
        const float yr = m_camera.yaw * float(M_PI) / 180.f;
        const QVector3D right( std::cos(yr), std::sin(yr), 0.f);
        const QVector3D fwd  (-std::sin(yr), std::cos(yr), 0.f);
        m_camera.target = m_pan_target0
                        - right * (d.x() * scale)
                        + fwd   * (d.y() * scale);
        update();
    } else if (m_orbiting) {
        const QPoint d = ev->pos() - m_drag_start;
        if (!m_orbit_moved && (std::abs(d.x()) > 3 || std::abs(d.y()) > 3)) {
            m_orbit_moved = true;
            m_had_orbit   = true;
            m_camera_user_moved = true;
        }
        m_camera.yaw   = std::fmod(m_drag_yaw0   - d.x() * 0.4f + 360.f, 360.f);
        m_camera.pitch = std::clamp(m_drag_pitch0 + d.y() * 0.25f, 5.f, 89.f);
        update();
    }

    QPointF geo;
    if (groundHit(ev->pos(), geo))
        emit cursorMoved(geo.x(), geo.y());
    else
        emit cursorMoved(qQNaN(), qQNaN());
}

void MapView3D::leaveEvent(QEvent*)
{
    emit cursorMoved(qQNaN(), qQNaN());
}

void MapView3D::mouseReleaseEvent(QMouseEvent* ev)
{
    if (ev->button() == Qt::LeftButton) {
        if (!m_pan_moved) {
            if (m_tool_mode == 4) {  // ContactPick
                QPointF geo;
                if (groundHit(ev->pos(), geo))
                    emit contactPickedAt(geo.x(), geo.y());
            } else {
                pickAt(ev->pos());  // left click without drag = select layer
            }
        }
        m_panning = false;
        setCursor(cursorForMode(m_tool_mode));
    }
    if (ev->button() == Qt::RightButton) {
        m_orbiting    = false;
        m_orbit_moved = false;
        setCursor(cursorForMode(m_tool_mode));
    }
}

void MapView3D::wheelEvent(QWheelEvent* ev)
{
    const float factor = (ev->angleDelta().y() > 0) ? 0.85f : 1.18f;
    m_camera.distance  = std::clamp(m_camera.distance * factor,
                                    1.f, m_scene_radius * 20.f);
    m_camera.near_z    = m_camera.distance * 0.0002f;
    m_camera.far_z     = m_camera.distance * 200.f;
    update();
}

void MapView3D::contextMenuEvent(QContextMenuEvent* ev)
{
    // m_orbit_moved is reset in mouseReleaseEvent before WM_CONTEXTMENU arrives;
    // m_had_orbit persists until the next right-press so the suppress still works.
    if (m_had_orbit) { m_had_orbit = false; ev->accept(); return; }

    const std::string hit = hitTestLayer(ev->pos());
    if (!hit.empty()) {
        setSelectedLayers({hit});
        emit layerClicked(hit);
    }

    emit contextMenuRequested(ev->globalPos());
    ev->accept();
}

} // namespace dolphin::ui
