#pragma once

#include "core/SidescanPing.h"
#include "ui/features/map/sidescan/SssGeorefParams.h"

#include <algorithm>
#include <cmath>

namespace dolphin::ui {

inline bool sssHasBakedGroundRanges(const core::SidescanPing& ping)
{
    return core::hasCorrectionFlag(
        ping.correction_flags, core::CorrectionFlag::SlantRange);
}

inline double sssUncorrectedAltitudeMetres(const core::SidescanPing& ping)
{
    return (ping.bottom_pick.valid() && ping.bottom_pick.source > 0)
        ? static_cast<double>(ping.bottom_pick.range_m)
        : std::max(0.0, static_cast<double>(ping.nav.altitude_m));
}

// Return the outer edge of the actual sample product. Baked pings carry ground
// ranges in each sample, so their farthest valid bin is authoritative. The
// metadata fallback mirrors SlantRangeNode exactly (nav altitude, or 1 m when
// unavailable). Unbaked pings retain the bottom-pick-preferred map geometry.
inline bool sssOuterGroundRangeMetres(const core::SidescanPing& ping,
                                      double& ground_m)
{
    const bool baked = sssHasBakedGroundRanges(ping);
    if (baked) {
        ground_m = 0.0;
        for (const auto& sample : ping.samples) {
            const double range_m = static_cast<double>(sample.range_m);
            if (std::isfinite(range_m) && range_m >= 0.0)
                ground_m = std::max(ground_m, range_m);
        }
        if (ground_m > 0.0)
            return true;
    }

    const double slant_m = ping.slant_range_m > 0.0f
        ? static_cast<double>(ping.slant_range_m)
        : 75.0;
    const double altitude_m = baked
        ? (ping.nav.altitude_m > 0.0f
            ? static_cast<double>(ping.nav.altitude_m) : 1.0)
        : sssUncorrectedAltitudeMetres(ping);
    if (altitude_m <= 0.0) {
        ground_m = slant_m;
        return true;
    }
    if (slant_m <= altitude_m)
        return false;
    ground_m = std::sqrt(slant_m * slant_m - altitude_m * altitude_m);
    return std::isfinite(ground_m);
}

// Canonical inner edge for both coverage and amplitude geometry. An applied
// slant-range correction closes the swath at the track. Raw-slant display keeps
// the configured water-column/nadir gap open; neither path may paint a different
// footprint from the other.
inline double sssInnerGapMetres(const core::SidescanPing& ping,
                                const SssGeorefParams& params)
{
    if (params.slant_range_corrected)
        return 0.0;
    const double altitude_m = sssUncorrectedAltitudeMetres(ping);
    const double slant_m = ping.slant_range_m > 0.0f
        ? static_cast<double>(ping.slant_range_m)
        : 75.0;
    return altitude_m > 1.0 ? altitude_m : slant_m * 0.10;
}

} // namespace dolphin::ui
