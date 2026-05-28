// SbpProfileBuild.cpp — builds LayerMapData (kind=Profile) from loaded SBP traces.
//
// Called from a background thread (QtConcurrent::run) after
// ImportService::loadAllSubBottomTraces has delivered the full trace vector.
// No Qt widget access; pure data transformation.

#include "ui/features/map/subbottom/SbpProfileBuild.h"
#include "core/SpatialRef.h"
#include "geo/GeoUtils.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace dolphin::ui {

namespace {

constexpr float  kSoundHalfSpeed = 750.f;    // 1500 m/s ÷ 2
constexpr double kMaxSegGapM     = 5000.0;   // 5 km gap → NaN sentinel
constexpr double kPadGeo         = 0.001;    // ~110 m padding in degrees
constexpr double kPadProjM       = 110.0;    // metres padding for projected CRS

} // namespace

LayerMapData buildSbpProfileMapData(
    const std::vector<core::SubBottomTrace>& traces,
    const core::SpatialRef& source_crs,
    const core::SpatialRef& display_crs)
{
    LayerMapData ld;
    ld.kind         = LayerMapKind::Profile;
    ld.scalar_label = "bottom depth (m)";

    const float  kFNaN = std::numeric_limits<float>::quiet_NaN();
    const double kDNaN = std::numeric_limits<double>::quiet_NaN();

    const bool use_exact_crs = !source_crs.empty() && source_crs.exact;

    core::NavPoint prev_norm_pt;
    bool have_prev = false;

    float  depth_min    =  std::numeric_limits<float>::max();
    float  depth_max    = -std::numeric_limits<float>::max();
    double spacing_sum  = 0.0;
    size_t spacing_n    = 0;

    // TrackStats counters (reuse the existing struct for diagnostics compatibility).
    size_t n_fb_geo = 0, n_fb_proj = 0, n_exact = 0;

    for (const auto& trace : traces) {
        ++ld.track_stats.total_traces;

        // ── Nav validity ──────────────────────────────────────────────────────
        if (!trace.nav.valid || !geo::isFiniteCoordinate(trace.nav.lat, trace.nav.lon)) {
            ++ld.track_stats.invalid_nav;
            ld.nav_track.push_back({kDNaN, kDNaN});
            ld.trace_scalar.push_back(kFNaN);
            ld.trace_amplitude.push_back(0.f);
            continue;
        }
        if (trace.nav.lat == 0.0 && trace.nav.lon == 0.0) {
            ++ld.track_stats.zero_coords;
            ld.nav_track.push_back({kDNaN, kDNaN});
            ld.trace_scalar.push_back(kFNaN);
            ld.trace_amplitude.push_back(0.f);
            continue;
        }

        // ── CRS normalisation ─────────────────────────────────────────────────
        core::NavPoint nav = trace.nav;
        if (use_exact_crs) {
            nav.spatial_ref  = source_crs;
            nav.is_projected = core::spatialRefIsProjected(source_crs);
            ++n_exact;
        } else {
            nav.is_projected = core::spatialRefIsProjected(nav.spatial_ref)
                             || nav.is_projected;
            if (nav.is_projected) ++n_fb_proj; else ++n_fb_geo;
        }

        core::NavPoint norm;
        if (!geo::normalizeNavForMap(nav, display_crs, norm)) {
            ++ld.track_stats.invalid_nav;
            ld.nav_track.push_back({kDNaN, kDNaN});
            ld.trace_scalar.push_back(kFNaN);
            ld.trace_amplitude.push_back(0.f);
            continue;
        }

        if (norm.is_projected) ld.is_projected = true;

        // ── Repeated-fix filter ───────────────────────────────────────────────
        if (!ld.nav_track.empty()) {
            const QPointF& prev = ld.nav_track.back();
            if (!std::isnan(prev.x()) && prev.x() == norm.lon && prev.y() == norm.lat) {
                ++ld.track_stats.repeated_fixes;
                continue;
            }
        }

        // ── Gap detection ─────────────────────────────────────────────────────
        if (have_prev) {
            const double step_m = geo::navDistanceMetres(prev_norm_pt, norm);
            spacing_sum += step_m;
            ++spacing_n;
            ld.track_stats.max_spacing_m =
                std::max(ld.track_stats.max_spacing_m, step_m);
            if (step_m > kMaxSegGapM) {
                ld.nav_track.push_back({kDNaN, kDNaN});
                ld.trace_scalar.push_back(kFNaN);
                ld.trace_amplitude.push_back(0.f);
                ++ld.track_stats.large_jumps;
            }
        }

        ld.nav_track.push_back({norm.lon, norm.lat});
        ++ld.track_stats.track_points;

        if (!ld.track_stats.has_endpoints) {
            ld.track_stats.first_lon = norm.lon;
            ld.track_stats.first_lat = norm.lat;
            ld.track_stats.has_endpoints = true;
        }
        ld.track_stats.last_lon = norm.lon;
        ld.track_stats.last_lat = norm.lat;

        // ── Bottom depth scalar ───────────────────────────────────────────────
        float depth = kFNaN;
        if (trace.bottom_sample_idx >= 0 && trace.sample_rate_hz > 0.f) {
            depth = static_cast<float>(trace.bottom_sample_idx)
                        / trace.sample_rate_hz * kSoundHalfSpeed;
            depth_min = std::min(depth_min, depth);
            depth_max = std::max(depth_max, depth);
        }
        ld.trace_scalar.push_back(depth);

        // ── Bottom amplitude scalar ───────────────────────────────────────────
        float amp = 0.f;
        if (!trace.samples.empty()) {
            const int bidx = trace.bottom_sample_idx;
            if (bidx >= 0 && bidx < static_cast<int>(trace.samples.size())) {
                amp = std::abs(trace.samples[bidx]);
            } else {
                for (float s : trace.samples)
                    amp = std::max(amp, std::abs(s));
            }
        }
        ld.trace_amplitude.push_back(amp);

        // ── Bbox ──────────────────────────────────────────────────────────────
        ld.lon_min = std::min(ld.lon_min, norm.lon);
        ld.lon_max = std::max(ld.lon_max, norm.lon);
        ld.lat_min = std::min(ld.lat_min, norm.lat);
        ld.lat_max = std::max(ld.lat_max, norm.lat);

        prev_norm_pt = norm;
        have_prev    = true;
    }

    // ── Scalar range ──────────────────────────────────────────────────────────
    if (depth_min <= depth_max) {
        ld.scalar_min = depth_min;
        ld.scalar_max = depth_max;
    }

    // ── TrackStats ────────────────────────────────────────────────────────────
    if (spacing_n > 0)
        ld.track_stats.avg_spacing_m = spacing_sum / static_cast<double>(spacing_n);

    ld.track_stats.segment_count =
        ld.track_stats.track_points > 0 ? ld.track_stats.large_jumps + 1 : 0;
    ld.track_stats.is_projected = ld.is_projected;

    if (n_exact > 0) {
        const bool proj = core::spatialRefIsProjected(source_crs);
        ld.track_stats.crs_label = source_crs.exact
            ? (source_crs.id.empty() ? std::string{} : source_crs.id + " ")
                + (proj ? "projected exact" : "geographic exact")
            : (proj ? "inferred projected" : "inferred geographic");
    } else if (n_fb_proj > 0 && n_fb_geo == 0)
        ld.track_stats.crs_label = "per-entry projected";
    else if (n_fb_geo > 0 && n_fb_proj == 0)
        ld.track_stats.crs_label = "per-entry geographic";
    else if (n_fb_geo > 0 && n_fb_proj > 0)
        ld.track_stats.crs_label = "mixed per-entry (geo + proj)";
    else
        ld.track_stats.crs_label = "unknown";

    // ── Bbox padding ──────────────────────────────────────────────────────────
    if (ld.lon_min < ld.lon_max) {
        const double pad = ld.is_projected ? kPadProjM : kPadGeo;
        ld.lon_min -= pad;  ld.lon_max += pad;
        ld.lat_min -= pad;  ld.lat_max += pad;
    }

    return ld;
}

} // namespace dolphin::ui
