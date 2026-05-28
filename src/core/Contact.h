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
    float       range_m      = 0.0f;  // slant range to pick (Phase 1 proxy for length)
    float       width_m      = 0.0f;
    float       height_m     = 0.0f;
    uint64_t    artifact_id  = 0;    // which artifact this contact was picked on
    uint32_t    sample_idx   = 0;    // index into artifact's sample array (if applicable)
    std::string line_id;
    std::string classification;
    Confidence  confidence   = Confidence::Possible;
    std::string notes;
    double      created_at   = 0.0;  // Unix epoch
    double      modified_at  = 0.0;
    std::vector<std::string> tags;
    std::string group_id;  // empty = ungrouped; references ItemGroup::id
};

} // namespace dolphin::core
