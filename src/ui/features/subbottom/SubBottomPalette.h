#pragma once
#include <QRgb>
#include <algorithm>
#include <cmath>

namespace dolphin::ui {

// ─────────────────────────────────────────────────────────────────────────────
//  SbpPalette — colour palettes for sub-bottom profiler amplitude display.
//
//  toRgb() maps a gain-applied, signed float amplitude in [-1, +1] to RGB.
//    Greyscale    — absolute value → luminance (black = silent, white = peak)
//    InvertedGrey — absolute value → inverted luminance (white = silent)
//    Seismic      — signed: positive → red, negative → blue, zero → black
//    Thermal      — absolute value → black→purple→red→orange→yellow
// ─────────────────────────────────────────────────────────────────────────────

struct SbpPalette {
    enum Index { Greyscale = 0, InvertedGrey = 1, Seismic = 2, Thermal = 3, Count = 4 };

    static const char* name(int i)
    {
        static const char* k[] = { "Greyscale", "Inverted Grey", "Seismic", "Thermal" };
        return (i >= 0 && i < Count) ? k[i] : k[0];
    }

    static QRgb toRgb(float amp, int palette_idx)
    {
        const float a = std::clamp(std::abs(amp), 0.f, 1.f);
        switch (palette_idx) {
        case InvertedGrey: {
            const int v = 255 - static_cast<int>(a * 255.f);
            return qRgb(v, v, v);
        }
        case Seismic: {
            const int v = static_cast<int>(a * 255.f);
            return (amp >= 0.f) ? qRgb(v, 0, 0) : qRgb(0, 0, v);
        }
        case Thermal: {
            // black → purple → red → orange → yellow
            const int r = static_cast<int>(std::clamp(a * 3.f,        0.f, 1.f) * 255.f);
            const int g = static_cast<int>(std::clamp(a * 3.f - 1.f,  0.f, 1.f) * 255.f);
            const int b = static_cast<int>(std::clamp(1.f - a * 4.f,  0.f, 1.f) * 255.f);
            return qRgb(r, g, b);
        }
        case Greyscale:
        default: {
            const int v = static_cast<int>(a * 255.f);
            return qRgb(v, v, v);
        }
        }
    }
};

} // namespace dolphin::ui
