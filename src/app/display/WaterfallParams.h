#pragma once
#include "render/sonar/SonarDisplayParams.h"
// AgcParams, TvgParams, ArcParams canonical definitions live in the app layer.
#include "app/corrections/SidescanCorrectionParams.h"
#include "pipeline/SidescanEnhancementAlgorithms.h"

namespace dolphin::ui {

// Aliases so all existing UI code continues to use unqualified names.
using AgcMode          = dolphin::app::AgcMode;
using AgcSmoothingType = dolphin::app::AgcSmoothingType;
using AgcParams        = dolphin::app::AgcParams;
using TvgParams        = dolphin::app::TvgParams;
using ArcParams        = dolphin::app::ArcParams;

// -- ArnParams — Adaptive Range Normalisation ---------------------------------
using ArnParams = dolphin::pipeline::enhancement::ArnSettings;
inline bool operator==(const ArnParams& a, const ArnParams& b) noexcept {
    return a.enabled == b.enabled && a.strength == b.strength
        && a.gain_cap_db == b.gain_cap_db && a.column_smooth == b.column_smooth;
}
inline bool operator!=(const ArnParams& a, const ArnParams& b) noexcept { return !(a == b); }

// -- DestripeParams — along-track destriping ----------------------------------
using DestripeParams = dolphin::pipeline::enhancement::DestripeSettings;
inline bool operator==(const DestripeParams& a, const DestripeParams& b) noexcept {
    return a.enabled == b.enabled && a.window == b.window
        && a.subdivision == b.subdivision && a.capping == b.capping
        && a.threshold_db == b.threshold_db;
}
inline bool operator!=(const DestripeParams& a, const DestripeParams& b) noexcept { return !(a == b); }

// -- BeamPatternParams — cross-track beam pattern normalisation ---------------
using BeamPatternParams = dolphin::pipeline::enhancement::BeamPatternSettings;
inline bool operator==(const BeamPatternParams& a, const BeamPatternParams& b) noexcept {
    return a.enabled == b.enabled && a.strength == b.strength
        && a.smooth_radius == b.smooth_radius && a.gain_cap_db == b.gain_cap_db;
}
inline bool operator!=(const BeamPatternParams& a, const BeamPatternParams& b) noexcept { return !(a == b); }

// -- MlEnhanceParams — adaptive local contrast enhancement (CLAHE-like) -------
using MlEnhanceParams = dolphin::pipeline::enhancement::AdaptiveContrastSettings;
inline bool operator==(const MlEnhanceParams& a, const MlEnhanceParams& b) noexcept {
    return a.enabled == b.enabled && a.tile_pings == b.tile_pings
        && a.tile_samps == b.tile_samps && a.clip_limit == b.clip_limit;
}
inline bool operator!=(const MlEnhanceParams& a, const MlEnhanceParams& b) noexcept { return !(a == b); }

// -- WaterfallParams — display parameters for the sidescan waterfall ----------
// Pure data; no Qt dependency.  Canonical location: app/display/.
// ui/features/waterfall/WaterfallParams.h is a forwarding include for callers
// that have not yet updated their include path.
enum class DisplayChannel { Both = 0, Port = 1, Starboard = 2 };

struct WaterfallParams : SonarDisplayParams {
    AgcParams         agc;
    TvgParams         tvg;
    ArnParams         arn;
    DestripeParams    destripe;
    BeamPatternParams beam_pattern;
    ArcParams         arc;
    MlEnhanceParams   ml_enhance;
    bool           slant_range_correction = false;
    DisplayChannel display_channel        = DisplayChannel::Both;
};

inline bool hasSidescanProcessing(const WaterfallParams& p) noexcept
{
    return p.tvg.enabled || p.agc.enabled || p.arc.enabled || p.arn.enabled
        || p.destripe.enabled || p.beam_pattern.enabled || p.ml_enhance.enabled
        || p.slant_range_correction;
}

// Reset scientific processing while retaining appearance-only choices (palette,
// gain/contrast/range and channel). Used by every revert workflow.
inline WaterfallParams withoutSidescanProcessing(const WaterfallParams& p)
{
    WaterfallParams result = p;
    result.tvg = {};
    result.agc = {};
    result.arc = {};
    result.arn = {};
    result.destripe = {};
    result.beam_pattern = {};
    result.ml_enhance = {};
    result.slant_range_correction = false;
    return result;
}

// Converts WaterfallParams to the app-layer correction subset.
inline dolphin::app::SidescanCorrectionParams toCorrectionParams(const WaterfallParams& p)
{
    return { p.tvg, p.arc, p.agc };
}

} // namespace dolphin::ui
