#pragma once
#include <string>
#include <vector>
#include "core/ArtifactIndex.h"

namespace dolphin::app {

// What kind of data a layer primarily contains.
// Defined here (not DataLayer.h) so callers can use it without pulling in
// the full DataLayer / NodeGraph / pipeline headers.
enum class Modality {
    Sidescan,
    SubBottom,
    Magnetometer,
    Multibeam,
    Raster,
    Mixed,    // transient: sources may produce Mixed; import splits into per-family layers
    Unknown,
};

// -- Modality utilities --------------------------------------------------------

std::string        modalityLabel         (Modality m);
Modality           inferModality         (const core::ArtifactIndex& idx);

// One-to-one mapping between a single artifact type and its module modality.
Modality           modalityForType       (core::ArtifactType type);
core::ArtifactType artifactTypeForModality(Modality m);

// Short suffix appended to the base layer label when a source is split by module.
// Example: "Survey01" → "Survey01 [SSS]", "Survey01 [SBP]"
const char*        moduleSuffix          (core::ArtifactType type);

// Return a copy of src containing only entries of the given type.
core::ArtifactIndex filteredByType(const core::ArtifactIndex& src,
                                   core::ArtifactType          type);

// Collect unique SSS frequency bands present in an artifact index.
// Sorted highest-first. Empty when no sidescan entries carry frequency metadata.
std::vector<float> sidescanFrequencyBands(const core::ArtifactIndex& index);

// Return the band from 'bands' closest to target_hz.
// Returns bands.front() when target_hz <= 0; returns 0 when bands is empty.
float nearestFrequencyBand(const std::vector<float>& bands, float target_hz);

// -- Module routing table ------------------------------------------------------
// Ordered canonical list: each type becomes its own DataLayer at import time.
// When multiple families are present the first type in this list claims the
// pre-allocated layer slot; remaining families each get a new layer.
// Extend this array when new modules ship (MAG, MBES, …).
inline constexpr core::ArtifactType kModuleArtifactTypes[] = {
    core::ArtifactType::Sidescan,
    core::ArtifactType::SubBottom,
    core::ArtifactType::Magnetometer,
    core::ArtifactType::Multibeam,
    core::ArtifactType::Raster,
};

} // namespace dolphin::app
