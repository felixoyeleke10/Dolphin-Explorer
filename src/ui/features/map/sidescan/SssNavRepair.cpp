#include "ui/features/map/sidescan/SssNavRepair.h"
#include "ui/features/map/sidescan/SssNavContinuity.h"

#include <algorithm>
#include <cmath>

namespace dolphin::ui::sssnavrepair {
namespace {

constexpr int64_t kHardSpanUs = 60'000'000LL;
constexpr double kHardDistanceM = 1'000.0;
constexpr double kMaxSpeedMps = 15.0;
constexpr double kCadenceFactor = 4.0;
constexpr int64_t kTimeFloorUs = 5'000'000LL;
constexpr int64_t kTimeCeilingUs = 30'000'000LL;
constexpr uint32_t kPingFloor = 20;
constexpr uint32_t kPingCeiling = 1'000;
constexpr size_t kMinCadenceSamples = 3;
constexpr double kCycleMergeToleranceM = 2.0;

struct Policy {
    int64_t max_cycle_gap_us = kTimeFloorUs;
    uint32_t max_ping_advance = kPingFloor;
};

std::vector<int64_t> cycleTimestamps(
    const std::vector<core::SidescanPing>& pings,
    const std::vector<size_t>& order)
{
    std::vector<int64_t> result(order.size());
    size_t begin = 0;
    while (begin < order.size()) {
        size_t end = begin + 1;
        while (end < order.size()
                && sssnavcontinuity::samePingCycle(
                    pings[order[begin]], pings[order[end]])) ++end;
        int64_t first = 0, last = 0;
        for (size_t i = begin; i < end; ++i) {
            const int64_t value = pings[order[i]].timestamp_us;
            if (value <= 0) continue;
            if (first == 0 || value < first) first = value;
            if (last == 0 || value > last) last = value;
        }
        const int64_t timestamp = first > 0 ? first + (last - first) / 2 : 0;
        for (size_t i = begin; i < end; ++i) result[i] = timestamp;
        begin = end;
    }
    return result;
}

double cadenceLimit(std::vector<double> values, double floor, double ceiling)
{
    std::erase_if(values, [](double value) {
        return !std::isfinite(value) || value <= 0.0;
    });
    if (values.size() < kMinCadenceSamples) return floor;
    const size_t middle = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + middle, values.end());
    return std::clamp(values[middle] * kCadenceFactor, floor, ceiling);
}

Policy derivePolicy(const std::vector<core::SidescanPing>& pings,
                    const std::vector<size_t>& order,
                    const std::vector<int64_t>& timestamps)
{
    std::vector<double> time_deltas, ping_deltas;
    for (size_t i = 1; i < order.size(); ++i) {
        const auto& previous = pings[order[i - 1]];
        const auto& current = pings[order[i]];
        if (sssnavcontinuity::samePingCycle(previous, current)) continue;
        const int64_t delta = timestamps[i] - timestamps[i - 1];
        if (delta > 0) time_deltas.push_back(static_cast<double>(delta));
        if (previous.ping_number > 0 && current.ping_number > previous.ping_number)
            ping_deltas.push_back(static_cast<double>(
                current.ping_number - previous.ping_number));
    }
    Policy policy;
    policy.max_cycle_gap_us = static_cast<int64_t>(std::llround(cadenceLimit(
        std::move(time_deltas), kTimeFloorUs, kTimeCeilingUs)));
    policy.max_ping_advance = static_cast<uint32_t>(std::llround(cadenceLimit(
        std::move(ping_deltas), kPingFloor, kPingCeiling)));
    return policy;
}

bool metadataContinues(const std::vector<int64_t>& timestamps,
                       const std::vector<core::SidescanPing>& pings,
                       const std::vector<size_t>& order,
                       const Policy& policy, size_t previous, size_t current)
{
    const auto& a = pings[order[previous]];
    const auto& b = pings[order[current]];
    const bool same_cycle = sssnavcontinuity::samePingCycle(a, b);
    const int64_t delta = timestamps[current] - timestamps[previous];
    if (same_cycle) return delta == 0;
    if (delta <= 0 || delta > policy.max_cycle_gap_us) return false;
    if (a.ping_number > 0 && b.ping_number > 0) {
        if (b.ping_number <= a.ping_number
                || b.ping_number - a.ping_number > policy.max_ping_advance)
            return false;
    }
    return true;
}

bool bounded(const std::vector<CorrectedSssNav>& table,
             const std::vector<int64_t>& timestamps,
             const std::vector<core::SidescanPing>& pings,
             const std::vector<size_t>& order, const Policy& policy,
             size_t left, size_t right)
{
    const auto& a = table[left];
    const auto& b = table[right];
    if (!a.valid || !b.valid || !sssnavcontinuity::compatibleFrames(a, b))
        return false;
    const int64_t span = timestamps[right] - timestamps[left];
    if (timestamps[left] <= 0 || span <= 0 || span > kHardSpanUs) return false;
    const double distance = sssnavcontinuity::distanceMetres(a, b);
    if (!std::isfinite(distance) || distance > kHardDistanceM
            || distance / (static_cast<double>(span) / 1'000'000.0) > kMaxSpeedMps)
        return false;
    for (size_t i = left + 1; i <= right; ++i)
        if (!metadataContinues(timestamps, pings, order, policy, i - 1, i)
                || timestamps[i] < timestamps[left]
                || timestamps[i] > timestamps[right]) return false;
    return true;
}

void interpolate(std::vector<CorrectedSssNav>& table,
                 const std::vector<int64_t>& timestamps,
                 const std::vector<core::SidescanPing>& pings,
                 const std::vector<size_t>& order, const Policy& policy,
                 size_t left, size_t right)
{
    if (!bounded(table, timestamps, pings, order, policy, left, right)) return;
    const auto a = table[left];
    const auto b = table[right];
    const double span = static_cast<double>(timestamps[right] - timestamps[left]);
    const uint32_t flags = a.flags & b.flags;
    const core::SpatialRef spatial_ref = !a.spatial_ref.empty()
        ? a.spatial_ref : b.spatial_ref;
    for (size_t i = left + 1; i < right; ++i) {
        const double alpha = static_cast<double>(timestamps[i] - timestamps[left]) / span;
        table[i].lat = a.lat + (b.lat - a.lat) * alpha;
        table[i].lon = sssnavcontinuity::interpolateLongitude(
            a.lon, b.lon, alpha, a.is_projected);
        table[i].valid = true;
        table[i].is_projected = a.is_projected;
        table[i].spatial_ref = spatial_ref;
        table[i].flags |= flags | kNavFlagInterpolated;
    }
}

void reconcileCycles(std::vector<CorrectedSssNav>& table,
                     const std::vector<core::SidescanPing>& pings,
                     const std::vector<size_t>& order)
{
    size_t begin = 0;
    while (begin < order.size()) {
        size_t end = begin + 1;
        while (end < order.size() && sssnavcontinuity::samePingCycle(
                pings[order[begin]], pings[order[end]])) ++end;
        size_t anchor = begin;
        while (anchor < end && !table[anchor].valid) ++anchor;
        if (anchor < end) {
            auto pose = table[anchor];
            size_t count = 1;
            bool compatible = true;
            for (size_t i = anchor + 1; i < end; ++i) {
                if (!table[i].valid) continue;
                if (!sssnavcontinuity::compatibleFrames(pose, table[i])
                        || sssnavcontinuity::distanceMetres(pose, table[i])
                            > kCycleMergeToleranceM) {
                    compatible = false;
                    break;
                }
                const double alpha = 1.0 / static_cast<double>(++count);
                pose.lat += (table[i].lat - pose.lat) * alpha;
                pose.lon = sssnavcontinuity::interpolateLongitude(
                    pose.lon, table[i].lon, alpha, pose.is_projected);
                pose.flags |= table[i].flags;
            }
            if (compatible) {
                for (size_t i = begin; i < end; ++i) {
                    const bool changed = !table[i].valid
                        || table[i].lat != pose.lat || table[i].lon != pose.lon;
                    table[i].lat = pose.lat;
                    table[i].lon = pose.lon;
                    table[i].valid = true;
                    table[i].is_projected = pose.is_projected;
                    table[i].spatial_ref = pose.spatial_ref;
                    table[i].flags |= pose.flags;
                    if (changed) table[i].flags |= kNavFlagInterpolated;
                }
            }
        }
        begin = end;
    }
}

} // namespace

void repairBoundedRuns(std::vector<CorrectedSssNav>& table,
                       const std::vector<core::SidescanPing>& pings,
                       const std::vector<size_t>& order)
{
    if (table.size() < 2) return;
    const auto timestamps = cycleTimestamps(pings, order);
    const auto policy = derivePolicy(pings, order, timestamps);
    reconcileCycles(table, pings, order);

    size_t i = 0;
    while (i < table.size()) {
        if (table[i].valid) { ++i; continue; }
        const size_t begin = i;
        while (i < table.size() && !table[i].valid) ++i;
        if (begin > 0 && i < table.size())
            interpolate(table, timestamps, pings, order, policy, begin - 1, i);
    }

    size_t left = 0;
    while (left < table.size()) {
        if (!table[left].valid) { ++left; continue; }
        size_t right = left + 1;
        while (right < table.size()
                && sssnavcontinuity::positionsCoincide(table[left], table[right])
                && metadataContinues(
                    timestamps, pings, order, policy, right - 1, right)) ++right;
        if (right > left + 1 && right < table.size() && table[right].valid)
            interpolate(table, timestamps, pings, order, policy, left, right);
        left = right > left + 1 ? right : left + 1;
    }
}

} // namespace dolphin::ui::sssnavrepair
