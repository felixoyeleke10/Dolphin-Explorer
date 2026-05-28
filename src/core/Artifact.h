#pragma once
#include "core/SidescanPing.h"
#include "core/SubBottomTrace.h"
#include "core/MagSample.h"
#include "core/MultibeamPing.h"
#include "core/RasterGrid.h"
#include <variant>
#include <cstdint>

namespace dolphin::core {

// Every observable product from a marine survey instrument is an Artifact.
// The variant index matches ArtifactType below — keep them in sync.
using Artifact = std::variant<
    SidescanPing,    // index 0
    SubBottomTrace,  // index 1
    MagSample,       // index 2
    MultibeamPing,   // index 3
    RasterGrid       // index 4
>;

enum class ArtifactType : uint8_t {
    Sidescan     = 0,
    SubBottom    = 1,
    Magnetometer = 2,
    Multibeam    = 3,
    Raster       = 4,
};

inline ArtifactType artifactType(const Artifact& a) {
    return static_cast<ArtifactType>(a.index());
}

inline uint64_t artifactId(const Artifact& a) {
    return std::visit([](const auto& x) -> uint64_t { return x.id; }, a);
}

inline int64_t artifactTimestamp(const Artifact& a) {
    return std::visit([](const auto& x) -> int64_t { return x.timestamp_us; }, a);
}

} // namespace dolphin::core
