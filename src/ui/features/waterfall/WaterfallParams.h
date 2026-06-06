#pragma once
#include "render/sonar/SonarDisplayParams.h"
// AgcParams, TvgParams, ArcParams canonical definitions live in the app layer.
#include "app/corrections/SidescanCorrectionParams.h"

namespace dolphin::ui {

// Aliases so all existing UI code continues to use unqualified names.
using AgcMode          = dolphin::app::AgcMode;
using AgcSmoothingType = dolphin::app::AgcSmoothingType;
using AgcParams        = dolphin::app::AgcParams;
using TvgParams        = dolphin::app::TvgParams;
using ArcParams        = dolphin::app::ArcParams;

// -----------------------------------------------------------------------------
//  ArnParams — Adaptive Range Normalisation.
//  Per-column histogram normalisation on assembled uint16 rows.
//  Removes range-dependent shading without assuming an acoustic model.
//  Uses the 40th-percentile amplitude per column as the reference level.
// -----------------------------------------------------------------------------

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

// -----------------------------------------------------------------------------
//  DestripeParams — along-track destriping.
//  Removes horizontal banding caused by ping-to-ping gain variations.
//  Divides the swath into range subdivisions and normalises each independently:
//  for each ping, the local window mean is divided into the global mean to
//  produce a correction factor, capped at ±capping.
// -----------------------------------------------------------------------------

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

// -----------------------------------------------------------------------------
//  BeamPatternParams — cross-track beam pattern normalisation.
//  Post-assembly (uint16 rows): per-column mean normalisation removes transducer
//  sensitivity variation by dividing each sample by its column's smoothed mean.
// -----------------------------------------------------------------------------

struct BeamPatternParams {
    bool  enabled       = false;
    float strength      = 1.0f;   // blend 0–1 (0 = off, 1 = full correction)
    int   smooth_radius = 10;     // half-window for smoothing the column mean curve
};
inline bool operator==(const BeamPatternParams& a, const BeamPatternParams& b) noexcept {
    return a.enabled == b.enabled && a.strength == b.strength && a.smooth_radius == b.smooth_radius;
}
inline bool operator!=(const BeamPatternParams& a, const BeamPatternParams& b) noexcept { return !(a == b); }

// -----------------------------------------------------------------------------
//  MlEnhanceParams — adaptive local contrast enhancement (CLAHE-like).
//  Post-assembly (uint16 rows): equalises local histograms in tiles to improve
//  contrast in both shadow and highlight regions simultaneously.
// -----------------------------------------------------------------------------

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

// -----------------------------------------------------------------------------
//  WaterfallParams — display parameters for the sidescan waterfall image.
//
//  Inherits SonarDisplayParams (palette; gain/contrast/threshold/smoothing
//  are kept for backward compatibility but are not driven from the UI).
//
//  Pure data struct; no Qt dependency.  Owned by WaterfallView and
//  reconstructed from UI controls by WaterfallWindow::pushParams().
// -----------------------------------------------------------------------------

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

// Converts WaterfallParams to the app-layer correction subset.
// Used by MainWindow callsites that pass params to SidescanCorrectionService.
inline dolphin::app::SidescanCorrectionParams toCorrectionParams(const WaterfallParams& p)
{
    return { p.tvg, p.arc, p.agc };
}

} // namespace dolphin::ui
