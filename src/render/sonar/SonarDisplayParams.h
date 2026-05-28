#pragma once

namespace dolphin::ui {

// Palette index constants — shared by SSSPalette and all sonar UI.
namespace PaletteIndex {
    inline constexpr int Thermal   = 0;   // black → red → orange → yellow → white
    inline constexpr int Greyscale = 1;   // black → white
    inline constexpr int Ocean     = 2;   // deep navy → blue → cyan → white
    inline constexpr int Copper    = 3;   // black → copper → pale gold
    inline constexpr int Inverted  = 4;   // white → black (inverted greyscale)
    inline constexpr int Viridis   = 5;   // purple → teal → green → yellow (perceptually uniform)
    inline constexpr int Plasma    = 6;   // deep purple → magenta → orange → yellow
    inline constexpr int Midnight  = 7;   // black → indigo → violet → magenta → white
    inline constexpr int Sand      = 8;   // dark earth → warm tan → pale cream
    inline constexpr int Spectrum  = 9;   // black → blue → cyan → green → yellow → red
    inline constexpr int Count     = 10;
}

// ─────────────────────────────────────────────────────────────────────────────
//  SonarDisplayParams — shared image-processing parameters for any sonar view.
//
//  Consumed by SSSAmplitudeProcessor and SSSPalette; inherited by
//  WaterfallParams which adds waterfall-specific extensions (AGC, SRC).
//
//  No Qt dependency — pure data, safe to include from any layer.
// ─────────────────────────────────────────────────────────────────────────────

struct SonarDisplayParams {
    float gain      = 1.0f;   // 0.5 – 10.0  (gamma-style brightness)
    float contrast  = 1.0f;   // 0.5 – 3.0   (linear stretch around midpoint)
    float threshold = 0.0f;   // 0.0 – 1.0   (noise floor cut-off)
    float smoothing = 0.0f;   // 0.0 – 5.0   (horizontal box-filter radius in samples)
    int   palette   = PaletteIndex::Greyscale;

    // Non-destructive percentile stretch computed from the data at load time.
    // display_low  — normalised physical amplitude mapped to black (values below → 0)
    // display_high — normalised physical amplitude mapped to white (values above → 255)
    // Both are derived from the 1st / 99th percentile of the assembled uint16 rows so
    // the viewer uses the full dynamic range regardless of the XTF's numeric range.
    // Default 0 / 1 = identity (no stretch) — overwritten by computeAutoStretch().
    float display_low  = 0.0f;
    float display_high = 1.0f;
};

inline bool operator==(const SonarDisplayParams& a, const SonarDisplayParams& b) noexcept
{
    return a.gain         == b.gain
        && a.contrast     == b.contrast
        && a.threshold    == b.threshold
        && a.smoothing    == b.smoothing
        && a.palette      == b.palette
        && a.display_low  == b.display_low
        && a.display_high == b.display_high;
}
inline bool operator!=(const SonarDisplayParams& a, const SonarDisplayParams& b) noexcept
{
    return !(a == b);
}

} // namespace dolphin::ui
