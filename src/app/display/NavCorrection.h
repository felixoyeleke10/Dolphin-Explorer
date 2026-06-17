#pragma once
#include "app/display/NavProcessingParams.h"
#include "core/SidescanPing.h"
#include "core/SubBottomTrace.h"
#include <vector>

namespace dolphin::ui {

// THE sidescan navigation correction — single source of truth, applied at
// render/load time from the project's model params (DataLayer::nav_state) and
// never baked into the .dlpd store. Both renderers consume it so they always
// agree (the SonarWiz / SeaView "one navigation model" approach):
//   - the waterfall display (WaterfallView::runNavCorrections delegates here), and
//   - the SSS map build (SidescanViewController::activateLayer).
//
// Layback + smoothing run the canonical pipeline nodes (GeoCorrectNode /
// NavSmoothNode); heading/pitch/roll are constant attitude offsets. Returns the
// input unchanged (fast, no buffer round-trip) when no correction is enabled.
//
// Lives in the app layer (depends only on pipeline + core, both below app) so any
// UI feature can reuse it without a cross-feature dependency.
std::vector<core::SidescanPing>
applySidescanNavCorrections(std::vector<core::SidescanPing> pings,
                            const NavProcessingParams&       params);

// The sub-bottom navigation correction (traces), applied in place. Pure-geo:
// gap-aware GPS smoothing (skips line breaks), towfish layback (course-over-ground
// fallback bearing), and a constant heading offset. Display-time only, never on
// load; a no-op when nothing is enabled. Co-located here so both modalities' nav
// corrections live in one app-layer module (was ui/features/subbottom).
//
// SBP intentionally keeps this gap-aware pure-geo correction rather than the
// SidescanPing-only GeoCorrect/NavSmooth nodes: the nodes are naive (no gap
// handling, heading-only layback), so routing SBP through them would be a downgrade.
void applySbpNavCorrections(std::vector<core::SubBottomTrace>& traces,
                            const NavProcessingParams& params);

} // namespace dolphin::ui
