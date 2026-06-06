#pragma once
#include "core/NavPoint.h"
#include <cstdint>
#include <vector>

namespace dolphin::core {

// Tracks which SBP processing corrections have been permanently baked into
// the stored float samples.  Baked flags are persisted in the .dlpd so the
// display pipeline can skip re-applying them on load.
enum class SbpCorrectionFlag : uint32_t {
    None       = 0,
    DcRemoval  = 1u << 0,  // per-trace mean subtracted
    Envelope   = 1u << 1,  // instantaneous amplitude |sample| taken
    Normalize  = 1u << 2,  // per-trace peak normalization applied
    StaticGain = 1u << 3,  // constant dB gain applied
    Agc        = 1u << 4,  // sliding-window RMS normalization applied
    BandPass   = 1u << 5,  // bandpass filter applied
};
inline uint32_t& operator|=(uint32_t& flags, SbpCorrectionFlag f)
    { return flags |= static_cast<uint32_t>(f); }
inline bool hasSbpCorrectionFlag(uint32_t flags, SbpCorrectionFlag f)
    { return (flags & static_cast<uint32_t>(f)) != 0; }

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
    uint32_t           correction_flags  = 0;     // bitmask of SbpCorrectionFlag
    std::vector<float> samples;                   // normalised amplitude -1..1
};

} // namespace dolphin::core
