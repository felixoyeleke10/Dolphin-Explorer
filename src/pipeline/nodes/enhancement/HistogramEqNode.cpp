#include "pipeline/nodes/enhancement/HistogramEqNode.h"
#include "core/SidescanPing.h"
#include <algorithm>
#include <array>

namespace dolphin::pipeline {

NodeSchema HistogramEqNode::schema() const
{
    return NodeSchema{
        "histogram_eq", "Histogram Equalization", "Enhancement",
        {
            {"strength", {"Blend strength (0-1)", 1.0f, 0.0f, 1.0f}},
        }
    };
}

ArtifactBuffer HistogramEqNode::process(const ArtifactBuffer& input,
                                         const NodeParams& params) const
{
    float strength = 1.0f;
    if (params.count("strength")) strength = std::get<float>(params.at("strength"));
    strength = std::clamp(strength, 0.f, 1.f);

    ArtifactBuffer output = input;
    for (const auto channel : {core::SidescanChannel::Port,
                               core::SidescanChannel::Starboard}) {
        std::array<int, 256> hist{};
        int n = 0;
        for (const auto& artifact : output) {
            const auto* ping = std::get_if<core::SidescanPing>(&artifact);
            if (!ping || ping->channel != channel || core::hasCorrectionFlag(
                    ping->correction_flags, core::CorrectionFlag::HistogramEqualized)) continue;
            for (const auto& sample : ping->samples)
                if (sample.amplitude > 0) { ++hist[sample.amplitude >> 8]; ++n; }
        }
        if (n == 0) continue;

        // CDF
        std::array<int, 256> cdf{};
        cdf[0] = hist[0];
        for (int i = 1; i < 256; ++i) cdf[i] = cdf[i - 1] + hist[i];

        const int cdf_min = *std::find_if(cdf.begin(), cdf.end(), [](int v){ return v > 0; });
        if (n == cdf_min) continue;

        for (auto& artifact : output) {
            auto* ping = std::get_if<core::SidescanPing>(&artifact);
            if (!ping || ping->channel != channel || core::hasCorrectionFlag(
                    ping->correction_flags, core::CorrectionFlag::HistogramEqualized)) continue;
            for (auto& sample : ping->samples) {
                if (sample.amplitude == 0) continue;
                const int bin = sample.amplitude >> 8;
                const float equalized = static_cast<float>(cdf[bin] - cdf_min)
                    / static_cast<float>(n - cdf_min) * 65535.f;
                const float blended = strength * equalized
                    + (1.f - strength) * sample.amplitude;
                sample.amplitude = static_cast<uint16_t>(
                    std::clamp(blended, 0.f, 65535.f));
            }
            ping->correction_flags |= core::CorrectionFlag::HistogramEqualized;
        }
    }
    return output;
}

} // namespace dolphin::pipeline
