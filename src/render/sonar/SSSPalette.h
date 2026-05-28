#pragma once
#include "render/sonar/SonarDisplayParams.h"

#include <QRgb>
#include <QString>
#include <array>
#include <cstdint>

namespace dolphin::ui {

// Colour palette for sidescan sonar imagery.
//
// Palette input is normalized display intensity in [0,1]. A table instance can
// cache the full uint16 domain for CPU rendering, while the GL path evaluates
// the same palette math in the fragment shader.
class SSSPalette {
public:
    void build(int palette_index);

    QRgb operator[](uint16_t v) const { return m_table[static_cast<size_t>(v)]; }

    static QRgb color(float intensity, int palette_index);
    static QRgb color(uint16_t intensity, int palette_index)
    {
        return color(static_cast<float>(intensity) / 65535.f, palette_index);
    }

    static const char* name(int palette_index);
    // Returns the palette index for a stored name string (e.g. "Gray", "Thermal").
    // Falls back to Greyscale for unknown names.
    static int indexFromName(const QString& name);

private:
    std::array<QRgb, 65536> m_table{};

    static QRgb thermal  (float t);
    static QRgb greyscale(float t);
    static QRgb ocean    (float t);
    static QRgb copper   (float t);
    static QRgb inverted (float t);
    static QRgb viridis  (float t);
    static QRgb plasma   (float t);
    static QRgb midnight (float t);
    static QRgb sand     (float t);
    static QRgb spectrum (float t);
};

} // namespace dolphin::ui
