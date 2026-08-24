#include "ui/features/waterfall/processing/WaterfallGeoProjection.h"

#include <cmath>
#include <iostream>

using namespace dolphin;

int main()
{
    int failed = 0;
    auto check = [&](bool condition, const char* message) {
        if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failed; }
    };

    ui::WaterfallGeoProjectionInput projected;
    projected.nav_lat = 1000.0;
    projected.nav_lon = 2000.0;
    projected.heading_deg = 0.0f;
    projected.range_m = 100.0f;
    projected.range_domain = core::SidescanRangeDomain::Ground;
    projected.is_projected = true;
    projected.channel = core::SidescanChannel::Starboard;
    const auto starboard = ui::projectWaterfallRange(projected);
    check(starboard.has_value(), "projected starboard position exists");
    check(starboard && std::abs(starboard->lat - 1000.0) < 1e-6, "northing unchanged");
    check(starboard && std::abs(starboard->lon - 2100.0) < 1e-6, "starboard offsets east");

    projected.channel = core::SidescanChannel::Port;
    const auto port = ui::projectWaterfallRange(projected);
    check(port && std::abs(port->lon - 1900.0) < 1e-6, "port offsets west");

    projected.range_domain = core::SidescanRangeDomain::Slant;
    projected.range_m = 5.0f;
    projected.altitude_m = 5.0f;
    const auto nadir = ui::projectWaterfallRange(projected);
    check(nadir && std::abs(nadir->lon - 2000.0) < 1e-6,
          "slant range at altitude projects to nadir");

    projected.nav_lat = 0.0;
    projected.nav_lon = 0.0;
    check(!ui::projectWaterfallRange(projected), "missing navigation is rejected");

    std::cout << (failed == 0 ? "test_waterfall_geo_projection: ALL PASS\n" : "");
    return failed == 0 ? 0 : 1;
}
