#include "geo/CoordinateTransformService.h"
#include "geo/GeoUtils.h"
#include "core/SpatialRef.h"

namespace dolphin::geo {

bool CoordinateTransformService::canTransform(const core::SpatialRef& src,
                                              const core::SpatialRef& dst) const
{
    if (src.empty() || dst.empty()) return false;
    if (!src.id.empty() && src.id == dst.id) return true;

    return isTransformableCrs(src) && isTransformableCrs(dst);
}

bool CoordinateTransformService::canTransformToWgs84(const core::SpatialRef& src) const
{
    return isTransformableCrs(src);
}

} // namespace dolphin::geo
