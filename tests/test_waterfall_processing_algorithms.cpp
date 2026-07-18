#include "ui/features/waterfall/processing/WaterfallProcessingAlgorithms.h"
#include "ui/features/waterfall/WaterfallView.h"
#include "ui/shared/processing/SssAmplitudeContext.h"
#include "ui/shared/processing/SssImagingAlgorithms.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

using dolphin::ui::PingRow;
using dolphin::ui::WaterfallParams;
namespace core = dolphin::core;
namespace wf = dolphin::ui::detail;

namespace {

core::SidescanPing makePing(float blanking_m, float slant_range_m, int samples, uint16_t amp)
{
    core::SidescanPing ping;
    ping.blanking_m = blanking_m;
    ping.slant_range_m = slant_range_m;
    ping.samples.resize(static_cast<size_t>(samples));
    for (auto& sample : ping.samples)
        sample.amplitude = amp;
    return ping;
}

bool near(float a, float b, float eps = 2.f)
{
    return std::fabs(a - b) <= eps;
}

} // namespace

int main()
{
    // Non-zero values in histogram bin zero and fully saturated data both need
    // a valid display interval; neither may collapse auto-stretch to {0,0}.
    {
        std::vector<core::SidescanPing> low{
            makePing(0.f, 10.f, 16, 32)};
        const auto low_stretch = dolphin::ui::imaging::computeAutoStretch(low);
        assert(low_stretch.low == 0.f);
        assert(low_stretch.high > low_stretch.low);

        std::vector<core::SidescanPing> saturated{
            makePing(0.f, 10.f, 16, 65535)};
        const auto saturated_stretch =
            dolphin::ui::imaging::computeAutoStretch(saturated);
        assert(saturated_stretch.high > saturated_stretch.low);
        assert(near(saturated_stretch.high, 1.f, 1e-6f));
    }

    {
        WaterfallParams params;
        params.tvg.enabled = true;
        params.tvg.spreading = 20.f;
        params.tvg.absorption = 0.f;

        std::vector<core::SidescanPing> pings = {makePing(1.f, 10.f, 10, 1000)};
        wf::applyTvg(pings, params);

        assert(pings[0].samples.front().amplitude == 1000);
        assert(near(static_cast<float>(pings[0].samples.back().amplitude), 10000.f));
    }

    // Given the same canonical ping set, the map-facing product and waterfall
    // assembly must carry identical displayed amplitudes.
    {
        WaterfallParams params;
        params.destripe.enabled = true;
        params.destripe.window = 3;
        params.destripe.subdivision = 2;

        std::vector<core::SidescanPing> raw;
        for (uint32_t n = 1; n <= 6; ++n) {
            auto port = makePing(0.f, 20.f, 8, static_cast<uint16_t>(4000 + n * 700));
            port.channel = core::SidescanChannel::Port;
            port.ping_number = n;
            port.timestamp_us = static_cast<int64_t>(n) * 1000;
            raw.push_back(port);
            auto stbd = makePing(0.f, 20.f, 8, static_cast<uint16_t>(6000 + n * 500));
            stbd.channel = core::SidescanChannel::Starboard;
            stbd.ping_number = n;
            stbd.timestamp_us = static_cast<int64_t>(n) * 1000;
            raw.push_back(stbd);
        }

        auto canonical = raw;
        dolphin::ui::imaging::applyDisplayPipeline(canonical, params);
        const auto result = dolphin::ui::WaterfallView::runPipeline(raw, params, {}, false);
        assert(result.rows.size() == 6);
        for (size_t i = 0; i < result.rows.size(); ++i) {
            assert(result.rows[i].port.size() == canonical[i * 2].samples.size());
            assert(result.rows[i].stbd.size() == canonical[i * 2 + 1].samples.size());
            for (size_t j = 0; j < result.rows[i].port.size(); ++j) {
                assert(result.rows[i].port[j] == canonical[i * 2].samples[j].amplitude);
                assert(result.rows[i].stbd[j] == canonical[i * 2 + 1].samples[j].amplitude);
            }
        }
        const auto stretch = dolphin::ui::imaging::computeAutoStretch(canonical);
        assert(std::fabs(result.stretch_low - stretch.low) < 1e-6f);
        assert(std::fabs(result.stretch_high - stretch.high) < 1e-6f);
    }

    // Resolution-only map compaction occurs after native-sample per-ping
    // calibration. Retained samples must therefore equal those from the full
    // waterfall product, even for range- and sample-statistic-dependent stages.
    {
        WaterfallParams params;
        params.tvg.enabled = true;
        params.tvg.spreading = 12.f;
        params.arc.enabled = true;
        params.arc.exponent = 1.2f;
        params.agc.enabled = true;
        params.agc.mode = dolphin::app::AgcMode::Global;
        params.agc.edge_skip_samples = 7;

        auto input = makePing(1.f, 80.f, 257, 0);
        input.bottom_pick.range_m = 9.f;
        input.bottom_pick.source = 1;
        for (size_t i = 0; i < input.samples.size(); ++i)
            input.samples[i].amplitude = static_cast<uint16_t>(800 + (i * 137) % 12000);

        std::vector<core::SidescanPing> full{input};
        dolphin::ui::imaging::applyDisplayPipeline(full, params);

        auto bounded = input;
        dolphin::ui::imaging::applyPerPingCalibration(bounded, params);
        constexpr size_t kRetained = 17;
        std::vector<size_t> source_indices;
        std::vector<core::SidescanSample> retained;
        source_indices.reserve(kRetained);
        retained.reserve(kRetained);
        for (size_t i = 0; i < kRetained; ++i) {
            const size_t at = i * (bounded.samples.size() - 1) / (kRetained - 1);
            source_indices.push_back(at);
            retained.push_back(bounded.samples[at]);
        }
        bounded.samples = std::move(retained);
        std::vector<core::SidescanPing> map_product{std::move(bounded)};
        dolphin::ui::imaging::applyContextCalibrationAndImaging(map_product, params);

        assert(map_product.front().samples.size() == kRetained);
        for (size_t i = 0; i < kRetained; ++i)
            assert(map_product.front().samples[i].amplitude
                   == full.front().samples[source_indices[i]].amplitude);
    }

    // Mixed stores are defensive input, but baked gain records must still never
    // be normalized a second time by Variable AGC.
    {
        WaterfallParams params;
        params.agc.enabled = true;
        params.agc.mode = dolphin::app::AgcMode::Variable;
        params.agc.along_track_win = 3;
        params.agc.edge_skip_samples = 0;

        std::vector<core::SidescanPing> pings;
        for (int i = 0; i < 3; ++i) {
            auto ping = makePing(0.f, 20.f, 16,
                static_cast<uint16_t>(2000 + i * 1000));
            ping.timestamp_us = i;
            pings.push_back(std::move(ping));
        }
        pings[1].correction_flags |= core::CorrectionFlag::GainNormalized;
        const auto baked_samples = pings[1].samples;
        dolphin::ui::imaging::applyCalibration(pings, params);
        assert(pings[1].samples.size() == baked_samples.size());
        for (size_t i = 0; i < baked_samples.size(); ++i)
            assert(pings[1].samples[i].amplitude == baked_samples[i].amplitude);
    }

    // Variable AGC gain smoothing is a real processing control, not inert UI.
    // A median window suppresses the isolated raw gain dip caused by one bright
    // ping while leaving the source samples themselves unsmoothed.
    {
        WaterfallParams unsmoothed;
        unsmoothed.agc.enabled = true;
        unsmoothed.agc.mode = dolphin::app::AgcMode::Variable;
        unsmoothed.agc.strength = 0.2f;
        unsmoothed.agc.along_track_win = 1;
        unsmoothed.agc.smoothing_win = 1;
        unsmoothed.agc.edge_skip_samples = 0;
        unsmoothed.agc.noise_floor_pct = 0.f;

        std::vector<core::SidescanPing> source;
        for (int i = 0; i < 5; ++i) {
            auto ping = makePing(0.f, 10.f, 16,
                static_cast<uint16_t>(i == 2 ? 10000 : 1000));
            ping.timestamp_us = i + 1;
            source.push_back(std::move(ping));
        }
        auto direct = source;
        dolphin::ui::imaging::applyCalibration(direct, unsmoothed);

        auto median_params = unsmoothed;
        median_params.agc.smoothing_win = 3;
        median_params.agc.smoothing_type = dolphin::app::AgcSmoothingType::Median;
        auto median = source;
        dolphin::ui::imaging::applyCalibration(median, median_params);
        assert(median[2].samples[0].amplitude
               > direct[2].samples[0].amplitude);
        assert(median[0].samples[0].amplitude
               == direct[0].samples[0].amplitude);
    }

    // Mixed durable stores must not apply already-baked line operators twice.
    // Baked rows still participate in the correction statistics so unbaked
    // neighbours receive the same line-level correction.
    {
        WaterfallParams params;
        params.beam_pattern.enabled = true;
        params.beam_pattern.strength = 1.f;
        params.beam_pattern.smooth_radius = 0;

        std::vector<core::SidescanPing> pings;
        pings.push_back(makePing(0.f, 10.f, 8, 1000));
        pings.push_back(makePing(0.f, 10.f, 8, 10000));
        pings[0].timestamp_us = 1;
        pings[1].timestamp_us = 2;
        pings[0].correction_flags |= core::CorrectionFlag::BeamPattern;
        const auto baked = pings[0].samples;
        const auto unbaked_before = pings[1].samples;

        dolphin::ui::imaging::applyImagingChain(pings, params);
        for (size_t i = 0; i < baked.size(); ++i)
            assert(pings[0].samples[i].amplitude == baked[i].amplitude);
        assert(pings[1].samples[0].amplitude
               != unbaked_before[0].amplitude);
    }

    {
        WaterfallParams params;
        params.destripe.enabled = true;
        params.destripe.window = 3;
        params.destripe.subdivision = 1;
        params.destripe.capping = 5.f;

        std::vector<core::SidescanPing> pings;
        pings.push_back(makePing(0.f, 10.f, 8, 1000));
        pings.push_back(makePing(0.f, 10.f, 8, 10000));
        pings.push_back(makePing(0.f, 10.f, 8, 1000));
        for (size_t i = 0; i < pings.size(); ++i)
            pings[i].timestamp_us = static_cast<int64_t>(i + 1);
        pings[0].correction_flags |= core::CorrectionFlag::Destriping;
        const auto baked = pings[0].samples;
        const auto unbaked_before = pings[2].samples;

        dolphin::ui::imaging::applyImagingChain(pings, params);
        for (size_t i = 0; i < baked.size(); ++i)
            assert(pings[0].samples[i].amplitude == baked[i].amplitude);
        assert(pings[2].samples[0].amplitude
               != unbaked_before[0].amplitude);
    }

    // A canonical line context makes context-sensitive enhancements independent
    // of the consumer's window. The same ping must have the same amplitudes in a
    // full product, a waterfall sub-window, and a compacted map product.
    {
        WaterfallParams params;
        params.agc.enabled = true;
        params.agc.mode = dolphin::app::AgcMode::Variable;
        params.agc.along_track_win = 5;
        params.agc.edge_skip_samples = 0;
        params.beam_pattern.enabled = true;
        params.beam_pattern.smooth_radius = 2;
        params.arn.enabled = true;
        params.arn.column_smooth = 1;
        params.destripe.enabled = true;
        params.destripe.window = 5;
        params.destripe.subdivision = 4;
        params.ml_enhance.enabled = true;
        params.ml_enhance.tile_pings = 16;
        params.ml_enhance.tile_samps = 16;

        std::vector<core::SidescanPing> raw;
        for (uint32_t group = 0; group < 24; ++group) {
            for (int side = 0; side < 2; ++side) {
                auto ping = makePing(0.f, 40.f, 65, 0);
                ping.id = static_cast<uint64_t>(group) * 2u + side + 1u;
                ping.ping_number = group + 1;
                ping.timestamp_us = static_cast<int64_t>(group + 1) * 100000;
                ping.channel = side == 0 ? core::SidescanChannel::Port
                                         : core::SidescanChannel::Starboard;
                for (size_t s = 0; s < ping.samples.size(); ++s)
                    ping.samples[s].amplitude = static_cast<uint16_t>(
                        700 + ((group * 997u + side * 311u + s * 173u) % 18000u));
                raw.push_back(std::move(ping));
            }
        }

        auto calibrated = raw;
        for (auto& ping : calibrated)
            dolphin::ui::imaging::applyPerPingCalibration(ping, params);

        std::vector<core::SidescanPing> context_source;
        for (size_t i = 0; i < calibrated.size(); i += 6) {
            context_source.push_back(calibrated[i]);
            if (i + 1 < calibrated.size())
                context_source.push_back(calibrated[i + 1]);
        }
        const auto context =
            dolphin::ui::imaging::buildSssAmplitudeContextFromCalibrated(
                context_source, params);
        assert(context && context->valid());

        auto full = calibrated;
        dolphin::ui::imaging::applySssAmplitudeContext(full, *context);

        std::vector<core::SidescanPing> window(
            calibrated.begin() + 10, calibrated.begin() + 34);
        dolphin::ui::imaging::applySssAmplitudeContext(window, *context);
        for (const auto& ping : window) {
            const auto it = std::find_if(full.cbegin(), full.cend(),
                [&](const core::SidescanPing& candidate) {
                    return candidate.id == ping.id;
                });
            assert(it != full.cend());
            assert(it->samples.size() == ping.samples.size());
            for (size_t s = 0; s < ping.samples.size(); ++s)
                assert(it->samples[s].amplitude == ping.samples[s].amplitude);
        }

        auto compact = calibrated[21];
        std::vector<size_t> retained_indices;
        std::vector<core::SidescanSample> retained;
        constexpr size_t kMapSamples = 17;
        for (size_t i = 0; i < kMapSamples; ++i) {
            const size_t at = i * (compact.samples.size() - 1) / (kMapSamples - 1);
            retained_indices.push_back(at);
            retained.push_back(compact.samples[at]);
        }
        compact.samples = std::move(retained);
        std::vector<core::SidescanPing> map_product{std::move(compact)};
        dolphin::ui::imaging::applySssAmplitudeContext(map_product, *context);
        const auto full_it = std::find_if(full.cbegin(), full.cend(),
            [&](const core::SidescanPing& candidate) {
                return candidate.id == map_product.front().id;
            });
        assert(full_it != full.cend());
        for (size_t i = 0; i < kMapSamples; ++i)
            assert(map_product.front().samples[i].amplitude
                   == full_it->samples[retained_indices[i]].amplitude);
    }

    {
        WaterfallParams params;
        params.tvg.enabled = true;
        params.tvg.spreading = 20.f;
        params.tvg.absorption = 0.f;

        std::vector<core::SidescanPing> pings = {makePing(0.f, 10.f, 10, 1000)};
        wf::applyTvg(pings, params);

        assert(pings[0].samples.front().amplitude == 1000);
        assert(near(static_cast<float>(pings[0].samples.back().amplitude), 10000.f));
    }

    {
        WaterfallParams params;
        params.tvg.enabled = true;
        params.tvg.spreading = 20.f;
        params.tvg.absorption = 1.f;

        std::vector<core::SidescanPing> pings = {makePing(12.f, 10.f, 10, 1000)};
        wf::applyTvg(pings, params);

        for (const auto& sample : pings[0].samples)
            assert(sample.amplitude == 1000);
    }

    {
        WaterfallParams params;
        params.arn.enabled = true;
        params.arn.strength = 1.f;
        params.arn.gain_cap_db = 20.f;
        params.arn.column_smooth = 0;

        std::vector<PingRow> rows(5);
        for (auto& row : rows) {
            row.port = {10000, 20000, 40000};
            row.stbd = {10000, 20000, 40000};
        }

        wf::applyArn(rows, params);

        for (const auto& row : rows) {
            assert(near(static_cast<float>(row.port[0]), 32768.f));
            assert(near(static_cast<float>(row.port[1]), 32768.f));
            assert(near(static_cast<float>(row.port[2]), 32768.f));
            assert(near(static_cast<float>(row.stbd[0]), 32768.f));
            assert(near(static_cast<float>(row.stbd[1]), 32768.f));
            assert(near(static_cast<float>(row.stbd[2]), 32768.f));
        }
    }

    {
        WaterfallParams params;
        params.arn.enabled = true;
        params.arn.strength = 1.f;
        params.arn.gain_cap_db = 20.f;
        params.arn.column_smooth = 1;

        std::vector<PingRow> rows(5);
        for (auto& row : rows) {
            row.port = {10000, 0, 10000};
            row.stbd = {10000, 0, 10000};
        }

        wf::applyArn(rows, params);

        for (const auto& row : rows) {
            assert(near(static_cast<float>(row.port[0]), 32768.f));
            assert(row.port[1] == 0);
            assert(near(static_cast<float>(row.port[2]), 32768.f));
            assert(near(static_cast<float>(row.stbd[0]), 32768.f));
            assert(row.stbd[1] == 0);
            assert(near(static_cast<float>(row.stbd[2]), 32768.f));
        }
    }

    return 0;
}
