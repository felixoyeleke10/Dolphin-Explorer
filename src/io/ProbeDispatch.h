#pragma once
#include "io/ProbeResult.h"
#include "core/Artifact.h"
#include <string>

namespace dolphin::io {

// Detect format by extension and dispatch to the appropriate reader's probe().
// Fully self-contained: opens and closes the file internally.
// Thread-safe: no shared mutable state — safe to call from QtConcurrent::run.
ProbeResult probeFile(const std::string& path);

// Qt file-dialog filter scoped to the supported formats for one sensor family,
// with an "All Files" fallback. Used by modality-specific import commands so the
// open dialog only offers that family's formats. Single source of truth for the
// per-modality extension lists — do NOT hardcode them in UI code.
std::string fileFilterForArtifactType(core::ArtifactType type);

// Qt file-dialog filter string for all natively supported survey formats.
// Keep in sync with the extension switch in ProbeDispatch.cpp.
// Use this as the single source of truth for file-open dialogs — do NOT
// hardcode extension lists in UI code.
inline constexpr const char* kSupportedFileFilter =
    "Survey Files (*.xtf *.jsf *.segy *.sgy *.dlpd *.dpcache);;"
    "XTF (*.xtf);;"
    "JSF (*.jsf);;"
    "SEG-Y (*.segy *.sgy);;"
    "DLPD Cache (*.dlpd *.dpcache);;"
    "All Files (*)";

} // namespace dolphin::io
