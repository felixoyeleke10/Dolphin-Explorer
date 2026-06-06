#pragma once
#include "ui/features/subbottom/SubBottomDisplayParams.h"
#include "ui/features/subbottom/SbpGainParams.h"
#include "ui/features/subbottom/SbpSignalParams.h"

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  SbpDisplayState — canonical per-layer display state for sub-bottom profiler.
//
//  Single source of truth for everything that controls how an SBP layer looks.
//  Owned by DataLayer; read by SubBottomWindow::applyDisplayState() on every
//  layer switch before the first paint.
//
//  No Qt dependency — safe to include from any layer.
// -----------------------------------------------------------------------------
struct SbpDisplayState {
    SubBottomDisplayParams display;
    SbpGainParams          gain;
    SbpSignalParams        signal;
    bool display_customized = false;  // true once display (palette/gain/contrast) params applied
    bool gain_customized    = false;  // true once gain params have been explicitly applied
    bool signal_customized  = false;  // true once signal params have been explicitly applied
};

} // namespace dolphin::ui
