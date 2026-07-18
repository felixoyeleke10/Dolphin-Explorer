// MapView3D.Input.cpp — mouse and wheel event handlers.

#include "ui/features/map/MapView3D.h"
#include "ui/features/map/MapLongitude.h"

#include <QContextMenuEvent>
#include <QEvent>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QVector3D>
#include <QVector4D>
#include <QWheelEvent>

#include <cmath>

namespace dolphin::ui {

// -- Viewport signal helper ----------------------------------------------------

static void emitCameraViewport(MapView3D* self,
                                float yaw_deg, float distance_m, int viewport_h)
{
    const double mpp = (viewport_h > 0)
        ? double(distance_m) * 2.0 / double(viewport_h)
        : 0.0;
    emit self->viewportChanged(mpp, double(yaw_deg));
}

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

// -- Picking and ground-ray helpers ------------------------------------------

std::string MapView3D::hitTestLayer(QPoint px) const
{
    if (!m_gl_ready) return {};

    const QMatrix4x4 mvp = m_camera.projMatrix() * m_camera.viewMatrix();
    constexpr float kPickRadius = 24.f;
    float best_d2 = kPickRadius * kPickRadius;
    std::string best_id;

    for (const auto& layer : m_layers) {
        if (!layer.layer_visible || !layer.nav_visible || layer.raw_track.empty())
            continue;
        const int step = std::max(1, int(layer.raw_track.size()) / 500);
        for (int i = 0; i < int(layer.raw_track.size()); i += step) {
            const QPointF& pt = layer.raw_track[static_cast<size_t>(i)];
            if (std::isnan(pt.x()) || std::isnan(pt.y())) continue;
            const QVector3D local = toLocal(pt.x(), pt.y(), 0.0);
            const QVector4D clip = mvp * QVector4D(local.x(), local.y(), 0.f, 1.f);
            if (clip.w() <= 0.f) continue;
            const float sx = (clip.x() / clip.w() * 0.5f + 0.5f) * width();
            const float sy = (1.f - (clip.y() / clip.w() * 0.5f + 0.5f)) * height();
            const float dx = sx - float(px.x());
            const float dy = sy - float(px.y());
            const float d2 = dx * dx + dy * dy;
            if (d2 < best_d2) {
                best_d2 = d2;
                best_id = layer.id;
            }
        }
    }

    // Drapes cover a wide area, so use their ground-plane bounding box when
    // the cursor is not close enough to a navigation centerline.
    if (best_id.empty() && m_has_origin && !m_drape_layers.empty()) {
        bool ok = false;
        const QMatrix4x4 inv = mvp.inverted(&ok);
        const float viewport_w = float(width());
        const float viewport_h = float(height());
        if (ok && viewport_w > 0.f && viewport_h > 0.f) {
            const float nx = (2.f * px.x() / viewport_w) - 1.f;
            const float ny = -(2.f * px.y() / viewport_h) + 1.f;
            const QVector4D near_clip = inv * QVector4D(nx, ny, -1.f, 1.f);
            const QVector4D far_clip = inv * QVector4D(nx, ny, 1.f, 1.f);
            if (std::abs(near_clip.w()) >= 1e-7f
                    && std::abs(far_clip.w()) >= 1e-7f) {
                const QVector3D near_world = near_clip.toVector3D() / near_clip.w();
                const QVector3D far_world = far_clip.toVector3D() / far_clip.w();
                const QVector3D direction = (far_world - near_world).normalized();
                if (std::abs(direction.z()) >= 1e-6f) {
                    const float distance = -near_world.z() / direction.z();
                    if (distance >= 0.f) {
                        const QVector3D hit = near_world + distance * direction;
                        for (const auto& drape : m_drape_layers) {
                            if (!drape.visible) continue;
                            if (hit.x() >= drape.bbox_x0
                                    && hit.x() <= drape.bbox_x0 + drape.bbox_w
                                    && hit.y() >= drape.bbox_y0
                                    && hit.y() <= drape.bbox_y0 + drape.bbox_h) {
                                // The last match is the topmost drawn drape.
                                best_id = drape.id;
                            }
                        }
                    }
                }
            }
        }
    }

    return best_id;
}

void MapView3D::pickAt(QPoint px)
{
    const std::string best_id = hitTestLayer(px);
    if (!best_id.empty()) {
        setSelectedLayers({best_id});
        emit layerClicked(best_id);
    } else {
        setSelectedLayers({});
        emit layersSelected({});
    }
}

bool MapView3D::groundHit(QPoint px, QPointF& geo) const
{
    if (!m_has_origin || !m_gl_ready) return false;

    const float viewport_w = static_cast<float>(width());
    const float viewport_h = static_cast<float>(height());
    if (viewport_w <= 0.f || viewport_h <= 0.f) return false;

    const float nx = (2.f * px.x() / viewport_w) - 1.f;
    const float ny = -(2.f * px.y() / viewport_h) + 1.f;

    bool ok = false;
    const QMatrix4x4 inverse_view_projection =
        (m_camera.projMatrix() * m_camera.viewMatrix()).inverted(&ok);
    if (!ok) return false;

    const QVector4D near_clip =
        inverse_view_projection * QVector4D(nx, ny, -1.f, 1.f);
    const QVector4D far_clip =
        inverse_view_projection * QVector4D(nx, ny, 1.f, 1.f);
    if (std::abs(near_clip.w()) < 1e-7f || std::abs(far_clip.w()) < 1e-7f)
        return false;

    const QVector3D near_world = near_clip.toVector3D() / near_clip.w();
    const QVector3D far_world = far_clip.toVector3D() / far_clip.w();
    const QVector3D direction = (far_world - near_world).normalized();
    if (std::abs(direction.z()) < 1e-6f) return false;

    const float distance = -near_world.z() / direction.z();
    if (distance < 0.f) return false;
    const QVector3D hit = near_world + distance * direction;

    const double local_x = static_cast<double>(hit.x());
    const double local_y = static_cast<double>(hit.y());
    if (m_is_projected) {
        geo = QPointF(m_origin_x + local_x, m_origin_y + local_y);
    } else {
        static constexpr double kMetPerDeg = 111320.0;
        const double cos_lat = std::cos(m_origin_y * M_PI / 180.0);
        geo = QPointF(m_origin_x + local_x / (cos_lat * kMetPerDeg),
                      m_origin_y + local_y / kMetPerDeg);
    }
    return true;
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
        emitCameraViewport(this, m_camera.yaw, m_camera.distance, height());
        update();
    }

    QPointF geo;
    if (groundHit(ev->pos(), geo)) {
        const double public_lon = m_is_projected
            ? geo.x() : maplongitude::canonical(geo.x());
        emit cursorMoved(public_lon, geo.y());
    }
    else
        emit cursorMoved(qQNaN(), qQNaN());
}

void MapView3D::mouseReleaseEvent(QMouseEvent* ev)
{
    if (ev->button() == Qt::LeftButton) {
        if (!m_pan_moved) {
            if (m_tool_mode == 4) {  // ContactPick
                QPointF geo;
                if (groundHit(ev->pos(), geo)) {
                    const double public_lon = m_is_projected
                        ? geo.x() : maplongitude::canonical(geo.x());
                    emit contactPickedAt(public_lon, geo.y());
                }
            } else {
                pickAt(ev->pos());  // left click without drag = select layer
            }
        }
        m_panning = false;
        setCursor(cursorForMode(m_tool_mode));
    }
    if (ev->button() == Qt::RightButton) {
        const bool was_orbit = m_orbit_moved;
        m_orbiting    = false;
        m_orbit_moved = false;
        setCursor(cursorForMode(m_tool_mode));
        // QWindow has no contextMenuEvent; emit the context menu here on a right-click
        // that wasn't an orbit-drag (matches the old contextMenuEvent suppression).
        if (!was_orbit) {
            const std::string hit = hitTestLayer(ev->pos());
            if (!hit.empty()) {
                setSelectedLayers({hit});
                emit layerClicked(hit);
            }
            emit contextMenuRequested(ev->globalPosition().toPoint());
        }
    }
}

void MapView3D::wheelEvent(QWheelEvent* ev)
{
    const float factor = (ev->angleDelta().y() > 0) ? 0.85f : 1.18f;
    m_camera.distance  = std::clamp(m_camera.distance * factor,
                                    1.f, m_scene_radius * 20.f);
    m_camera.near_z    = m_camera.distance * 0.0002f;
    m_camera.far_z     = m_camera.distance * 200.f;
    emitCameraViewport(this, m_camera.yaw, m_camera.distance, height());
    update();
}

// -- Programmatic camera control -----------------------------------------------

void MapView3D::setYaw(double deg)
{
    m_camera.yaw = static_cast<float>(std::fmod(deg + 360.0, 360.0));
    emitCameraViewport(this, m_camera.yaw, m_camera.distance, height());
    update();
}

void MapView3D::setDistance(float metres)
{
    m_camera.distance = std::clamp(metres, 1.f, m_scene_radius * 20.f);
    m_camera.near_z   = m_camera.distance * 0.0002f;
    m_camera.far_z    = m_camera.distance * 200.f;
    emitCameraViewport(this, m_camera.yaw, m_camera.distance, height());
    update();
}

} // namespace dolphin::ui
