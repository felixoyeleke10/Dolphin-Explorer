#pragma once

#include "core/SpatialRef.h"

namespace dolphin::geo {

// Stateless capability facade backed by PROJ's CRS database. Coordinate
// operations themselves are cached per thread by GeoUtils.
class CoordinateTransformService {
public:
    bool canTransform(const core::SpatialRef& src,
                      const core::SpatialRef& dst) const;
    bool canTransformToWgs84(const core::SpatialRef& src) const;
};

} // namespace dolphin::geo
