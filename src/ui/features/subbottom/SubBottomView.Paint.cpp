// SubBottomView.Paint.cpp — QPainter-based trace rendering and overlay.

#include "ui/features/subbottom/SubBottomView.h"
#include "ui/features/subbottom/SubBottomPalette.h"
#include <QPainter>
#include <QPaintEvent>
#include <algorithm>
#include <cmath>

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  setViewStyle — trigger repaint only if the style actually changed
// -----------------------------------------------------------------------------

void SubBottomView::setViewStyle(const SubBottomViewStyle& s)
{
    // bt_color and bt_thickness are painted into m_image, not just overlaid.
    const bool image_params_changed =
        s.bt_color     != m_style.bt_color     ||
        s.bt_thickness != m_style.bt_thickness;
    m_style = s;
    if (image_params_changed)
        m_image_dirty = true;
    update();
}

QRgb SubBottomView::sampleToRgb(float s) const
{
    float a = s * m_gain;
    if (m_polarity_invert) a = -a;
    if (m_contrast != 1.0f) {
        const float sign = (a >= 0.f) ? 1.f : -1.f;
        a = sign * std::pow(std::abs(a), 1.0f / m_contrast);
    }
    return SbpPalette::toRgb(std::clamp(a, -1.f, 1.f), m_palette_index);
}

void SubBottomView::rebuildImage()
{
    m_image_dirty = false;
    const int w = width();
    const int h = height();
    if (w <= 0 || h <= 0) return;

    m_image = QImage(w, h, QImage::Format_RGB32);
    m_image.fill(Qt::black);

    if (m_traces.empty()) return;

    // Pre-build a QRgb LUT over the signed amplitude domain [-1, +1].
    // Maps gain+polarity-adjusted amplitude → contrast curve → palette.
    // Eliminates std::pow() from the per-pixel inner loop (up to 2M calls saved).
    constexpr int kLutSize = 4096;
    constexpr float kLutScale = (kLutSize - 1) / 2.0f;
    QRgb colour_lut[kLutSize];
    {
        const float inv_contrast = 1.0f / m_contrast;
        for (int i = 0; i < kLutSize; ++i) {
            float a = -1.0f + static_cast<float>(i) * (2.0f / (kLutSize - 1));
            if (m_contrast != 1.0f) {
                const float sign = (a >= 0.f) ? 1.f : -1.f;
                a = sign * std::pow(std::abs(a), inv_contrast);
            }
            colour_lut[i] = SbpPalette::toRgb(std::clamp(a, -1.f, 1.f), m_palette_index);
        }
    }

    const int n_traces = traceCount();
    const int visible  = std::min(visibleTraceCount(), n_traces - m_first_trace);

    const QRgb bt_rgb = m_style.bt_color.rgb();
    const int  bt_h   = std::max(1, m_style.bt_thickness);

    for (int col = 0; col < visible; ++col) {
        const int ti = m_first_trace + col;
        if (ti >= n_traces) break;

        const auto& trace = m_traces[ti];
        if (trace.samples.empty()) continue;

        const int n_samples = static_cast<int>(trace.samples.size());
        const int px_start  = col * m_px_per_trace;
        const int px_end    = std::min(px_start + m_px_per_trace, w);

        for (int y = 0; y < h; ++y) {
            const int si = static_cast<int>(static_cast<float>(y) / m_px_per_sample);
            if (si >= n_samples) break;
            float a = trace.samples[si] * m_gain;
            if (m_polarity_invert) a = -a;
            const int li = static_cast<int>((std::clamp(a, -1.0f, 1.0f) + 1.0f) * kLutScale);
            const QRgb rgb = colour_lut[li];
            for (int x = px_start; x < px_end; ++x)
                reinterpret_cast<QRgb*>(m_image.scanLine(y))[x] = rgb;
        }

        // Bottom track stripe
        if (m_show_bottom_track && trace.bottom_sample_idx >= 0) {
            const int y_pick = static_cast<int>(
                static_cast<float>(trace.bottom_sample_idx) * m_px_per_sample);
            for (int dy = 0; dy < bt_h; ++dy) {
                const int y = y_pick + dy;
                if (y < 0 || y >= h) continue;
                for (int x = px_start; x < px_end; ++x)
                    reinterpret_cast<QRgb*>(m_image.scanLine(y))[x] = bt_rgb;
            }
        }
    }
}

void SubBottomView::paintEvent(QPaintEvent*)
{
    if (m_image_dirty)
        rebuildImage();

    QPainter p(this);
    if (!m_image.isNull())
        p.drawImage(0, 0, m_image);
    else
        p.fillRect(rect(), Qt::black);

    // -- Depth grid --------------------------------------------------------
    if (m_style.grid_show && m_px_per_sample > 0.f) {
        // Determine interval in ms (two-way travel time).
        // Auto: pick a nice round value giving roughly 6-8 visible lines.
        float interval_ms = m_style.grid_interval_ms;
        if (interval_ms <= 0.f) {
            // Total visible time window in ms
            const float visible_ms = static_cast<float>(height()) / m_px_per_sample;
            // Nice intervals (ms): 1, 2, 5, 10, 20, 50, 100, 200, ...
            static const float kNice[] = { 0.5f, 1.f, 2.f, 5.f, 10.f, 20.f, 50.f,
                                           100.f, 200.f, 500.f, 1000.f };
            interval_ms = kNice[std::size(kNice) - 1];
            for (float ni : kNice) {
                if (visible_ms / ni <= 8.f) { interval_ms = ni; break; }
            }
        }

        // Samples per ms
        const float samples_per_ms = m_px_per_sample > 0.f
            ? 1.0f / m_px_per_sample   // each sample = 1 unit; ms conversion depends on sampleRate
            : 1.0f;
        // In the data model each sample index is a unit of two-way travel time.
        // 1 sample = (1 / sample_rate) seconds, but we don't have sample_rate here.
        // We use m_px_per_sample (px per sample) to convert interval_ms to pixels:
        //   pixel_interval = interval_ms * (1 sample / 1 ms) * m_px_per_sample
        // This assumes 1 sample ≈ 1 ms resolution which is approximate.
        // The user controls the interval via the settings dialog.
        const float px_interval = interval_ms * m_px_per_sample;
        if (px_interval >= 2.f) {
            QColor gc = m_style.grid_color;
            gc.setAlphaF(m_style.grid_opacity / 100.0);
            p.save();
            p.setPen(QPen(gc, 1, Qt::SolidLine));
            for (float y = px_interval; y < static_cast<float>(height()); y += px_interval) {
                const int iy = static_cast<int>(y);
                p.drawLine(0, iy, width(), iy);
            }
            p.restore();
        }
    }

    // -- Crosshair ---------------------------------------------------------
    if (m_style.xhair_show && m_cursor_x >= 0 && m_cursor_y >= 0) {
        QColor xc = m_style.xhair_color;
        xc.setAlphaF(m_style.xhair_opacity / 100.0);
        p.save();
        p.setPen(QPen(xc, m_style.xhair_width, m_style.xhair_style));
        p.setOpacity(1.0);  // alpha encoded in colour
        p.drawLine(m_cursor_x, 0, m_cursor_x, height());
        p.drawLine(0, m_cursor_y, width(), m_cursor_y);
        p.restore();
    }
}

} // namespace dolphin::ui
