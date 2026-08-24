#pragma once

#include "core/SidescanPing.h"
#include "ui/features/waterfall/PingRow.h"
#include "ui/features/waterfall/WaterfallParams.h"
#include "ui/features/waterfall/processing/SeabedAutoDetector.h"

#include <vector>

namespace dolphin::ui {
namespace imaging { struct SssAmplitudeContext; }

struct WaterfallPipelineResult {
    std::vector<PingRow> rows;
    float stretch_low  = 0.f;
    float stretch_high = 1.f;
};

// Background-safe canonical waterfall processing pipeline. It has no widget or
// QObject dependency and owns calibration, imaging, assembly, bottom detection,
// and stretch derivation order.
WaterfallPipelineResult runWaterfallPipeline(
    const std::vector<core::SidescanPing>& pings,
    const WaterfallParams& params,
    const SeabedAutoParams& seabed_params,
    bool seabed_enabled,
    const imaging::SssAmplitudeContext* amplitude_context = nullptr);

} // namespace dolphin::ui
