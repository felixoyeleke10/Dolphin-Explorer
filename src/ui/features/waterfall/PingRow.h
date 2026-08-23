#pragma once
#include <cstdint>
#include <vector>

namespace dolphin::ui {

// Explicit seabed state for one row.
struct SeabedDetectionResult {
    float range_m    = -1.f;
    float confidence = 0.f;   // 0-1 quality estimate; 0 = unknown/failed
    bool  detected   = false;
    bool  is_manual  = false;
};

// One assembled waterfall ping row.
//
// Amplitudes are stored as 16-bit physical samples. Display transforms
// (stretch, gain, contrast, threshold, palette) are applied at render time.
// Per-sample slant ranges are preserved when the source file provides them so
// the seabed detector can use the true non-linear sample spacing.  When the
// source does not provide per-sample ranges the vectors are left empty and the
// detector falls back to linear interpolation from 0..slant_range_m.
struct PingRow {
    std::vector<uint16_t> port;          // 0-65535 amplitude, nadir at index 0
    std::vector<uint16_t> stbd;          // 0-65535 amplitude, nadir at index 0
    std::vector<float>    port_ranges;   // per-sample slant range (m); empty = linear
    std::vector<float>    stbd_ranges;   // per-sample slant range (m); empty = linear
    int64_t               timestamp_us  = 0;
    float                 slant_range_m = 0.f;
    SeabedDetectionResult seabed;        // bottom-track result + manual-edit flag
    int64_t               artifact_id   = 0;
    double                lat           = 0.0;  // nav position (0.0 = unavailable)
    double                lon           = 0.0;
    bool                  is_projected  = false;
    float                 heading_deg   = 0.f;  // vessel heading, clockwise from North
    float                 altitude_m    = 0.f;  // sensor altitude above seabed
    float                 port_altitude_m = 0.f; // trusted per-channel SRC reference
    float                 stbd_altitude_m = 0.f;
};

} // namespace dolphin::ui
