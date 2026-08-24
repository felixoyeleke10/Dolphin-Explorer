#include "ui/features/map/sidescan/SidescanSwathGeoreferencer.h"
#include "ui/features/map/sidescan/SssGeometryPolicy.h"
#include "geo/GeoUtils.h"

#include <algorithm>
#include <array>
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

    // -- Build corrected nav table ---------------------------------------------
    // Resolves heading (COG/EMA/backward-fill), position (fish/vessel/layback),
    // and applies smoothing.  Coverage building calls the same function so both
    // outputs share identical per-ping position and heading.
    const std::vector<CorrectedSssNav> cnav =
        buildCorrectedNavTable(raw_pings, order, params, &result.heading_stats);

    // Do not infer the coordinate frame from the first raw record: it may be a
    // missing-nav placeholder. Use the first resolved position that can actually
    // contribute geometry.
    for (const auto& cn : cnav) {
        if (cn.valid) {
            result.is_projected = cn.is_projected;
            break;
        }
    }

    // -- Georeference samples for each ping ------------------------------------
    result.strips.reserve(raw_pings.size());
    std::array<uint64_t, 2> channel_segments{};

    for (size_t pi = 0; pi < raw_pings.size(); ++pi) {
        const auto& ping = raw_pings[order[pi]];
        const auto& cn   = cnav[pi];
        uint64_t& continuity_segment = channel_segments[
            ping.channel == core::SidescanChannel::Port ? 0u : 1u];

        if (core::hasQcFlag(ping.qc_flags, core::QcFlag::Rejected)) {
            ++continuity_segment;
            continue;
        }

        // A bounded NoNav run can now have a trustworthy timestamp-interpolated
        // corrected position. Keep that repaired geometry; continue to reject a
        // raw NoNav record when no bounded repair was made.
        if (core::hasQcFlag(ping.qc_flags, core::QcFlag::NoNav)
                && (cn.flags & kNavFlagInterpolated) == 0) {
            ++result.skipped_no_position;
            ++continuity_segment;
            continue;
        }

        if (!cn.valid) {
            ++result.skipped_no_position;
            ++continuity_segment;
            continue;
        }
        if (!cn.heading_valid) {
            ++result.skipped_no_heading;
            ++continuity_segment;
            continue;
        }
        if (ping.samples.empty()) {
            ++result.skipped_no_samples;
            ++continuity_segment;
            continue;
        }

        const double heading_rad = cn.heading_rad;

        const bool is_port = params.swap_port_starboard
            ? (ping.channel != core::SidescanChannel::Port)
            : (ping.channel == core::SidescanChannel::Port);
        const double side_rad = is_port
            ? heading_rad - std::numbers::pi * 0.5
            : heading_rad + std::numbers::pi * 0.5;
        const double sin_side = std::sin(side_rad);
        const double cos_side = std::cos(side_rad);

        // Prefer user/auto-detected bottom pick over raw nav altitude — the pick
        // is more accurate and is what seabed correction was applied to in the viewer.
        const bool ranges_baked = sssHasBakedGroundRanges(ping);
        const bool correction_presented = sssCorrectionPresented(ping, params);
        const double altitude_m = sssUncorrectedAltitudeMetres(ping);
        const double min_range_m = ping.blanking_m > 0.f
            ? static_cast<double>(ping.blanking_m)
            : kDefaultBlankingM;
        const double total_range_m = ping.slant_range_m > 0.f
            ? static_cast<double>(ping.slant_range_m)
            : kFallbackRangeM;
        const double inner_gap_m = sssInnerGapMetres(ping, params);

        const size_t n    = ping.samples.size();
        const size_t step = std::max<size_t>(1, n / kMaxSamplesPerStrip);

        SssStrip strip;
        strip.channel      = ping.channel;
        strip.timestamp_us = ping.timestamp_us;
        strip.ping_number  = ping.ping_number;
        strip.nav_lon      = cn.lon;   // corrected position (fish/vessel/layback)
        strip.nav_lat      = cn.lat;
        strip.continuity_segment = continuity_segment;
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
            if (!std::isfinite(raw_range_m) || raw_range_m < 0.0) continue;

            double slant_m;
            if (!ranges_baked && raw_range_m == 0.0 && n > 1) {
                slant_m = total_range_m * static_cast<double>(i)
                                        / static_cast<double>(n - 1);
            } else {
                slant_m = raw_range_m;
            }
            if (slant_m < min_range_m) continue;

            double ground_m;
            if (ranges_baked) {
                // SlantRangeNode has already replaced every sample range with
                // ground range. Applying the altitude transform again shrinks
                // processed swaths. The per-ping correction flag is authoritative:
                // the UI may close the nadir without baking sample geometry.
                ground_m = slant_m;
            } else if (correction_presented && altitude_m > 0.0) {
                const auto converted = core::convertSidescanRange(
                    {slant_m, core::SidescanRangeDomain::Slant},
                    core::SidescanRangeDomain::Ground, altitude_m);
                if (!converted) continue;
                ground_m = converted->metres;
            } else {
                ground_m = slant_m;
            }
            if (!std::isfinite(ground_m) || ground_m < inner_gap_m)
                continue;

            const double east_m  = ground_m * sin_side;
            const double north_m = ground_m * cos_side;

            double out_lon = 0.0, out_lat = 0.0;
            if (!geo::offsetNavByGroundMetres(pos_nav, east_m, north_m, out_lon, out_lat))
                continue;

            strip.points.push_back(
                {out_lon, out_lat, ping.samples[i].amplitude,
                 static_cast<float>(ground_m)});
        }

        if (!strip.points.empty()) {
            // Vendor sample arrays are normally near-to-far, but malformed or
            // merged records can contain inversions/duplicate range knots. Sort
            // by the physical coordinate and coalesce duplicates so downstream
            // interpolation never creates zero-width or folded cells.
            std::stable_sort(strip.points.begin(), strip.points.end(),
                [](const SssPoint& a, const SssPoint& b) {
                    return a.ground_range_m < b.ground_range_m;
                });
            std::vector<SssPoint> coalesced;
            coalesced.reserve(strip.points.size());
            constexpr double kRangeEpsilonM = 1e-6;
            for (const auto& point : strip.points) {
                if (coalesced.empty()
                        || point.ground_range_m
                            > coalesced.back().ground_range_m + kRangeEpsilonM) {
                    coalesced.push_back(point);
                } else {
                    coalesced.back() = point;
                }
            }
            strip.points = std::move(coalesced);

            // Add the canonical inner sample-bin edge. For corrected geometry it
            // is the track centre; for raw-slant display it is the same open
            // water-column edge used by coverage ribbons.
            double inner_lon = cn.lon;
            double inner_lat = cn.lat;
            if (inner_gap_m > 0.0
                    && !geo::offsetNavByGroundMetres(
                        pos_nav, inner_gap_m * sin_side, inner_gap_m * cos_side,
                        inner_lon, inner_lat)) {
                ++continuity_segment;
                continue;
            }
            if (strip.points.front().ground_range_m
                    <= inner_gap_m + kRangeEpsilonM) {
                strip.points.front().lon = inner_lon;
                strip.points.front().lat = inner_lat;
                strip.points.front().ground_range_m =
                    static_cast<float>(inner_gap_m);
            } else {
                const uint16_t inner_amp = strip.points.front().amplitude;
                strip.points.insert(strip.points.begin(),
                    {inner_lon, inner_lat, inner_amp,
                     static_cast<float>(inner_gap_m),
                     !correction_presented});
            }

            // The bottom pick maps to ground range zero by definition. Its
            // specular return must not become a bright artificial track line,
            // but making the anchor non-renderable drops the entire first cell
            // between adjacent pings and opens a visible corridor. Keep the
            // geometry closed and replace the bottom-band amplitude with the
            // first trustworthy seabed bin instead.
            if (correction_presented && strip.points.size() >= 4
                    && strip.points.front().ground_range_m <= kRangeEpsilonM) {
                const double first_positive = strip.points[1].ground_range_m;
                const double second_positive = strip.points[2].ground_range_m;
                if (std::isfinite(first_positive)
                        && std::isfinite(second_positive)
                        && first_positive > kRangeEpsilonM
                        && second_positive > first_positive) {
                    const double bottom_band_limit = first_positive
                        + 0.5 * (second_positive - first_positive);
                    size_t trusted_i = 1;
                    while (trusted_i + 2 < strip.points.size()
                           && strip.points[trusted_i].ground_range_m
                                <= bottom_band_limit)
                        ++trusted_i;
                    const uint16_t seabed_amplitude =
                        strip.points[trusted_i].amplitude;
                    for (size_t point_i = 0; point_i < trusted_i; ++point_i) {
                        strip.points[point_i].amplitude = seabed_amplitude;
                        strip.points[point_i].renderable = true;
                    }
                }
            } else if (correction_presented && strip.points.size() >= 2
                       && strip.points.front().ground_range_m
                            <= kRangeEpsilonM) {
                strip.points.front().amplitude = strip.points[1].amplitude;
                strip.points.front().renderable = true;
            }
            result.strips.push_back(std::move(strip));
        } else {
            ++continuity_segment;
        }
    }

    return result;
}

} // namespace dolphin::ui
