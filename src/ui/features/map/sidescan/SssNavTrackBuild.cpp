// SssNavTrackBuild.cpp — buildSwathNavTrack implementation.
// Builds the nav polyline, bounding box, and NavStats from sorted pings.
#include "ui/features/map/sidescan/SssMapBuild.h"
#include "geo/GeoUtils.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <numeric>

namespace dolphin::ui {

namespace {
constexpr double  kMaxSegGapDeg      = 0.05;
constexpr double  kMaxSegGapM        = 100.0;
constexpr double  kDegToRad          = std::numbers::pi / 180.0;
constexpr double  kFallbackRangeM    = 75.0;
constexpr double  kMaxNavStepM       = 50.0;
constexpr int64_t kMaxTimestampGapUs = 5'000'000LL;

bool isUsableNavPoint(double lat, double lon)
{
    return std::isfinite(lat) && std::isfinite(lon) && (lat != 0.0 || lon != 0.0);
}
} // namespace

size_t buildSwathNavTrack(const std::vector<core::SidescanPing>& pings, LayerMapData& ld)
{
    ld.nav_track.clear();
    ld.lon_min =  1e18; ld.lon_max = -1e18;
    ld.lat_min =  1e18; ld.lat_max = -1e18;
    ld.nav_stats = {};

    std::vector<size_t> order(pings.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(),
        [&](size_t a, size_t b) { return pings[a].timestamp_us < pings[b].timestamp_us; });

    const double max_seg_gap = ld.is_projected ? kMaxSegGapM : kMaxSegGapDeg;
    constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

    double  prev_lon = kNaN, prev_lat = kNaN;
    int64_t prev_ts  = 0;
    core::NavPoint prev_nav_pt;
    bool    have_prev_nav = false;
    size_t  n_unique      = 0;
    double  spacing_sum   = 0.0;
    size_t  spacing_n     = 0;

    ld.nav_stats.total_pings = pings.size();

    for (size_t oi = 0; oi < pings.size(); ++oi) {
        const auto& p = pings[order[oi]];

        if (!p.nav.valid || !isUsableNavPoint(p.nav.lat, p.nav.lon)) {
            ++ld.nav_stats.invalid_nav;
            continue;
        }

        const double lon = p.nav.lon;
        const double lat = p.nav.lat;

        // Repeated-fix counter.
        if (lon == prev_lon && lat == prev_lat) {
            ++ld.nav_stats.repeated_fixes;
            continue;
        }

        // Timestamp gap counter.
        if (prev_ts > 0 && p.timestamp_us > 0
                && p.timestamp_us - prev_ts > kMaxTimestampGapUs)
            ++ld.nav_stats.time_gaps;

        // Spacing statistics and spike guard.
        // Spike points are kept in the nav track so survey lines after legitimate
        // large gaps remain visible, but excluded from bbox expansion and spacing
        // stats.  prev_nav_pt is always advanced so the next point is judged
        // against the current position — without this, the entire second survey
        // line would be misclassified as spikes because every point compares
        // against the frozen last-accepted position from line 1.
        bool is_spike = false;
        if (have_prev_nav) {
            const double step_m = geo::navDistanceMetres(prev_nav_pt, p.nav);
            if (step_m > kMaxNavStepM) {
                ++ld.nav_stats.nav_spikes;
                is_spike = true;
            } else {
                spacing_sum += step_m;
                ++spacing_n;
                ld.nav_stats.max_spacing_m = std::max(ld.nav_stats.max_spacing_m, step_m);
            }
        }

        if (!std::isnan(prev_lon) &&
            (std::abs(lat - prev_lat) > max_seg_gap ||
             std::abs(lon - prev_lon) > max_seg_gap))
            ld.nav_track.push_back({kNaN, kNaN});

        ld.nav_track.push_back({lon, lat});
        if (!is_spike) {
            ld.lon_min = std::min(ld.lon_min, lon);
            ld.lon_max = std::max(ld.lon_max, lon);
            ld.lat_min = std::min(ld.lat_min, lat);
            ld.lat_max = std::max(ld.lat_max, lat);
        }
        prev_lon      = lon;
        prev_lat      = lat;
        prev_ts       = p.timestamp_us;
        prev_nav_pt   = p.nav;
        have_prev_nav = true;
        ++n_unique;
    }

    if (spacing_n > 0)
        ld.nav_stats.avg_spacing_m = spacing_sum / static_cast<double>(spacing_n);

    if (n_unique == 0) return 0;

    // Pad bbox by max observed slant range so swath edges stay inside the view.
    double max_range = kFallbackRangeM;
    for (const auto& p : pings)
        if (p.slant_range_m > 0.f)
            max_range = std::max(max_range, static_cast<double>(p.slant_range_m));

    if (ld.is_projected) {
        ld.lon_min -= max_range; ld.lon_max += max_range;
        ld.lat_min -= max_range; ld.lat_max += max_range;
    } else {
        const double lat_pad = max_range / 111320.0;
        const double cen_lat = (ld.lat_min + ld.lat_max) * 0.5;
        const double lon_pad = max_range /
            (111320.0 * std::max(0.01, std::cos(cen_lat * kDegToRad)));
        ld.lon_min -= lon_pad; ld.lon_max += lon_pad;
        ld.lat_min -= lat_pad; ld.lat_max += lat_pad;
    }

    // Record the padded bbox used for image sizing and placement.
    ld.nav_stats.nav_lon_min = ld.lon_min;
    ld.nav_stats.nav_lon_max = ld.lon_max;
    ld.nav_stats.nav_lat_min = ld.lat_min;
    ld.nav_stats.nav_lat_max = ld.lat_max;

    return n_unique;
}

} // namespace dolphin::ui
