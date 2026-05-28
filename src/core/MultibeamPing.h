#pragma once
#include "core/NavPoint.h"
#include <cstdint>
#include <vector>
#include <cmath>

namespace dolphin::core {

// One beam sounding within a multibeam swath.
struct Sounding {
    float    angle_deg      = 0.f;   // beam angle from nadir (negative = port)
    float    range_m        = 0.f;   // two-way travel-time range
    float    depth_m        = NAN;   // computed depth (NaN = invalid)
    float    across_track_m = 0.f;   // across-track distance from vessel CRP
    float    along_track_m  = 0.f;   // along-track offset from vessel CRP
    uint16_t intensity      = 0;     // backscatter intensity 0-65535
    bool     valid          = false;
};

struct MultibeamPing {
    uint64_t              id                = 0;
    int64_t               timestamp_us      = 0;  // µs since Unix epoch
    NavPoint              nav;
    float                 frequency_hz      = 0.f;
    float                 sound_velocity_ms = 1500.f;  // SV used for ray-tracing
    std::vector<Sounding> soundings;
};

} // namespace dolphin::core
