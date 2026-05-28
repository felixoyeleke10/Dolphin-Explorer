#include "ui/features/waterfall/processing/SeabedAutoDetector.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

using dolphin::ui::PingRow;
using dolphin::ui::SeabedAutoDetector;
using dolphin::ui::SeabedAutoParams;
using dolphin::ui::SeabedMethod;

namespace {

std::vector<uint16_t> syntheticReturn(int n, int seabed_idx)
{
    std::vector<uint16_t> samples(static_cast<size_t>(n), 10);
    for (int i = seabed_idx; i < n; ++i) {
        const int decay = i - seabed_idx;
        samples[static_cast<size_t>(i)] = static_cast<uint16_t>(1000 - std::min(decay * 8, 650));
    }
    return samples;
}

std::vector<PingRow> syntheticRows(int count, int first_idx, int step_idx)
{
    std::vector<PingRow> rows(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        const int seabed_idx = first_idx + i * step_idx;
        auto samples = syntheticReturn(100, seabed_idx);
        rows[static_cast<size_t>(i)].port = samples;
        rows[static_cast<size_t>(i)].stbd = samples;
        rows[static_cast<size_t>(i)].slant_range_m = 99.f;
        rows[static_cast<size_t>(i)].timestamp_us = i + 1;
    }
    return rows;
}

void addEarlyFalseReturn(PingRow& row, int idx)
{
    if (idx < 0 || idx + 1 >= static_cast<int>(row.port.size())) return;
    row.port[static_cast<size_t>(idx)] = 1800;
    row.port[static_cast<size_t>(idx + 1)] = 1600;
    row.stbd[static_cast<size_t>(idx)] = 1800;
    row.stbd[static_cast<size_t>(idx + 1)] = 1600;
}

int detectedCount(const std::vector<PingRow>& rows)
{
    int count = 0;
    for (const auto& row : rows) {
        if (row.seabed.detected && row.seabed.range_m > 0.f)
            ++count;
    }
    return count;
}

} // namespace

int main()
{
    {
        SeabedAutoParams params;
        assert(params.method == SeabedMethod::ContinuityAware);
    }

    {
        auto rows = syntheticRows(8, 30, 1);
        SeabedAutoDetector::detectAll(rows, {});

        assert(detectedCount(rows) == static_cast<int>(rows.size()));
        for (const auto& row : rows) {
            assert(row.seabed.confidence > 0.38f);
            assert(row.seabed.range_m >= 28.f);
            assert(row.seabed.range_m <= 40.f);
        }
    }

    {
        auto rows = syntheticRows(10, 25, 2);
        SeabedAutoParams params;
        params.method = SeabedMethod::ContinuityAware;
        params.max_delta_m = 0.f;

        SeabedAutoDetector::detectAll(rows, params);

        assert(detectedCount(rows) == static_cast<int>(rows.size()));
        for (int i = 1; i < static_cast<int>(rows.size()); ++i) {
            assert(rows[static_cast<size_t>(i)].seabed.range_m
                   > rows[static_cast<size_t>(i - 1)].seabed.range_m);
        }
    }

    {
        auto rows = syntheticRows(14, 42, 0);
        addEarlyFalseReturn(rows[0], 8);
        addEarlyFalseReturn(rows[1], 9);

        SeabedAutoParams params;
        params.method = SeabedMethod::ContinuityAware;
        params.max_delta_m = 5.f;
        SeabedAutoDetector::detectAll(rows, params);

        for (int i = 2; i < static_cast<int>(rows.size()); ++i) {
            assert(rows[static_cast<size_t>(i)].seabed.detected);
            assert(rows[static_cast<size_t>(i)].seabed.range_m >= 39.f);
            assert(rows[static_cast<size_t>(i)].seabed.range_m <= 45.f);
        }
    }

    return 0;
}
