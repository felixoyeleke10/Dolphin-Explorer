#include "ui/features/waterfall/processing/SeabedAutoDetector.h"
#include "ui/features/waterfall/rendering/WaterfallRenderer.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

using dolphin::ui::PingRow;
using dolphin::ui::SeabedAutoDetector;
using dolphin::ui::SeabedAutoParams;
using dolphin::ui::SeabedMethod;
using dolphin::ui::WaterfallParams;
using dolphin::ui::WaterfallRenderer;
using dolphin::ui::WfLayout;

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

std::vector<uint16_t> syntheticFirstReturn(int n, int seabed_idx)
{
    std::vector<uint16_t> samples(static_cast<size_t>(n), 10);
    for (int i = seabed_idx; i < std::min(n, seabed_idx + 8); ++i) {
        const int decay = i - seabed_idx;
        samples[static_cast<size_t>(i)] = static_cast<uint16_t>(900 - decay * 70);
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

std::vector<PingRow> syntheticFirstReturnRows(int count, int first_idx, int step_idx)
{
    std::vector<PingRow> rows(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        const int seabed_idx = first_idx + i * step_idx;
        auto samples = syntheticFirstReturn(100, seabed_idx);
        rows[static_cast<size_t>(i)].port = samples;
        rows[static_cast<size_t>(i)].stbd = samples;
        rows[static_cast<size_t>(i)].slant_range_m = 99.f;
        rows[static_cast<size_t>(i)].timestamp_us = i + 1;
    }
    return rows;
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
        assert(params.method == SeabedMethod::Threshold);
    }

    {
        auto rows = syntheticRows(8, 30, 1);
        SeabedAutoParams params;
        params.method = SeabedMethod::Threshold;
        SeabedAutoDetector::detectAll(rows, params);

        assert(detectedCount(rows) == static_cast<int>(rows.size()));
        for (const auto& row : rows) {
            assert(row.seabed.confidence > 0.80f);
            assert(row.seabed.range_m >= 28.f);
            assert(row.seabed.range_m <= 40.f);
        }
    }

    {
        auto rows = syntheticFirstReturnRows(6, 25, 2);
        SeabedAutoParams params;
        params.method = SeabedMethod::FirstReturn;
        params.min_snr = 3.f;

        SeabedAutoDetector::detectAll(rows, params);

        assert(detectedCount(rows) == static_cast<int>(rows.size()));
        for (int i = 1; i < static_cast<int>(rows.size()); ++i) {
            assert(rows[static_cast<size_t>(i)].seabed.range_m
                   > rows[static_cast<size_t>(i - 1)].seabed.range_m);
        }
    }

    {
        auto rows = syntheticRows(12, 42, 0);

        SeabedAutoParams params;
        params.method = SeabedMethod::Threshold;
        params.max_delta_m = 5.f;
        SeabedAutoDetector::detectAll(rows, params);

        for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
            assert(rows[static_cast<size_t>(i)].seabed.range_m >= 39.f);
            assert(rows[static_cast<size_t>(i)].seabed.range_m <= 45.f);
        }
    }

    // Interpolated detector gaps are real derived picks, not display-only
    // ranges: correction geometry must see the same continuous bottom track.
    {
        std::vector<PingRow> rows(3);
        rows[0].seabed = {10.f, 0.8f, true, false};
        rows[2].seabed = {14.f, 0.7f, true, false};
        SeabedAutoDetector::gapFill(rows, 2);
        assert(rows[1].seabed.detected);
        assert(std::abs(rows[1].seabed.range_m - 12.f) < 1e-6f);
        assert(std::abs(rows[1].seabed.confidence - 0.7f) < 1e-6f);
    }

    // Non-linear range regression test.
    // Seabed echo is at sample index 60 of 100.  Linear interpolation places it
    // at ~60 m; the non-linear mapping places it at 90 m.  When port_ranges /
    // stbd_ranges are populated the detector must use the real range, not the
    // linear approximation.
    {
        const int   n_samp  = 100;
        const float slant   = 100.f;
        const int   sb_idx  = 60;

        // Build two row sets: one without per-sample ranges (linear fallback),
        // one with non-linear ranges where sample sb_idx maps to 90 m.
        std::vector<PingRow> rows_lin(4), rows_nl(4);
        for (int ri = 0; ri < 4; ++ri) {
            auto ampl = syntheticReturn(n_samp, sb_idx);

            rows_lin[ri].port  = ampl;  rows_lin[ri].stbd  = ampl;
            rows_nl[ri].port   = ampl;  rows_nl[ri].stbd   = ampl;
            rows_lin[ri].slant_range_m = slant;
            rows_nl[ri].slant_range_m  = slant;
            rows_lin[ri].timestamp_us  = ri + 1;
            rows_nl[ri].timestamp_us   = ri + 1;

            // Non-linear: samples 0..sb_idx span 0..90 m, rest span 90..100 m.
            rows_nl[ri].port_ranges.resize(static_cast<size_t>(n_samp));
            rows_nl[ri].stbd_ranges.resize(static_cast<size_t>(n_samp));
            rows_nl[ri].port_range_domain = dolphin::core::SidescanRangeDomain::Ground;
            rows_nl[ri].stbd_range_domain = dolphin::core::SidescanRangeDomain::Ground;
            for (int j = 0; j < n_samp; ++j) {
                const float r = (j <= sb_idx)
                    ? slant * 0.9f * static_cast<float>(j) / static_cast<float>(sb_idx)
                    : 0.9f * slant + 0.1f * slant
                        * static_cast<float>(j - sb_idx)
                        / static_cast<float>(n_samp - 1 - sb_idx);
                rows_nl[ri].port_ranges[static_cast<size_t>(j)] = r;
                rows_nl[ri].stbd_ranges[static_cast<size_t>(j)] = r;
            }
            // port_ranges[sb_idx] == 90.f; linear would give ~60.6 m.
        }

        SeabedAutoParams params;
        params.method = SeabedMethod::Threshold;

        SeabedAutoDetector::detectAll(rows_lin, params);
        SeabedAutoDetector::detectAll(rows_nl,  params);

        for (const auto& row : rows_lin)
            assert(row.seabed.range_m > 50.f && row.seabed.range_m < 72.f);

        for (const auto& row : rows_nl) {
            assert(row.seabed.range_m > 82.f && row.seabed.range_m < 98.f);
            assert(row.seabed_domain == dolphin::core::SidescanRangeDomain::Ground);
        }
    }

    // Rendering and hit-testing share the same nonlinear range table and the
    // same per-channel altitude fallback when no displayed seabed pick exists.
    {
        PingRow row;
        row.port = {1, 2, 3, 4, 5};
        row.stbd = row.port;
        row.port_ranges = {0.f, 10.f, 25.f, 60.f, 100.f};
        row.stbd_ranges = row.port_ranges;
        row.slant_range_m = 100.f;
        row.port_altitude_m = 10.f;
        row.stbd_altitude_m = 20.f;

        WaterfallRenderer renderer;
        WfLayout layout;
        layout.widget_w = 200;
        layout.widget_h = 100;
        layout.nadir_x = 100;
        layout.img_h = 50;
        renderer.setLayout(layout);
        WaterfallParams display;
        display.slant_range_correction = true;
        renderer.setParams(display);

        std::vector<PingRow> rows{row};
        dolphin::core::SidescanChannel channel{};
        float port_range = 0.f;
        float stbd_range = 0.f;
        assert(renderer.xToRange(49, 0, rows, 0.f, 0,
                                 channel, port_range));
        assert(channel == dolphin::core::SidescanChannel::Port);
        assert(renderer.xToRange(150, 0, rows, 0.f, 0,
                                 channel, stbd_range));
        assert(channel == dolphin::core::SidescanChannel::Starboard);
        assert(port_range > 50.f && stbd_range > 50.f);
        assert(std::abs(port_range - stbd_range) > 0.5f);

        row.port_ranges = {0.f, 1.f, 2.f, 3.f, 80.f};
        row.port_range_domain = dolphin::core::SidescanRangeDomain::Ground;
        rows[0] = row;
        float baked_near_nadir = -1.f;
        assert(renderer.xToRange(99, 0, rows, 0.f, 0,
                                 channel, baked_near_nadir));
        assert(channel == dolphin::core::SidescanChannel::Port);
        assert(baked_near_nadir < 3.f);
        float baked_mid = -1.f;
        assert(renderer.xToRange(50, 0, rows, 0.f, 0,
                                 channel, baked_mid));
        assert(baked_mid > 30.f && baked_mid < 45.f);
    }

    return 0;
}
