#include "ui/shared/processing/SssImagingAlgorithms.h"

#include "app/corrections/CorrectionAlgorithms.h"
#include "pipeline/SidescanEnhancementAlgorithms.h"

#include <algorithm>
#include <cstdint>
#include <future>

namespace dolphin::ui::imaging {
namespace {

void applyAgc(std::vector<core::SidescanPing>& pings,
              const WaterfallParams& params)
{
    if (!params.agc.enabled) return;

    const auto is_baked = [](const core::SidescanPing& ping) {
        return core::hasCorrectionFlag(
            ping.correction_flags, core::CorrectionFlag::GainNormalized);
    };
    const size_t baked_count = static_cast<size_t>(std::count_if(
        pings.cbegin(), pings.cend(), is_baked));
    if (baked_count == pings.size()) return;
    if (baked_count == 0) {
        app::corrections::normalizeAmplitudes(pings, params.agc);
        return;
    }

    std::vector<size_t> indices;
    std::vector<core::SidescanPing> work;
    indices.reserve(pings.size() - baked_count);
    work.reserve(pings.size() - baked_count);
    for (size_t i = 0; i < pings.size(); ++i) {
        if (is_baked(pings[i])) continue;
        indices.push_back(i);
        work.push_back(pings[i]);
    }
    if (work.empty()) return;

    app::corrections::normalizeAmplitudes(work, params.agc);
    for (size_t i = 0; i < work.size(); ++i) {
        pings[indices[i]].samples = std::move(work[i].samples);
        pings[indices[i]].correction_flags = work[i].correction_flags;
    }
}

void runChannel(std::vector<core::SidescanPing*>& channel,
                const WaterfallParams& params)
{
    if (channel.empty()) return;
    std::sort(channel.begin(), channel.end(),
              [](const core::SidescanPing* a, const core::SidescanPing* b) {
                  if (a->timestamp_us != b->timestamp_us)
                      return a->timestamp_us < b->timestamp_us;
                  if (a->ping_number != b->ping_number)
                      return a->ping_number < b->ping_number;
                  return a->id < b->id;
              });

    std::vector<std::vector<uint16_t>> amplitudes(channel.size());
    std::vector<std::vector<uint16_t>*> rows(channel.size());
    for (size_t i = 0; i < channel.size(); ++i) {
        const auto& samples = channel[i]->samples;
        amplitudes[i].resize(samples.size());
        for (size_t k = 0; k < samples.size(); ++k)
            amplitudes[i][k] = samples[k].amplitude;
        rows[i] = &amplitudes[i];
    }

    // Mixed legacy stores can contain baked and raw rows. Each operator learns
    // only from raw rows, and provenance is recorded only where output changed.
    const auto applyUnlessBaked = [&](core::CorrectionFlag flag,
                                      const auto& operation) {
        std::vector<std::vector<uint16_t>*> unbaked_rows;
        std::vector<core::SidescanPing*> unbaked_pings;
        std::vector<std::vector<uint16_t>> before;
        unbaked_rows.reserve(channel.size());
        unbaked_pings.reserve(channel.size());
        before.reserve(channel.size());
        for (size_t i = 0; i < channel.size(); ++i) {
            if (core::hasCorrectionFlag(channel[i]->correction_flags, flag)) continue;
            unbaked_rows.push_back(rows[i]);
            unbaked_pings.push_back(channel[i]);
            before.push_back(*rows[i]);
        }
        if (unbaked_rows.empty() || !operation(unbaked_rows)) return;
        for (size_t i = 0; i < unbaked_rows.size(); ++i)
            if (*unbaked_rows[i] != before[i])
                unbaked_pings[i]->correction_flags |= flag;
    };

    if (params.beam_pattern.enabled)
        applyUnlessBaked(core::CorrectionFlag::BeamPattern,
            [&](auto& active) { return beamPatternChannel(active, params.beam_pattern); });
    if (params.arn.enabled)
        applyUnlessBaked(core::CorrectionFlag::Arn,
            [&](auto& active) { return arnChannel(active, params.arn); });
    if (params.destripe.enabled)
        applyUnlessBaked(core::CorrectionFlag::Destriping,
            [&](auto& active) { return destripeChannel(active, params.destripe); });
    if (params.ml_enhance.enabled)
        applyUnlessBaked(core::CorrectionFlag::AdaptiveContrast,
            [&](auto& active) { return mlEnhanceChannel(active, params.ml_enhance); });

    for (size_t i = 0; i < channel.size(); ++i) {
        auto& samples = channel[i]->samples;
        for (size_t k = 0; k < samples.size(); ++k)
            samples[k].amplitude = amplitudes[i][k];
    }
}

} // namespace

bool beamPatternChannel(std::vector<std::vector<uint16_t>*>& rows,
                        const BeamPatternParams& settings)
{
    return pipeline::enhancement::applyBeamPattern(rows, settings);
}

bool arnChannel(std::vector<std::vector<uint16_t>*>& rows,
                const ArnParams& settings)
{
    return pipeline::enhancement::applyArn(rows, settings);
}

bool destripeChannel(std::vector<std::vector<uint16_t>*>& rows,
                     const DestripeParams& settings)
{
    return pipeline::enhancement::applyDestripe(rows, settings);
}

bool mlEnhanceChannel(std::vector<std::vector<uint16_t>*>& rows,
                      const MlEnhanceParams& settings)
{
    return pipeline::enhancement::applyAdaptiveContrast(rows, settings);
}

void applyImagingChain(std::vector<core::SidescanPing>& pings,
                       const WaterfallParams& params)
{
    const bool any = params.beam_pattern.enabled || params.arn.enabled
                  || params.destripe.enabled || params.ml_enhance.enabled;
    if (!any || pings.empty()) return;

    std::vector<core::SidescanPing*> ports;
    std::vector<core::SidescanPing*> starboards;
    ports.reserve(pings.size());
    starboards.reserve(pings.size());
    for (auto& ping : pings) {
        if (ping.channel == core::SidescanChannel::Port) ports.push_back(&ping);
        else starboards.push_back(&ping);
    }
    auto port_future = std::async(std::launch::async,
        [&] { runChannel(ports, params); });
    runChannel(starboards, params);
    port_future.get();
}

void applyPerPingCalibration(core::SidescanPing& ping,
                             const WaterfallParams& params)
{
    std::vector<core::SidescanPing> one;
    one.reserve(1);
    one.push_back(std::move(ping));
    auto& item = one.front();
    if (params.tvg.enabled
            && !core::hasCorrectionFlag(item.correction_flags, core::CorrectionFlag::Tvg))
        app::corrections::applyTvg(one, params.tvg);
    if (params.arc.enabled
            && !core::hasCorrectionFlag(item.correction_flags, core::CorrectionFlag::Arc))
        app::corrections::applyArc(one, params.arc);
    ping = std::move(one.front());
}

void applyCalibration(std::vector<core::SidescanPing>& pings,
                      const WaterfallParams& params)
{
    for (auto& ping : pings) applyPerPingCalibration(ping, params);
    applyAgc(pings, params);
}

void applyContextCalibrationAndImaging(std::vector<core::SidescanPing>& pings,
                                       const WaterfallParams& params)
{
    applyAgc(pings, params);
    applyImagingChain(pings, params);
}

void applyDisplayPipeline(std::vector<core::SidescanPing>& pings,
                          const WaterfallParams& params)
{
    applyCalibration(pings, params);
    applyImagingChain(pings, params);
}

SssAutoStretch computeAutoStretch(const std::vector<core::SidescanPing>& pings)
{
    constexpr int bins = 1024;
    uint64_t histogram[bins] = {};
    for (const auto& ping : pings)
        for (const auto& sample : ping.samples)
            if (sample.amplitude > 0) ++histogram[sample.amplitude >> 6];
    uint64_t total = 0;
    for (uint64_t count : histogram) total += count;
    if (total == 0) return {};

    const uint64_t tail = std::max<uint64_t>(1, total / 100u);
    const uint64_t high_target = total - tail;
    uint64_t cumulative = 0;
    int low = 0;
    int high = bins - 1;
    bool found_low = false;
    for (int i = 0; i < bins; ++i) {
        cumulative += histogram[i];
        if (!found_low && cumulative >= tail) { low = i; found_low = true; }
        if (cumulative >= high_target) { high = i; break; }
    }
    const uint32_t low_value = static_cast<uint32_t>(low) << 6;
    const uint32_t high_value = std::min<uint32_t>(
        (static_cast<uint32_t>(high) + 1u) << 6, 65535u);
    return {static_cast<float>(low_value) / 65535.f,
            static_cast<float>(std::max(high_value, low_value + 1u)) / 65535.f};
}

} // namespace dolphin::ui::imaging
