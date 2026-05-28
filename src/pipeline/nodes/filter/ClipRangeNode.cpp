#include "pipeline/nodes/filter/ClipRangeNode.h"
#include "core/SidescanPing.h"

namespace dolphin::pipeline {

NodeSchema ClipRangeNode::schema() const
{
    return NodeSchema{
        "clip_range", "Clip Range", "Correction",
        {
            {"near_m", {"Near range (m)",  2.0f,    0.0f, 500.0f}},
            {"far_m",  {"Far range (m)",  75.0f,    1.0f, 500.0f}},
        }
    };
}

ArtifactBuffer ClipRangeNode::process(const ArtifactBuffer& input,
                                       const NodeParams& params) const
{
    float near_m =  2.0f;
    float far_m  = 75.0f;
    if (params.count("near_m")) near_m = std::get<float>(params.at("near_m"));
    if (params.count("far_m"))  far_m  = std::get<float>(params.at("far_m"));

    ArtifactBuffer output = input;
    for (auto& artifact : output) {
        auto* ping = std::get_if<core::SidescanPing>(&artifact);
        if (!ping) continue;
        for (auto& s : ping->samples) {
            if (s.range_m < 0.f) continue;  // water column marker
            if (s.range_m < near_m || s.range_m > far_m)
                s.amplitude = 0;
        }
    }
    return output;
}

} // namespace dolphin::pipeline
