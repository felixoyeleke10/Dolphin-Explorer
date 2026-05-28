#include "app/project/PathAuthority.h"
#include "app/project/Project.h"
#include <filesystem>

namespace dolphin::app::PathAuthority {

std::string dataDir(const Project& project)
{
    return project.dataPath();
}

std::string artifactStorePath(const Project& project, const std::string& source_id)
{
    if (project.manifestPath().empty() || source_id.empty()) return {};
    namespace fs = std::filesystem;
    return (fs::path(project.dataPath()) / (source_id + ".dlpd")).string();
}

} // namespace dolphin::app::PathAuthority
