#include "pipeline/nodes/enhancement/GainNormalizeNode.h"
#include "core/SidescanPing.h"
#include <algorithm>
#include <numeric>

namespace dolphin::pipeline {

NodeSchema GainNormalizeNode::schema() const
{
    return NodeSchema{
        "gain_normalize", "Gain Normalize", "Enhancement",
        {
            {"target_mean", {"Target mean (0-65535)", 16384.0f, 256.0f, 65535.0f}},
        }
    };
}

ArtifactBuffer GainNormalizeNode::process(const ArtifactBuffer& input,
                                           const NodeParams& params) const
{
    float target = 16384.0f;
    if (params.count("target_mean")) target = std::get<float>(params.at("target_mean"));
    target = std::max(1.0f, target);

    ArtifactBuffer output = input;
    for (auto& artifact : output) {
        auto* ping = std::get_if<core::SidescanPing>(&artifact);
        if (!ping || ping->samples.empty()) continue;

        double sum = 0.0;
        int    cnt = 0;
        for (const auto& s : ping->samples)
            if (s.amplitude > 0) { sum += s.amplitude; ++cnt; }
        if (cnt == 0) continue;

        const float scale = target / static_cast<float>(sum / cnt);
        for (auto& s : ping->samples)
            s.amplitude = static_cast<uint16_t>(
                std::clamp(s.amplitude * scale, 0.f, 65535.f));
    }
    return output;
}

} // namespace dolphin::pipeline
