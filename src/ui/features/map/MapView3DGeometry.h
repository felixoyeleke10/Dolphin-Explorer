#pragma once

#include <QPointF>

#include <vector>

namespace dolphin::ui {

struct MapLocalFrame {
    double origin_x = 0.0;
    double origin_y = 0.0;
    bool is_projected = false;
};

struct CameraClipRange {
    float near_plane = 0.1f;
    float far_plane = 1000.f;
};

float normalizeCameraYaw(float degrees) noexcept;
double cameraMetresPerPixel(float distance, float vertical_fov_degrees,
                            int viewport_height) noexcept;
float cameraWheelScale(int angle_delta_y) noexcept;
CameraClipRange cameraClipRange(float distance, float scene_radius) noexcept;

// Appends xyz GL_LINES vertices for each continuous part of a navigation track.
int appendNavTrackLineVertices(const std::vector<QPointF>& track,
                               const MapLocalFrame& frame,
                               std::vector<float>& vertices);

// Builds xyz GL_LINES vertices for closed polygons separated by NaN sentinels.
std::vector<float> buildClosedOutlineVertices(const std::vector<QPointF>& polygons,
                                              const MapLocalFrame& frame);

} // namespace dolphin::ui
