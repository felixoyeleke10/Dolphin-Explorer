#include "ui/features/waterfall/processing/WaterfallProcessingAlgorithms.h"
#include "ui/features/waterfall/WaterfallView.h"
#include "ui/shared/processing/SssAmplitudeContext.h"
#include "ui/shared/processing/SssImagingAlgorithms.h"
#include "ui/shared/BottomTrackDisplayPolicy.h"
#include "core/SidescanGeometry.h"
#include "app/corrections/CorrectionAlgorithms.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <limits>
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
    // Bottom picks remain usable by processing without leaking into corrected
    // display output. Editing is the intentional, temporary QC exception.
    assert(dolphin::ui::shouldPaintBottomTrack(true, false, false));
    assert(!dolphin::ui::shouldPaintBottomTrack(true, true, false));
    assert(dolphin::ui::shouldPaintBottomTrack(true, true, true));
    assert(!dolphin::ui::shouldPaintBottomTrack(false, false, false));

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

    // SRC implicitly requires a bottom track when acquisition altitude is
    // absent, even if the user previously hid/cleared the seabed overlay.
    {
        WaterfallParams params;
        params.slant_range_correction = true;
        auto port = makePing(0.0f, 20.0f, 20, 10);
        auto stbd = port;
        port.channel = core::SidescanChannel::Port;
        stbd.channel = core::SidescanChannel::Starboard;
        port.timestamp_us = stbd.timestamp_us = 1'000;
        for (size_t i = 0; i < port.samples.size(); ++i) {
            const float range = 20.0f * static_cast<float>(i)
                / static_cast<float>(port.samples.size() - 1);
            port.samples[i].range_m = range;
            stbd.samples[i].range_m = range;
        }
        port.samples[8].amplitude = 50'000;
        stbd.samples[8].amplitude = 50'000;
        port.samples[9].amplitude = 30'000;
        stbd.samples[9].amplitude = 30'000;
        const auto result = dolphin::ui::WaterfallView::runPipeline(
            {port, stbd}, params, {}, false);
        assert(result.rows.size() == 1);
        assert(result.rows[0].seabed.detected);
        assert(result.rows[0].seabed.range_m > 0.0f);
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

    // Global AGC is one line-level gain per channel. It must preserve the
    // relative brightness between pings instead of independently forcing every
    // ping to the same mean (which is Variable AGC behaviour at window=1).
    {
        WaterfallParams params;
        params.agc.enabled = true;
        params.agc.mode = dolphin::app::AgcMode::Global;
        params.agc.strength = 1.f;
        params.agc.edge_skip_samples = 0;
        params.agc.noise_floor_pct = 0.f;

        auto dark = makePing(0.f, 10.f, 16, 1000);
        auto bright = makePing(0.f, 10.f, 16, 4000);
        dark.channel = bright.channel = core::SidescanChannel::Port;
        std::vector<core::SidescanPing> pings{dark, bright};
        dolphin::ui::imaging::applyCalibration(pings, params);

        const float dark_gain = static_cast<float>(pings[0].samples[0].amplitude) / 1000.f;
        const float bright_gain = static_cast<float>(pings[1].samples[0].amplitude) / 4000.f;
        assert(near(dark_gain, bright_gain, 0.01f));
        assert(pings[1].samples[0].amplitude > pings[0].samples[0].amplitude * 3);
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

    // An identity AGC request must not claim baked provenance.
    {
        WaterfallParams params;
        params.agc.enabled = true;
        params.agc.strength = 0.f;
        params.agc.edge_skip_samples = 0;
        params.agc.noise_floor_pct = 0.f;
        std::vector<core::SidescanPing> pings{makePing(0.f, 10.f, 16, 1000)};
        dolphin::ui::imaging::applyCalibration(pings, params);
        assert(!core::hasCorrectionFlag(
            pings.front().correction_flags,
            core::CorrectionFlag::GainNormalized));
    }

    // AGC amplification is bounded and strength interpolates in the dB domain.
    // A 24 dB request at 50% strength must become 12 dB (about 3.98x), not
    // half of the linear multiplier.
    {
        WaterfallParams params;
        params.agc.enabled = true;
        params.agc.mode = dolphin::app::AgcMode::Global;
        params.agc.strength = 0.5f;
        params.agc.edge_skip_samples = 0;
        params.agc.noise_floor_pct = 0.f;
        params.agc.gain_cap_db = 24.f;
        std::vector<core::SidescanPing> pings{makePing(0.f, 10.f, 16, 100)};
        dolphin::ui::imaging::applyCalibration(pings, params);
        const float factor = pings[0].samples[0].amplitude / 100.f;
        assert(near(factor, std::pow(10.f, 12.f / 20.f), 0.02f));
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
        // A uniform across-track profile has no beam-pattern distortion. The
        // professional normalizer preserves its along-track brightness instead
        // of forcing every column toward an arbitrary half-scale target.
        assert(pings[1].samples[0].amplitude
               == unbaked_before[0].amplitude);
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
        params.arc.enabled = true;
        params.arc.exponent = 1.2f;
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
                ping.bottom_pick.range_m = 5.f;
                ping.bottom_pick.source = 1;
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

        // A map first-paint can seed the process-wide context from pings it has
        // already decoded. The deliberately nonexistent store proves this path
        // does not perform a second disk read.
        auto seed_index = std::make_shared<core::ArtifactIndex>();
        core::ArtifactIndexEntry seed_entry;
        seed_entry.type = core::ArtifactType::Sidescan;
        seed_entry.artifact_id = context_source.front().id;
        seed_index->entries.push_back(seed_entry);
        dolphin::ui::imaging::SssAmplitudeContextRequest seed_request;
        seed_request.store_path = "nonexistent-seed-only-store.dlpd";
        seed_request.store_format = "DLPD";
        seed_request.artifact_index = seed_index;
        seed_request.params = params;
        const auto seeded = dolphin::ui::imaging::getOrBuildSssAmplitudeContext(
            seed_request, {}, &context_source);
        assert(seeded && seeded->valid());

        // Live bottom-track edits are physical ARC inputs. They must produce a
        // distinct resident context even when store identity and UI params are
        // unchanged, otherwise waterfall/map consumers can reuse stale gains.
        auto changed_geometry = context_source;
        for (auto& ping : changed_geometry) {
            ping.bottom_pick.range_m = 10.f;
            // Reconstruct the changed per-ping calibration from raw amplitudes.
            const auto raw_it = std::find_if(raw.cbegin(), raw.cend(),
                [&](const auto& source) { return source.id == ping.id; });
            assert(raw_it != raw.cend());
            ping = *raw_it;
            ping.bottom_pick.range_m = 10.f;
            ping.bottom_pick.source = 1;
            dolphin::ui::imaging::applyPerPingCalibration(ping, params);
        }
        const auto reseeded = dolphin::ui::imaging::getOrBuildSssAmplitudeContext(
            seed_request, {}, &changed_geometry);
        assert(reseeded && reseeded->valid());
        assert(reseeded.get() != seeded.get());

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
            assert(near(static_cast<float>(row.port[0]), 20000.f));
            assert(near(static_cast<float>(row.port[1]), 20000.f));
            assert(near(static_cast<float>(row.port[2]), 20000.f));
            assert(near(static_cast<float>(row.stbd[0]), 20000.f));
            assert(near(static_cast<float>(row.stbd[1]), 20000.f));
            assert(near(static_cast<float>(row.stbd[2]), 20000.f));
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
            assert(near(static_cast<float>(row.port[0]), 10000.f));
            assert(row.port[1] == 0);
            assert(near(static_cast<float>(row.port[2]), 10000.f));
            assert(near(static_cast<float>(row.stbd[0]), 10000.f));
            assert(row.stbd[1] == 0);
            assert(near(static_cast<float>(row.stbd[2]), 10000.f));
        }
    }

    // Beam-pattern normalisation preserves the line's robust radiometric level,
    // limits correction symmetrically, and does not force columns to half-scale.
    {
        WaterfallParams params;
        params.beam_pattern.enabled = true;
        params.beam_pattern.strength = 1.f;
        params.beam_pattern.smooth_radius = 0;
        params.beam_pattern.gain_cap_db = 6.f;
        std::vector<PingRow> rows(5);
        for (auto& row : rows) {
            row.port = {1000, 10000, 1000};
            row.stbd = row.port;
        }
        wf::applyBeamPattern(rows, params);
        for (const auto& row : rows) {
            assert(row.port[0] == 1000);
            assert(near(static_cast<float>(row.port[1]),
                        10000.f / std::pow(10.f, 6.f / 20.f), 1.f));
            assert(row.port[2] == 1000);
        }
    }

    // Destripe uses a local robust reference: remove an isolated gain stripe
    // without flattening a legitimate gradual along-track brightness trend.
    {
        WaterfallParams params;
        params.destripe.enabled = true;
        params.destripe.window = 5;
        params.destripe.subdivision = 1;
        params.destripe.capping = 5.f;
        const std::array<uint16_t, 7> levels = {
            1000, 1100, 1200, 10000, 1400, 1500, 1600};
        std::vector<PingRow> rows(levels.size());
        for (size_t i = 0; i < levels.size(); ++i) {
            rows[i].port.assign(8, levels[i]);
            rows[i].stbd.assign(8, levels[i]);
        }
        wf::applyDestripe(rows, params);
        assert(rows[2].port[0] == 1200);
        assert(rows[3].port[0] < 10000);
        assert(rows[4].port[0] == 1400);
    }

    // CLAHE must never turn zero/no-data pixels into apparent sonar returns.
    {
        WaterfallParams params;
        params.ml_enhance.enabled = true;
        params.ml_enhance.tile_pings = 16;
        params.ml_enhance.tile_samps = 16;
        params.ml_enhance.clip_limit = 2.f;
        std::vector<PingRow> rows(16);
        for (auto& row : rows) {
            row.port.assign(16, 4000);
            row.stbd.assign(16, 4000);
            row.port[3] = 0;
            row.stbd[7] = 0;
        }
        wf::applyMlEnhance(rows, params);
        for (const auto& row : rows) {
            assert(row.port[3] == 0);
            assert(row.stbd[7] == 0);
            assert(row.port[0] == 4000);
            assert(row.stbd[0] == 4000);
        }
    }

    // Guard against accidental quadratic smoothing/tile regressions. This is a
    // deliberately generous Debug-build ceiling; it catches multi-second UI
    // stalls without depending on benchmark-grade timing stability.
    {
        WaterfallParams params;
        params.beam_pattern.enabled = true;
        params.beam_pattern.smooth_radius = 64;
        params.arn.enabled = true;
        params.arn.column_smooth = 64;
        params.destripe.enabled = true;
        params.destripe.window = 101;
        params.destripe.subdivision = 8;
        params.ml_enhance.enabled = true;
        params.ml_enhance.tile_pings = 32;
        params.ml_enhance.tile_samps = 64;
        std::vector<core::SidescanPing> line;
        line.reserve(512);
        for (int row = 0; row < 256; ++row) {
            for (int side = 0; side < 2; ++side) {
                auto ping = makePing(0.f, 100.f, 1024, 0);
                ping.channel = side == 0 ? core::SidescanChannel::Port
                                         : core::SidescanChannel::Starboard;
                ping.timestamp_us = row;
                for (size_t column = 0; column < ping.samples.size(); ++column)
                    ping.samples[column].amplitude = static_cast<uint16_t>(
                        1000 + ((row * 37 + column * 19 + side * 101) % 20000));
                line.push_back(std::move(ping));
            }
        }
        const auto begin = std::chrono::steady_clock::now();
        dolphin::ui::imaging::applyImagingChain(line, params);
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - begin);
        assert(elapsed < std::chrono::seconds(5));
    }

    // TVG must use authoritative per-sample ranges rather than assuming bins
    // are uniformly spaced across the ping metadata range.
    {
        WaterfallParams params;
        params.tvg.enabled = true;
        params.tvg.spreading = 20.0f;
        auto ping = makePing(1.0f, 10.0f, 3, 1000);
        ping.samples[0].range_m = 1.0f;
        ping.samples[1].range_m = 2.0f;
        ping.samples[2].range_m = 10.0f;
        std::vector<core::SidescanPing> pings{ping};
        wf::applyTvg(pings, params);
        assert(near(static_cast<float>(pings[0].samples[1].amplitude), 2000.0f));
    }

    // Tow depth is depth below the surface, not sensor altitude above seabed.
    // It cannot define ARC grazing angle without a bottom pick/nav altitude.
    {
        WaterfallParams params;
        params.arc.enabled = true;
        params.arc.exponent = 1.0f;
        params.arc.gain_cap_db = 40.0f;
        auto ping = makePing(0.0f, 10.0f, 2, 1000);
        ping.tow_depth_m = 5.0f;
        std::vector<core::SidescanPing> no_altitude{ping};
        wf::applyArc(no_altitude, params);
        assert(no_altitude[0].samples.back().amplitude == 1000);

        ping.nav.altitude_m = 5.0f;
        assert(dolphin::app::corrections::canApplyArc(ping));
        std::vector<core::SidescanPing> with_altitude{ping};
        wf::applyArc(with_altitude, params);
        assert(near(static_cast<float>(with_altitude[0].samples.back().amplitude), 2000.0f));

        ping.nav.altitude_m = 20.f;
        assert(!dolphin::app::corrections::canApplyArc(ping));

        // Vendor readers may provide authoritative per-sample ranges even when
        // the aggregate far-range field is absent. ARC must use that geometry.
        auto stored_range_ping = makePing(0.f, 0.f, 2, 1000);
        stored_range_ping.nav.altitude_m = 5.f;
        stored_range_ping.samples[0].range_m = 5.f;
        stored_range_ping.samples[1].range_m = 10.f;
        assert(dolphin::app::corrections::canApplyArc(stored_range_ping));
        std::vector<core::SidescanPing> stored_range{stored_range_ping};
        wf::applyArc(stored_range, params);
        assert(near(static_cast<float>(stored_range[0].samples.back().amplitude),
                    2000.f));
    }

    // ARC geometry is shared by the viewer and node graph. It rejects water
    // column/invalid geometry and enforces the gain cap in amplitude dB.
    {
        const auto open_gain = core::angleRangeGainFactor(10.0, 5.0, 1.0, 40.0);
        assert(open_gain && near(static_cast<float>(*open_gain), 2.0f));
        const auto capped_gain = core::angleRangeGainFactor(100.0, 1.0, 2.0, 6.0);
        assert(capped_gain
            && near(static_cast<float>(*capped_gain), std::pow(10.f, 6.f / 20.f)));
        assert(!core::angleRangeGainFactor(5.0, 5.0, 1.0, 12.0));
        assert(!core::angleRangeGainFactor(
            std::numeric_limits<double>::quiet_NaN(), 5.0, 1.0, 12.0));
    }

    // The canonical algorithm must honor its own enabled contract, independent
    // of whether a caller remembered to guard it.
    {
        WaterfallParams params;
        params.agc.enabled = false;
        auto ping = makePing(0.0f, 10.0f, 4, 1000);
        std::vector<core::SidescanPing> pings{ping};
        wf::normalizeRawAmplitudes(pings, params);
        for (const auto& sample : pings[0].samples)
            assert(sample.amplitude == 1000);
    }

    return 0;
}
