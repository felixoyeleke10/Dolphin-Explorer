#pragma once

namespace dolphin::ui {

// Parameters for on-demand navigation corrections (sidescan waterfall + sub-bottom).
// Modality-agnostic; owned per-layer by DataLayer::nav_state and applied only when
// the user explicitly clicks Apply — never auto-applied on data load.
//
// Canonical location: app/display/ (no Qt dependency) so the app model and every
// UI feature can share it without a cross-feature include.
struct NavProcessingParams {
    // -- Position ----------------------------------------------------------
    bool  smooth_enabled  = false;
    int   smooth_window   = 5;    // total running-average window length in pings
    bool  layback_enabled = false;
    float layback_m       = 0.f;  // horizontal cable layback distance (m)

    // -- Attitude offsets (constant corrections added to every ping) -------
    float heading_offset_deg = 0.f;
    float pitch_offset_deg   = 0.f;
    float roll_offset_deg    = 0.f;
};

} // namespace dolphin::ui
