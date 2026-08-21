#pragma once

#include "core/SidescanPing.h"
#include "core/SidescanGeometry.h"
#include "ui/features/map/sidescan/SssGeorefParams.h"

#include <algorithm>
#include <cmath>

namespace dolphin::ui {

inline bool sssHasBakedGroundRanges(const core::SidescanPing& ping)
{
    return core::hasCorrectionFlag(
        ping.correction_flags, core::CorrectionFlag::SlantRange);
}

// A processed ping flag is authoritative. Layer metadata requests the same
// presentation for unbaked data only when that ping has an altitude reference;
// without one SRC cannot actually run and must not silently hide raw imagery.
inline bool sssCorrectionPresented(const core::SidescanPing& ping,
                                   const SssGeorefParams& params)
{
    if (sssHasBakedGroundRanges(ping))
        return true;
    return params.slant_range_corrected
        && core::sidescanAltitudeMetres(ping).has_value();
}

inline double sssUncorrectedAltitudeMetres(const core::SidescanPing& ping)
{
    return core::sidescanAltitudeMetres(ping).value_or(0.0);
}

// Return the outer edge of the actual sample product. Baked pings carry ground
// ranges in each sample, so their farthest valid bin is authoritative. The
// Unbaked pings retain the bottom-pick-preferred map geometry.
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
        return false;
    }

    const double slant_m = ping.slant_range_m > 0.0f
        ? static_cast<double>(ping.slant_range_m)
        : 75.0;
    const double altitude_m = sssUncorrectedAltitudeMetres(ping);
    if (altitude_m <= 0.0) {
        ground_m = slant_m;
        return true;
    }
    const auto corrected = core::slantToGroundRangeMetres(slant_m, altitude_m);
    if (!corrected) return false;
    ground_m = *corrected;
    return true;
}

// Canonical inner edge for both coverage and amplitude geometry. An applied
// slant-range correction closes the swath at the track. Raw-slant display keeps
// the configured water-column/nadir gap open; neither path may paint a different
// footprint from the other.
inline double sssInnerGapMetres(const core::SidescanPing& ping,
                                const SssGeorefParams& params)
{
    if (sssCorrectionPresented(ping, params) || params.show_nadir)
        return 0.0;
    const double altitude_m = sssUncorrectedAltitudeMetres(ping);
    const double slant_m = ping.slant_range_m > 0.0f
        ? static_cast<double>(ping.slant_range_m)
        : 75.0;
    return altitude_m > 1.0 ? altitude_m : slant_m * 0.10;
}

} // namespace dolphin::ui
