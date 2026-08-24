#include "ui/features/waterfall/processing/WaterfallPipelinePolicy.h"
#include "ui/shared/processing/SssAmplitudeContext.h"
#include "ui/shared/processing/SssImagingAlgorithms.h"

namespace dolphin::ui::waterfallpipeline {

bool requiresRowRebuild(const WaterfallParams& before,
                        const WaterfallParams& after)
{
    return before.agc          != after.agc
        || before.tvg          != after.tvg
        || before.arn          != after.arn
        || before.destripe     != after.destripe
        || before.beam_pattern != after.beam_pattern
        || before.arc          != after.arc
        || before.ml_enhance   != after.ml_enhance;
}

bool amplitudeContextMatches(const imaging::SssAmplitudeContext* context,
                             const WaterfallParams& params)
{
    return context
        && context->params_fingerprint
            == imaging::sssAmplitudeParamsFingerprint(params);
}

} // namespace dolphin::ui::waterfallpipeline
