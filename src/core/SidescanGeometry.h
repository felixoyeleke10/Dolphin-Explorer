#pragma once

#include "core/SidescanPing.h"

#include <cmath>
#include <optional>

namespace dolphin::core {

inline std::optional<double> sidescanAltitudeMetres(const SidescanPing& ping)
{
    if (ping.bottom_pick.source > 0 && ping.bottom_pick.valid()
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

} // namespace dolphin::core
