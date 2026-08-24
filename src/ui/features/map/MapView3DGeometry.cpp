#include "MapView3DGeometry.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dolphin::ui {

float normalizeCameraYaw(float degrees) noexcept
{
    if (!std::isfinite(degrees)) return 0.f;
    float normalized = std::fmod(degrees, 360.f);
    if (normalized < 0.f) normalized += 360.f;
    return normalized;
}

double cameraMetresPerPixel(float distance, float vertical_fov_degrees,
                            int viewport_height) noexcept
{
    if (!(distance > 0.f) || !(vertical_fov_degrees > 0.f)
            || vertical_fov_degrees >= 180.f || viewport_height <= 0)
        return 0.0;
    const double half_fov = static_cast<double>(vertical_fov_degrees)
        * std::numbers::pi / 360.0;
    return 2.0 * static_cast<double>(distance) * std::tan(half_fov)
        / static_cast<double>(viewport_height);
}

float cameraWheelScale(int angle_delta_y) noexcept
{
    return angle_delta_y == 0 ? 1.f
        : std::pow(0.85f, static_cast<float>(angle_delta_y) / 120.f);
}

CameraClipRange cameraClipRange(float distance, float scene_radius) noexcept
{
    distance = std::max(distance, 1.f);
    scene_radius = std::max(scene_radius, 1.f);
    const float near_plane = std::max(0.1f, distance * 0.001f);
    const float far_plane = std::max({100.f, distance * 8.f, scene_radius * 8.f});
    return {near_plane, std::max(far_plane, near_plane * 100.f)};
}

namespace {

struct LocalPoint { float x; float y; };

LocalPoint toLocal(const QPointF& point, const MapLocalFrame& frame)
{
    if (frame.is_projected) {
        return {static_cast<float>(point.x() - frame.origin_x),
                static_cast<float>(point.y() - frame.origin_y)};
    }
    constexpr double metres_per_degree = 111319.49079327357;
    const double cos_lat = std::cos(frame.origin_y * std::numbers::pi / 180.0);
    return {static_cast<float>((point.x() - frame.origin_x) * cos_lat * metres_per_degree),
            static_cast<float>((point.y() - frame.origin_y) * metres_per_degree)};
}

void appendSegment(std::vector<float>& vertices, LocalPoint a, LocalPoint b)
{
    vertices.insert(vertices.end(), {a.x, a.y, 0.f, b.x, b.y, 0.f});
}

} // namespace

int appendNavTrackLineVertices(const std::vector<QPointF>& track,
                               const MapLocalFrame& frame,
                               std::vector<float>& vertices)
{
    const size_t initial_size = vertices.size();
    LocalPoint previous{};
    bool have_previous = false;
    for (const auto& point : track) {
        if (!std::isfinite(point.x()) || !std::isfinite(point.y())) {
            have_previous = false;
            continue;
        }
        const LocalPoint current = toLocal(point, frame);
        if (have_previous) appendSegment(vertices, previous, current);
        previous = current;
        have_previous = true;
    }
    return static_cast<int>((vertices.size() - initial_size) / 3);
}

std::vector<float> buildClosedOutlineVertices(const std::vector<QPointF>& polygons,
                                              const MapLocalFrame& frame)
{
    std::vector<float> vertices;
    std::vector<LocalPoint> segment;
    const auto flush = [&]() {
        if (segment.size() == 2) {
            appendSegment(vertices, segment[0], segment[1]);
        } else if (segment.size() >= 3) {
            for (size_t i = 0; i < segment.size(); ++i)
                appendSegment(vertices, segment[i], segment[(i + 1) % segment.size()]);
        }
        segment.clear();
    };
    for (const auto& point : polygons) {
        if (!std::isfinite(point.x()) || !std::isfinite(point.y())) {
            flush();
            continue;
        }
        segment.push_back(toLocal(point, frame));
    }
    flush();
    return vertices;
}

} // namespace dolphin::ui
