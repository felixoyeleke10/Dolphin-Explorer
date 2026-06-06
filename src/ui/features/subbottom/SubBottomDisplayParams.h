#pragma once

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  SubBottomDisplayParams — display parameter snapshot passed via signals and
//  stored as per-layer display state.
//
//  No Qt dependency — safe to include from any layer.
//  palette_index 0 == SbpPalette::Greyscale (see SubBottomPalette.h for full enum).
// -----------------------------------------------------------------------------
struct SubBottomDisplayParams {
    int   palette_index      = 0;       // SbpPalette::Greyscale
    float gain               = 1.0f;   // amplitude multiplier before palette mapping
    float contrast           = 1.0f;   // power-curve exponent; 1 = linear, >1 = brighter mids
    bool  polarity_invert    = false;  // flip sample sign before palette mapping
    bool  show_bottom_track  = true;   // draw pre-computed seabed pick overlay
    float sound_speed_ms     = 1500.0f; // full round-trip speed (depth = time * speed / 2)
};

} // namespace dolphin::ui
