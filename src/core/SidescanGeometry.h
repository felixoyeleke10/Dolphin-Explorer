#pragma once

#include "core/SidescanPing.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <cstddef>

namespace dolphin::core {

inline constexpr float kMinimumAutomaticBottomConfidence = 0.5f;

inline std::optional<double> sidescanAltitudeMetres(const SidescanPing& ping)
{
    if (ping.bottom_pick.source > 0 && ping.bottom_pick.valid()
        && ping.bottom_pick.range_m > 0.0f)
        return static_cast<double>(ping.bottom_pick.range_m);
    if (std::isfinite(ping.nav.altitude_m) && ping.nav.altitude_m > 0.0f)
        return static_cast<double>(ping.nav.altitude_m);
    return std::nullopt;
}

// Geometry correction is deliberately stricter than QC/radiometry consumers:
// a weak automatic pick may still be useful for review but must not compress an
// entire swath. Manual picks remain authoritative.
inline std::optional<double> sidescanCorrectionAltitudeMetres(
    const SidescanPing& ping)
{
    const bool trusted_pick = ping.bottom_pick.source == 2
        || (ping.bottom_pick.source == 1
            && std::isfinite(ping.bottom_pick.confidence)
            && ping.bottom_pick.confidence >= kMinimumAutomaticBottomConfidence);
    if (trusted_pick && ping.bottom_pick.valid()
            && ping.bottom_pick.range_m > 0.0f)
        return static_cast<double>(ping.bottom_pick.range_m);
    if (std::isfinite(ping.nav.altitude_m) && ping.nav.altitude_m > 0.0f)
        return static_cast<double>(ping.nav.altitude_m);
    return std::nullopt;
}

inline std::optional<double> slantToGroundRangeMetres(double slant_m,
                                                       double altitude_m)
{
    if (!std::isfinite(slant_m) || !std::isfinite(altitude_m)
        || slant_m <= altitude_m || altitude_m <= 0.0)
        return std::nullopt;
    return std::sqrt((slant_m - altitude_m) * (slant_m + altitude_m));
}

inline std::optional<double> sidescanSampleRangeMetres(
    const SidescanPing& ping, size_t index)
{
    if (index >= ping.samples.size()) return std::nullopt;
    const double stored = ping.samples[index].range_m;
    if (std::isfinite(stored) && stored > 0.0) return stored;
    if (ping.samples.size() < 2 || !std::isfinite(ping.slant_range_m)
            || !std::isfinite(ping.blanking_m))
        return std::nullopt;
    const double blanking = std::max(0.0, static_cast<double>(ping.blanking_m));
    const double far_range = ping.slant_range_m;
    if (!(far_range > blanking)) return std::nullopt;
    return blanking + (far_range - blanking) * static_cast<double>(index)
        / static_cast<double>(ping.samples.size() - 1);
}

// ARC compensates the angular response using sin(grazing)=altitude/slant.
// Returns no value for water-column/invalid geometry; the caller must not mark
// ARC provenance unless at least one sample receives a factor.
inline std::optional<double> angleRangeGainFactor(
    double slant_range_m, double altitude_m, double exponent,
    double gain_cap_db)
{
    if (!std::isfinite(slant_range_m) || !std::isfinite(altitude_m)
            || !std::isfinite(exponent) || !std::isfinite(gain_cap_db)
            || !(slant_range_m > altitude_m) || !(altitude_m > 0.0)
            || exponent < 0.0 || gain_cap_db < 0.0)
        return std::nullopt;
    const double sin_grazing = std::clamp(altitude_m / slant_range_m, 0.01, 1.0);
    const double requested = 1.0 / std::pow(sin_grazing, exponent);
    const double cap = std::pow(10.0, gain_cap_db / 20.0);
    return std::min(requested, cap);
}

} // namespace dolphin::core
