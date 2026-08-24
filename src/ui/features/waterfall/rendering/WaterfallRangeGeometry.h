#pragma once

#include "ui/features/waterfall/PingRow.h"
#include "core/SidescanGeometry.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace dolphin::ui {

inline float waterfallSideAltitude(const PingRow& row,
                                   core::SidescanChannel channel) noexcept
{
    const auto& imported = channel == core::SidescanChannel::Port
        ? row.port_seabed : row.stbd_seabed;
    const auto imported_domain = channel == core::SidescanChannel::Port
        ? row.port_seabed_domain : row.stbd_seabed_domain;
    const auto trustedSlantPick = [](const SeabedDetectionResult& pick,
                                     core::SidescanRangeDomain domain) noexcept {
        return domain == core::SidescanRangeDomain::Slant && pick.detected
            && std::isfinite(pick.range_m) && pick.range_m > 0.f
            && (pick.is_manual
                || (std::isfinite(pick.confidence)
                    && pick.confidence >= core::kMinimumAutomaticBottomConfidence));
    };
    // A trusted side-specific slant pick is the strongest bottom reference.
    // A pick drawn on baked ground coordinates cannot be vertical altitude.
    if (trustedSlantPick(imported, imported_domain))
        return imported.range_m;
    // A shared manual value which does not merely mirror an imported channel
    // pick was made in the waterfall editor and intentionally applies to the row.
    const auto matchesImportedPick = [&](const SeabedDetectionResult& pick) {
        return pick.detected && std::isfinite(pick.range_m)
            && std::abs(pick.range_m - row.seabed.range_m) < 1e-4f;
    };
    if (trustedSlantPick(row.seabed, row.seabed_domain)
            && !matchesImportedPick(row.port_seabed)
            && !matchesImportedPick(row.stbd_seabed))
        return row.seabed.range_m;
    const float side = channel == core::SidescanChannel::Port
        ? row.port_altitude_m : row.stbd_altitude_m;
    if (std::isfinite(side) && side > 0.f) return side;
    return std::isfinite(row.altitude_m) && row.altitude_m > 0.f
        ? row.altitude_m : 0.f;
}

inline core::SidescanRangeDomain waterfallSeabedDomainForSide(
    const PingRow& row, core::SidescanChannel channel) noexcept
{
    const auto& side = channel == core::SidescanChannel::Port
        ? row.port_seabed : row.stbd_seabed;
    if (side.detected || side.is_manual) {
        const auto domain = channel == core::SidescanChannel::Port
            ? row.port_seabed_domain : row.stbd_seabed_domain;
        return domain;
    }
    return row.seabed_domain;
}

inline bool waterfallSideRangesAreGround(
    const PingRow& row, core::SidescanChannel channel) noexcept
{
    const auto domain = channel == core::SidescanChannel::Port
        ? row.port_range_domain : row.stbd_range_domain;
    return domain == core::SidescanRangeDomain::Ground;
}

inline float waterfallSideMaxRange(const PingRow& row,
                                   core::SidescanChannel channel) noexcept
{
    const auto& ranges = channel == core::SidescanChannel::Port
        ? row.port_ranges : row.stbd_ranges;
    if (!ranges.empty()) {
        const auto it = std::max_element(ranges.begin(), ranges.end());
        if (it != ranges.end() && std::isfinite(*it) && *it > 0.f) return *it;
    }
    return std::isfinite(row.slant_range_m) && row.slant_range_m > 0.f
        ? row.slant_range_m : 0.f;
}

inline const SeabedDetectionResult& waterfallSeabedForSide(
    const PingRow& row, core::SidescanChannel channel) noexcept
{
    const auto& side = channel == core::SidescanChannel::Port
        ? row.port_seabed : row.stbd_seabed;
    return side.detected || side.is_manual ? side : row.seabed;
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

inline float waterfallDisplayedSampleForRange(
    const PingRow& row, core::SidescanChannel channel, float range_m) noexcept
{
    const auto& ranges = channel == core::SidescanChannel::Port
        ? row.port_ranges : row.stbd_ranges;
    const int count = static_cast<int>(channel == core::SidescanChannel::Port
        ? row.port.size() : row.stbd.size());
    return waterfallSampleForRange(
        ranges, count, range_m, waterfallSideMaxRange(row, channel));
}

inline float waterfallPixelDistanceForRange(
    const PingRow& row, core::SidescanChannel channel,
    core::SidescanRangeCoordinate coordinate,
    bool src_enabled, int h_pan, float sample_zoom) noexcept
{
    const auto& ranges = channel == core::SidescanChannel::Port
        ? row.port_ranges : row.stbd_ranges;
    const int count = static_cast<int>(channel == core::SidescanChannel::Port
        ? row.port.size() : row.stbd.size());
    if (count <= 1 || !(sample_zoom > 0.f) || !coordinate.valid()) return -1.f;

    const bool baked = waterfallSideRangesAreGround(row, channel);
    const float altitude = waterfallSideAltitude(row, channel);
    float lookup_range = static_cast<float>(coordinate.metres);
    if (baked && coordinate.domain == core::SidescanRangeDomain::Slant
            && altitude > 0.f) {
        if (coordinate.metres + 1e-4 < altitude) return -1.f;
        const auto converted = core::convertSidescanRange(
            coordinate, core::SidescanRangeDomain::Ground, altitude);
        if (!converted) return -1.f;
        lookup_range = static_cast<float>(converted->metres);
    }
    float target_range = lookup_range;
    const float side_max = waterfallSideMaxRange(row, channel);
    const float source_sample = waterfallSampleForRange(
        ranges, count, lookup_range, side_max);
    const float unpanned_sample = source_sample - static_cast<float>(h_pan);
    if (unpanned_sample < 0.f) return -1.f;
    target_range = waterfallRangeAtSample(
        ranges, count, unpanned_sample, side_max);

    if (!src_enabled)
        return unpanned_sample * sample_zoom;

    float ground = target_range;
    if (!baked && coordinate.domain == core::SidescanRangeDomain::Slant
            && altitude > 0.f) {
        if (target_range + 1e-4f < altitude) return -1.f;
        const auto converted = core::convertSidescanRange(
            {target_range, core::SidescanRangeDomain::Slant},
            core::SidescanRangeDomain::Ground, altitude);
        if (!converted) return -1.f;
        ground = static_cast<float>(converted->metres);
    }
    float outer_ground = side_max;
    if (!baked && altitude > 0.f && side_max > altitude)
        outer_ground = static_cast<float>(*core::slantToGroundRangeMetres(
            side_max, altitude));
    if (!(outer_ground > 0.f)) return -1.f;
    return std::clamp(ground / outer_ground, 0.f, 1.f)
         * static_cast<float>(count) * sample_zoom;
}

inline bool waterfallNeedsRangeAwareCpu(const std::vector<PingRow>& rows) noexcept
{
    return std::any_of(rows.begin(), rows.end(), [](const PingRow& row) {
        return !row.port_ranges.empty() || !row.stbd_ranges.empty();
    });
}

} // namespace dolphin::ui
