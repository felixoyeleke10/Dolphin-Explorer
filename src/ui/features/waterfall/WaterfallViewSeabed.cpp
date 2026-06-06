// WaterfallViewSeabed.cpp — seabed editing helpers
//   applyManualSeabedPicks — overlays persistent manual picks onto freshly assembled rows
//   interpolateSeabedGaps  — fills eraser gaps with straight-line interpolation
//   detectSeabedInBox      — threshold-detects seabed within a box selection
//   smartPenRange          — snaps pen cursor to nearest peak amplitude

#include "ui/features/waterfall/WaterfallView.h"

#include <algorithm>
#include <cstdint>

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  Persistent manual picks — applied after every assembly/detection pass
// -----------------------------------------------------------------------------

void WaterfallView::applyManualSeabedPicks()
{
    if (m_manual_seabed.empty()) return;
    for (auto& row : m_rows) {
        if (row.timestamp_us == 0) continue;
        const auto it = m_manual_seabed.find(row.timestamp_us);
        if (it != m_manual_seabed.end())
            row.seabed = {it->second, 1.f, true, true};
    }
}

// -----------------------------------------------------------------------------
//  Eraser gap fill — straight-line interpolation between valid neighbours
// -----------------------------------------------------------------------------

void WaterfallView::interpolateSeabedGaps()
{
    const int n = static_cast<int>(m_rows.size());
    int i = 0;
    while (i < n) {
        if (m_rows[i].seabed.range_m > 0.f) { ++i; continue; }

        const int gap_start = i;
        while (i < n && m_rows[i].seabed.range_m <= 0.f) ++i;
        const int gap_end = i;

        const float v_before = (gap_start > 0) ? m_rows[gap_start - 1].seabed.range_m : -1.f;
        const float v_after  = (gap_end   < n) ? m_rows[gap_end].seabed.range_m        : -1.f;

        if (v_before <= 0.f && v_after <= 0.f) continue;

        for (int j = gap_start; j < gap_end; ++j) {
            float filled;
            if      (v_before <= 0.f) { filled = v_after; }
            else if (v_after  <= 0.f) { filled = v_before; }
            else {
                const int   len = gap_end - gap_start + 1;
                const float t   = static_cast<float>(j - gap_start + 1) / len;
                filled = v_before + t * (v_after - v_before);
            }
            m_rows[j].seabed = {filled, 1.f, true, true};
            if (m_rows[j].timestamp_us != 0)
                m_manual_seabed[m_rows[j].timestamp_us] = filled;
        }
    }
}

// -----------------------------------------------------------------------------
//  Box tool — threshold-detect seabed within rows [r0,r1] × [range_min, range_max]
// -----------------------------------------------------------------------------

void WaterfallView::detectSeabedInBox(int r0, int r1, float range_min_m, float range_max_m)
{
    // Guarantee at least a 2 m search window so a pure vertical drag still works.
    if (range_max_m - range_min_m < 2.f) {
        const float mid = (range_min_m + range_max_m) * 0.5f;
        range_min_m = std::max(0.f, mid - 1.f);
        range_max_m = mid + 1.f;
    }

    // Threshold detection: first sample ≥ threshold_pct% of in-window peak (seabed onset).
    // Falls back to the peak sample when nothing clears the threshold.
    // Tries both channels; averages when both succeed.
    const float kThreshFrac = m_seabed_auto_params.threshold_pct / 100.f;

    for (int ri = r0; ri <= r1; ++ri) {
        if (ri < 0 || ri >= rowCount()) continue;
        PingRow& row = m_rows[ri];
        if (row.slant_range_m <= 0.f) continue;

        auto detectCh = [&](const std::vector<uint16_t>& amp) -> float {
            const int ns = static_cast<int>(amp.size());
            if (ns == 0) return -1.f;
            const float rmax = row.slant_range_m;
            const int lo = std::clamp(static_cast<int>(range_min_m / rmax * (ns - 1)), 0, ns - 1);
            const int hi = std::clamp(static_cast<int>(range_max_m / rmax * (ns - 1)), 0, ns - 1);
            if (lo > hi) return -1.f;

            uint16_t peak = 0; int peak_i = lo;
            for (int i = lo; i <= hi; ++i)
                if (amp[i] > peak) { peak = amp[i]; peak_i = i; }
            if (peak == 0) return -1.f;

            const auto thresh = static_cast<uint16_t>(peak * kThreshFrac);
            int det_i = peak_i;
            for (int i = lo; i <= hi; ++i)
                if (amp[i] >= thresh) { det_i = i; break; }

            return rmax * static_cast<float>(det_i) / std::max(1, ns - 1);
        };

        // Respect channel selection: 0=Both, 1=Port only, 2=Starboard only.
        const float rp = (m_seabed_channel != 2) ? detectCh(row.port) : -1.f;
        const float rs = (m_seabed_channel != 1) ? detectCh(row.stbd) : -1.f;

        float range_m = -1.f;
        if      (rp > 0.f && rs > 0.f) range_m = (rp + rs) * 0.5f;
        else if (rp > 0.f)             range_m = rp;
        else if (rs > 0.f)             range_m = rs;

        if (range_m > 0.f) {
            row.seabed = {range_m, 1.f, true, true};
            if (row.timestamp_us != 0)
                m_manual_seabed[row.timestamp_us] = range_m;
        }
    }

    // Light 3-point smoothing pass to remove per-ping detection jitter.
    const int n = rowCount();
    for (int ri = r0 + 1; ri < r1; ++ri) {
        if (ri < 0 || ri >= n || m_rows[ri].seabed.range_m <= 0.f) continue;
        const float prev = (ri > r0     && m_rows[ri - 1].seabed.range_m > 0.f)
                            ? m_rows[ri - 1].seabed.range_m : m_rows[ri].seabed.range_m;
        const float next = (ri + 1 < n  && m_rows[ri + 1].seabed.range_m > 0.f)
                            ? m_rows[ri + 1].seabed.range_m : m_rows[ri].seabed.range_m;
        m_rows[ri].seabed.range_m = (prev + m_rows[ri].seabed.range_m + next) / 3.f;
    }
    // Sync smoothed values back into the persistent store.
    for (int ri = r0; ri <= r1 && ri < n; ++ri) {
        const auto& row = m_rows[ri];
        if (row.timestamp_us != 0 && row.seabed.is_manual && row.seabed.range_m > 0.f)
            m_manual_seabed[row.timestamp_us] = row.seabed.range_m;
    }
}

// -----------------------------------------------------------------------------
//  Smart pen snap — peak amplitude within ±kSnapRadius samples of the cursor
// -----------------------------------------------------------------------------

float WaterfallView::smartPenRange(int row, int screen_x) const
{
    constexpr int kSnapRadius = 40;

    if (row < 0 || row >= rowCount()) return 0.f;
    const PingRow& pr = m_rows[row];

    // Get a rough range from the cursor position (used as search centre).
    core::SidescanChannel cursor_ch;
    float rough_range = 0.f;
    if (!m_renderer.xToRange(screen_x, row, m_rows,
                              m_scroll.hZoom(), m_scroll.hPan(), cursor_ch, rough_range)
        || rough_range <= 0.f)
        return 0.f;

    // Pick amplitude array based on m_seabed_channel, not cursor position.
    // When Both: pick the array for whichever side the cursor is on (natural).
    // When Port/Starboard: always snap from that channel's data.
    const std::vector<uint16_t>& amp = [&]() -> const std::vector<uint16_t>& {
        if (m_seabed_channel == 1) return pr.port;
        if (m_seabed_channel == 2) return pr.stbd;
        return (cursor_ch == core::SidescanChannel::Port) ? pr.port : pr.stbd;
    }();

    const int ns = static_cast<int>(amp.size());
    if (ns == 0 || pr.slant_range_m <= 0.f) return rough_range;

    const int centre = static_cast<int>(rough_range / pr.slant_range_m * (ns - 1));
    const int lo = std::clamp(centre - kSnapRadius, 0, ns - 1);
    const int hi = std::clamp(centre + kSnapRadius, 0, ns - 1);

    int peak_i = centre; uint16_t peak_val = 0;
    for (int i = lo; i <= hi; ++i)
        if (amp[i] > peak_val) { peak_val = amp[i]; peak_i = i; }

    // Snap to seabed onset (first sample ≥ 35% of window peak) rather than
    // the specular peak, which usually sits deeper into the return.
    const auto onset_thresh = static_cast<uint16_t>(peak_val * 0.35f);
    int onset_i = peak_i;
    for (int i = lo; i <= hi; ++i) {
        if (amp[i] >= onset_thresh) { onset_i = i; break; }
    }

    return pr.slant_range_m * static_cast<float>(onset_i) / std::max(1, ns - 1);
}

} // namespace dolphin::ui
