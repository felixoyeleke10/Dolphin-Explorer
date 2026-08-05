// WaterfallViewProcessing.cpp — nav processing, pipeline orchestration, do* wrappers.
//
// Processing algorithms (TVG, ARC, AGC, beam, ARN, destripe, CLAHE) →
//   WaterfallProcessingAlgorithms.cpp / .h  (dolphin::ui::detail namespace)
#include "ui/features/waterfall/WaterfallView.h"
#include "ui/features/waterfall/processing/WaterfallProcessingAlgorithms.h"
#include "ui/features/waterfall/processing/WaterfallPingAssembler.h"
#include "ui/features/waterfall/processing/SeabedAutoDetector.h"
#include "ui/shared/processing/SssAmplitudeContext.h"
#include "ui/shared/processing/SssImagingAlgorithms.h"
#include "app/display/NavCorrection.h"

#include <vector>

namespace dolphin::ui {

// -- Nav Processing ------------------------------------------------------------

std::vector<core::SidescanPing>
WaterfallView::runNavCorrections(std::vector<core::SidescanPing> pings,
                                  const NavProcessingParams& params)
{
    // Single source of truth: the same correction the SSS map applies, so the
    // waterfall and the map always agree (see app/display/NavCorrection).
    return applySidescanNavCorrections(std::move(pings), params);
}

// -- runPipeline — background-safe full display pipeline -----------------------
//
// TVG/ARC/AGC → assemble rows → seabed → beam/ARN/destripe/ML → auto-stretch.
// Seabed detection runs on clean calibrated amplitudes (before display-specific
// enhancements) so beam-pattern correction / ARN / destripe / ML cannot bias picks.
// pings are read-only; we make one internal working copy here so the caller
// can keep the original for re-processing on param changes.

WaterfallView::WfPipelineResult WaterfallView::runPipeline(
    const std::vector<core::SidescanPing>& pings,
    const WaterfallParams&                 params,
    const SeabedAutoParams&                seabed_params,
    bool                                   seabed_enabled,
    const imaging::SssAmplitudeContext*    amplitude_context)
{
    if (amplitude_context
            && amplitude_context->params_fingerprint
                != imaging::sssAmplitudeParamsFingerprint(params)) {
        amplitude_context = nullptr;
    }

    auto calibrated = pings;
    if (amplitude_context) {
        for (auto& ping : calibrated)
            imaging::applyPerPingCalibration(ping, params);
    } else {
        imaging::applyCalibration(calibrated, params);
    }

    auto display = calibrated;
    if (amplitude_context)
        imaging::applySssAmplitudeContext(display, *amplitude_context);
    else
        imaging::applyImagingChain(display, params);

    WfPipelineResult result;
    result.rows = WaterfallPingAssembler::assemble(display, params);

    // SRC cannot be evaluated without altitude. When recorded altitude is
    // absent, always derive a reviewable bottom track even if its overlay was
    // previously cleared; the renderer uses this pick as the altitude source.
    if (seabed_enabled || params.slant_range_correction) {
        auto clean_rows = WaterfallPingAssembler::assemble(calibrated, params);
        SeabedAutoDetector::detectAll(clean_rows, seabed_params);
        if (seabed_params.smoothing > 0.f)
            SeabedAutoDetector::smooth(clean_rows,
                                       static_cast<int>(seabed_params.smoothing));
        const auto count = std::min(result.rows.size(), clean_rows.size());
        for (size_t i = 0; i < count; ++i) result.rows[i].seabed = clean_rows[i].seabed;
    }
    if (amplitude_context && amplitude_context->valid()) {
        result.stretch_low = amplitude_context->stretch_low;
        result.stretch_high = amplitude_context->stretch_high;
    } else {
        const auto stretch = imaging::computeAutoStretch(display);
        result.stretch_low = stretch.low;
        result.stretch_high = stretch.high;
    }

    return result;
}

} // namespace dolphin::ui
