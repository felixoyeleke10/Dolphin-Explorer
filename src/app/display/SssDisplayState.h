#pragma once
#include "app/display/WaterfallParams.h"

namespace dolphin::ui {

// Per-layer SSS display state.  Owned by DataLayer; read by
// WaterfallWindow::applyDisplayState() on every layer switch.
// Canonical location: app/display/ (no Qt dependency).
struct SssDisplayState {
    WaterfallParams params;
    bool show_amp_bar = true;
    bool customized   = false;  // true once the user has explicitly applied params
};

} // namespace dolphin::ui
