#pragma once

#include <cstdint>
#include <string>

namespace dolphin::core {

enum class SpatialRefKind : uint8_t {
    Unknown = 0,
    Geographic,
    Projected,
    Local,
};

struct SpatialRef {
    std::string   id;
    SpatialRefKind kind  = SpatialRefKind::Unknown;
    bool          exact = false;

    bool empty() const { return id.empty() && kind == SpatialRefKind::Unknown; }
};

inline bool spatialRefIsProjected(const SpatialRef& ref)
{
    return ref.kind == SpatialRefKind::Projected
        || ref.kind == SpatialRefKind::Local;
}

inline bool spatialRefIsGeographic(const SpatialRef& ref)
{
    return ref.kind == SpatialRefKind::Geographic;
}

inline SpatialRef makeWgs84SpatialRef()
{
    SpatialRef ref;
    ref.id = "EPSG:4326";
    ref.kind = SpatialRefKind::Geographic;
    ref.exact = true;
    return ref;
}

inline SpatialRef makeUnknownProjectedSpatialRef(const std::string& id = "PROJECTED:UNKNOWN")
{
    SpatialRef ref;
    ref.id = id;
    ref.kind = SpatialRefKind::Projected;
    ref.exact = false;
    return ref;
}

inline SpatialRef makePseudoWgs84SpatialRef()
{
    SpatialRef ref;
    ref.id = "DISPLAY:PSEUDO_WGS84";
    ref.kind = SpatialRefKind::Geographic;
    ref.exact = false;
    return ref;
}

} // namespace dolphin::core
