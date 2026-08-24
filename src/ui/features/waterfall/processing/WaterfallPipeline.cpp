#include "ui/features/waterfall/processing/WaterfallPipeline.h"
#include "ui/features/waterfall/processing/WaterfallPingAssembler.h"
#include "ui/shared/processing/SssAmplitudeContext.h"
#include "ui/shared/processing/SssImagingAlgorithms.h"

#include <algorithm>

namespace dolphin::ui {

WaterfallPipelineResult runWaterfallPipeline(
    const std::vector<core::SidescanPing>& pings,
    const WaterfallParams& params,
    const SeabedAutoParams& seabed_params,
    bool seabed_enabled,
    const imaging::SssAmplitudeContext* amplitude_context)
{
    if (amplitude_context
            && amplitude_context->params_fingerprint
                != imaging::sssAmplitudeParamsFingerprint(params))
        amplitude_context = nullptr;

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

    WaterfallPipelineResult result;
    result.rows = WaterfallPingAssembler::assemble(display, params);

    if (seabed_enabled || params.slant_range_correction) {
        auto clean_rows = WaterfallPingAssembler::assemble(calibrated, params);
        SeabedAutoDetector::detectAll(clean_rows, seabed_params);
        if (seabed_params.smoothing > 0.f)
            SeabedAutoDetector::smooth(
                clean_rows, static_cast<int>(seabed_params.smoothing));
        const auto count = std::min(result.rows.size(), clean_rows.size());
        for (size_t i = 0; i < count; ++i) {
            result.rows[i].seabed = clean_rows[i].seabed;
            result.rows[i].seabed_domain = clean_rows[i].seabed_domain;
        }
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
