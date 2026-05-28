#pragma once
// WaterfallProcessingAlgorithms.h — display-pipeline algorithm declarations.
//
// Implemented in WaterfallProcessingAlgorithms.cpp.
// Called from WaterfallViewProcessing.cpp (runPipeline + do* wrappers).
#include "ui/features/waterfall/WaterfallView.h"

#include <vector>

namespace dolphin::ui {
namespace detail {

// ── Pre-assembly (operate on raw SidescanPing amplitudes) ─────────────────────

void normalizeRawAmplitudes(std::vector<core::SidescanPing>& pings,
                            const WaterfallParams& params);
void stretchRawAmplitudes(std::vector<core::SidescanPing>& pings);
void applyTvg(std::vector<core::SidescanPing>& pings, const WaterfallParams& params);
void applyArc(std::vector<core::SidescanPing>& pings, const WaterfallParams& params);

// ── Post-assembly (operate on assembled PingRows) ─────────────────────────────

void applyBeamPattern(std::vector<PingRow>& rows, const WaterfallParams& params);
void applyArn(std::vector<PingRow>& rows, const WaterfallParams& params);
void applyDestripe(std::vector<PingRow>& rows, const WaterfallParams& params);
void applyMlEnhance(std::vector<PingRow>& rows, const WaterfallParams& params);

} // namespace detail
} // namespace dolphin::ui
