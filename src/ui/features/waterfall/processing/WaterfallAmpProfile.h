#pragma once
#include "ui/features/waterfall/PingRow.h"
#include "ui/features/waterfall/rendering/WaterfallRenderer.h"
#include "ui/features/waterfall/WaterfallScrollController.h"

#include <vector>

namespace dolphin::ui {

// Per-sample mean amplitude across all visible rows, computed once per paint
// and consumed by WaterfallOverlayPainterAmplitude.  Port and starboard are
// stored separately (both indexed 0 = nadir) at the same length as ns.
struct WaterfallAmpProfile {
    std::vector<float> port_mean;  // mean 0-65535 per sample index
    std::vector<float> stbd_mean;
    int                ns = 0;     // sample count (length of both vectors)
};

// Build a WaterfallAmpProfile from the visible portion of rows.
// Returns an empty profile (ns == 0) when there is no data.
WaterfallAmpProfile computeAmpProfile(const std::vector<PingRow>&      rows,
                                      const WaterfallScrollController& scroll,
                                      const WfLayout&                  lay);

} // namespace dolphin::ui
