#pragma once

#include "ui/features/map/MapTypes.h"

#include <vector>

namespace dolphin::ui {

// Builds closed sonar-drape outline segments. NaN points separate ribbons.
std::vector<QPointF> buildSonarDrapeHull(const LayerMapData& data);

// Authoritative filled footprint: every original coverage ribbon is retained
// as an independent polygon. Unlike the display hull, this never pairs or
// merges independently segmented channels.
std::vector<QPointF> buildSonarFootprint(const LayerMapData& data);

} // namespace dolphin::ui
