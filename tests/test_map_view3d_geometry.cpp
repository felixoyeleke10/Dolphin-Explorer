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

    const ui::MapLocalFrame geographic{0.0, 0.0, false};
    std::vector<float> geographic_nav;
    ui::appendNavTrackLineVertices({{0, 0}, {1, 0}}, geographic, geographic_nav);
    check(geographic_nav.size() == 6 && std::abs(geographic_nav[3] - 111319.49f) < 0.1f,
          "geographic longitude converts to local metres");

    if (failed == 0) std::cout << "test_map_view3d_geometry: ALL PASS\n";
    return failed == 0 ? 0 : 1;
}
