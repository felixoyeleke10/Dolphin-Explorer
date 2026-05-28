#pragma once
#include <QMatrix4x4>
#include <QVector3D>
#include <cmath>

namespace dolphin::ui {

// ─────────────────────────────────────────────────────────────────────────────
//  Camera3D — orbit camera for the 3D map view.
//
//  Coordinate system (Z-up, right-hand):
//    X = east   (+X points right on screen at yaw=0)
//    Y = north  (+Y points up  on screen at yaw=0)
//    Z = up     (+Z is altitude / away from seabed)
//
//  Camera orbits a `target` point at `distance` metres.
//    yaw=0   → camera is south of target (looking north)
//    yaw=90  → camera is west  of target (looking east)
//    pitch=5 → low oblique (nearly horizontal)
//    pitch=89→ nearly top-down
// ─────────────────────────────────────────────────────────────────────────────

struct Camera3D {
    QVector3D target   = {0.f, 0.f, 0.f};
    float     distance = 2000.f;    // metres from target to camera
    float     yaw      = 0.f;       // degrees, 0=looking north, CW positive
    float     pitch    = 45.f;      // degrees above horizontal (5–89)
    float     fov      = 45.f;      // vertical field-of-view, degrees
    float     aspect   = 1.f;       // width / height
    float     near_z   = 0.5f;      // near clip plane (metres)
    float     far_z    = 500000.f;  // far  clip plane (metres)

    // Camera position in world space.
    QVector3D position() const noexcept {
        const float yr = yaw   * float(M_PI) / 180.f;
        const float pr = pitch * float(M_PI) / 180.f;
        return target + distance * QVector3D(
            std::sin(yr) * std::cos(pr),
           -std::cos(yr) * std::cos(pr),
            std::sin(pr));
    }

    // View matrix (world → camera space).
    QMatrix4x4 viewMatrix() const noexcept {
        QMatrix4x4 m;
        m.lookAt(position(), target, QVector3D(0.f, 0.f, 1.f));
        return m;
    }

    // Perspective projection matrix.
    QMatrix4x4 projMatrix() const noexcept {
        QMatrix4x4 m;
        m.perspective(fov, aspect, near_z, far_z);
        return m;
    }

    // Convenience: combined MVP with given model matrix (usually identity).
    QMatrix4x4 mvp(const QMatrix4x4& model = {}) const noexcept {
        return projMatrix() * viewMatrix() * model;
    }
};

} // namespace dolphin::ui
