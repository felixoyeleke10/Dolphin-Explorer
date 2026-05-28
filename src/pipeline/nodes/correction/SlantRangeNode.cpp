#include "pipeline/nodes/correction/SlantRangeNode.h"
#include "core/SidescanPing.h"
#include <cmath>

namespace dolphin::pipeline {

NodeSchema SlantRangeNode::schema() const
{
    return NodeSchema{
        "slant_range", "Slant-Range Correction", "Correction",
        {
            {"sound_velocity", {"Sound velocity (m/s)", 1500.0f, 1400.0f, 1600.0f}},
        }
    };
}

ArtifactBuffer SlantRangeNode::process(const ArtifactBuffer& input,
                                        const NodeParams& params) const
{
    float sv = 1500.0f;
    if (params.count("sound_velocity"))
        sv = std::get<float>(params.at("sound_velocity"));

    ArtifactBuffer output = input;

    for (auto& artifact : output) {
        auto* ping = std::get_if<core::SidescanPing>(&artifact);
        if (!ping) continue;

        float alt = ping->nav.altitude_m;
        if (alt <= 0.f) alt = 1.f;

        for (auto& sample : ping->samples) {
            if (sample.range_m < 0.f) continue;  // water column marker

            float slant_r  = sample.range_m;
            float ground_r2 = slant_r * slant_r - alt * alt;
            sample.range_m = (ground_r2 > 0.f) ? std::sqrt(ground_r2) : 0.f;
        }
        ping->correction_flags |= core::CorrectionFlag::SlantRange;
    }

    return output;
}

} // namespace dolphin::pipeline
