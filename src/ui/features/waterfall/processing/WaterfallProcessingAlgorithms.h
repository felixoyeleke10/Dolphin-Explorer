#pragma once
// WaterfallProcessingAlgorithms.h — amplitude processing algorithm declarations.
//
// Implemented in WaterfallProcessingAlgorithms.cpp.
// Called from WaterfallViewProcessing.cpp (runPipeline) and
// SidescanCorrectionService (bake-to-dlpd path).
// Pure data: no Qt, no OpenGL, no QObject.
#include "ui/features/waterfall/WaterfallParams.h"
#include "ui/features/waterfall/PingRow.h"
#include "core/SidescanPing.h"

#include <vector>

namespace dolphin::ui {
namespace detail {

// -- Pre-assembly (operate on raw SidescanPing amplitudes) ---------------------

void normalizeRawAmplitudes(std::vector<core::SidescanPing>& pings,
                            const WaterfallParams& params);
void stretchRawAmplitudes(std::vector<core::SidescanPing>& pings);
void applyTvg(std::vector<core::SidescanPing>& pings, const WaterfallParams& params);
void applyArc(std::vector<core::SidescanPing>& pings, const WaterfallParams& params);

// -- Post-assembly (operate on assembled PingRows) -----------------------------

void applyBeamPattern(std::vector<PingRow>& rows, const WaterfallParams& params);
void applyArn(std::vector<PingRow>& rows, const WaterfallParams& params);
void applyDestripe(std::vector<PingRow>& rows, const WaterfallParams& params);
void applyMlEnhance(std::vector<PingRow>& rows, const WaterfallParams& params);

} // namespace detail
} // namespace dolphin::ui
