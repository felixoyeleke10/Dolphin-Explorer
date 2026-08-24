// WaterfallViewSeabed.cpp — seabed editing helpers
//   applyManualSeabedPicks    — overlays persistent manual picks onto freshly assembled rows
//   applySeabedPicksToPings   — writes viewer seabed state into raw ping bottom_pick fields
//   interpolateSeabedGaps     — fills eraser gaps with straight-line interpolation
//   detectSeabedInBox         — threshold-detects seabed within a box selection
//   smartPenRange             — snaps pen cursor to nearest peak amplitude

#include "ui/features/waterfall/WaterfallView.h"
#include "ui/features/waterfall/rendering/WaterfallRangeGeometry.h"
#include "core/SidescanGeometry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  Export seabed picks to raw pings (for DLPD persistence)
// -----------------------------------------------------------------------------

void WaterfallView::applySeabedPicksToPings(std::vector<core::SidescanPing>& pings) const
{
    if (pings.empty() || (m_rows.empty() && m_manual_seabed.empty()))
        return;

    // Build timestamp → (range_m, source) from assembled rows first (source=1),
    // then overlay m_manual_seabed entries (source=2) so user edits win.
    struct StoredPick {
        core::BottomPick pick;
        core::SidescanRangeDomain domain = core::SidescanRangeDomain::Slant;
    };
    std::unordered_map<WaterfallChannelRecordKey, StoredPick,
                       WaterfallChannelRecordKeyHash> picks;
    picks.reserve(m_rows.size() + m_manual_seabed.size());

    for (const auto& row : m_rows) {
        if (row.seabed.detected && row.seabed.range_m > 0.f && !row.seabed.is_manual) {
            const StoredPick pick{{
                row.seabed.range_m,
                std::clamp(row.seabed.confidence, 0.0f, 1.0f),
                uint8_t{1}}, row.seabed_domain};
            const auto port_key = waterfallChannelRecordKey(
                row.port_artifact_id, row.port_timestamp_us,
                core::SidescanChannel::Port);
            const auto stbd_key = waterfallChannelRecordKey(
                row.stbd_artifact_id, row.stbd_timestamp_us,
                core::SidescanChannel::Starboard);
            if (port_key.artifact_id != 0 || port_key.timestamp_us != 0)
                picks.emplace(port_key, pick);
            if (stbd_key.artifact_id != 0 || stbd_key.timestamp_us != 0)
                picks.emplace(stbd_key, pick);
        }
    }

    for (auto& ping : pings) {
        const auto manual = m_manual_seabed.get(waterfallChannelRecordKey(
            ping.id, ping.timestamp_us, ping.channel));
        if (manual) {
            float range_m = static_cast<float>(manual->metres);
            const auto domain = manual->domain;
            if (range_m > 0.f && domain == core::SidescanRangeDomain::Ground) {
                const double altitude =
                    core::sidescanCorrectionAltitudeMetres(ping).value_or(0.0);
                const auto converted = core::convertSidescanRange(
                    {range_m, domain}, core::SidescanRangeDomain::Slant, altitude);
                if (converted) range_m = static_cast<float>(converted->metres);
            }
            if (range_m > 0.f) {
                ping.bottom_pick = {range_m, 1.0f, uint8_t{2}};
                continue;
            }
        }
        const auto it = picks.find(waterfallChannelRecordKey(
            ping.id, ping.timestamp_us, ping.channel));
        if (it == picks.end()) continue;
        auto pick = it->second.pick;
        if (it->second.domain == core::SidescanRangeDomain::Ground) {
            const double altitude =
                core::sidescanCorrectionAltitudeMetres(ping).value_or(0.0);
            const auto converted = core::convertSidescanRange(
                {pick.range_m, it->second.domain},
                core::SidescanRangeDomain::Slant, altitude);
            if (converted) pick.range_m = static_cast<float>(converted->metres);
        }
        ping.bottom_pick = pick;
    }
}

// -----------------------------------------------------------------------------
//  Persistent manual picks — applied after every assembly/detection pass
// -----------------------------------------------------------------------------

void WaterfallView::applyManualSeabedPicks()
{
    if (m_manual_seabed.empty()) return;
    for (auto& row : m_rows) {
        const auto port = m_manual_seabed.get(waterfallChannelRecordKey(
            row.port_artifact_id, row.port_timestamp_us,
            core::SidescanChannel::Port));
        if (port && port->metres > 0.0) {
            row.port_seabed = {static_cast<float>(port->metres), 1.f, true, true};
            row.port_seabed_domain = port->domain;
        }
        const auto stbd = m_manual_seabed.get(waterfallChannelRecordKey(
            row.stbd_artifact_id, row.stbd_timestamp_us,
            core::SidescanChannel::Starboard));
        if (stbd && stbd->metres > 0.0) {
            row.stbd_seabed = {static_cast<float>(stbd->metres), 1.f, true, true};
            row.stbd_seabed_domain = stbd->domain;
        }
    }
}

// -----------------------------------------------------------------------------
//  Eraser gap fill — straight-line interpolation between valid neighbours
// -----------------------------------------------------------------------------

void WaterfallView::interpolateSeabedGaps()
{
    const int n = static_cast<int>(m_rows.size());
    const auto fillSide = [&](core::SidescanChannel channel) {
        auto pick = [&](int row) -> SeabedDetectionResult& {
            return channel == core::SidescanChannel::Port
                ? m_rows[row].port_seabed : m_rows[row].stbd_seabed;
        };
        int i = 0;
        while (i < n) {
            if (pick(i).range_m > 0.f) { ++i; continue; }
            const int gap_start = i;
            while (i < n && pick(i).range_m <= 0.f) ++i;
            const int gap_end = i;
            const float before = gap_start > 0 ? pick(gap_start - 1).range_m : -1.f;
            const float after = gap_end < n ? pick(gap_end).range_m : -1.f;
            if (before <= 0.f && after <= 0.f) continue;
            for (int j = gap_start; j < gap_end; ++j) {
                const float t = static_cast<float>(j - gap_start + 1)
                              / static_cast<float>(gap_end - gap_start + 1);
                const float filled = before <= 0.f ? after : after <= 0.f
                    ? before : before + t * (after - before);
                pick(j) = {filled, 1.f, true, true};
                (channel == core::SidescanChannel::Port
                    ? m_rows[j].port_seabed_domain
                    : m_rows[j].stbd_seabed_domain) =
                        waterfallSideRangesAreGround(m_rows[j], channel)
                            ? core::SidescanRangeDomain::Ground
                            : core::SidescanRangeDomain::Slant;
                const int64_t timestamp = channel == core::SidescanChannel::Port
                    ? m_rows[j].port_timestamp_us : m_rows[j].stbd_timestamp_us;
                const std::uint64_t artifact_id = channel == core::SidescanChannel::Port
                    ? m_rows[j].port_artifact_id : m_rows[j].stbd_artifact_id;
                const auto domain = waterfallSideRangesAreGround(m_rows[j], channel)
                    ? core::SidescanRangeDomain::Ground
                    : core::SidescanRangeDomain::Slant;
                m_manual_seabed.set(waterfallChannelRecordKey(
                    artifact_id, timestamp, channel), {filled, domain});
            }
        }
    };
    fillSide(core::SidescanChannel::Port);
    fillSide(core::SidescanChannel::Starboard);
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

        auto detectCh = [&](const std::vector<uint16_t>& amp,
                            core::SidescanChannel channel) -> float {
            const int ns = static_cast<int>(amp.size());
            if (ns == 0) return -1.f;
            const auto& ranges = channel == core::SidescanChannel::Port
                ? row.port_ranges : row.stbd_ranges;
            const float rmax = waterfallSideMaxRange(row, channel);
            const int lo = std::clamp(static_cast<int>(waterfallSampleForRange(
                ranges, ns, range_min_m, rmax)), 0, ns - 1);
            const int hi = std::clamp(static_cast<int>(std::ceil(
                waterfallSampleForRange(ranges, ns, range_max_m, rmax))), 0, ns - 1);
            if (lo > hi) return -1.f;

            uint16_t peak = 0; int peak_i = lo;
            for (int i = lo; i <= hi; ++i)
                if (amp[i] > peak) { peak = amp[i]; peak_i = i; }
            if (peak == 0) return -1.f;

            const auto thresh = static_cast<uint16_t>(peak * kThreshFrac);
            int det_i = peak_i;
            for (int i = lo; i <= hi; ++i)
                if (amp[i] >= thresh) { det_i = i; break; }

            return waterfallRangeAtSample(
                ranges, ns, static_cast<float>(det_i), rmax);
        };

        // Respect channel selection: 0=Both, 1=Port only, 2=Starboard only.
        const float rp = m_seabed_channel != 2
            ? detectCh(row.port, core::SidescanChannel::Port) : -1.f;
        const float rs = m_seabed_channel != 1
            ? detectCh(row.stbd, core::SidescanChannel::Starboard) : -1.f;

        if (rp > 0.f || rs > 0.f) {
            if (rp > 0.f) {
                row.port_seabed = {rp, 1.f, true, true};
                row.port_seabed_domain = row.port_range_domain;
            }
            if (rs > 0.f) {
                row.stbd_seabed = {rs, 1.f, true, true};
                row.stbd_seabed_domain = row.stbd_range_domain;
            }
            const float representative = rp > 0.f && rs > 0.f
                ? (rp + rs) * 0.5f : (rp > 0.f ? rp : rs);
            row.seabed = {representative, 1.f, true, true};
            if (rp > 0.f) m_manual_seabed.set(waterfallChannelRecordKey(
                row.port_artifact_id, row.port_timestamp_us,
                core::SidescanChannel::Port),
                {rp, row.port_range_domain});
            if (rs > 0.f) m_manual_seabed.set(waterfallChannelRecordKey(
                row.stbd_artifact_id, row.stbd_timestamp_us,
                core::SidescanChannel::Starboard),
                {rs, row.stbd_range_domain});
        }
    }

    // Light 3-point smoothing pass to remove per-ping detection jitter.
    const int n = rowCount();
    const auto smoothSide = [&](core::SidescanChannel channel) {
        auto pick = [&](int row) -> SeabedDetectionResult& {
            return channel == core::SidescanChannel::Port
                ? m_rows[row].port_seabed : m_rows[row].stbd_seabed;
        };
        std::vector<float> smoothed(static_cast<size_t>(n), -1.f);
        for (int ri = std::max(r0 + 1, 1); ri < std::min(r1, n - 1); ++ri) {
            if (pick(ri).range_m <= 0.f) continue;
            const float prev = pick(ri - 1).range_m > 0.f
                ? pick(ri - 1).range_m : pick(ri).range_m;
            const float next = pick(ri + 1).range_m > 0.f
                ? pick(ri + 1).range_m : pick(ri).range_m;
            smoothed[static_cast<size_t>(ri)] =
                (prev + pick(ri).range_m + next) / 3.f;
        }
        for (int ri = std::max(r0 + 1, 1); ri < std::min(r1, n - 1); ++ri)
            if (smoothed[static_cast<size_t>(ri)] > 0.f)
                pick(ri).range_m = smoothed[static_cast<size_t>(ri)];
    };
    if (m_seabed_channel != 2) smoothSide(core::SidescanChannel::Port);
    if (m_seabed_channel != 1) smoothSide(core::SidescanChannel::Starboard);
    // Sync smoothed values back into the persistent store.
    for (int ri = r0; ri <= r1 && ri < n; ++ri) {
        const auto& row = m_rows[ri];
        if (row.port_timestamp_us != 0 && row.port_seabed.is_manual
                && row.port_seabed.range_m > 0.f) {
            m_manual_seabed.set(waterfallChannelRecordKey(
                row.port_artifact_id, row.port_timestamp_us,
                core::SidescanChannel::Port),
                {row.port_seabed.range_m, row.port_seabed_domain});
        }
        if (row.stbd_timestamp_us != 0 && row.stbd_seabed.is_manual
                && row.stbd_seabed.range_m > 0.f) {
            m_manual_seabed.set(waterfallChannelRecordKey(
                row.stbd_artifact_id, row.stbd_timestamp_us,
                core::SidescanChannel::Starboard),
                {row.stbd_seabed.range_m, row.stbd_seabed_domain});
        }
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
    core::SidescanChannel selected_ch = cursor_ch;
    if (m_seabed_channel == 1) selected_ch = core::SidescanChannel::Port;
    if (m_seabed_channel == 2) selected_ch = core::SidescanChannel::Starboard;
    const std::vector<uint16_t>& amp = selected_ch == core::SidescanChannel::Port
        ? pr.port : pr.stbd;
    const std::vector<float>& ranges = selected_ch == core::SidescanChannel::Port
        ? pr.port_ranges : pr.stbd_ranges;

    const int ns = static_cast<int>(amp.size());
    if (ns == 0) return rough_range;

    const float max_range = waterfallSideMaxRange(pr, selected_ch);
    const int centre = static_cast<int>(waterfallSampleForRange(
        ranges, ns, rough_range, max_range));
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

    return waterfallRangeAtSample(
        ranges, ns, static_cast<float>(onset_i), max_range);
}

} // namespace dolphin::ui
