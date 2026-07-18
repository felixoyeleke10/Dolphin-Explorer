#pragma once

// Shared continuity policy for map-side sidescan geometry.
//
// Low/Medium previews intentionally retain a regular subset of source pings, so
// native fixed limits (for example 5 seconds or 20 ping numbers) are not valid
// on the decoded preview sequence. Derive limits from its robust cadence while
// retaining conservative floors for short inputs and absolute survey-break
// ceilings for pathological gaps.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace dolphin::ui::ssscontinuity {

inline constexpr double kNavGapFloorM       = 10.0;
inline constexpr double kNavGapFallbackM    = 50.0;
inline constexpr double kNavGapCeilingM     = 5'000.0;
inline constexpr double kTimeGapFloorUs     = 5'000'000.0;
inline constexpr double kTimeGapCeilingUs   = 300'000'000.0;
inline constexpr double kPingGapFloor       = 20.0;
inline constexpr double kPingGapCeiling     = 5'000.0;
inline constexpr double kCadenceMultiplier  = 10.0;
inline constexpr size_t kMinCadenceSamples  = 3;

struct Thresholds {
    double   nav_gap_m   = kNavGapFloorM;
    int64_t  time_gap_us = static_cast<int64_t>(kTimeGapFloorUs);
    uint32_t ping_gap    = static_cast<uint32_t>(kPingGapFloor);
};

inline double threshold(std::vector<double> deltas,
                        double floor,
                        double ceiling,
                        double short_input_fallback)
{
    deltas.erase(std::remove_if(deltas.begin(), deltas.end(),
        [](double value) { return !std::isfinite(value) || value <= 0.0; }),
        deltas.end());
    if (deltas.size() < kMinCadenceSamples)
        return short_input_fallback;

    // Learn the nominal retained cadence from the lower cluster, not the
    // median of all deltas. Multiple short survey lines can contain as many
    // inter-line gaps as in-line steps; a plain median then learns the breaks
    // and reconnects them. The lower quartile remains representative for a
    // consistently thinned line while resisting those large-gap observations.
    const size_t nominal_index = (deltas.size() - 1) / 4;
    std::nth_element(deltas.begin(), deltas.begin() + nominal_index, deltas.end());
    return std::clamp(
        deltas[nominal_index] * kCadenceMultiplier, floor, ceiling);
}

inline Thresholds fromDeltas(std::vector<double> nav_deltas,
                             std::vector<double> time_deltas = {},
                             std::vector<double> ping_deltas = {})
{
    Thresholds out;
    out.nav_gap_m = threshold(
        std::move(nav_deltas), kNavGapFloorM, kNavGapCeilingM,
        kNavGapFallbackM);
    out.time_gap_us = static_cast<int64_t>(std::llround(threshold(
        std::move(time_deltas), kTimeGapFloorUs, kTimeGapCeilingUs,
        kTimeGapFloorUs)));
    out.ping_gap = static_cast<uint32_t>(std::llround(threshold(
        std::move(ping_deltas), kPingGapFloor, kPingGapCeiling,
        kPingGapFloor)));
    return out;
}

} // namespace dolphin::ui::ssscontinuity
