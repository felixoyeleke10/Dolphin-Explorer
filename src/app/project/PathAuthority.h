#pragma once
#include <string>

namespace dolphin::app {
class Project;

// Centralises every filesystem path that derives from the project manifest.
// All artifact-store locations must be computed through these helpers so that
// project folder moves stay self-consistent.
namespace PathAuthority {

// Returns the project's data directory (parent of all artifact stores).
// Empty string if the project has no manifest.
std::string dataDir(const Project& project);

// Returns the canonical artifact store path for a given source ID:
//   <project_dir>/data/<source_id>.dlpd
// Empty string if the project has no manifest or source_id is empty.
std::string artifactStorePath(const Project& project, const std::string& source_id);

} // namespace PathAuthority
} // namespace dolphin::app
