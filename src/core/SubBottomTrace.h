#pragma once
#include "core/NavPoint.h"
#include <cstdint>
#include <vector>

namespace dolphin::core {

// One SBP/chirp trace — the acoustic equivalent of an SSS ping.
struct SubBottomTrace {
    uint64_t           id             = 0;
    int64_t            timestamp_us   = 0;  // µs since Unix epoch
    NavPoint           nav;
    float              frequency_hz   = 0.f;
    float              sample_rate_hz = 0.f;
    float              tow_depth_m       = 0.f;   // towfish / transducer depth
    float              two_way_time_s    = 0.f;   // full record window in seconds
    int32_t            bottom_sample_idx = -1;    // first-return seabed pick; -1 = not detected
    std::vector<float> samples;                   // normalised amplitude -1..1
};

} // namespace dolphin::core
