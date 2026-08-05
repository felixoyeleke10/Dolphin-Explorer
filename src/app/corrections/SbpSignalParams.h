#pragma once

namespace dolphin::app {

struct SbpSignalParams {
    bool  envelope_en    = false;   // full-wave rectified amplitude
    bool  dc_removal_en  = false;   // subtract per-trace mean before display
    bool  bandpass_en    = false;   // zero-phase Butterworth bandpass
    float bp_lo_hz       = 100.0f;  // bandpass low cut-off (Hz)
    float bp_hi_hz       = 3000.0f; // bandpass high cut-off (Hz)
};

} // namespace dolphin::app
