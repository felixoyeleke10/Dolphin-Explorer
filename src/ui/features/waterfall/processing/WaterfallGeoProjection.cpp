#include "WaterfallGeoProjection.h"

#include <cmath>
#include <numbers>

namespace dolphin::ui {

std::optional<WaterfallGeoPosition> projectWaterfallRange(
    const WaterfallGeoProjectionInput& input)
{
    if (input.nav_lat == 0.0 && input.nav_lon == 0.0) return std::nullopt;

    double ground_range = input.range_m;
    if (input.range_domain == core::SidescanRangeDomain::Slant && input.altitude_m > 0.f) {
        const auto converted = core::convertSidescanRange(
            {input.range_m, input.range_domain}, core::SidescanRangeDomain::Ground,
            input.altitude_m);
        if (!converted) return std::nullopt;
        ground_range = converted->metres;
    }

    constexpr double degrees_to_radians = std::numbers::pi / 180.0;
    const double bearing_deg = input.heading_deg
        + (input.channel == core::SidescanChannel::Port ? -90.0 : 90.0);
    const double bearing_rad = bearing_deg * degrees_to_radians;

    WaterfallGeoPosition result{input.nav_lat, input.nav_lon, input.is_projected};
    if (input.is_projected) {
        result.lat += ground_range * std::cos(bearing_rad);
        result.lon += ground_range * std::sin(bearing_rad);
        return result;
    }

    constexpr double earth_radius_m = 6371000.0;
    const double angular_distance = ground_range / earth_radius_m;
    const double lat1 = input.nav_lat * degrees_to_radians;
    const double lon1 = input.nav_lon * degrees_to_radians;
    const double lat2 = std::asin(std::sin(lat1) * std::cos(angular_distance)
        + std::cos(lat1) * std::sin(angular_distance) * std::cos(bearing_rad));
    const double lon2 = lon1 + std::atan2(
        std::sin(bearing_rad) * std::sin(angular_distance) * std::cos(lat1),
        std::cos(angular_distance) - std::sin(lat1) * std::sin(lat2));
    result.lat = lat2 / degrees_to_radians;
    result.lon = lon2 / degrees_to_radians;
    return result;
}

} // namespace dolphin::ui
