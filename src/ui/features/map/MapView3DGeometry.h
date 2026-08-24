#pragma once

#include <QPointF>

#include <vector>

namespace dolphin::ui {

struct MapLocalFrame {
    double origin_x = 0.0;
    double origin_y = 0.0;
    bool is_projected = false;
};

// Appends xyz GL_LINES vertices for each continuous part of a navigation track.
int appendNavTrackLineVertices(const std::vector<QPointF>& track,
                               const MapLocalFrame& frame,
                               std::vector<float>& vertices);

// Builds xyz GL_LINES vertices for closed polygons separated by NaN sentinels.
std::vector<float> buildClosedOutlineVertices(const std::vector<QPointF>& polygons,
                                              const MapLocalFrame& frame);

} // namespace dolphin::ui
