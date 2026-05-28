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

private:
    static constexpr int64_t kTolUs = 50'000;

    static PingRow buildRow(const core::SidescanPing* port_ping,
                            const core::SidescanPing* stbd_ping);
};

} // namespace dolphin::ui
