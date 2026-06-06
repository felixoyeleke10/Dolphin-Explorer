#pragma once

// App-layer correction parameter structs for sidescan amplitude processing.
// These are pure data with no Qt/UI dependency; WaterfallParams.h forwards
// them into namespace dolphin::ui via using aliases.

namespace dolphin::app {

// -- AGC ----------------------------------------------------------------------

enum class AgcMode          { Global = 0, Variable = 1 };
enum class AgcSmoothingType { Mean   = 0, Median   = 1 };

struct AgcParams {
    bool             enabled           = false;
    AgcMode          mode              = AgcMode::Global;
    float            strength          = 0.5f;
    int              along_track_win   = 50;
    AgcSmoothingType smoothing_type    = AgcSmoothingType::Mean;
    int              smoothing_win     = 5;
    int              edge_skip_samples = 50;
    float            noise_floor_pct   = 2.f;
};
inline bool operator==(const AgcParams& a, const AgcParams& b) noexcept
{
    return a.enabled == b.enabled && a.mode == b.mode && a.strength == b.strength
        && a.along_track_win == b.along_track_win && a.smoothing_type == b.smoothing_type
        && a.smoothing_win == b.smoothing_win && a.edge_skip_samples == b.edge_skip_samples
        && a.noise_floor_pct == b.noise_floor_pct;
}
inline bool operator!=(const AgcParams& a, const AgcParams& b) noexcept { return !(a == b); }

// -- TVG ----------------------------------------------------------------------

struct TvgParams {
    bool  enabled    = false;
    float spreading  = 20.f;
    float absorption = 0.f;
};
inline bool operator==(const TvgParams& a, const TvgParams& b) noexcept {
    return a.enabled == b.enabled && a.spreading == b.spreading && a.absorption == b.absorption;
}
inline bool operator!=(const TvgParams& a, const TvgParams& b) noexcept { return !(a == b); }

// -- ARC ----------------------------------------------------------------------

struct ArcParams {
    bool  enabled     = false;
    float exponent    = 1.5f;
    float gain_cap_db = 12.f;
};
inline bool operator==(const ArcParams& a, const ArcParams& b) noexcept {
    return a.enabled == b.enabled && a.exponent == b.exponent && a.gain_cap_db == b.gain_cap_db;
}
inline bool operator!=(const ArcParams& a, const ArcParams& b) noexcept { return !(a == b); }

// -- Composite ----------------------------------------------------------------

struct SidescanCorrectionParams {
    TvgParams tvg;
    ArcParams arc;
    AgcParams agc;
};

} // namespace dolphin::app
