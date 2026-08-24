#include "ui/features/map/MapView3DGeometry.h"

#include <cmath>
#include <iostream>
#include <limits>

using namespace dolphin;

int main()
{
    int failed = 0;
    auto check = [&](bool condition, const char* message) {
        if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failed; }
    };
    const ui::MapLocalFrame projected{100.0, 200.0, true};
    const double nan = std::numeric_limits<double>::quiet_NaN();

    std::vector<float> nav{99.f};
    const int count = ui::appendNavTrackLineVertices(
        {{100, 200}, {110, 205}, {nan, nan}, {120, 210}, {125, 215}},
        projected, nav);
    check(count == 4, "two continuous pairs create four line vertices");
    check(nav.size() == 13, "existing output is preserved while vertices append");
    check(nav[1] == 0.f && nav[2] == 0.f && nav[4] == 10.f && nav[5] == 5.f,
          "projected points are made origin-relative");

    const auto outline = ui::buildClosedOutlineVertices(
        {{100, 200}, {110, 200}, {110, 210}, {nan, nan},
         {120, 220}, {130, 220}}, projected);
    check(outline.size() == 18, "triangle creates three closed GL line segments");
    check(outline[12] == 10.f && outline[13] == 10.f
          && outline[15] == 0.f && outline[16] == 0.f,
          "last outline edge closes back to first vertex");

    const auto concave_fill = ui::buildFilledPolygonVertices(
        {{100, 200}, {110, 200}, {110, 210}, {105, 205}, {100, 210}, {nan, nan}},
        projected);
    check(concave_fill.size() == 27,
          "concave five-point footprint tessellates into three triangles");

    const ui::MapLocalFrame geographic{0.0, 0.0, false};
    std::vector<float> geographic_nav;
    ui::appendNavTrackLineVertices({{0, 0}, {1, 0}}, geographic, geographic_nav);
    check(geographic_nav.size() == 6 && std::abs(geographic_nav[3] - 111319.49f) < 0.1f,
          "geographic longitude converts to local metres");

    check(std::abs(ui::normalizeCameraYaw(-450.f) - 270.f) < 1e-6f,
          "camera yaw normalizes arbitrary negative rotations");
    check(ui::cameraWheelScale(0) == 1.f,
          "zero-delta wheel does not change zoom");
    check(ui::cameraWheelScale(120) < 1.f && ui::cameraWheelScale(-120) > 1.f,
          "wheel zoom direction is symmetric");
    check(std::abs(ui::cameraMetresPerPixel(1000.f, 45.f, 1000)
                   - 0.828427) < 1e-5,
          "metres per pixel honors field of view");
    const auto clip = ui::cameraClipRange(1000.f, 5000.f);
    check(clip.near_plane >= 1.f && clip.far_plane >= 40000.f
              && clip.far_plane / clip.near_plane <= 40000.f,
          "clip planes preserve depth precision");

    if (failed == 0) std::cout << "test_map_view3d_geometry: ALL PASS\n";
    return failed == 0 ? 0 : 1;
}
