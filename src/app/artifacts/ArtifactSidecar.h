#pragma once

#include <cstdint>
#include <string>

namespace dolphin::app {

std::string sidecarArtifactPath(const std::string& artifact_path,
                                const std::string& layer_id,
                                std::uint8_t artifact_role);

} // namespace dolphin::app
