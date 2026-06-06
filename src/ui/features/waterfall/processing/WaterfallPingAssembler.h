#pragma once
#include "ui/features/waterfall/PingRow.h"
#include "ui/features/waterfall/WaterfallParams.h"
#include "core/SidescanPing.h"

#include <vector>

namespace dolphin::ui {

// Pairs raw SidescanPings into PingRows.
//
// Strategy:
//   1. ping_number matching, when hardware provides matched pairs
//   2. timestamp merging within kTolUs
//   3. single-channel rows for unpaired port/starboard runs
//
// Amplitudes are kept in 16-bit physical space. The assembler only masks
// invalid water-column samples; display transforms are applied by the renderer
// so parameter changes do not require re-assembly.
class WaterfallPingAssembler {
public:
    static std::vector<PingRow> assemble(
        const std::vector<core::SidescanPing>& pings,
        const WaterfallParams&                 params);

    // Waterfall input contract — call before assembly or pipeline entry.
    // Removes pings with fewer than 2 samples or a non-finite / non-positive
    // slant_range_m (NaN, Inf, 0, negative all rejected).
    // Clears per-sample range_m that is NaN or Inf only — negative values are
    // preserved because they carry the "masked/water-column" semantic that
    // SSSAmplitudeProcessor::physical16() uses to zero those samples.
    // Returns the number of pings removed.
    static int sanitize(std::vector<core::SidescanPing>& pings);

private:
    static constexpr int64_t kTolUs = 50'000;

    static PingRow buildRow(const core::SidescanPing* port_ping,
                            const core::SidescanPing* stbd_ping);
};

} // namespace dolphin::ui
