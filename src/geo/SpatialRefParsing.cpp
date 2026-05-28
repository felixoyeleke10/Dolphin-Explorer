#include "geo/GeoUtils.h"

#include <cctype>
#include <string>

namespace dolphin::geo {

namespace {

static std::string upperTrimmed(std::string_view text)
{
    size_t start = 0;
    size_t end = text.size();
    while (start < end && std::isspace(static_cast<unsigned char>(text[start]))) ++start;
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;

    std::string out;
    out.reserve(end - start);
    for (size_t i = start; i < end; ++i)
        out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(text[i]))));
    return out;
}

} // namespace

std::string spatialRefKindToString(core::SpatialRefKind kind)
{
    switch (kind) {
    case core::SpatialRefKind::Geographic: return "geographic";
    case core::SpatialRefKind::Projected:  return "projected";
    case core::SpatialRefKind::Local:      return "local";
    default:                               return "unknown";
    }
}

core::SpatialRefKind spatialRefKindFromString(std::string_view text)
{
    const std::string norm = upperTrimmed(text);
    if (norm == "GEOGRAPHIC") return core::SpatialRefKind::Geographic;
    if (norm == "PROJECTED")  return core::SpatialRefKind::Projected;
    if (norm == "LOCAL")      return core::SpatialRefKind::Local;
    return core::SpatialRefKind::Unknown;
}

core::SpatialRef spatialRefFromLegacy(bool is_projected)
{
    return is_projected ? core::makeUnknownProjectedSpatialRef()
                        : core::makeWgs84SpatialRef();
}

} // namespace dolphin::geo
