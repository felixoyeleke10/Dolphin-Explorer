// SssNavTrackBuild.cpp — buildSwathNavTrack implementation.
// Builds the nav polyline, bounding box, and NavStats from sorted pings.
#include "ui/features/map/sidescan/SssMapBuild.h"
#include "ui/features/map/sidescan/SssContinuity.h"
#include "geo/GeoUtils.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <numeric>

namespace dolphin::ui {

namespace {
constexpr double  kDegToRad          = std::numbers::pi / 180.0;
constexpr double  kFallbackRangeM    = 75.0;
} // namespace

size_t buildSwathNavTrack(const std::vector<core::SidescanPing>& pings,
                          LayerMapData& ld,
                          const SssGeorefParams& params)
{
    ld.nav_track.clear();
    ld.lon_min =  1e18; ld.lon_max = -1e18;
    ld.lat_min =  1e18; ld.lat_max = -1e18;
    ld.nav_stats = {};

    std::vector<size_t> order(pings.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(),
        [&](size_t a, size_t b) { return pings[a].timestamp_us < pings[b].timestamp_us; });

    constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

    ld.nav_stats.total_pings = pings.size();

    const std::vector<CorrectedSssNav> corrected =
        buildCorrectedNavTable(pings, order, params);

    struct TrackPose {
        CorrectedSssNav nav;
        int64_t         timestamp_us = 0;
        uint32_t        ping_number  = 0;
    };
    std::vector<TrackPose> poses;
    poses.reserve(pings.size());

    for (size_t oi = 0; oi < order.size(); ++oi) {
        const auto& ping = pings[order[oi]];
        const auto& nav  = corrected[oi];

        if (!nav.valid)
            ++ld.nav_stats.invalid_nav;
        if ((nav.flags & kNavFlagInterpolated) != 0)
            ++ld.nav_stats.interpolated_nav;
        if (core::hasQcFlag(ping.qc_flags, core::QcFlag::Rejected) || !nav.valid)
            continue;
        if (core::hasQcFlag(ping.qc_flags, core::QcFlag::NoNav)
                && (nav.flags & kNavFlagInterpolated) == 0)
            continue;

        if (!poses.empty() && nav.lon == poses.back().nav.lon
                && nav.lat == poses.back().nav.lat) {
            ++ld.nav_stats.repeated_fixes;
            continue;
        }

        poses.push_back({nav, ping.timestamp_us, ping.ping_number});
    }

    if (poses.empty())
        return 0;
    ld.is_projected = poses.front().nav.is_projected;

    const auto distance = [](const TrackPose& a, const TrackPose& b) {
        core::NavPoint na{}, nb{};
        na.lon = a.nav.lon; na.lat = a.nav.lat;
        nb.lon = b.nav.lon; nb.lat = b.nav.lat;
        na.valid = nb.valid = true;
        na.is_projected = a.nav.is_projected;
        nb.is_projected = b.nav.is_projected;
        na.spatial_ref = a.nav.spatial_ref;
        nb.spatial_ref = b.nav.spatial_ref;
        return geo::navDistanceMetres(na, nb);
    };

    std::vector<double> nav_deltas;
    std::vector<double> time_deltas;
    std::vector<double> ping_deltas;
    nav_deltas.reserve(poses.size() - 1);
    time_deltas.reserve(poses.size() - 1);
    ping_deltas.reserve(poses.size() - 1);
    for (size_t i = 1; i < poses.size(); ++i) {
        nav_deltas.push_back(distance(poses[i - 1], poses[i]));
        if (poses[i - 1].timestamp_us > 0
                && poses[i].timestamp_us > poses[i - 1].timestamp_us)
            time_deltas.push_back(static_cast<double>(
                poses[i].timestamp_us - poses[i - 1].timestamp_us));
        if (poses[i - 1].ping_number > 0
                && poses[i].ping_number > poses[i - 1].ping_number)
            ping_deltas.push_back(static_cast<double>(
                poses[i].ping_number - poses[i - 1].ping_number));
    }
    const ssscontinuity::Thresholds thresholds = ssscontinuity::fromDeltas(
        std::move(nav_deltas), std::move(time_deltas), std::move(ping_deltas));

    // Reject only an isolated excursion that returns to the local track. A
    // one-way large step is a legitimate segment break and must still expand
    // the bbox; excluding every >50 m step cropped regularly thinned surveys.
    std::vector<bool> isolated_spike(poses.size(), false);
    for (size_t i = 1; i + 1 < poses.size(); ++i) {
        const double before = distance(poses[i - 1], poses[i]);
        const double after  = distance(poses[i], poses[i + 1]);
        const double bridge = distance(poses[i - 1], poses[i + 1]);
        if (before > thresholds.nav_gap_m && after > thresholds.nav_gap_m
                && bridge <= thresholds.nav_gap_m) {
            isolated_spike[i] = true;
            ++ld.nav_stats.nav_spikes;
        }
    }

    size_t n_unique = 0;
    double spacing_sum = 0.0;
    size_t spacing_n = 0;
    const TrackPose* previous = nullptr;
    for (size_t i = 0; i < poses.size(); ++i) {
        if (isolated_spike[i])
            continue;
        const auto& pose = poses[i];

        if (previous) {
            const double step_m = distance(*previous, pose);
            const bool time_break = previous->timestamp_us > 0
                && pose.timestamp_us > previous->timestamp_us
                && pose.timestamp_us - previous->timestamp_us > thresholds.time_gap_us;
            const bool ping_break = previous->ping_number > 0
                && pose.ping_number > 0
                && (pose.ping_number <= previous->ping_number
                    || pose.ping_number - previous->ping_number > thresholds.ping_gap);
            const bool segment_break = step_m > thresholds.nav_gap_m
                                    || time_break || ping_break;
            if (segment_break) {
                ld.nav_track.push_back({kNaN, kNaN});
                if (time_break)
                    ++ld.nav_stats.time_gaps;
            } else {
                spacing_sum += step_m;
                ++spacing_n;
                ld.nav_stats.max_spacing_m =
                    std::max(ld.nav_stats.max_spacing_m, step_m);
            }
        }

        ld.nav_track.push_back({pose.nav.lon, pose.nav.lat});
        ld.lon_min = std::min(ld.lon_min, pose.nav.lon);
        ld.lon_max = std::max(ld.lon_max, pose.nav.lon);
        ld.lat_min = std::min(ld.lat_min, pose.nav.lat);
        ld.lat_max = std::max(ld.lat_max, pose.nav.lat);
        previous = &pose;
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
