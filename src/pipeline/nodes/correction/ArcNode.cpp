#include "pipeline/nodes/correction/ArcNode.h"

#include "pipeline/SidescanRadiometryAlgorithms.h"

namespace dolphin::pipeline {

NodeSchema ArcNode::schema() const
{
    return NodeSchema{"arc", "Angle-Range Correction", "Correction", {
        {"exponent", {"Grazing-angle exponent", 1.5f, 0.5f, 4.f}},
        {"gain_cap_db", {"Maximum gain (dB)", 12.f, 0.f, 40.f}},
    }};
}

ArtifactBuffer ArcNode::process(const ArtifactBuffer& input,
                                const NodeParams& params) const
{
    ArtifactBuffer output = input;
    std::vector<size_t> indices;
    std::vector<core::SidescanPing> pings;
    for (size_t i = 0; i < output.size(); ++i)
        if (auto* ping = std::get_if<core::SidescanPing>(&output[i])) {
            indices.push_back(i); pings.push_back(*ping);
        }
    const auto value = [&](const char* key, float fallback) {
        const auto found = params.find(key);
        return found == params.end() ? fallback : std::get<float>(found->second);
    };
    radiometry::applyArc(pings, {true, value("exponent", 1.5f),
        value("gain_cap_db", 12.f)});
    for (size_t i = 0; i < indices.size(); ++i) output[indices[i]] = std::move(pings[i]);
    return output;
}

} // namespace dolphin::pipeline
