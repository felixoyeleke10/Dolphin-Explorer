#pragma once
// SwathRasterizer.h — bilinear sidescan swath rasterizer for the map mosaic.
//
// Replaces the flat-colour triangle pair that MapViewPaint used to draw for
// each sonar cell.  For every pixel inside the cell the amplitude is bilinearly
// interpolated from the four corner values, giving smooth variation both
// along-track and across-track — closer to the SonarWiz-style mosaic quality.
//
// Corner layout (matches MapViewPaint variable names):
//
//   pa --- na     ← previous ping strip (top edge)
//   |          |
//   pb --- nb     ← current  ping strip (bottom edge)
//
//   a00 = amplitude at pa    a10 = amplitude at na
//   a01 = amplitude at pb    a11 = amplitude at nb
//
// Pixel-level inverse mapping uses a parallelogram approximation
// (2×2 linear solve).  This is exact for rectangular cells and accurate for
// the slightly skewed cells produced by gradual heading changes, which covers
// virtually all real survey data.
//
// A 65 536-entry uint16 → QRgb LUT is built once per mosaic build call
// so the per-pixel path is: 4 FMAs for bilinear + 1 array lookup.

#include "render/sonar/SSSAmplitudeProcessor.h"
#include "render/sonar/SSSPalette.h"
#include "render/sonar/SonarDisplayParams.h"

#include <QPointF>
#include <QRgb>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace dolphin::ui {

class SwathRasterizer {
public:
    // Build the uint16 → QRgb LUT from the current display parameters.
    // Call once per buildLayerMosaic() invocation before any rasterizeCell calls.
    void buildLut(const SonarDisplayParams& params, int palette_index);

    // Rasterize one bilinear quad cell into a QRgb pixel buffer.
    //
    // pixels        – flat row-major QImage::Format_RGB32 buffer
    // img_w, img_h  – buffer dimensions in physical pixels
    // pa, na        – physical-pixel corners on the previous ping strip (top edge)
    // pb, nb        – physical-pixel corners on the current  ping strip (bottom edge)
    // a00..a11      – uint16 amplitudes at each corner (see layout above)
    // amp_out       – optional parallel uint16 buffer (same size as pixels).
    //                 When non-null, receives the raw bilinear amplitude at each
    //                 written pixel so callers can rebuild the QImage with a
    //                 different palette without re-rasterizing geometry.
    //                 0 = no data (transparent); values stored as amp+1 so true
    //                 zero amplitude is distinguishable from "not written".
    void rasterizeCell(QRgb*     pixels,
                       int       img_w,
                       int       img_h,
                       QPointF   pa,
                       QPointF   na,
                       QPointF   pb,
                       QPointF   nb,
                       uint16_t  a00,
                       uint16_t  a10,
                       uint16_t  a01,
                       uint16_t  a11,
                       int       max_cell_pix = std::numeric_limits<int>::max(),
                       uint16_t* amp_out      = nullptr) const noexcept;

private:
    std::array<QRgb, 65536> m_lut{};
};

// -----------------------------------------------------------------------------

inline void SwathRasterizer::buildLut(const SonarDisplayParams& params,
                                      int palette_index)
{
    for (int i = 0; i < 65536; ++i) {
        const float intensity = SSSAmplitudeProcessor::displayIntensity(
            static_cast<uint16_t>(i), params);
        m_lut[static_cast<size_t>(i)] =
            SSSPalette::color(intensity, palette_index);
    }
}

inline void SwathRasterizer::rasterizeCell(QRgb*     pixels,
                                            int       img_w,
                                            int       img_h,
                                            QPointF   pa,
                                            QPointF   na,
                                            QPointF   pb,
                                            QPointF   nb,
                                            uint16_t  a00,
                                            uint16_t  a10,
                                            uint16_t  a01,
                                            uint16_t  a11,
                                            int       max_cell_pix,
                                            uint16_t* amp_out) const noexcept
{
    // Parallelogram basis from pa:
    //   P ≈ pa  +  u * ex  +  v * ey   for u, v ∈ [0, 1)
    //
    // Half-open intervals give each pixel a unique owner.  The pixel on the
    // far edge of cell j (u = 1) is the near edge of cell j+1 (u = 0), so
    // excluding u ≥ 1 here means the adjacent cell will claim it without any
    // gap or double-write.  The final cell in each direction has no neighbour,
    // but its far-edge pixels are at the swath boundary (nadir dead zone or
    // far-range limit) where the amplitude contribution is negligible.
    const float ex_x = static_cast<float>(na.x() - pa.x());
    const float ex_y = static_cast<float>(na.y() - pa.y());
    const float ey_x = static_cast<float>(pb.x() - pa.x());
    const float ey_y = static_cast<float>(pb.y() - pa.y());

    const float det = ex_x * ey_y - ex_y * ey_x;
    if (std::abs(det) < 0.25f) return;   // degenerate / near-zero-area cell
    const float inv_det = 1.0f / det;

    // Pixel-aligned bounding box, clamped to image bounds.
    const float xs[4] = {
        static_cast<float>(pa.x()), static_cast<float>(na.x()),
        static_cast<float>(pb.x()), static_cast<float>(nb.x())
    };
    const float ys[4] = {
        static_cast<float>(pa.y()), static_cast<float>(na.y()),
        static_cast<float>(pb.y()), static_cast<float>(nb.y())
    };

    const int xmin = std::max(0,         static_cast<int>(std::floor(*std::min_element(xs, xs + 4))));
    const int xmax = std::min(img_w - 1, static_cast<int>(std::ceil (*std::max_element(xs, xs + 4))));
    const int ymin = std::max(0,         static_cast<int>(std::floor(*std::min_element(ys, ys + 4))));
    const int ymax = std::min(img_h - 1, static_cast<int>(std::ceil (*std::max_element(ys, ys + 4))));

    if (xmin > xmax || ymin > ymax) return;

    // Per-cell pixel-budget guard: caller passes max_cell_pix (INT_MAX = disabled).
    if ((xmax - xmin) > max_cell_pix || (ymax - ymin) > max_cell_pix) return;

    const float ox = static_cast<float>(pa.x());
    const float oy = static_cast<float>(pa.y());

    // Incremental u, v steps along the pixel-column axis.
    // u = (hx*ey_y - hy*ey_x) * inv_det  →  du/dpx = ey_y * inv_det
    // v = (ex_x*hy - ex_y*hx) * inv_det  →  dv/dpx = -ex_y * inv_det
    const float u_step = ey_y * inv_det;
    const float v_step = -ex_y * inv_det;

    // Amplitude terms for the bilinear formula:
    //   amp = a00 + da_u*u + da_v*v + da_uv*u*v
    const float fa00  = static_cast<float>(a00);
    const float da_u  = static_cast<float>(a10) - fa00;
    const float da_v  = static_cast<float>(a01) - fa00;
    const float da_uv = fa00 - static_cast<float>(a10)
                             - static_cast<float>(a01)
                             + static_cast<float>(a11);

    for (int py = ymin; py <= ymax; ++py) {
        QRgb* const row = pixels + py * img_w;

        const float hy   = static_cast<float>(py) + 0.5f - oy;
        const float hx0  = static_cast<float>(xmin) + 0.5f - ox;

        // u and v at the leftmost column of this row.
        float u = (hx0 * ey_y - hy * ey_x) * inv_det;
        float v = (ex_x * hy - ex_y * hx0) * inv_det;

        for (int px = xmin; px <= xmax; ++px, u += u_step, v += v_step) {
            // Half-open [0, 1): far-edge pixels belong to the adjacent cell.
            if (u < 0.f || u >= 1.f || v < 0.f || v >= 1.f) continue;

            // Bilinear amplitude interpolation.
            const float amp_f = fa00 + da_u * u + da_v * v + da_uv * u * v;

            // Clamp to uint16 range and look up pre-built colour.
            const int amp_i = static_cast<int>(amp_f + 0.5f);
            const int clamped = amp_i < 0 ? 0 : amp_i > 65535 ? 65535 : amp_i;
            row[px] = m_lut[static_cast<size_t>(clamped)];

            // Store raw amplitude for intensity cache (palette-free recolor).
            // Use clamped+1 so 0 stays reserved as the "no data" sentinel.
            if (amp_out)
                amp_out[py * img_w + px] =
                    static_cast<uint16_t>(clamped < 65535 ? clamped + 1 : 65535);
        }
    }
}

} // namespace dolphin::ui
