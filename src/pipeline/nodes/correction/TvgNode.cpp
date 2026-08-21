#include "pipeline/nodes/correction/TvgNode.h"

#include "pipeline/SidescanRadiometryAlgorithms.h"

namespace dolphin::pipeline {

NodeSchema TvgNode::schema() const
{
    return NodeSchema{"tvg", "TVG", "Correction", {
        {"spreading", {"Spreading (dB/decade)", 20.f, 0.f, 40.f}},
        {"absorption", {"Absorption (dB/m)", 0.f, 0.f, 2.f}},
        {"blanking_m", {"Fallback blanking (m)", 1.f, 0.f, 50.f}},
    }};
}

ArtifactBuffer TvgNode::process(const ArtifactBuffer& input,
                                const NodeParams& params) const
{
    ArtifactBuffer output = input;
    std::vector<size_t> indices;
    std::vector<core::SidescanPing> pings;
    for (size_t i = 0; i < output.size(); ++i)
        if (auto* ping = std::get_if<core::SidescanPing>(&output[i])) {
            indices.push_back(i); pings.push_back(std::move(*ping));
        }
    const auto value = [&](const char* key, float fallback) {
        const auto found = params.find(key);
        return found == params.end() ? fallback : std::get<float>(found->second);
    };
    radiometry::applyTvg(pings, {true, value("spreading", 20.f),
        value("absorption", 0.f), value("blanking_m", 1.f)});
    for (size_t i = 0; i < indices.size(); ++i) output[indices[i]] = std::move(pings[i]);
    return output;
}

} // namespace dolphin::pipeline
