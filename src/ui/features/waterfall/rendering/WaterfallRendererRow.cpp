// WaterfallRendererRow.cpp — per-row pixel writing, smoothing, and coordinate mapping
//
// rebuildRow()        — maps sonar samples to RGB pixels for one waterfall row
// applyRowSmoothing() — horizontal box-filter blur (port and starboard halves)
// xToRange()          — widget pixel x → (SidescanChannel, slant range m)

#include "ui/features/waterfall/rendering/WaterfallRenderer.h"
#include "ui/features/waterfall/rendering/WaterfallRangeGeometry.h"

#include <QtGlobal>  // qBound, qMax
#include <algorithm>
#include <cmath>
#include <vector>

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  Per-row image building
// -----------------------------------------------------------------------------

void WaterfallRenderer::rebuildRow(const PingRow& pr, QRgb* line0,
                                   int port_w, int stbd_w,
                                   float h_zoom, int h_pan) const
{
    const int nadir   = m_layout.nadir_x;
    const int ns_port = static_cast<int>(pr.port.size());
    const int ns_stbd = static_cast<int>(pr.stbd.size());

    const float z_port = (h_zoom > 0.f) ? h_zoom
                       : (port_w > 0 && ns_port > 0 ? float(port_w) / ns_port : 1.f);
    const float z_stbd = (h_zoom > 0.f) ? h_zoom
                       : (stbd_w > 0 && ns_stbd > 0 ? float(stbd_w) / ns_stbd : 1.f);

    // Slant Range Correction: each output pixel represents equal ground range instead
    // of equal slant range.  Mapping: pixel fraction → ground range → slant range
    // via Pythagoras → sample.
    // Altitude priority: seabed-detected range (most accurate) → ping nav altitude_m
    // (from sensor, always populated when the towfish has an altimeter).
    const float max_r  = pr.slant_range_m;

    const bool show_port = m_params.display_channel != DisplayChannel::Starboard;
    const bool show_stbd = m_params.display_channel != DisplayChannel::Port;

    // SRC sonar extents: width of the projected sonar image in logical pixels.
    // Matches the GL shader (ns * z) and xToRange() — not the widget half-width.
    const float src_port_w = (ns_port > 1) ? float(ns_port) * z_port : 0.f;
    const float src_stbd_w = (ns_stbd > 1) ? float(ns_stbd) * z_stbd : 0.f;

    // Port: pixel at x=nadir-1 is sample h_pan; further left = farther range.
    if (!pr.port.empty() && show_port) {
        const bool baked = waterfallSideRangesAreGround(
            pr, core::SidescanChannel::Port);
        const float side_max = waterfallSideMaxRange(
            pr, core::SidescanChannel::Port);
        const float h_alt = waterfallSideAltitude(pr, core::SidescanChannel::Port);
        const bool src = m_params.slant_range_correction
                      && (baked || (h_alt > 0.f && side_max > h_alt));
        const float max_g = src && !baked
            ? std::sqrt(std::max(0.f, side_max * side_max - h_alt * h_alt)) : 0.f;
        for (int x = kWfRulerW; x < nadir; ++x) {
            float si_f;
            if (src && (baked ? side_max : max_g) > 0.f && src_port_w > 0.f) {
                const float frac = static_cast<float>(nadir - 1 - x) / src_port_w;
                if (frac < 0.f || frac > 1.f) continue;
                const float g = frac * (baked ? side_max : max_g);
                const float r = baked ? g : std::sqrt(g * g + h_alt * h_alt);
                si_f = waterfallSampleForRange(
                    pr.port_ranges, ns_port, r, side_max) + static_cast<float>(h_pan);
            } else {
                si_f = static_cast<float>(nadir - 1 - x) / z_port + static_cast<float>(h_pan);
            }
            if (si_f < 0.f || si_f > static_cast<float>(ns_port - 1)) continue;
            const float si_c = si_f;
            const int   si0  = static_cast<int>(si_c);
            const int   si1  = std::min(si0 + 1, ns_port - 1);
            const float t    = si_c - static_cast<float>(si0);
            const uint16_t amp = static_cast<uint16_t>(std::clamp(
                static_cast<float>(pr.port[si0]) * (1.f - t) +
                static_cast<float>(pr.port[si1]) * t + 0.5f,
                0.f, 65535.f));
            line0[x] = m_colour_table16[amp];
        }
    }

    // Starboard: pixel at x=nadir is sample h_pan; further right = farther range.
    if (!pr.stbd.empty() && show_stbd) {
        const bool baked = waterfallSideRangesAreGround(
            pr, core::SidescanChannel::Starboard);
        const float side_max = waterfallSideMaxRange(
            pr, core::SidescanChannel::Starboard);
        const float h_alt = waterfallSideAltitude(pr, core::SidescanChannel::Starboard);
        const bool src = m_params.slant_range_correction
                      && (baked || (h_alt > 0.f && side_max > h_alt));
        const float max_g = src && !baked
            ? std::sqrt(std::max(0.f, side_max * side_max - h_alt * h_alt)) : 0.f;
        const int w = m_layout.widget_w;
        for (int x = nadir; x < w; ++x) {
            float si_f;
            if (src && (baked ? side_max : max_g) > 0.f && src_stbd_w > 0.f) {
                const float frac = static_cast<float>(x - nadir) / src_stbd_w;
                if (frac < 0.f || frac > 1.f) continue;
                const float g = frac * (baked ? side_max : max_g);
                const float r = baked ? g : std::sqrt(g * g + h_alt * h_alt);
                si_f = waterfallSampleForRange(
                    pr.stbd_ranges, ns_stbd, r, side_max) + static_cast<float>(h_pan);
            } else {
                si_f = static_cast<float>(x - nadir) / z_stbd + static_cast<float>(h_pan);
            }
            if (si_f < 0.f || si_f > static_cast<float>(ns_stbd - 1)) continue;
            const float si_c = si_f;
            const int   si0  = static_cast<int>(si_c);
            const int   si1  = std::min(si0 + 1, ns_stbd - 1);
            const float t    = si_c - static_cast<float>(si0);
            const uint16_t amp = static_cast<uint16_t>(std::clamp(
                static_cast<float>(pr.stbd[si0]) * (1.f - t) +
                static_cast<float>(pr.stbd[si1]) * t + 0.5f,
                0.f, 65535.f));
            line0[x] = m_colour_table16[amp];
        }
    }
}

// -----------------------------------------------------------------------------
//  Horizontal smoothing (box filter, port and starboard halves separate)
// -----------------------------------------------------------------------------

void WaterfallRenderer::applyRowSmoothing(QRgb* line, int nadir, int canvas_w) const
{
    const int radius = static_cast<int>(m_params.smoothing + 0.5f);
    if (radius <= 0) return;

    auto blurSegment = [&](int from, int to) {
        if (to - from < 3) return;
        std::vector<QRgb> tmp(line + from, line + to);
        const int n = to - from;
        for (int i = 0; i < n; ++i) {
            int r = 0, g = 0, b = 0, cnt = 0;
            for (int k = -radius; k <= radius; ++k) {
                const int j = i + k;
                if (j >= 0 && j < n) {
                    r += qRed  (tmp[j]);
                    g += qGreen(tmp[j]);
                    b += qBlue (tmp[j]);
                    ++cnt;
                }
            }
            if (cnt > 0) line[from + i] = qRgb(r / cnt, g / cnt, b / cnt);
        }
    };

    blurSegment(kWfRulerW, nadir);         // port half
    blurSegment(nadir + 1, canvas_w);     // starboard half (nadir pixel excluded)
}

// -----------------------------------------------------------------------------
//  Amplitude processing — delegates to SSSAmplitudeProcessor
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
//  Coordinate mapping: widget pixel → (channel, slant range)
// -----------------------------------------------------------------------------

bool WaterfallRenderer::xToRange(int x, int row_idx,
                                 const std::vector<PingRow>& rows,
                                 float h_zoom, int h_pan,
                                 core::SidescanChannel& ch, float& range_m) const
{
    if (rows.empty()) return false;

    const int nx     = m_layout.nadir_x;
    const int port_w = nx - kWfRulerW;
    const int stbd_w = m_layout.widget_w - nx;
    const int total  = static_cast<int>(rows.size());

    // Resolve max slant range for this row (fallback: first finite non-zero row)
    float max_r = 0.f;
    if (row_idx >= 0 && row_idx < total && std::isfinite(rows[row_idx].slant_range_m))
        max_r = rows[row_idx].slant_range_m;
    if (max_r == 0.f)
        for (const auto& r : rows)
            if (r.slant_range_m > 0.f && std::isfinite(r.slant_range_m))
                { max_r = r.slant_range_m; break; }
    if (max_r == 0.f) return false;

    // Resolve sample counts (fallback: first non-empty row)
    int ns_port = 0, ns_stbd = 0;
    if (row_idx >= 0 && row_idx < total) {
        ns_port = static_cast<int>(rows[row_idx].port.size());
        ns_stbd = static_cast<int>(rows[row_idx].stbd.size());
    }
    for (const auto& r : rows) {
        if (ns_port == 0 && !r.port.empty()) ns_port = static_cast<int>(r.port.size());
        if (ns_stbd == 0 && !r.stbd.empty()) ns_stbd = static_cast<int>(r.stbd.size());
        if (ns_port > 0 && ns_stbd > 0) break;
    }

    // SRC geometry — mirrors rebuildRow() when SRC is active for this row.
    const PingRow& selected_row = rows[static_cast<size_t>(
        std::clamp(row_idx, 0, total - 1))];

    if (x < nx) {
        ch = core::SidescanChannel::Port;
        const bool baked = waterfallSideRangesAreGround(selected_row, ch);
        const float side_max = waterfallSideMaxRange(selected_row, ch);
        const float h_alt = waterfallSideAltitude(selected_row, ch);
        const bool src = m_params.slant_range_correction
                      && (baked || (h_alt > 0.f && side_max > h_alt));
        const float max_g = src && !baked
            ? std::sqrt(side_max * side_max - h_alt * h_alt) : 0.f;
        if (src && (baked ? side_max : max_g) > 0.f && ns_port > 1) {
            const float z    = (h_zoom > 0.f) ? h_zoom : (port_w > 0 ? float(port_w) / ns_port : 1.f);
            const float sw   = float(ns_port) * z;
            const float frac = std::clamp(static_cast<float>(nx - 1 - x) / sw, 0.f, 1.f);
            const float g = frac * (baked ? side_max : max_g);
            const float r = baked ? g : std::sqrt(g * g + h_alt * h_alt);
            const float si = waterfallSampleForRange(
                selected_row.port_ranges, ns_port, r, side_max) + h_pan;
            range_m = waterfallRangeAtSample(
                selected_row.port_ranges, ns_port, si, side_max);
        } else {
            const int   ns = qMax(1, ns_port);
            const float z  = (h_zoom > 0.f) ? h_zoom
                           : (port_w > 0 ? float(port_w) / ns : 1.f);
            const int   si = qBound(0, static_cast<int>((nx - 1 - x) / z) + h_pan, ns - 1);
            range_m = waterfallRangeAtSample(
                selected_row.port_ranges, ns, static_cast<float>(si), max_r);
        }
    } else {
        ch = core::SidescanChannel::Starboard;
        const bool baked = waterfallSideRangesAreGround(selected_row, ch);
        const float side_max = waterfallSideMaxRange(selected_row, ch);
        const float h_alt = waterfallSideAltitude(selected_row, ch);
        const bool src = m_params.slant_range_correction
                      && (baked || (h_alt > 0.f && side_max > h_alt));
        const float max_g = src && !baked
            ? std::sqrt(side_max * side_max - h_alt * h_alt) : 0.f;
        if (src && (baked ? side_max : max_g) > 0.f && ns_stbd > 1) {
            const float z    = (h_zoom > 0.f) ? h_zoom : (stbd_w > 0 ? float(stbd_w) / ns_stbd : 1.f);
            const float sw   = float(ns_stbd) * z;
            const float frac = std::clamp(static_cast<float>(x - nx) / sw, 0.f, 1.f);
            const float g = frac * (baked ? side_max : max_g);
            const float r = baked ? g : std::sqrt(g * g + h_alt * h_alt);
            const float si = waterfallSampleForRange(
                selected_row.stbd_ranges, ns_stbd, r, side_max) + h_pan;
            range_m = waterfallRangeAtSample(
                selected_row.stbd_ranges, ns_stbd, si, side_max);
        } else {
            const int   ns = qMax(1, ns_stbd);
            const float z  = (h_zoom > 0.f) ? h_zoom
                           : (stbd_w > 0 ? float(stbd_w) / ns : 1.f);
            const int   si = qBound(0, static_cast<int>((x - nx) / z) + h_pan, ns - 1);
            range_m = waterfallRangeAtSample(
                selected_row.stbd_ranges, ns, static_cast<float>(si), max_r);
        }
    }
    return range_m >= 0.f;
}

} // namespace dolphin::ui
