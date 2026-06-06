// SeabedAutoDetector.cpp — public API, utility helpers, and post-processing
//
// Per-ping scanners     → SeabedAutoDetector.Scanners.cpp
//
// rangeAt               — sample i slant range (per-sample when available)
// blankingSamples       — blanking zone in sample counts
// estimateNoiseFloor    — 15th-percentile noise estimate for the water column
// detectOne             — single ping pair → seabed slant-range
// detectAll             — batch fill using the selected amplitude scanner
// gapFill               — linear interpolation between anchors (bounded)
// rejectOutliers        — remove picks that deviate from local median > max_delta_m
// smooth                — moving-average pass on the detected line

#include "ui/features/waterfall/processing/SeabedAutoDetector.h"

#include <algorithm>
#include <cmath>
#include <climits>
#include <numeric>
#include <vector>

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  Utility helpers
// -----------------------------------------------------------------------------

float SeabedAutoDetector::rangeAt(const core::SidescanPing* ping, int i)
{
    const auto& s = ping->samples[i];
    if (s.range_m > 0.f) return s.range_m;
    const int n = static_cast<int>(ping->samples.size());
    if (n <= 1) return ping->slant_range_m;
    return ping->slant_range_m * static_cast<float>(i) / static_cast<float>(n - 1);
}

int SeabedAutoDetector::blankingSamples(const core::SidescanPing* ping,
                                         const SeabedAutoParams& p)
{
    const int n = static_cast<int>(ping->samples.size());
    if (p.blanking_m > 0.f && ping->slant_range_m > 0.f)
        return std::max(1, static_cast<int>(n * p.blanking_m / ping->slant_range_m));
    return std::max(1, static_cast<int>(n * p.blanking_pct / 100.f));
}

// Estimates noise from the water-column window immediately after blanking.
// Takes the 15th percentile of `window_n` samples starting at `skip` — robust
// against any early weak returns that may appear in the window.
float SeabedAutoDetector::estimateNoiseFloor(const core::SidescanPing* ping,
                                              int skip, int window_n)
{
    const int n   = static_cast<int>(ping->samples.size());
    const int end = std::min(n, skip + window_n);
    if (end <= skip) return 0.f;

    std::vector<uint32_t> vals;
    vals.reserve(static_cast<size_t>(end - skip));
    for (int i = skip; i < end; ++i)
        vals.push_back(ping->samples[i].amplitude);

    const int idx = std::max(0, static_cast<int>(vals.size() * 0.15f) - 1);
    std::nth_element(vals.begin(), vals.begin() + idx, vals.end());
    return static_cast<float>(vals[static_cast<size_t>(idx)]);
}

// -----------------------------------------------------------------------------
//  Public API
// -----------------------------------------------------------------------------

float SeabedAutoDetector::detectOne(const core::SidescanPing* port_ping,
                                     const core::SidescanPing* stbd_ping,
                                     const SeabedAutoParams&   params,
                                     float*                    out_confidence)
{
    auto scan = [&](const core::SidescanPing* ping) -> ScanResult {
        switch (params.method) {
        case SeabedMethod::Threshold:       return scanPingThreshold  (ping, params);
        case SeabedMethod::FirstReturn:     return scanPingFirstReturn(ping, params);
        }
        return scanPingThreshold(ping, params);
    };

    const ScanResult sb   = scan(stbd_ping);
    const ScanResult pt   = scan(port_ping);
    const ScanResult best = combineChannels(sb, pt, params.channel_agree_m);

    if (out_confidence) *out_confidence = best.confidence;
    return best.range_m;
}

void SeabedAutoDetector::detectAll(std::vector<PingRow>& rows,
                                    const SeabedAutoParams& params)
{
    for (auto& row : rows) {
        if (row.seabed.is_manual) continue;

        core::SidescanPing port_stub, stbd_stub;
        port_stub.slant_range_m = row.slant_range_m;
        stbd_stub.slant_range_m = row.slant_range_m;
        port_stub.samples.resize(row.port.size());
        stbd_stub.samples.resize(row.stbd.size());
        for (std::size_t i = 0; i < row.port.size(); ++i) {
            port_stub.samples[i].amplitude = row.port[i];
            if (i < row.port_ranges.size())
                port_stub.samples[i].range_m = row.port_ranges[i];
        }
        for (std::size_t i = 0; i < row.stbd.size(); ++i) {
            stbd_stub.samples[i].amplitude = row.stbd[i];
            if (i < row.stbd_ranges.size())
                stbd_stub.samples[i].range_m = row.stbd_ranges[i];
        }

        float conf = 0.f;
        const float r     = detectOne(&port_stub, &stbd_stub, params, &conf);
        row.seabed.range_m    = r;
        row.seabed.confidence = conf;
        row.seabed.detected   = (r > 0.f);
    }

    if (params.max_delta_m > 0.f)
        rejectOutliers(rows, params.max_delta_m);

    gapFill(rows, 50);
}

// -----------------------------------------------------------------------------
//  Post-processing
// -----------------------------------------------------------------------------

void SeabedAutoDetector::gapFill(std::vector<PingRow>& rows, int max_gap)
{
    const int n = static_cast<int>(rows.size());

    auto isAnchor = [&](int i) -> bool {
        return i >= 0 && i < n
            && (rows[static_cast<size_t>(i)].seabed.detected
                || rows[static_cast<size_t>(i)].seabed.is_manual)
            && rows[static_cast<size_t>(i)].seabed.range_m > 0.f;
    };

    int first_anchor = -1, last_anchor = -1;
    for (int i = 0; i < n; ++i) {
        if (isAnchor(i)) {
            if (first_anchor < 0) first_anchor = i;
            last_anchor = i;
        }
    }
    if (first_anchor < 0) return;

    // 1. Flat-extend the first anchor backward, up to max_gap rows.
    //    Guard against signed overflow when max_gap == INT_MAX.
    const int back_start = (max_gap >= first_anchor) ? 0 : first_anchor - max_gap;
    for (int i = back_start; i < first_anchor; ++i) {
        if (!rows[static_cast<size_t>(i)].seabed.is_manual)
            rows[static_cast<size_t>(i)].seabed.range_m =
                rows[static_cast<size_t>(first_anchor)].seabed.range_m;
    }

    // 2. Interpolate between consecutive anchors.
    //    Gaps wider than max_gap are left unfilled (range_m stays 0).
    int prev_anchor = first_anchor;
    for (int i = first_anchor + 1; i <= n; ++i) {
        const bool anchor = isAnchor(i);
        if (anchor || i == n) {
            const int gap = i - prev_anchor - 1;
            if (i < n && gap > 0 && gap <= max_gap) {
                const float r0   = rows[static_cast<size_t>(prev_anchor)].seabed.range_m;
                const float r1   = rows[static_cast<size_t>(i)].seabed.range_m;
                const float span = static_cast<float>(i - prev_anchor);
                for (int j = prev_anchor + 1; j < i; ++j) {
                    if (!rows[static_cast<size_t>(j)].seabed.is_manual) {
                        const float t = static_cast<float>(j - prev_anchor) / span;
                        rows[static_cast<size_t>(j)].seabed.range_m = r0 + t * (r1 - r0);
                    }
                }
            }
            if (i < n) prev_anchor = i;
        }
    }

    // 3. Flat-extend the last anchor forward, up to max_gap rows.
    //    Guard against signed overflow when max_gap == INT_MAX.
    const int fwd_end = (max_gap >= n - last_anchor - 1) ? n
                                                          : last_anchor + 1 + max_gap;
    for (int i = last_anchor + 1; i < fwd_end; ++i) {
        if (!rows[static_cast<size_t>(i)].seabed.is_manual)
            rows[static_cast<size_t>(i)].seabed.range_m =
                rows[static_cast<size_t>(last_anchor)].seabed.range_m;
    }
}

void SeabedAutoDetector::rejectOutliers(std::vector<PingRow>& rows,
                                         float max_delta_m)
{
    if (max_delta_m <= 0.f || rows.empty()) return;

    const int n = static_cast<int>(rows.size());
    constexpr int kRadius = 7;

    std::vector<float> orig(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        orig[static_cast<size_t>(i)] = rows[static_cast<size_t>(i)].seabed.range_m;

    for (int i = 0; i < n; ++i) {
        if (rows[static_cast<size_t>(i)].seabed.is_manual
            || !rows[static_cast<size_t>(i)].seabed.detected) continue;

        int prev = -1;
        for (int j = i - 1; j >= std::max(0, i - kRadius); --j) {
            if (orig[static_cast<size_t>(j)] > 0.f) {
                prev = j;
                break;
            }
        }

        int next = -1;
        for (int j = i + 1; j <= std::min(n - 1, i + kRadius); ++j) {
            if (orig[static_cast<size_t>(j)] > 0.f) {
                next = j;
                break;
            }
        }

        float expected = -1.f;
        if (prev >= 0 && next >= 0) {
            const float span = static_cast<float>(next - prev);
            const float t = static_cast<float>(i - prev) / span;
            expected = orig[static_cast<size_t>(prev)]
                     + t * (orig[static_cast<size_t>(next)]
                          - orig[static_cast<size_t>(prev)]);
        } else if (prev >= 0) {
            expected = orig[static_cast<size_t>(prev)];
        } else if (next >= 0) {
            expected = orig[static_cast<size_t>(next)];
        }

        if (expected > 0.f
            && std::fabs(orig[static_cast<size_t>(i)] - expected) > max_delta_m) {
            rows[static_cast<size_t>(i)].seabed.range_m    = -1.f;
            rows[static_cast<size_t>(i)].seabed.confidence = 0.f;
            rows[static_cast<size_t>(i)].seabed.detected   = false;
        }
    }
}

void SeabedAutoDetector::smooth(std::vector<PingRow>& rows, int radius)
{
    if (radius <= 0 || rows.empty()) return;

    const int n = static_cast<int>(rows.size());
    std::vector<float> smoothed(static_cast<size_t>(n));

    for (int i = 0; i < n; ++i) {
        if (rows[static_cast<size_t>(i)].seabed.is_manual
            || !rows[static_cast<size_t>(i)].seabed.detected) {
            smoothed[static_cast<size_t>(i)] = rows[static_cast<size_t>(i)].seabed.range_m;
            continue;
        }
        float sum = 0.f;
        int   cnt = 0;
        for (int j = std::max(0, i - radius); j <= std::min(n - 1, i + radius); ++j) {
            const auto& sj = rows[static_cast<size_t>(j)].seabed;
            if (sj.range_m > 0.f && (sj.detected || sj.is_manual)) {
                sum += sj.range_m;
                ++cnt;
            }
        }
        smoothed[static_cast<size_t>(i)] = (cnt > 0)
            ? sum / cnt
            : rows[static_cast<size_t>(i)].seabed.range_m;
    }

    for (int i = 0; i < n; ++i)
        if (!rows[static_cast<size_t>(i)].seabed.is_manual)
            rows[static_cast<size_t>(i)].seabed.range_m = smoothed[static_cast<size_t>(i)];
}

} // namespace dolphin::ui
