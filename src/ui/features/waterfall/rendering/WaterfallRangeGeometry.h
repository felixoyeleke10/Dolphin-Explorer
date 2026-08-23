#pragma once

#include "ui/features/waterfall/PingRow.h"
#include "core/SidescanPing.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace dolphin::ui {

inline float waterfallSideAltitude(const PingRow& row,
                                   core::SidescanChannel channel) noexcept
{
    if (row.seabed.is_manual && std::isfinite(row.seabed.range_m)
            && row.seabed.range_m > 0.f)
        return row.seabed.range_m;
    const float side = channel == core::SidescanChannel::Port
        ? row.port_altitude_m : row.stbd_altitude_m;
    if (std::isfinite(side) && side > 0.f) return side;
    if (row.seabed.detected && std::isfinite(row.seabed.range_m)
            && row.seabed.range_m > 0.f)
        return row.seabed.range_m;
    return std::isfinite(row.altitude_m) && row.altitude_m > 0.f
        ? row.altitude_m : 0.f;
}

inline float waterfallRangeAtSample(const std::vector<float>& ranges,
                                    int sample_count, float sample_index,
                                    float max_range) noexcept
{
    if (sample_count <= 1) return 0.f;
    const float si = std::clamp(sample_index, 0.f,
                                static_cast<float>(sample_count - 1));
    if (ranges.size() == static_cast<size_t>(sample_count)) {
        const int i0 = static_cast<int>(si);
        const int i1 = std::min(i0 + 1, sample_count - 1);
        const float a = ranges[static_cast<size_t>(i0)];
        const float b = ranges[static_cast<size_t>(i1)];
        if (std::isfinite(a) && std::isfinite(b) && a >= 0.f && b >= a)
            return a + (b - a) * (si - static_cast<float>(i0));
    }
    return max_range * si / static_cast<float>(sample_count - 1);
}

inline float waterfallSampleForRange(const std::vector<float>& ranges,
                                     int sample_count, float range,
                                     float max_range) noexcept
{
    if (sample_count <= 1) return 0.f;
    if (ranges.size() == static_cast<size_t>(sample_count)
            && std::is_sorted(ranges.begin(), ranges.end())) {
        const auto it = std::lower_bound(ranges.begin(), ranges.end(), range);
        if (it == ranges.begin()) return 0.f;
        if (it == ranges.end()) return static_cast<float>(sample_count - 1);
        const size_t hi = static_cast<size_t>(it - ranges.begin());
        const float lo_r = ranges[hi - 1];
        const float hi_r = ranges[hi];
        if (std::isfinite(lo_r) && std::isfinite(hi_r) && hi_r > lo_r) {
            return static_cast<float>(hi - 1)
                + std::clamp((range - lo_r) / (hi_r - lo_r), 0.f, 1.f);
        }
    }
    if (!(max_range > 0.f)) return 0.f;
    return std::clamp(range / max_range, 0.f, 1.f)
        * static_cast<float>(sample_count - 1);
}

inline bool waterfallNeedsRangeAwareCpu(const std::vector<PingRow>& rows) noexcept
{
    return std::any_of(rows.begin(), rows.end(), [](const PingRow& row) {
        return !row.port_ranges.empty() || !row.stbd_ranges.empty();
    });
}

} // namespace dolphin::ui
