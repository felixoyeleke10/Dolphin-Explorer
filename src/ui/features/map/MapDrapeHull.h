#pragma once

#include "ui/features/map/MapTypes.h"

#include <vector>

namespace dolphin::ui {

// Builds closed sonar-drape outline segments. NaN points separate ribbons.
std::vector<QPointF> buildSonarDrapeHull(const LayerMapData& data);

// Extracts only the exterior valid/no-data boundary of a preview raster.
// Internal transparent holes are excluded. Returned map-coordinate segments
// describe pixel-cell edges and are suitable for cached 2D/3D outlines.
std::vector<QLineF> buildSonarRasterBoundary(const QImage& image,
                                             double x_min, double y_min,
                                             double x_max, double y_max);
std::vector<QLineF> buildSonarRasterBoundary(
    const std::vector<uint16_t>& intensity, int width, int height,
    double x_min, double y_min, double x_max, double y_max);

} // namespace dolphin::ui
