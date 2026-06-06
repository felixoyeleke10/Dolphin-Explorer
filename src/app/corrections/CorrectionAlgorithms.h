#pragma once
// Pre-assembly amplitude correction algorithms operating on raw SidescanPing samples.
// Called from SidescanCorrectionService (bake-to-dlpd path) and — via the
// WaterfallProcessingAlgorithms wrapper — from the display-time pipeline.
//
// Pure data: no Qt, no OpenGL, no QObject.
#include "app/corrections/SidescanCorrectionParams.h"
#include "core/SidescanPing.h"
#include <vector>

namespace dolphin::app::corrections {

void applyTvg             (std::vector<core::SidescanPing>& pings, const TvgParams& tvg);
void applyArc             (std::vector<core::SidescanPing>& pings, const ArcParams& arc);
void normalizeAmplitudes  (std::vector<core::SidescanPing>& pings, const AgcParams& agc);

} // namespace dolphin::app::corrections
