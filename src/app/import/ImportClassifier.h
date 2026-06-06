#pragma once
#include "app/import/ImportAction.h"

class QString;

namespace dolphin::app {
class Project;

// Classify a single file against the current project to determine what import
// action to take before any side effect runs.
//
// Returns a FileImportAction with kind set to one of:
//   ImportNew       — source unknown to the project; create new source + layer
//   ReuseExisting   — source known and DLPD cache is valid; no re-parse needed
//   RebuildExisting — source known but cache is missing or stale; re-parse required
//
// existing_layer_id and existing_source_id are populated for Reuse/Rebuild.
// path, source_crs, module_filter, and band_choice are left for the caller to fill.
FileImportAction classifyImportAction(const QString& path, const Project* project);

} // namespace dolphin::app
