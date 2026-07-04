#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "core/SpatialRef.h"

namespace dolphin::core {

enum class Confidence { Possible, Probable, Certain };

struct Contact {
    uint64_t    id           = 0;
    std::string label;
    double      lat          = 0.0;
    double      lon          = 0.0;
    SpatialRef  spatial_ref;
    float       depth_m      = 0.0f;
    float       range_m      = 0.0f;  // slant range of the pick (measured, not editable)
    float       length_m     = 0.0f;  // measured object length
    float       width_m      = 0.0f;
    float       height_m     = 0.0f;
    float       shadow_m     = 0.0f;  // acoustic shadow length
    float       burial_depth_m = 0.0f;  // depth of burial below seabed
    bool        height_not_measurable = false;  // "Not measurable" — height is indeterminate
    uint64_t    artifact_id  = 0;    // which artifact this contact was picked on
    uint32_t    sample_idx   = 0;    // index into artifact's sample array (if applicable)
    std::string line_id;
    std::string classification;
    Confidence  confidence   = Confidence::Possible;
    std::string notes;               // free-text description
    std::string symbol;              // map glyph id (empty = default per classification)
    uint32_t    color_rgb    = 0;    // 0xAARRGGBB display colour; 0 = auto (per classification)
    bool        use_for_report = false;  // include this contact in generated reports
    bool        visible      = true;  // shown on the map / viewer overlays (explorer checkbox)
    double      created_at   = 0.0;  // Unix epoch
    double      modified_at  = 0.0;
    std::vector<std::string> tags;
    std::string group_id;  // empty = ungrouped; references ItemGroup::id
};

} // namespace dolphin::core
