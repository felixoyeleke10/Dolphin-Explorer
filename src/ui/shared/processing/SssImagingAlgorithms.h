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

struct SssAutoStretch {
    float low  = 0.f;
    float high = 1.f;
};

// -- Per-channel cores (operate in place on the pointed-to rows) ---------------
bool beamPatternChannel(std::vector<std::vector<uint16_t>*>& rows, const BeamPatternParams& bp);
bool arnChannel        (std::vector<std::vector<uint16_t>*>& rows, const ArnParams& arn);
bool destripeChannel   (std::vector<std::vector<uint16_t>*>& rows, const DestripeParams& d);
bool mlEnhanceChannel  (std::vector<std::vector<uint16_t>*>& rows, const MlEnhanceParams& me);

// -- Whole-pings convenience ---------------------------------------------------
// Apply the enabled post-assembly imaging chain (beam → ARN → destripe → ML) to
// a set of pings, in place, in their native (raw) amplitude domain.  Splits the
// pings by channel, runs each enabled algorithm, and writes the result back into
// ping.samples[].amplitude.  No-op when nothing is enabled.
void applyImagingChain(std::vector<core::SidescanPing>& pings, const WaterfallParams& params);

// Local calibration stage used by focused processing and fallback paths. This
// applies TVG/ARC/AGC while respecting baked flags. Cross-view rendering uses
// SssAmplitudeContext after the native per-ping stage below.
void applyCalibration(std::vector<core::SidescanPing>& pings, const WaterfallParams& params);

// Native-sample, per-ping portion of calibration. Map loading calls this before
// resolution-only sample compaction so TVG/ARC and Global AGC see exactly the
// same physical sample positions/statistics as the waterfall.
void applyPerPingCalibration(core::SidescanPing& ping, const WaterfallParams& params);

// Finish calibration that genuinely needs along-track context (Variable AGC),
// then apply the shared imaging chain. Use after applyPerPingCalibration has
// already run on every ping in a bounded map product.
void applyContextCalibrationAndImaging(std::vector<core::SidescanPing>& pings,
                                       const WaterfallParams& params);

// Complete local display-amplitude product: calibration followed by the enabled
// beam/ARN/destripe/ML chain, all in native per-channel ping order. It is exact
// for the supplied set; use SssAmplitudeContext when independent viewer subsets
// must share one line-level result.
void applyDisplayPipeline(std::vector<core::SidescanPing>& pings, const WaterfallParams& params);

// Canonical 1st/99th-percentile display range. Zero amplitudes are excluded.
SssAutoStretch computeAutoStretch(const std::vector<core::SidescanPing>& pings);

} // namespace dolphin::ui::imaging
