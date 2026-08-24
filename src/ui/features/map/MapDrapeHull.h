#pragma once

#include "ui/features/map/MapTypes.h"

#include <vector>

namespace dolphin::ui {

// Builds closed sonar-drape outline segments. NaN points separate ribbons.
std::vector<QPointF> buildSonarDrapeHull(const LayerMapData& data);

} // namespace dolphin::ui
