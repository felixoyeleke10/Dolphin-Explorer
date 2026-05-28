// SSSPalette.cpp - colour palette construction.
//
// Palettes consume normalized float display intensity. The only quantisation
// here is final screen colour construction through QRgb.

#include "render/sonar/SSSPalette.h"

#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <QString>

namespace dolphin::ui {

namespace {

struct Stop { float pos; int r, g, b; };

int channel(float v)
{
    return qBound(0, static_cast<int>(v + 0.5f), 255);
}

QRgb lerpStops(float t, const Stop* stops, int n)
{
    t = std::clamp(t, 0.f, 1.f);
    if (t <= stops[0].pos)   return qRgb(stops[0].r,   stops[0].g,   stops[0].b);
    if (t >= stops[n-1].pos) return qRgb(stops[n-1].r, stops[n-1].g, stops[n-1].b);

    for (int i = 0; i < n - 1; ++i) {
        if (t <= stops[i+1].pos) {
            const float s = (t - stops[i].pos) / (stops[i+1].pos - stops[i].pos);
            const auto lerp = [s](int a, int b) {
                return channel(a + s * (b - a));
            };
            return qRgb(lerp(stops[i].r, stops[i+1].r),
                        lerp(stops[i].g, stops[i+1].g),
                        lerp(stops[i].b, stops[i+1].b));
        }
    }
    return qRgb(stops[n-1].r, stops[n-1].g, stops[n-1].b);
}

} // namespace

void SSSPalette::build(int palette_index)
{
    for (int i = 0; i < 65536; ++i)
        m_table[static_cast<size_t>(i)] = color(static_cast<uint16_t>(i), palette_index);
}

QRgb SSSPalette::color(float intensity, int palette_index)
{
    switch (palette_index) {
    case PaletteIndex::Greyscale: return greyscale(intensity);
    case PaletteIndex::Ocean:     return ocean(intensity);
    case PaletteIndex::Copper:    return copper(intensity);
    case PaletteIndex::Inverted:  return inverted(intensity);
    case PaletteIndex::Viridis:   return viridis(intensity);
    case PaletteIndex::Plasma:    return plasma(intensity);
    case PaletteIndex::Midnight:  return midnight(intensity);
    case PaletteIndex::Sand:      return sand(intensity);
    case PaletteIndex::Spectrum:  return spectrum(intensity);
    default:                      return thermal(intensity);
    }
}

const char* SSSPalette::name(int palette_index)
{
    switch (palette_index) {
    case PaletteIndex::Thermal:   return "Thermal";
    case PaletteIndex::Greyscale: return "Greyscale";
    case PaletteIndex::Ocean:     return "Ocean";
    case PaletteIndex::Copper:    return "Copper";
    case PaletteIndex::Inverted:  return "Inverted";
    case PaletteIndex::Viridis:   return "Viridis";
    case PaletteIndex::Plasma:    return "Plasma";
    case PaletteIndex::Midnight:  return "Midnight";
    case PaletteIndex::Sand:      return "Sand";
    case PaletteIndex::Spectrum:  return "Spectrum";
    default:                      return "Thermal";
    }
}

int SSSPalette::indexFromName(const QString& name)
{
    if (name == QLatin1String("Gray")    || name == QLatin1String("Greyscale")) return PaletteIndex::Greyscale;
    if (name == QLatin1String("Hot")     || name == QLatin1String("Thermal"))   return PaletteIndex::Thermal;
    if (name == QLatin1String("Copper"))                                         return PaletteIndex::Copper;
    if (name == QLatin1String("Viridis"))                                        return PaletteIndex::Viridis;
    if (name == QLatin1String("Turbo")   || name == QLatin1String("Spectrum"))  return PaletteIndex::Spectrum;
    if (name == QLatin1String("Ocean"))                                          return PaletteIndex::Ocean;
    if (name == QLatin1String("Inverted"))                                       return PaletteIndex::Inverted;
    if (name == QLatin1String("Plasma"))                                         return PaletteIndex::Plasma;
    if (name == QLatin1String("Midnight"))                                       return PaletteIndex::Midnight;
    if (name == QLatin1String("Sand"))                                           return PaletteIndex::Sand;
    bool ok = false;
    const int v = name.toInt(&ok);
    if (ok && v >= 0 && v < PaletteIndex::Count) return v;
    return PaletteIndex::Greyscale;
}

QRgb SSSPalette::thermal(float t)
{
    static constexpr Stop kStops[] = {
        { 0.000f,   5,   0,   0 },
        { 0.200f,  80,   0,   0 },
        { 0.380f, 200,   0,   0 },
        { 0.550f, 255,  80,   0 },
        { 0.720f, 255, 200,   0 },
        { 0.870f, 255, 255, 100 },
        { 1.000f, 255, 255, 255 },
    };
    return lerpStops(t, kStops, std::size(kStops));
}

QRgb SSSPalette::greyscale(float t)
{
    const int k = channel(std::clamp(t, 0.f, 1.f) * 255.f);
    return qRgb(k, k, k);
}

QRgb SSSPalette::ocean(float t)
{
    static constexpr Stop kStops[] = {
        { 0.000f,   0,   0,  18 },
        { 0.170f,   0,  22,  68 },
        { 0.340f,   0,  62, 140 },
        { 0.510f,   0, 115, 195 },
        { 0.670f,   0, 175, 218 },
        { 0.820f,  40, 218, 232 },
        { 0.940f, 155, 240, 248 },
        { 1.000f, 230, 252, 255 },
    };
    return lerpStops(t, kStops, std::size(kStops));
}

QRgb SSSPalette::copper(float t)
{
    static constexpr Stop kStops[] = {
        { 0.000f,   6,   2,   0 },
        { 0.175f,  52,  17,   4 },
        { 0.360f, 120,  52,  14 },
        { 0.550f, 185,  95,  35 },
        { 0.730f, 228, 152,  75 },
        { 0.880f, 248, 210, 148 },
        { 1.000f, 255, 242, 220 },
    };
    return lerpStops(t, kStops, std::size(kStops));
}

QRgb SSSPalette::inverted(float t)
{
    const int k = channel((1.f - std::clamp(t, 0.f, 1.f)) * 255.f);
    return qRgb(k, k, k);
}

QRgb SSSPalette::viridis(float t)
{
    static constexpr Stop kStops[] = {
        { 0.000f,  68,   1,  84 },
        { 0.143f,  71,  44, 122 },
        { 0.286f,  59,  82, 139 },
        { 0.429f,  44, 113, 142 },
        { 0.571f,  33, 145, 140 },
        { 0.714f,  94, 201,  98 },
        { 0.857f, 172, 220,  52 },
        { 1.000f, 253, 231,  37 },
    };
    return lerpStops(t, kStops, std::size(kStops));
}

QRgb SSSPalette::plasma(float t)
{
    static constexpr Stop kStops[] = {
        { 0.000f,  13,   8, 135 },
        { 0.167f,  84,   2, 163 },
        { 0.333f, 139,  10, 165 },
        { 0.500f, 185,  50, 137 },
        { 0.667f, 219,  92, 104 },
        { 0.833f, 244, 136,  73 },
        { 1.000f, 252, 231,  37 },
    };
    return lerpStops(t, kStops, std::size(kStops));
}

QRgb SSSPalette::midnight(float t)
{
    static constexpr Stop kStops[] = {
        { 0.000f,   0,   0,   0 },
        { 0.200f,  10,   3,  60 },
        { 0.400f,  50,   0, 145 },
        { 0.600f, 140,   0, 210 },
        { 0.800f, 240,  70, 220 },
        { 1.000f, 255, 255, 255 },
    };
    return lerpStops(t, kStops, std::size(kStops));
}

QRgb SSSPalette::sand(float t)
{
    static constexpr Stop kStops[] = {
        { 0.000f,  15,  10,   5 },
        { 0.250f,  65,  38,  12 },
        { 0.500f, 140,  95,  45 },
        { 0.750f, 205, 168, 110 },
        { 1.000f, 250, 235, 205 },
    };
    return lerpStops(t, kStops, std::size(kStops));
}

QRgb SSSPalette::spectrum(float t)
{
    static constexpr Stop kStops[] = {
        { 0.000f,   0,   0,   0 },
        { 0.125f,   0,   0, 210 },
        { 0.250f,   0, 160, 255 },
        { 0.375f,   0, 230, 200 },
        { 0.500f,   0, 210,   0 },
        { 0.625f, 210, 230,   0 },
        { 0.750f, 255, 120,   0 },
        { 0.875f, 235,  20,   0 },
        { 1.000f, 255, 255, 255 },
    };
    return lerpStops(t, kStops, std::size(kStops));
}

} // namespace dolphin::ui
