#pragma once
#include "render/sonar/SonarDisplayParams.h"

namespace dolphin::ui {

// ─────────────────────────────────────────────────────────────────────────────
//  AgcParams — automatic gain control settings for sidescan amplitude.
//
//  Global   — each ping normalised independently by its own mean amplitude.
//  Variable — along-track sliding window: each ping normalised by the mean
//             of its neighbourhood, so slow drift in system response is
//             equalised while short-scale features are preserved.
// ─────────────────────────────────────────────────────────────────────────────

enum class AgcMode          { Global = 0, Variable = 1 };
enum class AgcSmoothingType { Mean   = 0, Median   = 1 };

struct AgcParams {
    bool             enabled          = false;
    AgcMode          mode             = AgcMode::Global;
    float            strength         = 0.5f;   // 0.0 – 1.0  (blend: 0=off, 1=full)
    int              along_track_win  = 50;      // pings — Variable mode window half-width
    AgcSmoothingType smoothing_type   = AgcSmoothingType::Mean;
    int              smoothing_win    = 5;       // samples — post-gain smoothing kernel
    int              edge_skip_samples = 50;     // samples near nadir to exclude
    float            noise_floor_pct  = 2.f;    // % amplitude below which samples are noise
};

inline bool operator==(const AgcParams& a, const AgcParams& b) noexcept
{
    return a.enabled            == b.enabled
        && a.mode               == b.mode
        && a.strength           == b.strength
        && a.along_track_win    == b.along_track_win
        && a.smoothing_type     == b.smoothing_type
        && a.smoothing_win      == b.smoothing_win
        && a.edge_skip_samples  == b.edge_skip_samples
        && a.noise_floor_pct    == b.noise_floor_pct;
}
inline bool operator!=(const AgcParams& a, const AgcParams& b) noexcept { return !(a == b); }

// ─────────────────────────────────────────────────────────────────────────────
//  TvgParams — Time-Varying Gain: compensates range-dependent spreading loss.
//  Applied to raw 16-bit pings before assembly.  Gain = 0 dB at blanking_m
//  (the first valid sample per ping), rising with range beyond it.
// ─────────────────────────────────────────────────────────────────────────────

struct TvgParams {
    bool  enabled    = false;
    float spreading  = 20.f;   // dB/decade (20 = spherical, 0 = off)
    float absorption = 0.f;    // additional one-way loss dB/m
};
inline bool operator==(const TvgParams& a, const TvgParams& b) noexcept {
    return a.enabled == b.enabled && a.spreading == b.spreading && a.absorption == b.absorption;
}
inline bool operator!=(const TvgParams& a, const TvgParams& b) noexcept { return !(a == b); }

// ─────────────────────────────────────────────────────────────────────────────
//  ArnParams — Adaptive Range Normalisation.
//  Per-column histogram normalisation on assembled uint16 rows.
//  Removes range-dependent shading without assuming an acoustic model.
//  Uses the 40th-percentile amplitude per column as the reference level.
// ─────────────────────────────────────────────────────────────────────────────

struct ArnParams {
    bool  enabled       = false;
    float strength      = 0.8f;   // blend 0–1 (0 = off, 1 = full correction)
    float gain_cap_db   = 12.f;   // maximum per-column correction in dB
    int   column_smooth = 5;      // half-window for smoothing the reference curve
};
inline bool operator==(const ArnParams& a, const ArnParams& b) noexcept {
    return a.enabled == b.enabled && a.strength == b.strength
        && a.gain_cap_db == b.gain_cap_db && a.column_smooth == b.column_smooth;
}
inline bool operator!=(const ArnParams& a, const ArnParams& b) noexcept { return !(a == b); }

// ─────────────────────────────────────────────────────────────────────────────
//  DestripeParams — along-track destriping.
//  Removes horizontal banding caused by ping-to-ping gain variations.
//  Divides the swath into range subdivisions and normalises each independently:
//  for each ping, the local window mean is divided into the global mean to
//  produce a correction factor, capped at ±capping.
// ─────────────────────────────────────────────────────────────────────────────

struct DestripeParams {
    bool  enabled     = false;
    int   window      = 50;    // along-track window half-width (pings)
    int   subdivision = 4;     // number of range zones processed independently
    float capping     = 2.0f;  // maximum correction factor (1 = no correction)
};
inline bool operator==(const DestripeParams& a, const DestripeParams& b) noexcept {
    return a.enabled == b.enabled && a.window == b.window
        && a.subdivision == b.subdivision && a.capping == b.capping;
}
inline bool operator!=(const DestripeParams& a, const DestripeParams& b) noexcept { return !(a == b); }

// ─────────────────────────────────────────────────────────────────────────────
//  BeamPatternParams — cross-track beam pattern normalisation.
//  Post-assembly (uint16 rows): per-column mean normalisation removes transducer
//  sensitivity variation by dividing each sample by its column's smoothed mean.
// ─────────────────────────────────────────────────────────────────────────────

struct BeamPatternParams {
    bool  enabled       = false;
    float strength      = 1.0f;   // blend 0–1 (0 = off, 1 = full correction)
    int   smooth_radius = 10;     // half-window for smoothing the column mean curve
};
inline bool operator==(const BeamPatternParams& a, const BeamPatternParams& b) noexcept {
    return a.enabled == b.enabled && a.strength == b.strength && a.smooth_radius == b.smooth_radius;
}
inline bool operator!=(const BeamPatternParams& a, const BeamPatternParams& b) noexcept { return !(a == b); }

// ─────────────────────────────────────────────────────────────────────────────
//  ArcParams — Angle Range Correction.
//  Pre-assembly (raw 16-bit pings): compensates grazing-angle-dependent
//  backscatter using a Lambert-law model (gain ∝ 1/sin^exponent(grazing)).
//  Grazing angle is derived from altitude and per-sample slant range.
// ─────────────────────────────────────────────────────────────────────────────

struct ArcParams {
    bool  enabled      = false;
    float exponent     = 1.5f;   // Lambert-law exponent (1 = cosine, 2 = cos²)
    float gain_cap_db  = 12.f;   // maximum gain applied at far range (dB); prevents saturation
};
inline bool operator==(const ArcParams& a, const ArcParams& b) noexcept {
    return a.enabled == b.enabled && a.exponent == b.exponent && a.gain_cap_db == b.gain_cap_db;
}
inline bool operator!=(const ArcParams& a, const ArcParams& b) noexcept { return !(a == b); }

// ─────────────────────────────────────────────────────────────────────────────
//  MlEnhanceParams — adaptive local contrast enhancement (CLAHE-like).
//  Post-assembly (uint16 rows): equalises local histograms in tiles to improve
//  contrast in both shadow and highlight regions simultaneously.
// ─────────────────────────────────────────────────────────────────────────────

struct MlEnhanceParams {
    bool  enabled    = false;
    int   tile_pings = 64;    // tile height in pings (along-track)
    int   tile_samps = 128;   // tile width in samples (across-track)
    float clip_limit = 2.0f;  // histogram clip limit (1 = no clipping, 4 = strong)
};
inline bool operator==(const MlEnhanceParams& a, const MlEnhanceParams& b) noexcept {
    return a.enabled == b.enabled && a.tile_pings == b.tile_pings
        && a.tile_samps == b.tile_samps && a.clip_limit == b.clip_limit;
}
inline bool operator!=(const MlEnhanceParams& a, const MlEnhanceParams& b) noexcept { return !(a == b); }

// ─────────────────────────────────────────────────────────────────────────────
//  WaterfallParams — display parameters for the sidescan waterfall image.
//
//  Inherits SonarDisplayParams (palette; gain/contrast/threshold/smoothing
//  are kept for backward compatibility but are not driven from the UI).
//
//  Pure data struct; no Qt dependency.  Owned by WaterfallView and
//  reconstructed from UI controls by WaterfallWindow::pushParams().
// ─────────────────────────────────────────────────────────────────────────────

enum class DisplayChannel { Both = 0, Port = 1, Starboard = 2 };

struct WaterfallParams : SonarDisplayParams {
    AgcParams         agc;
    TvgParams         tvg;
    ArnParams         arn;
    DestripeParams    destripe;
    BeamPatternParams beam_pattern;
    ArcParams         arc;
    MlEnhanceParams   ml_enhance;
    bool           slant_range_correction = false;   // geometric slant→ground range remap
    DisplayChannel display_channel        = DisplayChannel::Both;
};

} // namespace dolphin::ui
