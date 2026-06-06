#pragma once
#include "core/NavPoint.h"
#include <cmath>
#include <cstdint>
#include <vector>

namespace dolphin::core {

enum class SidescanChannel : uint8_t { Port = 0, Starboard = 1 };

struct SidescanSample {
    uint16_t amplitude = 0;    // raw ADC 0-65535
    float    range_m   = 0.f;  // slant range; negative = water column (masked)
};

// -- Correction history --------------------------------------------------------
// Each processing node ORs its flag into SidescanPing::correction_flags so
// downstream code can inspect what has already been applied without re-running
// the pipeline.
enum class CorrectionFlag : uint32_t {
    None          = 0,
    Tvg           = 1u << 0,  // time-varying gain applied
    SlantRange    = 1u << 1,  // slant-range → ground-range conversion applied
    BeamPattern   = 1u << 2,  // beam pattern normalisation applied
    Destriping    = 1u << 3,  // across-track destriping applied
    NavSmoothed   = 1u << 4,  // navigation position smoothed
    GainNormalized = 1u << 5, // gain normalisation applied
    BandPass      = 1u << 6,  // band-pass filter applied
    SpeckleFilter = 1u << 7,  // speckle / texture filter applied
    GeoCorrect    = 1u << 8,  // layback / towfish position correction applied
    Arc           = 1u << 9,  // angle range correction applied
};
inline uint32_t& operator|=(uint32_t& flags, CorrectionFlag f)
    { return flags |= static_cast<uint32_t>(f); }
inline bool hasCorrectionFlag(uint32_t flags, CorrectionFlag f)
    { return (flags & static_cast<uint32_t>(f)) != 0; }

// -- QC flags ------------------------------------------------------------------
// Bitmask for data quality.  Set by QC nodes, detection algorithms, or the user.
enum class QcFlag : uint8_t {
    Ok         = 0,
    Suspect    = 1u << 0,  // data is unreliable but retained
    Rejected   = 1u << 1,  // data is bad; exclude from output / export
    NoNav      = 1u << 2,  // navigation missing or below quality threshold
    LowSignal  = 1u << 3,  // signal level below noise floor
    BottomLost = 1u << 4,  // bottom tracking failed for this ping
    UserEdited = 1u << 5,  // ping has been manually modified
};
inline uint8_t& operator|=(uint8_t& flags, QcFlag f)
    { return flags |= static_cast<uint8_t>(f); }
inline bool hasQcFlag(uint8_t flags, QcFlag f)
    { return (flags & static_cast<uint8_t>(f)) != 0; }

// -- Bottom detection result ---------------------------------------------------
// Written by BottomDetectNode; read by SlantRangeNode and the waterfall renderer.
struct BottomPick {
    float   range_m    = -1.f; // detected bottom slant range in metres; -1 = no detection
    float   confidence =  0.f; // 0..1 algorithm confidence; 0 when range_m < 0
    uint8_t source     =  0;   // 0 = none, 1 = auto-detected, 2 = user-edited

    bool valid() const { return range_m >= 0.f && std::isfinite(range_m); }
};

// -- Sidescan ping -------------------------------------------------------------
struct SidescanPing {
    uint64_t                    id                 = 0;
    int64_t                     timestamp_us       = 0;     // µs since Unix epoch
    uint32_t                    ping_number        = 0;     // XTF PingNumber (0 = unknown/JSF)
    SidescanChannel             channel            = SidescanChannel::Port;
    NavPoint                    nav;
    float                       frequency_hz       = 0.f;
    float                       sample_rate_hz     = 0.f;
    float                       slant_range_m      = 0.f;   // max slant range for this ping
    float                       sound_velocity_ms  = 0.f;   // SV applied during recording (0 = unknown)
    float                       tow_depth_m        = 0.f;   // sensor depth below surface
    float                       blanking_m         = 0.f;   // near-field blanking distance
    float                       volt_scale         = 1.f;   // millivolts per count (calibration)
    uint16_t                    gain_code          = 0;     // applied gain setting
    uint16_t                    initial_gain_code  = 0;
    uint32_t                    bandwidth_hz       = 0;     // signal bandwidth
    // Towfish geometry
    float                       kp_m               = 0.f;   // survey chainage (KP) in metres
    float                       layback_m          = 0.f;   // horizontal distance ship → towfish
    float                       cable_out_m        = 0.f;   // cable paid out
    float                       fish_delta_x_m     = 0.f;   // towfish easting offset from ship
    float                       fish_delta_y_m     = 0.f;   // towfish northing offset from ship
    // Processing state (persisted in DLPD)
    uint32_t                    correction_flags   = 0;     // bitmask of CorrectionFlag
    uint8_t                     qc_flags           = 0;     // bitmask of QcFlag
    BottomPick                  bottom_pick;                // result from BottomDetectNode
    // Amplitude samples
    std::vector<SidescanSample> samples;
};

} // namespace dolphin::core
