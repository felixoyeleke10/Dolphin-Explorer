#pragma once

#include "core/SpatialRef.h"

#include <string>
#include <vector>

namespace dolphin::app {

class Project;

struct MissingArtifactRecovery {
    std::string      source_path;
    std::string      layer_id;
    std::string      layer_label;
    core::SpatialRef source_crs;
};

// Build source-wide recovery requests for layers whose parsed artifacts are
// unavailable. At most one request is returned per source.
std::vector<MissingArtifactRecovery>
planMissingArtifactRecovery(Project& project);

} // namespace dolphin::app
