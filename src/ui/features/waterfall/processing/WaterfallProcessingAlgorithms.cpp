// WaterfallProcessingAlgorithms.cpp — amplitude processing algorithm implementations.
//
// Pre-assembly algorithms (TVG, ARC, AGC normalise) delegate to the canonical
// implementations in app/corrections/CorrectionAlgorithms.cpp.  Post-assembly
// algorithms (beam pattern, ARN, destripe, ML) delegate to the shared per-channel
// cores in ui/shared/processing/SssImagingAlgorithms — the SAME code the SSS map
// mosaic build uses, so the waterfall and the map apply identical corrections.
#include "ui/features/waterfall/processing/WaterfallProcessingAlgorithms.h"
#include "ui/shared/processing/SssImagingAlgorithms.h"
#include "app/corrections/CorrectionAlgorithms.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace dolphin::ui {
namespace detail {

// -- Pre-assembly --------------------------------------------------------------

void normalizeRawAmplitudes(std::vector<core::SidescanPing>& pings,
                            const WaterfallParams& params)
{
    app::corrections::normalizeAmplitudes(pings, params.agc);
}

void stretchRawAmplitudes(std::vector<core::SidescanPing>& pings)
{
    constexpr uint32_t kBins  = 1024;
    constexpr float    kScale = float(kBins) / 65536.f;

    uint32_t hist_p[kBins] = {}, hist_s[kBins] = {};
    uint32_t total_p = 0, total_s = 0;

    for (const auto& ping : pings) {
        const bool is_port = (ping.channel == core::SidescanChannel::Port);
        auto& hist  = is_port ? hist_p  : hist_s;
        auto& total = is_port ? total_p : total_s;
        for (const auto& s : ping.samples)
            if (s.amplitude > 0) {
                hist[static_cast<uint32_t>(s.amplitude * kScale)]++;
                ++total;
            }
    }

    auto findStretch = [&](const uint32_t* hist, uint32_t total,
                           uint32_t& raw_lo, uint32_t& raw_hi) -> bool {
        if (total == 0) return false;
        const uint32_t lo_tgt = std::max(uint32_t(1), total / 100u);
        const uint32_t hi_tgt = total - lo_tgt;
        uint32_t p01_bin = 0, p99_bin = kBins - 1;
        uint32_t cum = 0; bool found_lo = false;
        for (uint32_t i = 0; i < kBins; ++i) {
            cum += hist[i];
            if (!found_lo && cum >= lo_tgt) { p01_bin = i; found_lo = true; }
            if (cum >= hi_tgt)              { p99_bin = i; break; }
        }
        raw_lo = p01_bin * 65536u / kBins;
        raw_hi = std::min((p99_bin + 1) * 65536u / kBins, uint32_t(65535));
        return raw_hi > raw_lo;
    };

    uint32_t p_lo = 0, p_hi = 65535, s_lo = 0, s_hi = 65535;
    const bool ok_p = findStretch(hist_p, total_p, p_lo, p_hi);
    const bool ok_s = findStretch(hist_s, total_s, s_lo, s_hi);
    if (!ok_p && !ok_s) return;

    for (auto& ping : pings) {
        const bool is_port = (ping.channel == core::SidescanChannel::Port);
        if (is_port && !ok_p) continue;
        if (!is_port && !ok_s) continue;
        const float raw_lo   = static_cast<float>(is_port ? p_lo : s_lo);
        const float inv_span = is_port ? (65535.f / float(p_hi - p_lo))
                                       : (65535.f / float(s_hi - s_lo));
        for (auto& s : ping.samples) {
            const int v = static_cast<int>(
                (static_cast<float>(s.amplitude) - raw_lo) * inv_span + 0.5f);
            s.amplitude = static_cast<uint16_t>(std::clamp(v, 0, 65535));
        }
    }
}

void applyTvg(std::vector<core::SidescanPing>& pings, const WaterfallParams& params)
{
    app::corrections::applyTvg(pings, params.tvg);
}

void applyArc(std::vector<core::SidescanPing>& pings, const WaterfallParams& params)
{
    app::corrections::applyArc(pings, params.arc);
}

// -- Post-assembly -------------------------------------------------------------
// Delegate to the shared per-channel cores (ui/shared/processing/
// SssImagingAlgorithms).  PingRow stores port/stbd as separate amplitude rows;
// hand the core a pointer list per channel so it corrects them in place.

namespace {
void channelPtrs(std::vector<PingRow>& rows,
                 std::vector<std::vector<uint16_t>*>& port,
                 std::vector<std::vector<uint16_t>*>& stbd)
{
    port.clear(); stbd.clear();
    port.reserve(rows.size()); stbd.reserve(rows.size());
    for (auto& r : rows) { port.push_back(&r.port); stbd.push_back(&r.stbd); }
}
} // namespace

void applyBeamPattern(std::vector<PingRow>& rows, const WaterfallParams& params)
{
    std::vector<std::vector<uint16_t>*> port, stbd; channelPtrs(rows, port, stbd);
    imaging::beamPatternChannel(port, params.beam_pattern);
    imaging::beamPatternChannel(stbd, params.beam_pattern);
}

void applyArn(std::vector<PingRow>& rows, const WaterfallParams& params)
{
    std::vector<std::vector<uint16_t>*> port, stbd; channelPtrs(rows, port, stbd);
    imaging::arnChannel(port, params.arn);
    imaging::arnChannel(stbd, params.arn);
}

void applyDestripe(std::vector<PingRow>& rows, const WaterfallParams& params)
{
    std::vector<std::vector<uint16_t>*> port, stbd; channelPtrs(rows, port, stbd);
    imaging::destripeChannel(port, params.destripe);
    imaging::destripeChannel(stbd, params.destripe);
}

void applyMlEnhance(std::vector<PingRow>& rows, const WaterfallParams& params)
{
    std::vector<std::vector<uint16_t>*> port, stbd; channelPtrs(rows, port, stbd);
    imaging::mlEnhanceChannel(port, params.ml_enhance);
    imaging::mlEnhanceChannel(stbd, params.ml_enhance);
}

} // namespace detail
} // namespace dolphin::ui
