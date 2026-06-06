#pragma once
#include "ui/features/waterfall/WaterfallParams.h"

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  SssDisplayState — canonical per-layer display state for sidescan sonar.
//
//  Single source of truth for everything that controls how a SSS layer looks.
//  Owned by DataLayer; read by WaterfallWindow::applyDisplayState() on every
//  layer switch before the first paint.
//
//  No Qt dependency — safe to include from any layer.
// -----------------------------------------------------------------------------
struct SssDisplayState {
    WaterfallParams params;
    bool show_amp_bar = true;
    bool customized   = false;  // true once the user has explicitly applied params
};

} // namespace dolphin::ui
