#pragma once
#include "core/SpatialRef.h"

namespace dolphin::geo {

// Capability check for CRS transforms.
// Answers which CRS pairs normalizeNavForMap actually supports.
// Stateless and thread-safe — all methods are const.
//
// Supported transforms (to/from WGS84 or any geographic CRS):
//   - identity (src.id == dst.id, or both geographic)
//   - WGS84 UTM zones (EPSG:32601–32760)
//   - ETRS89 UTM North (EPSG:25827–25838)
//   - NAD83 UTM North (EPSG:26901–26923)
//   - GDA94 MGA (EPSG:28349–28356) — GRS80 ≈ WGS84
//   - GDA2020 MGA (EPSG:7849–7856)
//   - ED50 / UTM North (EPSG:23028–23038) — approx. WGS84 (~100-200 m offset)
//   - UTM:<zone>N/S string format
//
// Everything else (BNG, RD New, NZTM, state plane, Web Mercator, …) is
// Unsupported. Call canTransform() before normalizing to decide whether to
// warn the user via the Problems panel.
class CoordinateTransformService {
public:
    // True if normalizeNavForMap can transform the src→dst pair.
    bool canTransform(const core::SpatialRef& src,
                      const core::SpatialRef& dst) const;

    // Convenience: can we transform src to WGS84 (the usual display CRS)?
    bool canTransformToWgs84(const core::SpatialRef& src) const;
};

} // namespace dolphin::geo
