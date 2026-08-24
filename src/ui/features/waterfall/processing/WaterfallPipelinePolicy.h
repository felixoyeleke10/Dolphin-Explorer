#pragma once

#include "ui/features/waterfall/WaterfallParams.h"

#include <cstdint>

namespace dolphin::ui::imaging { struct SssAmplitudeContext; }

namespace dolphin::ui::waterfallpipeline {

// Processing-stage changes require rebuilding rows; renderer-only changes do not.
bool requiresRowRebuild(const WaterfallParams& before,
                        const WaterfallParams& after);

bool amplitudeContextMatches(const imaging::SssAmplitudeContext* context,
                             const WaterfallParams& params);

} // namespace dolphin::ui::waterfallpipeline
