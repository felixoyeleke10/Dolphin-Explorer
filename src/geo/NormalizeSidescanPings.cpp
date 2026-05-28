#include "geo/GeoUtils.h"
#include "core/SidescanPing.h"
#include "core/SpatialRef.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace dolphin::geo {

namespace {
constexpr double kMetresPerDegLat = 111320.0;

bool looksLikeGeographicDegrees(const core::NavPoint& nav)
{
    return std::abs(nav.lat) <= 90.0
        && std::abs(nav.lon) <= 180.0
        && (nav.lat != 0.0 || nav.lon != 0.0);
}

bool isXtfProjectedHint(const core::NavPoint& nav)
{
    return nav.spatial_ref.id == "PROJECTED:XTF_NAVUNITS3"
        || nav.spatial_ref.id == "PROJECTED:XTF_MAGNITUDE";
}
} // namespace

std::vector<core::SidescanPing>
normalizeSidescanPingsForMap(std::vector<core::SidescanPing> raw,
                             const core::SpatialRef&         display_ref,
                             std::vector<core::SpatialRef>*  out_unresolved)
{
    std::vector<core::SidescanPing> out = std::move(raw);
    const core::SpatialRef target_ref = display_ref.empty()
        ? core::makeWgs84SpatialRef()
        : display_ref;

    // Pre-pass:
    //  1. Any nav with coordinates outside geographic range must be projected,
    //     regardless of what the stored flags say. This corrects caches
    //     serialised before is_projected was reliably set.
    //  2. Some XTF files claim projected NavUnits in the header but actually
    //     store geographic degrees in each packet. Repair those legacy/projected
    //     hints before map normalisation so valid lat/lon is not collapsed into
    //     a pseudo-degree fallback near zero.
    for (auto& ping : out) {
        if (!ping.nav.valid) continue;
        if (navUsesProjectedCoordinates(ping.nav)
            && !ping.nav.spatial_ref.exact
            && isXtfProjectedHint(ping.nav)
            && looksLikeGeographicDegrees(ping.nav)) {
            ping.nav.is_projected = false;
            ping.nav.spatial_ref  = core::makeWgs84SpatialRef();
            continue;
        }
        if (std::abs(ping.nav.lat) > 90.0 || std::abs(ping.nav.lon) > 180.0) {
            if (!navUsesProjectedCoordinates(ping.nav)) {
                ping.nav.is_projected = true;
                ping.nav.spatial_ref  = core::makeUnknownProjectedSpatialRef();
            }
        }
    }

    // First pass: convert pings with an exact transform. Track whether any
    // projected pings could not be converted (unknown CRS / no UTM zone).
    bool has_unconverted = false;

    for (auto& ping : out) {
        // Save source CRS before normalizeNavForMap overwrites is_projected/spatial_ref.
        // Fish and vessel positions share the same CRS as the main nav.
        const bool src_projected = navUsesProjectedCoordinates(ping.nav);
        const core::SpatialRef src_ref = ping.nav.spatial_ref;
        const double fish_lat_src   = ping.nav.fish_lat;
        const double fish_lon_src   = ping.nav.fish_lon;
        const double vessel_lat_src = ping.nav.vessel_lat;
        const double vessel_lon_src = ping.nav.vessel_lon;

        core::NavPoint normalized;
        if (normalizeNavForMap(ping.nav, target_ref, normalized)) {
            ping.nav = normalized;

            // Normalize fish position with the same source CRS as the main nav.
            if (ping.nav.fish_nav_valid) {
                core::NavPoint fish_tmp;
                fish_tmp.lat          = fish_lat_src;
                fish_tmp.lon          = fish_lon_src;
                fish_tmp.valid        = true;
                fish_tmp.is_projected = src_projected;
                fish_tmp.spatial_ref  = src_ref;
                core::NavPoint fish_norm;
                if (normalizeNavForMap(fish_tmp, target_ref, fish_norm)) {
                    ping.nav.fish_lat = fish_norm.lat;
                    ping.nav.fish_lon = fish_norm.lon;
                } else {
                    ping.nav.fish_nav_valid = false;
                }
            }

            // Normalize vessel position with the same source CRS.
            if (ping.nav.vessel_nav_valid) {
                core::NavPoint vessel_tmp;
                vessel_tmp.lat          = vessel_lat_src;
                vessel_tmp.lon          = vessel_lon_src;
                vessel_tmp.valid        = true;
                vessel_tmp.is_projected = src_projected;
                vessel_tmp.spatial_ref  = src_ref;
                core::NavPoint vessel_norm;
                if (normalizeNavForMap(vessel_tmp, target_ref, vessel_norm)) {
                    ping.nav.vessel_lat = vessel_norm.lat;
                    ping.nav.vessel_lon = vessel_norm.lon;
                } else {
                    ping.nav.vessel_nav_valid = false;
                }
            }
        } else if (ping.nav.valid && navUsesProjectedCoordinates(ping.nav)
                && core::spatialRefIsGeographic(target_ref)) {
            has_unconverted = true;
        }
    }

    // Collect unique unresolved source CRS IDs for diagnostic reporting.
    if (has_unconverted && out_unresolved) {
        for (const auto& ping : out) {
            if (!ping.nav.valid || !navUsesProjectedCoordinates(ping.nav)) continue;
            const core::SpatialRef& src = ping.nav.spatial_ref;
            bool already = false;
            for (const auto& existing : *out_unresolved)
                if (existing.id == src.id) { already = true; break; }
            if (!already)
                out_unresolved->push_back(src);
        }
    }

    // Second pass: for projected pings whose CRS could not be resolved, map
    // raw metre coordinates to a pseudo-degree display frame.
    //
    // The latitude conversion is direct (northing ≈ lat_deg * 111320 m/deg).
    // The longitude conversion uses each ping's own converted latitude to
    // derive the local metres-per-degree-longitude scale factor.  This
    // preserves the correct east–west aspect ratio of the survey footprint
    // (the old flat /111320 compressed east–west at high latitudes) and is
    // deterministic per-coordinate so spatial separation across multiple
    // imports from the same area is maintained.
    if (has_unconverted) {
        constexpr double kDegToRad = std::numbers::pi / 180.0;
        for (auto& ping : out) {
            if (!ping.nav.valid || !navUsesProjectedCoordinates(ping.nav))
                continue;
            const double lat_deg = ping.nav.lat / kMetresPerDegLat;
            const double cos_lat = std::max(0.001,
                std::cos(std::clamp(lat_deg, -89.0, 89.0) * kDegToRad));
            ping.nav.lon = ping.nav.lon / (kMetresPerDegLat * cos_lat);
            ping.nav.lat = lat_deg;

            // Apply the same pseudo-degree scaling to fish/vessel positions.
            if (ping.nav.fish_nav_valid) {
                const double f_lat = ping.nav.fish_lat / kMetresPerDegLat;
                const double f_cos = std::max(0.001,
                    std::cos(std::clamp(f_lat, -89.0, 89.0) * kDegToRad));
                ping.nav.fish_lat = f_lat;
                ping.nav.fish_lon = ping.nav.fish_lon / (kMetresPerDegLat * f_cos);
                ping.nav.fish_nav_valid =
                    isFiniteCoordinate(ping.nav.fish_lat, ping.nav.fish_lon);
            }
            if (ping.nav.vessel_nav_valid) {
                const double v_lat = ping.nav.vessel_lat / kMetresPerDegLat;
                const double v_cos = std::max(0.001,
                    std::cos(std::clamp(v_lat, -89.0, 89.0) * kDegToRad));
                ping.nav.vessel_lat = v_lat;
                ping.nav.vessel_lon = ping.nav.vessel_lon / (kMetresPerDegLat * v_cos);
                ping.nav.vessel_nav_valid =
                    isFiniteCoordinate(ping.nav.vessel_lat, ping.nav.vessel_lon);
            }

            ping.nav.spatial_ref  = core::makePseudoWgs84SpatialRef();
            ping.nav.is_projected = false;
            ping.nav.valid = isFiniteCoordinate(ping.nav.lat, ping.nav.lon);
        }
    }

    return out;
}

} // namespace dolphin::geo
