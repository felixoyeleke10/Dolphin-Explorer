#pragma once
// Thread-safe SBP profile map builder.
//
// Takes pre-loaded SubBottomTrace objects (from ImportService::loadAllSubBottomTraces)
// and returns a LayerMapData with:
//   kind         = Profile
//   nav_track    = normalised vessel positions
//   trace_scalar = bottom depth per trace in metres (NaN where no pick exists)
//   scalar_min/max, scalar_label
//
// Designed to run off the main thread via QtConcurrent::run.

#include "ui/features/map/MapTypes.h"
#include "core/SubBottomTrace.h"
#include "core/SpatialRef.h"
#include <vector>

namespace dolphin::ui {

LayerMapData buildSbpProfileMapData(
    const std::vector<core::SubBottomTrace>& traces,
    const core::SpatialRef& source_crs,
    const core::SpatialRef& display_crs);

} // namespace dolphin::ui
