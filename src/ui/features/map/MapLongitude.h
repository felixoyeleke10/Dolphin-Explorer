#pragma once

// Longitude-branch helpers for map rendering.
//
// Geographic geometry is allowed to use an unwrapped longitude branch (for
// example 179.9..180.1) so surveys crossing the antimeridian remain local.
// Values are wrapped back to [-180, 180) only when they leave the map as user-
// visible coordinates or persisted annotations.

#include "ui/features/map/MapTypes.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace dolphin::ui::maplongitude {

struct Interval {
    double min = 0.0;
    double max = 0.0;
};

inline double canonical(double longitude_deg) noexcept
{
    if (!std::isfinite(longitude_deg))
        return longitude_deg;

    double wrapped = std::fmod(longitude_deg + 180.0, 360.0);
    if (wrapped < 0.0)
        wrapped += 360.0;
    return wrapped - 180.0;
}

inline double unwrapNear(double longitude_deg, double reference_deg) noexcept
{
    if (!std::isfinite(longitude_deg) || !std::isfinite(reference_deg))
        return longitude_deg;
    return reference_deg + std::remainder(longitude_deg - reference_deg, 360.0);
}

inline bool validBounds(const LayerMapData& data) noexcept
{
    return std::isfinite(data.lon_min) && std::isfinite(data.lon_max)
        && data.lon_min <= data.lon_max;
}

inline double boundsCenter(const LayerMapData& data) noexcept
{
    return data.lon_min + (data.lon_max - data.lon_min) * 0.5;
}

// Artifact-index extents are simple canonical min/max values. A local survey
// crossing the dateline consequently arrives as roughly [-180, +180]. Convert
// that representation to its short unwrapped interval before fitting the view.
inline Interval shortGeographicInterval(double lon_min, double lon_max) noexcept
{
    if (!std::isfinite(lon_min) || !std::isfinite(lon_max)
            || lon_min > lon_max || lon_max - lon_min <= 180.0) {
        return {lon_min, lon_max};
    }
    return {lon_max, lon_min + 360.0};
}

inline double branchShift(double longitude_deg, double reference_deg) noexcept
{
    if (!std::isfinite(longitude_deg) || !std::isfinite(reference_deg))
        return 0.0;
    const double aligned = unwrapNear(longitude_deg, reference_deg);
    return 360.0 * std::round((aligned - longitude_deg) / 360.0);
}

inline void shiftRange(double& lon_min, double& lon_max, double delta) noexcept
{
    if (std::isfinite(lon_min) && std::isfinite(lon_max) && lon_min <= lon_max) {
        lon_min += delta;
        lon_max += delta;
    }
}

// Shift every longitude-bearing member together. The preview image itself is
// unchanged: moving its bbox by a whole turn places the same pixels on the
// selected world branch. MapView stores this aligned copy and forwards it to
// MapView3D, keeping both renderers on exactly the same branch.
inline double alignLayerToReference(LayerMapData& data,
                                    double reference_deg) noexcept
{
    if (data.is_projected || !validBounds(data)
            || !std::isfinite(reference_deg)) {
        return 0.0;
    }

    const double delta = branchShift(boundsCenter(data), reference_deg);
    if (delta == 0.0)
        return 0.0;

    data.lon_min += delta;
    data.lon_max += delta;

    for (QPointF& point : data.nav_track)
        if (std::isfinite(point.x()))
            point.setX(point.x() + delta);

    for (SwathCoverage& coverage : data.coverage)
        for (auto& ribbon : coverage.ribbons)
            for (QPointF& point : ribbon)
                if (std::isfinite(point.x()))
                    point.setX(point.x() + delta);

    shiftRange(data.nav_stats.nav_lon_min,
               data.nav_stats.nav_lon_max, delta);
    shiftRange(data.nav_stats.strip_lon_min,
               data.nav_stats.strip_lon_max, delta);

    if (data.track_stats.has_endpoints) {
        data.track_stats.first_lon += delta;
        data.track_stats.last_lon  += delta;
    }

    return delta;
}

} // namespace dolphin::ui::maplongitude
