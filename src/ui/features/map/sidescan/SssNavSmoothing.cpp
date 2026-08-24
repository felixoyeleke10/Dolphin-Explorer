#include "ui/features/map/sidescan/SssNavSmoothing.h"
#include "ui/features/map/sidescan/SssNavContinuity.h"

#include <algorithm>
#include <cmath>

namespace dolphin::ui::sssnavsmoothing {
namespace {

double median(std::vector<double> values)
{
    const size_t middle = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + middle, values.end());
    if ((values.size() & 1u) != 0) return values[middle];
    return (*std::max_element(values.begin(), values.begin() + middle)
            + values[middle]) * 0.5;
}

} // namespace

void apply(std::vector<CorrectedSssNav>& table,
           const std::vector<core::SidescanPing>& pings,
           const std::vector<size_t>& order,
           SssNavSmoothingMode mode,
           int window)
{
    const size_t count = table.size();
    if (count < 2 || mode == SssNavSmoothingMode::Off) return;
    const auto continuity = sssnavcontinuity::deriveThresholds(
        pings, order, table);

    if (mode == SssNavSmoothingMode::SpikeRejection) {
        const auto original = table;
        for (const auto channel : {core::SidescanChannel::Port,
                                   core::SidescanChannel::Starboard}) {
            std::vector<size_t> indices;
            for (size_t i = 0; i < count; ++i)
                if (pings[order[i]].channel == channel && original[i].valid)
                    indices.push_back(i);
            for (size_t j = 1; j + 1 < indices.size(); ++j) {
                const size_t previous = indices[j - 1];
                const size_t current = indices[j];
                const size_t next = indices[j + 1];
                if (!sssnavcontinuity::isContinuousPair(
                        original[previous], original[next],
                        pings[order[previous]], pings[order[next]], continuity))
                    continue;
                if (sssnavcontinuity::distanceMetres(
                        original[previous], original[current]) <= continuity.nav_gap_m
                    || sssnavcontinuity::distanceMetres(
                        original[current], original[next]) <= continuity.nav_gap_m)
                    continue;
                const int64_t left = pings[order[previous]].timestamp_us;
                const int64_t here = pings[order[current]].timestamp_us;
                const int64_t right = pings[order[next]].timestamp_us;
                const double alpha = left > 0 && here >= left && right > here
                    ? static_cast<double>(here - left) / static_cast<double>(right - left)
                    : 0.5;
                table[current].lat = original[previous].lat
                    + (original[next].lat - original[previous].lat) * alpha;
                table[current].lon = sssnavcontinuity::interpolateLongitude(
                    original[previous].lon, original[next].lon, alpha,
                    original[previous].is_projected);
                table[current].flags |= kNavFlagInterpolated;
            }
        }
        return;
    }

    if (mode != SssNavSmoothingMode::MovingAverage
            && mode != SssNavSmoothingMode::Median) return;
    const int half_window = std::max(1, window / 2);
    auto smoothed = table;
    const auto sameSegment = [&](size_t a, size_t b) {
        const size_t first = std::min(a, b);
        const size_t last = std::max(a, b);
        for (size_t i = first + 1; i <= last; ++i)
            if (!sssnavcontinuity::isContinuousPair(
                    table[i - 1], table[i], pings[order[i - 1]],
                    pings[order[i]], continuity)) return false;
        return true;
    };

    for (size_t i = 0; i < count; ++i) {
        if (!table[i].valid) continue;
        std::vector<double> latitudes, longitudes;
        for (int j = static_cast<int>(i) - half_window;
                j <= static_cast<int>(i) + half_window; ++j) {
            if (j < 0 || static_cast<size_t>(j) >= count || !table[j].valid
                    || !sameSegment(i, static_cast<size_t>(j))) continue;
            latitudes.push_back(table[j].lat);
            longitudes.push_back(table[i].is_projected ? table[j].lon
                : table[i].lon + std::remainder(table[j].lon - table[i].lon, 360.0));
        }
        if (latitudes.size() <= 1) continue;
        if (mode == SssNavSmoothingMode::Median) {
            smoothed[i].lat = median(latitudes);
            smoothed[i].lon = median(longitudes);
        } else {
            double latitude_sum = 0.0, longitude_sum = 0.0;
            for (double value : latitudes) latitude_sum += value;
            for (double value : longitudes) longitude_sum += value;
            smoothed[i].lat = latitude_sum / latitudes.size();
            smoothed[i].lon = longitude_sum / longitudes.size();
        }
        if (!table[i].is_projected)
            smoothed[i].lon = std::remainder(smoothed[i].lon, 360.0);
    }
    table = std::move(smoothed);
}

} // namespace dolphin::ui::sssnavsmoothing
