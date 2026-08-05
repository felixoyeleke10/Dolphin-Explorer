// NavCorrection.cpp — the single sidescan navigation correction (see header).
// Lifted out of WaterfallView::runNavCorrections so the waterfall and the SSS map
// apply the identical correction. Layback + smoothing reuse the canonical pipeline
// nodes; attitude offsets are applied directly.
#include "app/display/NavCorrection.h"

#include "pipeline/nodes/correction/GeoCorrectNode.h"
#include "pipeline/nodes/correction/NavSmoothNode.h"
#include "geo/GeoUtils.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <variant>

namespace dolphin::ui {

std::vector<core::SidescanPing>
applySidescanNavCorrections(std::vector<core::SidescanPing> pings,
                            const NavProcessingParams&       params)
{
    const bool do_layback = params.layback_enabled && params.layback_m > 0.f;
    const bool do_smooth  = params.smooth_enabled  && params.smooth_window > 1;
    const bool do_hdg     = params.heading_offset_deg != 0.f;
    const bool do_pitch   = params.pitch_offset_deg   != 0.f;
    const bool do_roll    = params.roll_offset_deg    != 0.f;
    // Fast path: nothing to do — skip the ArtifactBuffer round-trip entirely.
    if (!do_layback && !do_smooth && !do_hdg && !do_pitch && !do_roll)
        return pings;

    if (do_layback || do_smooth) {
        pipeline::ArtifactBuffer buf;
        buf.reserve(pings.size());
        for (const auto& ping : pings)
            buf.emplace_back(ping);

        if (do_layback) {
            pipeline::GeoCorrectNode node;
            pipeline::NodeParams     np;
            np["layback_m"] = params.layback_m;
            buf = node.process(buf, np);
        }
        if (do_smooth) {
            pipeline::NavSmoothNode node;
            pipeline::NodeParams    np;
            np["window_pings"] = params.smooth_window;
            buf = node.process(buf, np);
        }

        pings.clear();
        pings.reserve(buf.size());
        for (auto& a : buf)
            if (auto* p = std::get_if<core::SidescanPing>(&a))
                pings.push_back(std::move(*p));
    }

    if (do_hdg || do_pitch || do_roll) {
        auto offsetHeading = [offset = params.heading_offset_deg](float& heading) {
            if (!std::isfinite(heading) || heading == 0.f) return;
            heading = std::fmod(heading + offset, 360.f);
            if (heading < 0.f) heading += 360.f;
        };
        for (auto& ping : pings) {
            if (do_hdg) {
                offsetHeading(ping.nav.heading_deg);
                offsetHeading(ping.nav.sensor_heading_deg);
                offsetHeading(ping.nav.ship_heading_deg);
            }
            if (do_pitch) ping.nav.pitch_deg   += params.pitch_offset_deg;
            if (do_roll)  ping.nav.roll_deg    += params.roll_offset_deg;
        }
    }

    return pings;
}

// -- Sub-bottom (traces) — pure-geo, gap-aware (moved from SbpNavCorrection) ----

namespace {

constexpr int64_t kMaxTimestampGapUs = 5'000'000;  // 5 s — don't smooth across line breaks
constexpr double  kSbpDegToRad       = 3.14159265358979323846 / 180.0;

// Travel bearing (rad, CW from north) for trace i: prefer an explicit heading
// field, else course-over-ground from the nearest valid neighbour.
bool sbpTravelBearing(const std::vector<core::SubBottomTrace>& traces,
                      size_t i, double& out_rad)
{
    const core::NavPoint& nav = traces[i].nav;
    if (nav.heading_deg != 0.f)        { out_rad = nav.heading_deg * kSbpDegToRad;        return true; }
    if (nav.sensor_heading_deg != 0.f) { out_rad = nav.sensor_heading_deg * kSbpDegToRad; return true; }
    if (nav.ship_heading_deg != 0.f)   { out_rad = nav.ship_heading_deg * kSbpDegToRad;   return true; }

    const size_t n = traces.size();
    if (i + 1 < n && traces[i + 1].nav.valid) {
        out_rad = geo::headingFromNavDeltaRad(nav, traces[i + 1].nav);
        return true;
    }
    if (i > 0 && traces[i - 1].nav.valid) {
        out_rad = geo::headingFromNavDeltaRad(traces[i - 1].nav, nav);
        return true;
    }
    return false;
}

} // namespace

void applySbpNavCorrections(std::vector<core::SubBottomTrace>& traces,
                            const NavProcessingParams& params)
{
    const size_t n = traces.size();
    if (n == 0) return;

    const bool do_smooth  = params.smooth_enabled && params.smooth_window > 0;
    const bool do_layback = params.layback_enabled && params.layback_m != 0.f;
    const bool do_heading = params.heading_offset_deg != 0.f;
    if (!do_smooth && !do_layback && !do_heading) return;

    // 1. GPS smoothing: gap-aware moving average of positions.
    if (do_smooth && n >= 2) {
        const int window = std::max(1, params.smooth_window);
        const int left = (window - 1) / 2;
        const int right = window / 2;
        std::vector<core::NavPoint> smoothed(n);
        for (size_t i = 0; i < n; ++i) smoothed[i] = traces[i].nav;

        for (size_t i = 0; i < n; ++i) {
            if (!traces[i].nav.valid) continue;
            double sum_lat = 0.0, sum_lon = 0.0;
            int    count   = 0;
            const int lo = static_cast<int>(i) - left;
            const int hi = static_cast<int>(i) + right;
            for (int j = lo; j <= hi; ++j) {
                if (j < 0 || static_cast<size_t>(j) >= n) continue;
                if (!traces[static_cast<size_t>(j)].nav.valid) continue;
                if (std::llabs(traces[static_cast<size_t>(j)].timestamp_us
                               - traces[i].timestamp_us) > kMaxTimestampGapUs)
                    continue;
                const auto& candidate = traces[static_cast<size_t>(j)].nav;
                sum_lat += candidate.lat;
                if (traces[i].nav.is_projected) {
                    sum_lon += candidate.lon;
                } else {
                    sum_lon += traces[i].nav.lon
                             + std::remainder(candidate.lon - traces[i].nav.lon, 360.0);
                }
                ++count;
            }
            if (count > 1) {
                smoothed[i].lat = sum_lat / count;
                smoothed[i].lon = sum_lon / count;
                if (!traces[i].nav.is_projected)
                    smoothed[i].lon = std::remainder(smoothed[i].lon, 360.0);
            }
        }
        for (size_t i = 0; i < n; ++i) traces[i].nav = smoothed[i];
    }

    // 2. Constant heading offset (before layback so layback uses corrected heading).
    if (do_heading) {
        const float off = params.heading_offset_deg;
        auto wrap = [](float deg) {
            float w = std::fmod(deg, 360.f);
            if (w < 0.f) w += 360.f;
            return w;
        };
        for (auto& t : traces) {
            if (t.nav.heading_deg != 0.f)
                t.nav.heading_deg = wrap(t.nav.heading_deg + off);
            if (t.nav.sensor_heading_deg != 0.f)
                t.nav.sensor_heading_deg = wrap(t.nav.sensor_heading_deg + off);
            if (t.nav.ship_heading_deg != 0.f)
                t.nav.ship_heading_deg = wrap(t.nav.ship_heading_deg + off);
        }
    }

    // 3. Layback: shift each position backward along the track.
    if (do_layback) {
        std::vector<core::NavPoint> shifted(n);
        for (size_t i = 0; i < n; ++i) shifted[i] = traces[i].nav;

        for (size_t i = 0; i < n; ++i) {
            const core::NavPoint& nav = traces[i].nav;
            if (!nav.valid) continue;

            double bearing_rad = 0.0;
            if (!sbpTravelBearing(traces, i, bearing_rad)) continue;

            const double back    = -static_cast<double>(params.layback_m);
            const double east_m  = back * std::sin(bearing_rad);
            const double north_m = back * std::cos(bearing_rad);

            double out_lon = nav.lon, out_lat = nav.lat;
            if (geo::offsetNavByGroundMetres(nav, east_m, north_m, out_lon, out_lat)) {
                shifted[i].lon = out_lon;
                shifted[i].lat = out_lat;
            }
        }
        for (size_t i = 0; i < n; ++i) traces[i].nav = shifted[i];
    }
}

} // namespace dolphin::ui
