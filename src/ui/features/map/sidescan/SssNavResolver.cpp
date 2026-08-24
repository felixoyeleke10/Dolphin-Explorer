// SssNavResolver.cpp — single-ping navigation and heading source policy.

#include "ui/features/map/sidescan/SssGeorefParams.h"
#include "geo/GeoUtils.h"

#include <cmath>
#include <numbers>

namespace dolphin::ui {
namespace {

constexpr double kDegToRad = std::numbers::pi / 180.0;

bool usablePosition(double lat, double lon)
{
    return std::isfinite(lat) && std::isfinite(lon)
        && (lat != 0.0 || lon != 0.0);
}

bool usableHeading(float heading)
{
    return std::isfinite(heading) && heading != 0.0f;
}

} // namespace

ResolvedHeading resolveSssHeading(
    const core::NavPoint& nav,
    const SssGeorefParams& params,
    double cog_rad,
    double smoothed_cog_rad)
{
    const double offset = params.heading_offset_deg * kDegToRad;
    ResolvedHeading result;
    const auto accept = [&](double heading, SssHeadingSource source) {
        result.heading_rad = heading + offset;
        result.valid = true;
        result.source = source;
    };

    switch (params.heading_source) {
    case SssHeadingSource::FishSensor:
        if (usableHeading(nav.sensor_heading_deg))
            accept(nav.sensor_heading_deg * kDegToRad, SssHeadingSource::FishSensor);
        break;
    case SssHeadingSource::VesselShip:
        if (usableHeading(nav.ship_heading_deg))
            accept(nav.ship_heading_deg * kDegToRad, SssHeadingSource::VesselShip);
        break;
    case SssHeadingSource::CourseOverGround:
        if (!std::isnan(cog_rad))
            accept(cog_rad, SssHeadingSource::CourseOverGround);
        break;
    case SssHeadingSource::SmoothedCourseOverGround:
        if (!std::isnan(smoothed_cog_rad))
            accept(smoothed_cog_rad, SssHeadingSource::SmoothedCourseOverGround);
        break;
    case SssHeadingSource::Auto:
    default:
        if (usableHeading(nav.sensor_heading_deg))
            accept(nav.sensor_heading_deg * kDegToRad, SssHeadingSource::FishSensor);
        else if (usableHeading(nav.ship_heading_deg))
            accept(nav.ship_heading_deg * kDegToRad, SssHeadingSource::VesselShip);
        else if (!std::isnan(smoothed_cog_rad))
            accept(smoothed_cog_rad, SssHeadingSource::SmoothedCourseOverGround);
        else if (!std::isnan(cog_rad))
            accept(cog_rad, SssHeadingSource::CourseOverGround);
        break;
    }
    return result;
}

ResolvedPosition resolveSssPosition(
    const core::SidescanPing& ping,
    const SssGeorefParams& params,
    double heading_rad)
{
    const auto& nav = ping.nav;
    ResolvedPosition result;
    result.is_projected = nav.is_projected;
    result.spatial_ref = nav.spatial_ref;

    const auto useFish = [&] {
        if (!nav.fish_nav_valid || !usablePosition(nav.fish_lat, nav.fish_lon))
            return false;
        result.lat = nav.fish_lat;
        result.lon = nav.fish_lon;
        result.valid = true;
        result.flags |= kNavFlagFishPos;
        return true;
    };
    const auto useVessel = [&] {
        if (!nav.vessel_nav_valid
                || !usablePosition(nav.vessel_lat, nav.vessel_lon))
            return false;
        result.lat = nav.vessel_lat;
        result.lon = nav.vessel_lon;
        result.valid = true;
        result.flags |= kNavFlagVesselPos;
        return true;
    };

    switch (params.nav_source) {
    case SssNavPositionSource::FishSensor:
        useFish();
        break;
    case SssNavPositionSource::VesselShip:
        useVessel();
        break;
    case SssNavPositionSource::VesselLayback:
        if (useVessel() && !std::isnan(heading_rad) && params.enable_layback) {
            const double layback = params.use_file_layback
                ? static_cast<double>(ping.layback_m) : params.manual_layback_m;
            core::NavPoint point;
            point.lat = result.lat;
            point.lon = result.lon;
            point.valid = true;
            point.is_projected = result.is_projected;
            point.spatial_ref = result.spatial_ref;
            double lon = 0.0, lat = 0.0;
            if (layback > 0.0 && geo::offsetNavByGroundMetres(
                    point, -layback * std::sin(heading_rad),
                    -layback * std::cos(heading_rad), lon, lat)) {
                result.lon = lon;
                result.lat = lat;
                result.flags |= kNavFlagLayback;
            }
        }
        break;
    case SssNavPositionSource::ManualOffset:
        [[fallthrough]];
    case SssNavPositionSource::Auto:
    default:
        if (!useFish() && !useVessel()
                && nav.valid && usablePosition(nav.lat, nav.lon)) {
            result.lat = nav.lat;
            result.lon = nav.lon;
            result.valid = true;
        }
        break;
    }

    if (result.valid && (params.x_offset_m != 0.0 || params.y_offset_m != 0.0)) {
        core::NavPoint point;
        point.lat = result.lat;
        point.lon = result.lon;
        point.valid = true;
        point.is_projected = result.is_projected;
        point.spatial_ref = result.spatial_ref;
        double lon = 0.0, lat = 0.0;
        if (geo::offsetNavByGroundMetres(
                point, params.x_offset_m, params.y_offset_m, lon, lat)) {
            result.lon = lon;
            result.lat = lat;
            result.flags |= kNavFlagManualOffset;
        }
    }
    return result;
}

} // namespace dolphin::ui
