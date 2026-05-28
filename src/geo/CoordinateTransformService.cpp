#include "geo/CoordinateTransformService.h"
#include "geo/GeoUtils.h"
#include "core/SpatialRef.h"

namespace dolphin::geo {

bool CoordinateTransformService::canTransform(const core::SpatialRef& src,
                                              const core::SpatialRef& dst) const
{
    if (src.empty() || dst.empty()) return false;
    if (!src.id.empty() && src.id == dst.id) return true;

    const bool src_geo = core::spatialRefIsGeographic(src);
    const bool dst_geo = core::spatialRefIsGeographic(dst);

    if (src_geo && dst_geo) return true;
    if (src_geo && isTransformableCrs(dst)) return true;
    if (dst_geo && isTransformableCrs(src)) return true;
    return false;
}

bool CoordinateTransformService::canTransformToWgs84(const core::SpatialRef& src) const
{
    return isTransformableCrs(src);
}

} // namespace dolphin::geo
