#pragma once
#include "ui/features/map/MapTypes.h"
#include "ui/features/map/sidescan/SssGeorefParams.h"
#include "core/SidescanPing.h"
#include <vector>

namespace dolphin::ui {

struct SwathGeorefResult {
    std::vector<SssStrip> strips;
    bool is_projected = false;
    HeadingStats heading_stats;
    size_t skipped_no_position = 0;
    size_t skipped_no_heading  = 0;
    size_t skipped_no_samples  = 0;
};

// Convert raw sidescan pings into georeferenced strip points.
// Heading source, offset, and port/starboard swap are controlled by params.
SwathGeorefResult georeferenceSidescanPings(
    const std::vector<core::SidescanPing>& raw_pings,
    const SssGeorefParams&                 params = {});

} // namespace dolphin::ui
