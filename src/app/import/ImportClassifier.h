#pragma once
#include "app/import/ImportAction.h"
#include "core/Artifact.h"
#include <vector>

class QString;

namespace dolphin::app {
class Project;

// Classify a single file against the current project to determine what import
// action to take before any side effect runs.
//
// The decision is MODALITY-AWARE: `requested_modules` is the set of artifact
// families the user asked to import (from the wizard's module choice; empty = all
// families = legacy whole-file import). A source-level valid cache only satisfies
// the modalities that already have a layer — if a requested modality has no layer
// yet (e.g. importing SBP from a mixed file previously imported as SSS only), the
// action is ImportNew so that modality's layer gets created (reusing the cached
// decode when the source is unchanged; see ImportService buildArtifactStore).
//
// Returns a FileImportAction with kind set to one of:
//   ImportNew       — source unknown, OR a requested modality has no layer yet
//   ReuseExisting   — every requested modality already has a layer with a valid cache
//   RebuildExisting — every requested modality has a layer, but the cache is stale
//
// existing_layer_id and existing_source_id are populated for Reuse/Rebuild (and
// existing_source_id for the add-modality ImportNew). path, source_crs,
// module_filter, and band_choice are left for the caller to fill.
FileImportAction classifyImportAction(
    const QString& path, const Project* project,
    const std::vector<core::ArtifactType>& requested_modules = {});

} // namespace dolphin::app
