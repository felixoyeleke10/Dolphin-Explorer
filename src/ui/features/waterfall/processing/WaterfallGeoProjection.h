#pragma once

#include "core/SidescanGeometry.h"
#include "core/SidescanPing.h"

#include <optional>

namespace dolphin::ui {

struct WaterfallGeoProjectionInput {
    double nav_lat = 0.0;
    double nav_lon = 0.0;
    float heading_deg = 0.0f;
    float altitude_m = 0.0f;
    float range_m = 0.0f;
    core::SidescanChannel channel = core::SidescanChannel::Port;
    core::SidescanRangeDomain range_domain = core::SidescanRangeDomain::Slant;
    bool is_projected = false;
};

struct WaterfallGeoPosition {
    double lat = 0.0;
    double lon = 0.0;
    bool is_projected = false;
};

std::optional<WaterfallGeoPosition> projectWaterfallRange(
    const WaterfallGeoProjectionInput& input);

} // namespace dolphin::ui
