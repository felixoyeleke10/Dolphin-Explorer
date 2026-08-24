#include "MapView3DGeometry.h"

#include <algorithm>
#include <cmath>
#include <numeric>
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
        if (segment.size() >= 3) {
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

std::vector<float> buildFilledPolygonVertices(const std::vector<QPointF>& polygons,
                                              const MapLocalFrame& frame)
{
    std::vector<float> vertices;
    std::vector<LocalPoint> polygon;
    const auto cross = [](const LocalPoint& a, const LocalPoint& b,
                          const LocalPoint& c) {
        return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    };
    const auto appendTriangle = [&](const LocalPoint& a, const LocalPoint& b,
                                    const LocalPoint& c) {
        vertices.insert(vertices.end(), {a.x, a.y, 0.f, b.x, b.y, 0.f,
                                         c.x, c.y, 0.f});
    };
    const auto flush = [&]() {
        if (polygon.size() < 3) { polygon.clear(); return; }
        if (polygon.front().x == polygon.back().x
                && polygon.front().y == polygon.back().y)
            polygon.pop_back();
        if (polygon.size() < 3) { polygon.clear(); return; }

        double area = 0.0;
        for (size_t i = 0; i < polygon.size(); ++i) {
            const auto& a = polygon[i];
            const auto& b = polygon[(i + 1) % polygon.size()];
            area += static_cast<double>(a.x) * b.y
                  - static_cast<double>(b.x) * a.y;
        }
        if (std::abs(area) < 1e-8) { polygon.clear(); return; }
        const bool ccw = area > 0.0;
        std::vector<size_t> indices(polygon.size());
        std::iota(indices.begin(), indices.end(), 0);
        size_t guard = indices.size() * indices.size();
        while (indices.size() > 3 && guard-- > 0) {
            bool clipped = false;
            for (size_t k = 0; k < indices.size(); ++k) {
                const size_t ia = indices[(k + indices.size() - 1) % indices.size()];
                const size_t ib = indices[k];
                const size_t ic = indices[(k + 1) % indices.size()];
                const double turn = cross(polygon[ia], polygon[ib], polygon[ic]);
                if ((ccw && turn <= 1e-7) || (!ccw && turn >= -1e-7)) continue;
                bool contains = false;
                for (const size_t ip : indices) {
                    if (ip == ia || ip == ib || ip == ic) continue;
                    const double c1 = cross(polygon[ia], polygon[ib], polygon[ip]);
                    const double c2 = cross(polygon[ib], polygon[ic], polygon[ip]);
                    const double c3 = cross(polygon[ic], polygon[ia], polygon[ip]);
                    if (ccw ? (c1 >= 0 && c2 >= 0 && c3 >= 0)
                            : (c1 <= 0 && c2 <= 0 && c3 <= 0)) {
                        contains = true; break;
                    }
                }
                if (contains) continue;
                appendTriangle(polygon[ia], polygon[ib], polygon[ic]);
                indices.erase(indices.begin() + static_cast<std::ptrdiff_t>(k));
                clipped = true;
                break;
            }
            if (!clipped) break;
        }
        if (indices.size() == 3)
            appendTriangle(polygon[indices[0]], polygon[indices[1]], polygon[indices[2]]);
        polygon.clear();
    };
    for (const QPointF& point : polygons) {
        if (!std::isfinite(point.x()) || !std::isfinite(point.y())) flush();
        else polygon.push_back(toLocal(point, frame));
    }
    flush();
    return vertices;
}

} // namespace dolphin::ui
