// SssCoverageBuild.cpp — buildSwathCoverage implementation.
// Builds port and starboard ribbon polygons from corrected nav + slant range.
#include "ui/features/map/sidescan/SssMapBuild.h"
#include "ui/features/map/sidescan/SidescanSwathGeoreferencer.h"
#include "ui/features/map/sidescan/SssContinuity.h"
#include "ui/features/map/sidescan/SssGeometryPolicy.h"
#include "geo/GeoUtils.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <numeric>

namespace dolphin::ui {

void buildSwathCoverage(const std::vector<core::SidescanPing>& pings,
                        LayerMapData& ld,
                        const SssGeorefParams& params)
{
    ld.coverage.clear();
    if (pings.empty()) return;

    // Sort by timestamp (matches georeferenceSidescanPings ordering).
    std::vector<size_t> order(pings.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(),
        [&](size_t a, size_t b) { return pings[a].timestamp_us < pings[b].timestamp_us; });

    // -- 1. Build corrected nav table ------------------------------------------
    // Same call as georeferenceSidescanPings uses, so coverage ribbons and the
    // raster mosaic share identical per-ping positions and headings.
    const std::vector<CorrectedSssNav> cnav = buildCorrectedNavTable(pings, order, params);

    const auto usable = [&](size_t pi, core::SidescanChannel channel) {
        const auto& ping = pings[order[pi]];
        const auto& nav  = cnav[pi];
        if (ping.channel != channel || !nav.valid || !nav.heading_valid
                || ping.samples.empty()
                || core::hasQcFlag(ping.qc_flags, core::QcFlag::Rejected))
            return false;
        return !core::hasQcFlag(ping.qc_flags, core::QcFlag::NoNav)
            || (nav.flags & kNavFlagInterpolated) != 0;
    };

    const auto channelThresholds = [&](core::SidescanChannel channel) {
        std::vector<double> nav_deltas;
        std::vector<double> time_deltas;
        std::vector<double> ping_deltas;
        size_t previous = pings.size();
        for (size_t pi = 0; pi < pings.size(); ++pi) {
            if (!usable(pi, channel))
                continue;
            if (previous != pings.size()) {
                core::NavPoint a{}, b{};
                a.lon = cnav[previous].lon; a.lat = cnav[previous].lat;
                b.lon = cnav[pi].lon;       b.lat = cnav[pi].lat;
                a.is_projected = cnav[previous].is_projected;
                b.is_projected = cnav[pi].is_projected;
                nav_deltas.push_back(geo::navDistanceMetres(a, b));

                const auto& p0 = pings[order[previous]];
                const auto& p1 = pings[order[pi]];
                if (p0.timestamp_us > 0 && p1.timestamp_us > p0.timestamp_us)
                    time_deltas.push_back(static_cast<double>(
                        p1.timestamp_us - p0.timestamp_us));
                if (p0.ping_number > 0 && p1.ping_number > p0.ping_number)
                    ping_deltas.push_back(static_cast<double>(
                        p1.ping_number - p0.ping_number));
            }
            previous = pi;
        }
        return ssscontinuity::fromDeltas(
            std::move(nav_deltas), std::move(time_deltas), std::move(ping_deltas));
    };

    // -- 2. Build ribbon for each channel --------------------------------------
    bool resolved_frame = false;
    for (const auto channel : {core::SidescanChannel::Port, core::SidescanChannel::Starboard}) {

        SwathCoverage cov;
        cov.channel = channel;
        const ssscontinuity::Thresholds thresholds = channelThresholds(channel);

        std::vector<QPointF> seg_inner;   // vessel nav positions
        std::vector<QPointF> seg_outer;   // swath edge positions
        core::NavPoint previous_nav;
        bool     have_previous = false;
        int64_t  prev_ts       = 0;
        uint32_t prev_ping     = 0;

        auto flush = [&]() {
            if (seg_inner.size() < 2) { seg_inner.clear(); seg_outer.clear(); return; }
            // Closed polygon: inner forward + outer reversed.
            std::vector<QPointF> ribbon;
            ribbon.reserve(seg_inner.size() * 2);
            for (const auto& pt : seg_inner) ribbon.push_back(pt);
            for (int j = static_cast<int>(seg_outer.size()) - 1; j >= 0; --j)
                ribbon.push_back(seg_outer[static_cast<size_t>(j)]);
            cov.ribbons.push_back(std::move(ribbon));
            seg_inner.clear();
            seg_outer.clear();
        };
        auto hardBreak = [&]() {
            flush();
            have_previous = false;
            prev_ts = 0;
            prev_ping = 0;
        };

        for (size_t pi = 0; pi < pings.size(); ++pi) {
            const auto& ping = pings[order[pi]];
            const auto& cn   = cnav[pi];
            if (ping.channel != channel)
                continue;
            if (!usable(pi, channel)) {
                hardBreak();
                continue;
            }

            if (!resolved_frame) {
                ld.is_projected = cn.is_projected;
                resolved_frame = true;
            }

            const double heading_rad = cn.heading_rad;

            // Coverage and raster share the same retained-cadence continuity
            // policy, so one cannot show an invented bridge while the other
            // shows a transparent stripe.
            core::NavPoint current_nav;
            current_nav.lat          = cn.lat;
            current_nav.lon          = cn.lon;
            current_nav.valid        = true;
            current_nav.is_projected = cn.is_projected;
            current_nav.spatial_ref  = cn.spatial_ref;
            if (have_previous) {
                const bool nav_break =
                    geo::navDistanceMetres(previous_nav, current_nav)
                    > thresholds.nav_gap_m;
                const bool time_break = prev_ts > 0 && ping.timestamp_us > prev_ts
                    && ping.timestamp_us - prev_ts > thresholds.time_gap_us;
                const bool ping_break = prev_ping > 0
                    && ping.ping_number > 0
                    && (ping.ping_number <= prev_ping
                        || ping.ping_number - prev_ping > thresholds.ping_gap);
                if (nav_break || time_break || ping_break)
                    flush();
            }

            // Slant → ground range with optional altitude correction.
            double ground_m = 0.0;
            if (!sssOuterGroundRangeMetres(ping, ground_m)) {
                hardBreak();
                continue;
            }

            // Perpendicular offset direction for this channel.
            const bool is_port = (channel == core::SidescanChannel::Port) != params.swap_port_starboard;
            const double side_rad = is_port
                ? heading_rad - std::numbers::pi * 0.5
                : heading_rad + std::numbers::pi * 0.5;
            const double east_m  = ground_m * std::sin(side_rad);
            const double north_m = ground_m * std::cos(side_rad);

            core::NavPoint pos_nav;
            pos_nav.lat          = cn.lat;
            pos_nav.lon          = cn.lon;
            pos_nav.valid        = true;
            pos_nav.is_projected = cn.is_projected;
            pos_nav.spatial_ref  = cn.spatial_ref;

            double out_lon = 0.0, out_lat = 0.0;
            if (!geo::offsetNavByGroundMetres(pos_nav, east_m, north_m, out_lon, out_lat)) {
                hardBreak();
                continue;
            }

            // Inner edge: vessel track when SRC applied ("closed"),
            // or nadir dead-zone offset when SRC not applied ("opened").
            if (params.slant_range_corrected) {
                seg_inner.push_back({cn.lon, cn.lat});
            } else {
                const double nadir_m = sssInnerGapMetres(ping, params);
                double inner_lon = 0.0, inner_lat = 0.0;
                if (!geo::offsetNavByGroundMetres(pos_nav,
                        nadir_m * std::sin(side_rad),
                        nadir_m * std::cos(side_rad),
                        inner_lon, inner_lat))
                    seg_inner.push_back({cn.lon, cn.lat});  // fallback: on track
                else
                    seg_inner.push_back({inner_lon, inner_lat});
            }
            seg_outer.push_back({out_lon, out_lat});
            previous_nav = current_nav;
            have_previous = true;
            prev_ts       = ping.timestamp_us;
            prev_ping     = ping.ping_number;
        }
        flush();

        if (!cov.ribbons.empty())
            ld.coverage.push_back(std::move(cov));
    }

    size_t ribbon_count = 0;
    for (const auto& c : ld.coverage) ribbon_count += c.ribbons.size();
    ld.nav_stats.coverage_ribbons_built = ribbon_count;
}

} // namespace dolphin::ui
