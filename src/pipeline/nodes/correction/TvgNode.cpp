#include "pipeline/nodes/correction/TvgNode.h"
#include "core/SidescanPing.h"
#include <cmath>
#include <algorithm>

namespace dolphin::pipeline {

NodeSchema TvgNode::schema() const
{
    return NodeSchema{
        "tvg", "TVG", "Correction",
        {
            {"spreading",   {"Spreading (dB/decade)", 20.0f, 0.0f, 40.0f}},
            {"absorption",  {"Absorption (dB/m)",      0.0f, 0.0f,  2.0f}},
            {"blanking_m",  {"Blanking (m)",            1.0f, 0.0f, 50.0f}},
        }
    };
}

ArtifactBuffer TvgNode::process(const ArtifactBuffer& input,
                                  const NodeParams& params) const
{
    float spreading  = 20.0f;
    float absorption =  0.0f;
    float blanking_m =  1.0f;  // reference range; TVG = 0 dB at this distance

    if (params.count("spreading"))  spreading  = std::get<float>(params.at("spreading"));
    if (params.count("absorption")) absorption = std::get<float>(params.at("absorption"));
    if (params.count("blanking_m")) blanking_m = std::get<float>(params.at("blanking_m"));

    const float kRef = std::max(blanking_m, 1.0f);

    ArtifactBuffer output = input;

    for (auto& artifact : output) {
        auto* ping = std::get_if<core::SidescanPing>(&artifact);
        if (!ping) continue;

        for (auto& sample : ping->samples) {
            const float r = sample.range_m > 0.f ? sample.range_m : 0.f;
            if (r < kRef) continue;

            const float gain_db = spreading  * std::log10(r / kRef)
                                + absorption * (r - kRef);
            const float factor  = std::pow(10.f, gain_db / 20.f);
            const float amp     = sample.amplitude * factor;
            sample.amplitude    = static_cast<uint16_t>(
                std::clamp(amp, 0.f, static_cast<float>(UINT16_MAX)));
        }
        ping->correction_flags |= core::CorrectionFlag::Tvg;
    }

    return output;
}

} // namespace dolphin::pipeline
