#include "ui/features/waterfall/processing/WaterfallPipelinePolicy.h"

#include <cassert>
#include <iostream>

using dolphin::ui::WaterfallParams;
namespace policy = dolphin::ui::waterfallpipeline;

int main()
{
    WaterfallParams base;
    auto changed = base;
    assert(!policy::requiresRowRebuild(base, changed));

    changed.gain += 1.f;
    assert(!policy::requiresRowRebuild(base, changed));

    changed = base;
    changed.agc.enabled = !base.agc.enabled;
    assert(policy::requiresRowRebuild(base, changed));

    changed = base;
    changed.destripe.enabled = !base.destripe.enabled;
    assert(policy::requiresRowRebuild(base, changed));

    assert(!policy::amplitudeContextMatches(nullptr, base));
    std::cout << "WaterfallPipelinePolicy checks passed\n";
    return 0;
}
