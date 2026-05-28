#include "ui/features/map/sidescan/SidescanSwathGeoreferencer.h"
#include "geo/GeoUtils.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>

namespace dolphin::ui {

namespace {

constexpr double kFallbackRangeM    = 75.0;
constexpr double kDefaultBlankingM  = 0.5;
constexpr size_t kMaxSamplesPerStrip = 131072;

} // namespace

SwathGeorefResult georeferenceSidescanPings(
    const std::vector<core::SidescanPing>& raw_pings,
    const SssGeorefParams&                 params)
{
    SwathGeorefResult result;
    if (raw_pings.empty())
        return result;

    std::vector<size_t> order(raw_pings.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(),
        [&](size_t a, size_t b) {
            return raw_pings[a].timestamp_us < raw_pings[b].timestamp_us;
        });

    result.is_projected = raw_pings[order.front()].nav.is_projected;

    // ── Build corrected nav table ─────────────────────────────────────────────
    // Resolves heading (COG/EMA/backward-fill), position (fish/vessel/layback),
    // and applies smoothing.  Coverage building calls the same function so both
    // outputs share identical per-ping position and heading.
    const std::vector<CorrectedSssNav> cnav =
        buildCorrectedNavTable(raw_pings, order, params, &result.heading_stats);

    // ── Georeference samples for each ping ────────────────────────────────────
    result.strips.reserve(raw_pings.size());

    for (size_t pi = 0; pi < raw_pings.size(); ++pi) {
        const auto& ping = raw_pings[order[pi]];
        const auto& cn   = cnav[pi];

        if (core::hasQcFlag(ping.qc_flags, core::QcFlag::Rejected) ||
            core::hasQcFlag(ping.qc_flags, core::QcFlag::NoNav))
            continue;

        if (!cn.valid)           { ++result.skipped_no_position; continue; }
        if (!cn.heading_valid)   { ++result.skipped_no_heading;  continue; }
        if (ping.samples.empty()){ ++result.skipped_no_samples;  continue; }

        const double heading_rad = cn.heading_rad;

        const bool is_port = params.swap_port_starboard
            ? (ping.channel != core::SidescanChannel::Port)
            : (ping.channel == core::SidescanChannel::Port);
        const double side_rad = is_port
            ? heading_rad - std::numbers::pi * 0.5
            : heading_rad + std::numbers::pi * 0.5;
        const double sin_side = std::sin(side_rad);
        const double cos_side = std::cos(side_rad);

        const double altitude_m  = std::max(0.0, static_cast<double>(ping.nav.altitude_m));
        const double min_range_m = ping.blanking_m > 0.f
            ? static_cast<double>(ping.blanking_m)
            : kDefaultBlankingM;
        const double total_range_m = ping.slant_range_m > 0.f
            ? static_cast<double>(ping.slant_range_m)
            : kFallbackRangeM;

        const size_t n    = ping.samples.size();
        const size_t step = std::max<size_t>(1, n / kMaxSamplesPerStrip);

        SssStrip strip;
        strip.channel      = ping.channel;
        strip.timestamp_us = ping.timestamp_us;
        strip.ping_number  = ping.ping_number;
        strip.nav_lon      = cn.lon;   // corrected position (fish/vessel/layback)
        strip.nav_lat      = cn.lat;
        strip.points.reserve(n / step + 1);

        // Build a minimal NavPoint for the ground-offset calculation using the
        // corrected position rather than the raw ping.nav centre.
        core::NavPoint pos_nav;
        pos_nav.lat          = cn.lat;
        pos_nav.lon          = cn.lon;
        pos_nav.valid        = true;
        pos_nav.is_projected = cn.is_projected;
        pos_nav.spatial_ref  = cn.spatial_ref;

        for (size_t i = 0; i < n; i += step) {
            const double raw_range_m = static_cast<double>(ping.samples[i].range_m);
            if (raw_range_m < 0.0) continue;

            double slant_m;
            if (raw_range_m == 0.0 && n > 1) {
                slant_m = total_range_m * static_cast<double>(i)
                                        / static_cast<double>(n - 1);
            } else {
                slant_m = raw_range_m;
            }
            if (slant_m < min_range_m) continue;

            double ground_m;
            if (altitude_m > 0.0) {
                if (slant_m <= altitude_m) continue;
                ground_m = std::sqrt(slant_m * slant_m - altitude_m * altitude_m);
            } else {
                ground_m = slant_m;
            }

            const double east_m  = ground_m * sin_side;
            const double north_m = ground_m * cos_side;

            double out_lon = 0.0, out_lat = 0.0;
            if (!geo::offsetNavByGroundMetres(pos_nav, east_m, north_m, out_lon, out_lat))
                continue;

            strip.points.push_back({out_lon, out_lat, ping.samples[i].amplitude});
        }

        if (!strip.points.empty()) {
            // Prepend a synthetic nadir point at the vessel-track position so
            // the rasterized quads extend all the way to nadir, closing the
            // narrow transparent gap between port and starboard along the vessel
            // track that blanking_m would otherwise leave uncovered.
            const uint16_t nadir_amp = strip.points.front().amplitude;
            strip.points.insert(strip.points.begin(), {cn.lon, cn.lat, nadir_amp});
            result.strips.push_back(std::move(strip));
        }
    }

    return result;
}

} // namespace dolphin::ui
