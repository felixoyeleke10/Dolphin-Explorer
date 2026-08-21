#pragma once
#include "app/display/SubBottomDisplayParams.h"
#include "app/corrections/SbpGainParams.h"
#include "app/corrections/SbpSignalParams.h"

namespace dolphin::ui {

// Per-layer SBP display state.  Owned by DataLayer; read by
// SubBottomWindow::applyDisplayState() on every layer switch.
// Canonical location: app/display/ (no Qt dependency).
struct SbpDisplayState {
    SubBottomDisplayParams display;
    dolphin::app::SbpGainParams   gain;
    dolphin::app::SbpSignalParams signal;
    bool display_customized = false;
    bool gain_customized    = false;
    bool signal_customized  = false;
};

inline bool hasSbpProcessing(const dolphin::app::SbpGainParams& gain,
                             const dolphin::app::SbpSignalParams& signal) noexcept
{
    return gain.static_gain_en || gain.agc_en || gain.normalize_en
        || signal.envelope_en || signal.dc_removal_en || signal.bandpass_en;
}

} // namespace dolphin::ui
