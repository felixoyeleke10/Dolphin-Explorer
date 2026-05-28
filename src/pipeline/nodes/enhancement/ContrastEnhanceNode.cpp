#include "pipeline/nodes/enhancement/ContrastEnhanceNode.h"
#include "core/SidescanPing.h"
#include <algorithm>
#include <vector>

namespace dolphin::pipeline {

NodeSchema ContrastEnhanceNode::schema() const
{
    return NodeSchema{
        "contrast_enhance", "Contrast Enhance", "Enhancement",
        {
            {"low_pct",  {"Low clip percentile",  2.0f,  0.0f, 49.0f}},
            {"high_pct", {"High clip percentile", 98.0f, 51.0f, 100.0f}},
        }
    };
}

ArtifactBuffer ContrastEnhanceNode::process(const ArtifactBuffer& input,
                                             const NodeParams& params) const
{
    float low_pct  =  2.0f;
    float high_pct = 98.0f;
    if (params.count("low_pct"))  low_pct  = std::get<float>(params.at("low_pct"));
    if (params.count("high_pct")) high_pct = std::get<float>(params.at("high_pct"));

    ArtifactBuffer output = input;
    for (auto& artifact : output) {
        auto* ping = std::get_if<core::SidescanPing>(&artifact);
        if (!ping || ping->samples.empty()) continue;

        std::vector<uint16_t> sorted;
        sorted.reserve(ping->samples.size());
        for (const auto& s : ping->samples) sorted.push_back(s.amplitude);
        std::sort(sorted.begin(), sorted.end());

        const int   n     = static_cast<int>(sorted.size());
        const float lo    = sorted[std::clamp(static_cast<int>(low_pct  / 100.f * n), 0, n - 1)];
        const float hi    = sorted[std::clamp(static_cast<int>(high_pct / 100.f * n), 0, n - 1)];
        const float range = hi - lo;
        if (range <= 0.f) continue;

        for (auto& s : ping->samples)
            s.amplitude = static_cast<uint16_t>(
                std::clamp((s.amplitude - lo) / range * 65535.f, 0.f, 65535.f));
    }
    return output;
}

} // namespace dolphin::pipeline
