#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "core/SpatialRef.h"

namespace dolphin::core {

// -----------------------------------------------------------------------------
//  Feature — a SHAPE annotation: a polyline (open line) or polygon (closed area).
//
//  Features are intentionally distinct from Contacts (Contact.h). A Contact is a
//  single point pick (a boulder, debris, an anomaly). A Feature traces a boundary
//  or encloses a region — a debris-field outline, a cable corridor, an exclusion
//  zone, a sand-wave field. The two concepts must never be conflated in the UI or
//  the data model.
//
//  Geometry is stored as ordered geographic vertices (lon/lat in `spatial_ref`,
//  normally the project display CRS / WGS84) so a feature renders consistently on
//  both the map chart and, later, the waterfall view.
// -----------------------------------------------------------------------------

enum class FeatureType {
    Polyline,   // open chain of vertices (cable run, boundary, track)
    Polygon,    // closed area; the last vertex implicitly connects to the first
};

// A single geographic vertex. Kept local to Feature so the core model stays
// self-contained (no existing shared geo-point type to reuse).
struct GeoVertex {
    double lat = 0.0;
    double lon = 0.0;
};

struct Feature {
    uint64_t                 id          = 0;
    std::string              label;
    FeatureType              type        = FeatureType::Polygon;
    std::vector<GeoVertex>   vertices;            // ordered; polygon is implicitly closed
    SpatialRef               spatial_ref;         // CRS the vertices are expressed in
    std::string              line_id;             // source layer drawn on (empty = map-drawn)
    std::string              classification;      // e.g. Debris Field, Cable Corridor, Zone
    std::string              notes;
    double                   created_at  = 0.0;   // Unix epoch
    double                   modified_at = 0.0;
    std::vector<std::string> tags;
    std::string              group_id;            // empty = ungrouped; references ItemGroup::id
    bool                     visible     = true;  // shown on the map (explorer checkbox)
};

} // namespace dolphin::core
