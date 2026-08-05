#include "app/artifacts/ArtifactSidecar.h"

#include "io/IFormatReader.h"

#include <filesystem>

namespace dolphin::app {

std::string sidecarArtifactPath(const std::string& artifact_path,
                                const std::string& layer_id,
                                std::uint8_t artifact_role)
{
    namespace fs = std::filesystem;

    const fs::path path(artifact_path);
    const std::string suffix = "_" + layer_id;
    const std::string stem = path.stem().string();
    const bool legacy_named = stem.size() > suffix.size()
        && stem.compare(stem.size() - suffix.size(), suffix.size(), suffix) == 0;

    if (artifact_role == io::kArtifactRoleSidecar || legacy_named)
        return artifact_path;

    return (path.parent_path() / (stem + suffix + ".dlpd")).string();
}

} // namespace dolphin::app
