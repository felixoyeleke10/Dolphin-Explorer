#pragma once

namespace dolphin::ui {

struct SbpGainParams {
    bool  static_gain_en = false;
    float static_gain_db = 0.0f;   // dB; linear factor = 10^(dB/20); range -20..+20
    bool  agc_en         = false;
    int   agc_window     = 20;     // half-window in traces for running RMS normalisation
    bool  normalize_en   = false;  // per-trace peak normalization (divide by trace max)
};

} // namespace dolphin::ui
