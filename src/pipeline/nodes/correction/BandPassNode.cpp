#include "pipeline/nodes/correction/BandPassNode.h"
#include "core/SidescanPing.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace dolphin::pipeline {

NodeSchema BandPassNode::schema() const
{
    return NodeSchema{
        "bpf", "Band-Pass Filter", "Filter",
        {
            {"low_hz",  {"Low cutoff (Hz)",  10000.0f, 100.0f,  200000.0f}},
            {"high_hz", {"High cutoff (Hz)", 30000.0f, 100.0f,  200000.0f}},
        }
    };
}

ArtifactBuffer BandPassNode::process(const ArtifactBuffer& input,
                                      const NodeParams& params) const
{
    float low_hz  = 10000.0f;
    float high_hz = 30000.0f;

    if (params.count("low_hz"))  low_hz  = std::get<float>(params.at("low_hz"));
    if (params.count("high_hz")) high_hz = std::get<float>(params.at("high_hz"));

    ArtifactBuffer output = input;

    for (auto& artifact : output) {
        auto* ping = std::get_if<core::SidescanPing>(&artifact);
        if (!ping || ping->sample_rate_hz <= 0.f || ping->samples.empty()) continue;

        // Band-pass = LP_at_high_hz  –  LP_at_low_hz
        //
        // A wider box-filter window produces a lower cutoff frequency.
        //   hp_window = sample_rate / (2 * high_hz)  →  LP at high_hz  (narrower window)
        //   lp_window = sample_rate / (2 * low_hz)   →  LP at low_hz   (wider window)
        //
        // Pass 1: apply hp_window to original samples → flt[] contains content below high_hz.
        // Pass 2: subtract lp_window mean of flt[]   → removes content below low_hz.
        // Result: content between low_hz and high_hz.
        int hp_window = static_cast<int>(ping->sample_rate_hz / (2.f * high_hz));
        int lp_window = static_cast<int>(ping->sample_rate_hz / (2.f * low_hz));
        hp_window = std::max(1, hp_window);
        lp_window = std::max(1, lp_window);

        size_t n = ping->samples.size();
        std::vector<float> flt(n);

        // Pass 1: LP filter at high_hz cutoff (hp_window)
        for (size_t i = 0; i < n; ++i) {
            int lo = std::max(0, static_cast<int>(i) - hp_window);
            int hi = std::min(static_cast<int>(n) - 1, static_cast<int>(i) + hp_window);
            float sum = 0.f;
            for (int j = lo; j <= hi; ++j)
                sum += ping->samples[j].amplitude;
            flt[i] = sum / (hi - lo + 1);
        }

        // Pass 2: subtract LP filter at low_hz cutoff (lp_window) to high-pass the result
        for (size_t i = 0; i < n; ++i) {
            int lo = std::max(0, static_cast<int>(i) - lp_window);
            int hi = std::min(static_cast<int>(n) - 1, static_cast<int>(i) + lp_window);
            float lf = 0.f;
            for (int j = lo; j <= hi; ++j)
                lf += flt[j];
            lf /= (hi - lo + 1);

            float result = flt[i] - lf + 32768.f;
            ping->samples[i].amplitude = static_cast<uint16_t>(
                std::clamp(result, 0.f, 65535.f));
        }
        ping->correction_flags |= core::CorrectionFlag::BandPass;
    }

    return output;
}

} // namespace dolphin::pipeline
