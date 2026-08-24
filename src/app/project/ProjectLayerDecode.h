#pragma once
#include "app/project/Project.h"
#include "util/Json.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>

namespace dolphin::app::detail {

inline Modality modalityFromString(const std::string& value)
{
    if (value == "sidescan") return Modality::Sidescan;
    if (value == "subbottom") return Modality::SubBottom;
    if (value == "magnetometer") return Modality::Magnetometer;
    if (value == "multibeam") return Modality::Multibeam;
    if (value == "raster") return Modality::Raster;
    if (value == "mixed") return Modality::Mixed;
    return Modality::Unknown;
}

inline const ProjectSource* findSourceById(
    const std::vector<ProjectSource>& sources, const std::string& source_id)
{
    const auto found = std::find_if(sources.begin(), sources.end(),
        [&](const ProjectSource& source) { return source.id == source_id; });
    return found != sources.end() ? &*found : nullptr;
}

template <typename UInt>
bool readExactUnsigned(const util::JsonValue& node, UInt& result,
                       bool require_nonzero = false)
{
    static_assert(std::is_unsigned_v<UInt>);
    if (!node.isNumber()) return false;
    const double value = node.asDouble();
    const double upper = std::ldexp(1.0, std::numeric_limits<UInt>::digits);
    if (!std::isfinite(value) || value < 0.0 || value >= upper
            || std::trunc(value) != value
            || (require_nonzero && value == 0.0))
        return false;
    result = static_cast<UInt>(value);
    return true;
}

inline bool readExactInt64(const util::JsonValue& node, int64_t& result)
{
    if (!node.isNumber()) return false;
    const double value = node.asDouble();
    const double upper = std::ldexp(1.0, std::numeric_limits<int64_t>::digits);
    if (!std::isfinite(value) || value < -upper || value >= upper
            || std::trunc(value) != value)
        return false;
    result = static_cast<int64_t>(value);
    return true;
}

} // namespace dolphin::app::detail
