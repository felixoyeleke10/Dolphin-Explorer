#pragma once
#include "core/Artifact.h"
#include "core/SpatialRef.h"
#include <QString>
#include <string>
#include <vector>

namespace dolphin::app {

// Resolved per-file import action produced by the import wizard and consumed
// by ImportJobManager::importBatch().
struct FileImportAction {
    enum class Kind {
        ImportNew,        // new source + new layer
        ReuseExisting,    // use existing parsed data, no re-parse
        RebuildExisting,  // re-parse an existing source + overwrite cache
        Skip,             // do nothing
    };

    // Which frequency bands to create for dual-frequency files.
    // Single-frequency files always produce one normal layer regardless.
    enum class BandChoice {
        Both,    // create _HF + _LF (default)
        HFOnly,  // create only the high-frequency layer
        LFOnly,  // create only the low-frequency layer
    };

    QString          path;
    Kind             kind               = Kind::ImportNew;
    std::string      existing_layer_id;
    std::string      existing_source_id;
    core::SpatialRef source_crs;        // empty = auto-detect from file header
    BandChoice       band_choice        = BandChoice::Both;
    std::vector<core::ArtifactType> module_filter;  // empty = import all families
};

} // namespace dolphin::app
