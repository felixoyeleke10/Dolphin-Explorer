// WaterfallViewProcessing.cpp — nav processing, pipeline orchestration, do* wrappers.
//
// Processing algorithms (TVG, ARC, AGC, beam, ARN, destripe, CLAHE) →
//   WaterfallProcessingAlgorithms.cpp / .h  (dolphin::ui::detail namespace)
#include "ui/features/waterfall/WaterfallView.h"
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

} // namespace dolphin::ui
