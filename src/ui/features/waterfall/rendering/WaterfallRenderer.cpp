// WaterfallRenderer.cpp — sidescan raster image building (frame orchestration)
//
// Per-row pixel writing and coordinate mapping → WaterfallRendererRow.cpp
//
// Performance design:
//   m_colour_table — 256-entry QRgb table: direct palette mapping.
//                    Rebuilt in O(256) on any palette change; never per-pixel.
//   m_strip_cache  — caches rendered pixels for each PingRow.  Pure scroll is
//                    O(visible_rows) memcpy with zero per-pixel work.
//                    Invalidated by table_version mismatch (param change) or
//                    geometry change (resize / zoom / pan).

#include "ui/features/waterfall/rendering/WaterfallRenderer.h"
#include "render/sonar/SSSAmplitudeProcessor.h"
#include "ui/shell/Theme.h"
#include <QtGlobal>  // qBound, qMax, qMin
#include <algorithm> // std::fill
#include <cmath>     // std::pow, std::clamp, std::floor
#include <cstring>   // memcpy

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  Construction
// -----------------------------------------------------------------------------

WaterfallRenderer::WaterfallRenderer()
{
    rebuildColourTable();
}

// -----------------------------------------------------------------------------
//  Configuration
// -----------------------------------------------------------------------------

void WaterfallRenderer::setLayout(const WfLayout& layout)
{
    m_layout = layout;
}

void WaterfallRenderer::setParams(const WaterfallParams& params)
{
    const SonarDisplayParams old_display = static_cast<const SonarDisplayParams&>(m_params);
    m_params = params;
    // Colour table depends only on display-facing fields (gain/contrast/threshold/
    // palette/stretch).  Skipping the rebuild when those are unchanged avoids
    // invalidating the entire strip cache on every scroll-window load.
    if (static_cast<const SonarDisplayParams&>(m_params) != old_display)
        rebuildColourTable();
}

// -----------------------------------------------------------------------------
//  Colour table — physical amplitude i → display tone curve → palette.
//  Called in O(256) on any display/palette change; never per-pixel.
// -----------------------------------------------------------------------------

void WaterfallRenderer::rebuildColourTable()
{
    for (int i = 0; i < 65536; ++i) {
        const auto physical = static_cast<uint16_t>(i);
        const float display = SSSAmplitudeProcessor::displayIntensity(physical, m_params);
        m_colour_table16[i] = SSSPalette::color(display, m_params.palette);
    }
    ++m_table_version;
}

// -----------------------------------------------------------------------------
//  Frame rebuild — outer loop over visible rows, with strip cache
// -----------------------------------------------------------------------------

int WaterfallRenderer::maxVisible() const
{
    const int rh = std::max(1, m_layout.row_height);
    return m_layout.img_h > 0 ? m_layout.img_h / rh : 1;
}

bool WaterfallRenderer::rebuild(const std::vector<PingRow>& rows,
                                int   scroll_row,
                                float h_zoom,
                                int   h_pan)
{
    const int w = m_layout.widget_w;
    const int h = m_layout.img_h;
    if (w <= 0 || h <= 0) return false;

    const int rh     = std::max(1, m_layout.row_height);
    const int nadir  = m_layout.nadir_x;
    const int port_w = nadir - kWfRulerW;
    const int stbd_w = w - nadir;

    // Reuse the QImage allocation — only reallocate on size change.
    if (m_img.width() != w || m_img.height() != h)
        m_img = QImage(w, h, QImage::Format_RGB32);
    m_img.fill(QColor(Theme::kBg));   // background for empty rows below data

    if (rows.empty()) return true;

    // Invalidate strip cache when geometry changes (resize / zoom / pan).
    if (m_cache_w != w || m_cache_zoom != h_zoom || m_cache_pan != h_pan) {
        m_strip_cache.clear();
        m_cache_w    = w;
        m_cache_zoom = h_zoom;
        m_cache_pan  = h_pan;
    }

    const int total      = static_cast<int>(rows.size());
    const int max_scroll = qMax(0, total - h / rh);
    const int first      = qBound(0, scroll_row, max_scroll);

    // Grow cache to cover all rows (never shrinks — shrink on geometry change).
    if (static_cast<int>(m_strip_cache.size()) < total)
        m_strip_cache.resize(total);

    const size_t row_bytes = static_cast<size_t>(w) * sizeof(QRgb);

    auto ensureStrip = [&](int row) -> RenderedStrip& {
        RenderedStrip& strip = m_strip_cache[row];

        if (strip.pixels.size() != static_cast<size_t>(w) ||
            strip.table_ver != m_table_version) {
            // Cache miss — render this row into the strip buffer.
            strip.pixels.resize(w);
            QRgb* line = strip.pixels.data();

            // Fill: background colour first, then ruler colour over left strip.
            std::fill(line,              line + w,        QRgb(0x111113));
            std::fill(line,              line + kWfRulerW, QRgb(0x1c1c1e));

            rebuildRow(rows[row], line, port_w, stbd_w, h_zoom, h_pan);

            if (m_params.smoothing >= 1.f)
                applyRowSmoothing(line, nadir, w);

            strip.table_ver = m_table_version;
        }

        return strip;
    };

    for (int y = 0; y < h; ++y) {
        const float row_f = static_cast<float>(first)
            + (static_cast<float>(y) + 0.5f) / static_cast<float>(rh) - 0.5f;

        if (row_f < -0.5f || row_f > static_cast<float>(total) - 0.5f)
            continue;

        const float row_sample = std::clamp(row_f, 0.f, static_cast<float>(total - 1));
        const int row0 = static_cast<int>(std::floor(row_sample));
        const int row1 = std::min(row0 + 1, total - 1);
        const float t  = row_sample - static_cast<float>(row0);

        const RenderedStrip& strip0 = ensureStrip(row0);
        QRgb* dst = reinterpret_cast<QRgb*>(m_img.scanLine(y));

        if (row0 == row1 || t <= 0.001f) {
            std::memcpy(dst, strip0.pixels.data(), row_bytes);
            continue;
        }

        const RenderedStrip& strip1 = ensureStrip(row1);
        const float inv_t = 1.f - t;
        for (int x = 0; x < w; ++x) {
            const QRgb a = strip0.pixels[static_cast<size_t>(x)];
            const QRgb b = strip1.pixels[static_cast<size_t>(x)];
            const int r = static_cast<int>(qRed(a)   * inv_t + qRed(b)   * t + 0.5f);
            const int g = static_cast<int>(qGreen(a) * inv_t + qGreen(b) * t + 0.5f);
            const int bch = static_cast<int>(qBlue(a)  * inv_t + qBlue(b)  * t + 0.5f);
            dst[x] = qRgb(r, g, bch);
        }
    }

    return true;
}

} // namespace dolphin::ui
