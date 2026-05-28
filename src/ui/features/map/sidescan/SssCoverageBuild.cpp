// SssCoverageBuild.cpp — buildSwathCoverage implementation.
// Builds port and starboard ribbon polygons from corrected nav + slant range.
#include "ui/features/map/sidescan/SssMapBuild.h"
#include "ui/features/map/sidescan/SidescanSwathGeoreferencer.h"
#include "geo/GeoUtils.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <numeric>

namespace dolphin::ui {

namespace {
constexpr double  kDegToRad           = std::numbers::pi / 180.0;
constexpr double  kFallbackRangeM     = 75.0;
constexpr int64_t kMaxTimestampGapUs  = 5'000'000LL;

bool isUsableNavPoint(double lat, double lon)
{
    return std::isfinite(lat) && std::isfinite(lon) && (lat != 0.0 || lon != 0.0);
}
} // namespace

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

    // ── 1. Build corrected nav table ──────────────────────────────────────────
    // Same call as georeferenceSidescanPings uses, so coverage ribbons and the
    // raster mosaic share identical per-ping positions and headings.
    const double kNoHeading = std::numeric_limits<double>::quiet_NaN();
    const std::vector<CorrectedSssNav> cnav = buildCorrectedNavTable(pings, order, params);

    // Dynamic gap threshold: 2× max slant range.
    double max_slant_cov = kFallbackRangeM;
    for (const auto& ping : pings)
        if (ping.slant_range_m > 0.f)
            max_slant_cov = std::max(max_slant_cov, static_cast<double>(ping.slant_range_m));

    const double cov_gap_m = 2.0 * max_slant_cov;

    // Compute center latitude directly from pings — ld.lat_min/max may not yet
    // be populated if buildSwathNavTrack has not been called first.
    double cov_lat_lo = 1e18, cov_lat_hi = -1e18;
    for (const auto& ping : pings)
        if (ping.nav.valid && isUsableNavPoint(ping.nav.lat, ping.nav.lon)) {
            cov_lat_lo = std::min(cov_lat_lo, ping.nav.lat);
            cov_lat_hi = std::max(cov_lat_hi, ping.nav.lat);
        }
    const double cov_cen_lat = (cov_lat_lo <= cov_lat_hi)
        ? (cov_lat_lo + cov_lat_hi) * 0.5
        : 0.0;

    const double cov_gap_lat = ld.is_projected ? cov_gap_m : cov_gap_m / 111320.0;
    const double cov_gap_lon = ld.is_projected ? cov_gap_m
        : cov_gap_m / (111320.0 * std::max(0.01, std::cos(cov_cen_lat * kDegToRad)));

    // ── 2. Build ribbon for each channel ──────────────────────────────────────
    for (const auto channel : {core::SidescanChannel::Port, core::SidescanChannel::Starboard}) {

        SwathCoverage cov;
        cov.channel = channel;

        std::vector<QPointF> seg_inner;   // vessel nav positions
        std::vector<QPointF> seg_outer;   // swath edge positions
        double   prev_lon = kNoHeading, prev_lat = kNoHeading;   // NaN initially
        int64_t  prev_ts  = 0;

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

        for (size_t pi = 0; pi < pings.size(); ++pi) {
            const auto& ping = pings[order[pi]];
            const auto& cn   = cnav[pi];
            if (ping.channel != channel) continue;
            if (!cn.valid || !cn.heading_valid) continue;

            const double heading_rad = cn.heading_rad;

            // Segment break on spatial gap or timestamp gap.
            if (!std::isnan(prev_lon) &&
                    (std::abs(cn.lat - prev_lat) > cov_gap_lat ||
                     std::abs(cn.lon - prev_lon) > cov_gap_lon ||
                     (prev_ts > 0 && ping.timestamp_us > 0
                      && ping.timestamp_us - prev_ts > kMaxTimestampGapUs)))
                flush();

            // Slant → ground range with optional altitude correction.
            const double slant_m  = ping.slant_range_m > 0.f
                ? static_cast<double>(ping.slant_range_m)
                : kFallbackRangeM;
            const double alt_m    = std::max(0.0, static_cast<double>(ping.nav.altitude_m));
            const double ground_m = (alt_m > 0.0 && slant_m > alt_m)
                ? std::sqrt(slant_m * slant_m - alt_m * alt_m)
                : slant_m;

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
            if (!geo::offsetNavByGroundMetres(pos_nav, east_m, north_m, out_lon, out_lat))
                continue;

            // Inner edge: vessel track when SRC applied ("closed"),
            // or nadir dead-zone offset when SRC not applied ("opened").
            if (params.slant_range_corrected) {
                seg_inner.push_back({cn.lon, cn.lat});
            } else {
                // Near-nadir dead zone: altitude if known, else 10% of slant range.
                const double nadir_m = (alt_m > 1.0) ? alt_m : slant_m * 0.10;
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
            prev_lon = cn.lon;
            prev_lat = cn.lat;
            prev_ts  = ping.timestamp_us;
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
