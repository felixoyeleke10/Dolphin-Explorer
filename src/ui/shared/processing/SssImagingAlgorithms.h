#pragma once
// Post-assembly sidescan imaging algorithms — Beam Pattern / ARN / Destripe / ML
// Enhance — operating on one channel's amplitude rows.
//
// Single source of truth shared by:
//   - the waterfall display pipeline (WaterfallProcessingAlgorithms, via PingRow),
//   - the SSS *map* mosaic build (so the same tools render on the map, not only
//     in the waterfall).
//
// Each *Channel function takes a list of pointers to mutable amplitude rows (one
// per ping for a single channel, nadir at index 0) and corrects them in place.
// The algorithms are relative (ratio-to-reference), so they work whether the
// amplitudes are raw 16-bit or physical-16 domain.
//
// Pure data: no Qt, no OpenGL, no QObject.  Lives in dolphin-ui-shared so both
// dolphin-ui-map and dolphin-ui-waterfall can use it without a cross-feature link.
#include "app/display/WaterfallParams.h"
#include "core/SidescanPing.h"

#include <cstdint>
#include <vector>

namespace dolphin::ui::imaging {

// -- Per-channel cores (operate in place on the pointed-to rows) ---------------
void beamPatternChannel(std::vector<std::vector<uint16_t>*>& rows, const BeamPatternParams& bp);
void arnChannel        (std::vector<std::vector<uint16_t>*>& rows, const ArnParams& arn);
void destripeChannel   (std::vector<std::vector<uint16_t>*>& rows, const DestripeParams& d);
void mlEnhanceChannel  (std::vector<std::vector<uint16_t>*>& rows, const MlEnhanceParams& me);

// -- Whole-pings convenience ---------------------------------------------------
// Apply the enabled post-assembly imaging chain (beam → ARN → destripe → ML) to
// a set of pings, in place, in their native (raw) amplitude domain.  Splits the
// pings by channel, runs each enabled algorithm, and writes the result back into
// ping.samples[].amplitude.  No-op when nothing is enabled.
void applyImagingChain(std::vector<core::SidescanPing>& pings, const WaterfallParams& params);

// Apply the *full* SSS amplitude pipeline (TVG/ARC/AGC pre-assembly + the imaging
// chain) to map pings in place, so the rasterizer produces a corrected mosaic.
// Honors already-baked correction_flags so nothing is double-applied.  No-op when
// the layer has no enabled corrections.
void applySssMapCorrections(std::vector<core::SidescanPing>& pings, const WaterfallParams& params);

} // namespace dolphin::ui::imaging
