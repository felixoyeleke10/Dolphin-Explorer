#pragma once

#include "core/ArtifactIndex.h"

#include <string>

namespace dolphin::app {

class DataLayer;

struct BaselineArtifact {
    std::string path;
    std::string format;
    core::ArtifactIndex index;
    std::string error;

    explicit operator bool() const noexcept {
        return error.empty() && !path.empty();
    }
};

// Resolve the immutable imported artifact for processing/revert. If the active
// artifact is already the baseline, its in-memory index is reused with no I/O.
// Otherwise the baseline footer is read and its logical source id restored.
BaselineArtifact resolveBaselineArtifact(const DataLayer& layer);

} // namespace dolphin::app
