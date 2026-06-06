// SssGeorefParams.cpp — shared heading/position resolvers for sidescan georeferencing.
//
// Used by both SidescanSwathGeoreferencer (mosaic/preview) and
// SssMapBuild::buildSwathCoverage (ribbon footprints) so that coverage and
// mosaic always share identical per-ping heading and position.

#include "ui/features/map/sidescan/SssGeorefParams.h"
#include "geo/GeoUtils.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace dolphin::ui {

namespace {

constexpr double kDegToRad     = std::numbers::pi / 180.0;
constexpr double kHeadingBlend = 0.2;   // EMA alpha for smoothed COG
constexpr double kMaxNavStepM  = 50.0;  // survey-gap / spike threshold (metres)
constexpr int64_t kMaxTimestampGapUs = 5'000'000LL;  // 5 s = survey line break

bool isUsableNavPoint(double lat, double lon)
{
    return std::isfinite(lat) && std::isfinite(lon) && (lat != 0.0 || lon != 0.0);
}

bool isValidHeadingField(float h)
{
    return std::isfinite(h) && h != 0.0f;
}

// Returns true if at least one finite non-zero value exists in a float field
// across the ordered ping set.  Used to decide whether 0.0 can be a real value.
template <typename FieldFn>
bool fieldHasNonZero(const std::vector<core::SidescanPing>& pings,
                     const std::vector<size_t>& order,
                     FieldFn fn)
{
    for (size_t i = 0; i < pings.size(); ++i) {
        const float v = fn(pings[order[i]].nav);
        if (std::isfinite(v) && v != 0.0f) return true;
    }
    return false;
}

// Apply moving-average (or spike rejection) smoothing to CorrectedSssNav positions.
// Never smooths across timestamp gaps > kMaxTimestampGapUs.
void applyNavSmoothing(std::vector<CorrectedSssNav>&         table,
                       const std::vector<core::SidescanPing>& pings,
                       const std::vector<size_t>&              order,
                       const SssGeorefParams&                  params)
{
    const size_t n = table.size();
    if (n < 2) return;

    if (params.smoothing_mode == SssNavSmoothingMode::SpikeRejection) {
        // Hold-last-good: if a resolved position jumps more than kMaxNavStepM
        // compared to the previous valid position, replace it with the last good one.
        double prev_lat = 0.0, prev_lon = 0.0;
        bool   have_prev = false;
        for (size_t i = 0; i < n; ++i) {
            if (!table[i].valid) continue;

            if (have_prev) {
                core::NavPoint a{}, b{};
                a.lat = prev_lat; a.lon = prev_lon;
                a.is_projected = table[i].is_projected;
                b.lat = table[i].lat; b.lon = table[i].lon;
                b.is_projected = table[i].is_projected;

                // Skip gap guard — don't hold-last-good across survey line breaks.
                const int64_t dt = (i > 0)
                    ? (pings[order[i]].timestamp_us - pings[order[i - 1]].timestamp_us)
                    : 0;
                if (std::abs(dt) > kMaxTimestampGapUs) {
                    prev_lat = table[i].lat;
                    prev_lon = table[i].lon;
                    continue;
                }

                if (geo::navDistanceMetres(a, b) > kMaxNavStepM) {
                    table[i].lat = prev_lat;
                    table[i].lon = prev_lon;
                    // keep table[i].valid = true with the held position
                } else {
                    prev_lat = table[i].lat;
                    prev_lon = table[i].lon;
                }
            } else {
                prev_lat = table[i].lat;
                prev_lon = table[i].lon;
                have_prev = true;
            }
        }
        return;
    }

    if (params.smoothing_mode == SssNavSmoothingMode::MovingAverage ||
        params.smoothing_mode == SssNavSmoothingMode::Median) {
        // Median mode falls through to moving average in this build.
        const int hw = std::max(1, params.smoothing_window / 2);
        std::vector<CorrectedSssNav> smoothed = table;

        for (size_t i = 0; i < n; ++i) {
            if (!table[i].valid) continue;

            double sum_lat = 0.0, sum_lon = 0.0;
            int    count   = 0;

            for (int j = static_cast<int>(i) - hw;
                 j <= static_cast<int>(i) + hw; ++j) {
                if (j < 0 || static_cast<size_t>(j) >= n) continue;
                if (!table[j].valid) continue;
                // Don't average across survey line breaks.
                const int64_t dt = pings[order[j]].timestamp_us
                                 - pings[order[i]].timestamp_us;
                if (std::abs(dt) > kMaxTimestampGapUs) continue;
                sum_lat += table[j].lat;
                sum_lon += table[j].lon;
                ++count;
            }

            if (count > 1) {
                smoothed[i].lat = sum_lat / count;
                smoothed[i].lon = sum_lon / count;
            }
        }
        table = std::move(smoothed);
    }
}

} // namespace

// -- resolveSssHeading ---------------------------------------------------------

ResolvedHeading resolveSssHeading(
    const core::NavPoint&  nav,
    const SssGeorefParams& params,
    double cog_rad,
    double smoothed_cog_rad)
{
    const double offset_rad = params.heading_offset_deg * kDegToRad;
    ResolvedHeading r;

    switch (params.heading_source) {
    case SssHeadingSource::FishSensor:
        if (isValidHeadingField(nav.sensor_heading_deg)) {
            r.heading_rad = static_cast<double>(nav.sensor_heading_deg) * kDegToRad
                          + offset_rad;
            r.valid  = true;
            r.source = SssHeadingSource::FishSensor;
        }
        break;

    case SssHeadingSource::VesselShip:
        if (isValidHeadingField(nav.ship_heading_deg)) {
            r.heading_rad = static_cast<double>(nav.ship_heading_deg) * kDegToRad
                          + offset_rad;
            r.valid  = true;
            r.source = SssHeadingSource::VesselShip;
        }
        break;

    case SssHeadingSource::CourseOverGround:
        if (!std::isnan(cog_rad)) {
            r.heading_rad = cog_rad + offset_rad;
            r.valid  = true;
            r.source = SssHeadingSource::CourseOverGround;
        }
        break;

    case SssHeadingSource::SmoothedCourseOverGround:
        if (!std::isnan(smoothed_cog_rad)) {
            r.heading_rad = smoothed_cog_rad + offset_rad;
            r.valid  = true;
            r.source = SssHeadingSource::SmoothedCourseOverGround;
        }
        break;

    case SssHeadingSource::Auto:
    default:
        // Priority: fish sensor → vessel ship → smoothed COG → raw COG
        if (isValidHeadingField(nav.sensor_heading_deg)) {
            r.heading_rad = static_cast<double>(nav.sensor_heading_deg) * kDegToRad
                          + offset_rad;
            r.valid  = true;
            r.source = SssHeadingSource::FishSensor;
        } else if (isValidHeadingField(nav.ship_heading_deg)) {
            r.heading_rad = static_cast<double>(nav.ship_heading_deg) * kDegToRad
                          + offset_rad;
            r.valid  = true;
            r.source = SssHeadingSource::VesselShip;
        } else if (!std::isnan(smoothed_cog_rad)) {
            r.heading_rad = smoothed_cog_rad + offset_rad;
            r.valid  = true;
            r.source = SssHeadingSource::SmoothedCourseOverGround;
        } else if (!std::isnan(cog_rad)) {
            r.heading_rad = cog_rad + offset_rad;
            r.valid  = true;
            r.source = SssHeadingSource::CourseOverGround;
        }
        break;
    }

    return r;
}

// -- resolveSssPosition --------------------------------------------------------

ResolvedPosition resolveSssPosition(
    const core::SidescanPing& ping,
    const SssGeorefParams&    params,
    double heading_rad)
{
    const auto& nav = ping.nav;
    ResolvedPosition r;
    r.is_projected = nav.is_projected;
    r.spatial_ref  = nav.spatial_ref;

    switch (params.nav_source) {

    case SssNavPositionSource::FishSensor:
        if (nav.fish_nav_valid && isUsableNavPoint(nav.fish_lat, nav.fish_lon)) {
            r.lat = nav.fish_lat; r.lon = nav.fish_lon;
            r.valid = true; r.flags |= kNavFlagFishPos;
        }
        break;

    case SssNavPositionSource::VesselShip:
        if (nav.vessel_nav_valid && isUsableNavPoint(nav.vessel_lat, nav.vessel_lon)) {
            r.lat = nav.vessel_lat; r.lon = nav.vessel_lon;
            r.valid = true; r.flags |= kNavFlagVesselPos;
        }
        break;

    case SssNavPositionSource::VesselLayback: {
        if (!nav.vessel_nav_valid || !isUsableNavPoint(nav.vessel_lat, nav.vessel_lon))
            break;
        r.lat = nav.vessel_lat; r.lon = nav.vessel_lon;
        r.valid = true; r.flags |= kNavFlagVesselPos;

        if (!std::isnan(heading_rad) && params.enable_layback) {
            const double lb_m = params.use_file_layback
                ? static_cast<double>(ping.layback_m)
                : params.manual_layback_m;
            if (lb_m > 0.0) {
                // Fish trails behind vessel: offset in the backward direction.
                core::NavPoint tmp;
                tmp.lat          = r.lat;
                tmp.lon          = r.lon;
                tmp.valid        = true;
                tmp.is_projected = r.is_projected;
                tmp.spatial_ref  = r.spatial_ref;
                double new_lon, new_lat;
                if (geo::offsetNavByGroundMetres(
                        tmp,
                        -lb_m * std::sin(heading_rad),
                        -lb_m * std::cos(heading_rad),
                        new_lon, new_lat)) {
                    r.lon = new_lon; r.lat = new_lat;
                    r.flags |= kNavFlagLayback;
                }
            }
        }
        break;
    }

    case SssNavPositionSource::ManualOffset:
        // Use Auto selection as base, then fall through to offset application below.
        [[fallthrough]];

    case SssNavPositionSource::Auto:
    default:
        // Fish / sensor first; vessel / ship fallback; legacy lat/lon last.
        if (nav.fish_nav_valid && isUsableNavPoint(nav.fish_lat, nav.fish_lon)) {
            r.lat = nav.fish_lat; r.lon = nav.fish_lon;
            r.valid = true; r.flags |= kNavFlagFishPos;
        } else if (nav.vessel_nav_valid && isUsableNavPoint(nav.vessel_lat, nav.vessel_lon)) {
            r.lat = nav.vessel_lat; r.lon = nav.vessel_lon;
            r.valid = true; r.flags |= kNavFlagVesselPos;
        } else if (nav.valid && isUsableNavPoint(nav.lat, nav.lon)) {
            // Backward-compat fallback for cached pings without separate fields.
            r.lat = nav.lat; r.lon = nav.lon;
            r.valid = true;
        }
        break;
    }

    // Manual x/y offsets applied to every source when non-zero.
    if (r.valid && (params.x_offset_m != 0.0 || params.y_offset_m != 0.0)) {
        core::NavPoint tmp;
        tmp.lat          = r.lat;
        tmp.lon          = r.lon;
        tmp.valid        = true;
        tmp.is_projected = r.is_projected;
        tmp.spatial_ref  = r.spatial_ref;
        double new_lon, new_lat;
        if (geo::offsetNavByGroundMetres(tmp,
                params.x_offset_m, params.y_offset_m,
                new_lon, new_lat)) {
            r.lon = new_lon; r.lat = new_lat;
            r.flags |= kNavFlagManualOffset;
        }
    }

    return r;
}

// -- buildCorrectedNavTable ----------------------------------------------------

std::vector<CorrectedSssNav> buildCorrectedNavTable(
    const std::vector<core::SidescanPing>& pings,
    const std::vector<size_t>&             order,
    const SssGeorefParams&                 params,
    HeadingStats*                          out_stats)
{
    const size_t n = pings.size();
    std::vector<CorrectedSssNav> result(n);

    // Phase 1: heading table (EMA/COG/backward-fill).
    const std::vector<double> headings = buildHeadingTable(pings, order, params, out_stats);

    // Phase 2: per-ping position resolution.
    for (size_t i = 0; i < n; ++i) {
        result[i].heading_rad   = headings[i];
        result[i].heading_valid = !std::isnan(headings[i]);

        const auto pos = resolveSssPosition(pings[order[i]], params, headings[i]);
        result[i].lat          = pos.lat;
        result[i].lon          = pos.lon;
        result[i].valid        = pos.valid;
        result[i].is_projected = pos.is_projected;
        result[i].spatial_ref  = pos.spatial_ref;
        result[i].flags        = pos.flags;
    }

    // Phase 3: optional nav smoothing.
    if (params.smoothing_mode != SssNavSmoothingMode::Off)
        applyNavSmoothing(result, pings, order, params);

    return result;
}

// -- buildHeadingTable ---------------------------------------------------------
// Retained for backward compatibility; calls the internal heading logic directly.

std::vector<double> buildHeadingTable(
    const std::vector<core::SidescanPing>& pings,
    const std::vector<size_t>&             order,
    const SssGeorefParams&                 params,
    HeadingStats*                          out_stats)
{
    constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
    std::vector<double> table(pings.size(), kNaN);
    HeadingStats stats;

    const double offset_rad = params.heading_offset_deg * kDegToRad;

    const bool want_sensor = (params.heading_source == SssHeadingSource::Auto
                           || params.heading_source == SssHeadingSource::FishSensor);
    const bool want_ship   = (params.heading_source == SssHeadingSource::Auto
                           || params.heading_source == SssHeadingSource::VesselShip);
    const bool want_cog    = (params.heading_source == SssHeadingSource::Auto
                           || params.heading_source == SssHeadingSource::CourseOverGround
                           || params.heading_source == SssHeadingSource::SmoothedCourseOverGround);
    const double cog_alpha =
        (params.heading_source == SssHeadingSource::CourseOverGround) ? 1.0 : kHeadingBlend;

    // Pre-scan: determine whether a field's 0.0 value is a real north heading
    // or an unpopulated sentinel.  A field that is all-zero is treated as absent.
    const bool legacy_has_data = fieldHasNonZero(pings, order,
        [](const core::NavPoint& n) { return n.heading_deg; });
    auto legacyOk = [legacy_has_data](float h) {
        return std::isfinite(h) && (legacy_has_data || h != 0.0f);
    };

    // Pass 1: explicit instrument headings (sensor / ship fields only).
    // The legacy heading_deg field is intentionally deferred to Pass 3 so that
    // track-derived COG (Pass 2) takes precedence over it when both are available.
    if (want_sensor || want_ship) {
        const bool sensor_has_data = fieldHasNonZero(pings, order,
            [](const core::NavPoint& n) { return n.sensor_heading_deg; });
        const bool ship_has_data   = fieldHasNonZero(pings, order,
            [](const core::NavPoint& n) { return n.ship_heading_deg;   });

        auto sensorOk = [sensor_has_data](float h) {
            return std::isfinite(h) && (sensor_has_data || h != 0.0f);
        };
        auto shipOk = [ship_has_data](float h) {
            return std::isfinite(h) && (ship_has_data || h != 0.0f);
        };

        for (size_t i = 0; i < pings.size(); ++i) {
            const auto& p = pings[order[i]];
            if (!p.nav.valid) continue;

            if (want_sensor && sensorOk(p.nav.sensor_heading_deg)) {
                table[i] = static_cast<double>(p.nav.sensor_heading_deg) * kDegToRad
                           + offset_rad;
                ++stats.from_sensor;
            } else if (want_ship && shipOk(p.nav.ship_heading_deg)) {
                table[i] = static_cast<double>(p.nav.ship_heading_deg) * kDegToRad
                           + offset_rad;
                ++stats.from_ship;
            }
        }
    }

    // Pass 2: COG fill.
    if (want_cog) {
        core::NavPoint prev_nav;
        bool   have_prev = false;
        bool   have_cog  = false;
        double cog_blend = 0.0;

        for (size_t i = 0; i < pings.size(); ++i) {
            const auto& p = pings[order[i]];
            if (!p.nav.valid || !isUsableNavPoint(p.nav.lat, p.nav.lon)) continue;

            bool had_large_step = false;
            if (have_prev && (p.nav.lon != prev_nav.lon || p.nav.lat != prev_nav.lat)) {
                const double step_m = geo::navDistanceMetres(prev_nav, p.nav);
                if (!have_cog || step_m <= kMaxNavStepM) {
                    const double obs = geo::headingFromNavDeltaRad(prev_nav, p.nav);
                    cog_blend = have_cog
                        ? geo::blendAngleRad(cog_blend, obs, cog_alpha)
                        : obs;
                    have_cog = true;
                } else {
                    had_large_step = true;
                }
            }
            prev_nav  = p.nav;
            have_prev = true;

            if (std::isnan(table[i]) && have_cog && !had_large_step) {
                table[i] = cog_blend + offset_rad;
                ++stats.from_cog;
            }
        }

        // Backward-fill startup window.
        double first_hdg = kNaN;
        for (size_t i = 0; i < pings.size(); ++i)
            if (!std::isnan(table[i])) { first_hdg = table[i]; break; }
        if (!std::isnan(first_hdg)) {
            for (size_t i = 0; i < pings.size(); ++i) {
                if (std::isnan(table[i])) table[i] = first_hdg;
                else break;
            }
        }
    }

    // Pass 3: legacy heading_deg fallback for entries still NaN after sensor
    // fields and COG.  Cache-loaded pings (pre-v24) store only heading_deg;
    // new-format files have sensor/ship/COG available and won't reach here.
    if (want_sensor || want_ship) {
        for (size_t i = 0; i < pings.size(); ++i) {
            if (!std::isnan(table[i])) continue;
            const auto& p = pings[order[i]];
            if (!p.nav.valid) continue;
            if (legacyOk(p.nav.heading_deg)) {
                table[i] = static_cast<double>(p.nav.heading_deg) * kDegToRad
                           + offset_rad;
                ++stats.from_sensor;
            }
        }
    }

    for (size_t i = 0; i < pings.size(); ++i)
        if (std::isnan(table[i]) && pings[order[i]].nav.valid)
            ++stats.skipped;

    if (out_stats) *out_stats = stats;
    return table;
}

} // namespace dolphin::ui
