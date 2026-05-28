#pragma once
// Thread-safe nav-track builder for non-swath acoustic layers (SBP, MAG, MBE, etc.).
//
// buildTrackLayerMapData iterates entries of the given artifact_type in the
// artifact index, normalizes coordinates to display_crs, and returns a
// LayerMapData with nav_track, bbox, is_projected, kind = Track, and TrackStats.
//
// Thread-safe; designed to run off the main thread via QtConcurrent::run.

#include "ui/features/map/MapTypes.h"
#include "core/Artifact.h"
#include "core/ArtifactIndex.h"
#include "core/SpatialRef.h"

namespace dolphin::ui {

LayerMapData buildTrackLayerMapData(const core::ArtifactIndex& index,
                                    core::ArtifactType         artifact_type,
                                    const core::SpatialRef&    source_crs,
                                    const core::SpatialRef&    display_crs);

} // namespace dolphin::ui
