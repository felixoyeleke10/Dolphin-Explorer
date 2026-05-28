#pragma once
#include "core/ArtifactIndex.h"
#include <cstddef>
#include <vector>

namespace dolphin::ui {

// Subsample sidescan entries from an index down to at most max_ping_groups groups.
// Port and starboard entries from the same ping firing cycle are kept together.
std::vector<core::ArtifactIndexEntry>
thinSidescanEntriesForMap(const core::ArtifactIndex& index, size_t max_ping_groups);

// Remove sidescan entries whose frequency band differs from the band nearest to
// target_hz.  No-op when target_hz <= 0 or the index contains only one band.
void filterSidescanEntriesByBand(core::ArtifactIndex& index, float target_hz);

} // namespace dolphin::ui
