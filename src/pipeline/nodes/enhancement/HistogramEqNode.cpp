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
    for (auto& artifact : output) {
        auto* ping = std::get_if<core::SidescanPing>(&artifact);
        if (!ping || ping->samples.empty()) continue;

        // Build histogram over 256 bins (8-bit buckets of 16-bit values)
        std::array<int, 256> hist{};
        for (const auto& s : ping->samples)
            hist[s.amplitude >> 8]++;

        // CDF
        std::array<int, 256> cdf{};
        cdf[0] = hist[0];
        for (int i = 1; i < 256; ++i) cdf[i] = cdf[i - 1] + hist[i];

        const int n       = static_cast<int>(ping->samples.size());
        const int cdf_min = *std::find_if(cdf.begin(), cdf.end(), [](int v){ return v > 0; });
        if (n == cdf_min) continue;

        for (auto& s : ping->samples) {
            int   bin    = s.amplitude >> 8;
            float eq_val = static_cast<float>(cdf[bin] - cdf_min) /
                           static_cast<float>(n - cdf_min) * 65535.f;
            float blended = strength * eq_val + (1.f - strength) * s.amplitude;
            s.amplitude = static_cast<uint16_t>(std::clamp(blended, 0.f, 65535.f));
        }
    }
    return output;
}

} // namespace dolphin::pipeline
