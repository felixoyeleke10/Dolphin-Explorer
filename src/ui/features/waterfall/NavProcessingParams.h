#pragma once

namespace dolphin::ui {

// Parameters for on-demand nav corrections in the waterfall.
// These modify the stored raw pings and are applied only when the user
// explicitly clicks "Apply" — never auto-applied on data load.
struct NavProcessingParams {
    // -- Position ----------------------------------------------------------
    bool  smooth_enabled  = false;
    int   smooth_window   = 5;    // running-average half-window in pings
    bool  layback_enabled = false;
    float layback_m       = 0.f;  // horizontal cable layback distance (m)

    // -- Attitude offsets (constant corrections added to every ping) -------
    float heading_offset_deg = 0.f;
    float pitch_offset_deg   = 0.f;
    float roll_offset_deg    = 0.f;
};

} // namespace dolphin::ui
