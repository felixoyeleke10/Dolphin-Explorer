#include "pipeline/nodes/analysis/ShadowDetectNode.h"
#include "core/SidescanPing.h"
#include <algorithm>

namespace dolphin::pipeline {

NodeSchema ShadowDetectNode::schema() const
{
    return NodeSchema{
        "shadow_detect", "Shadow Detection", "Analysis",
        {
            {"threshold",     {"Amplitude threshold (0-1)", 0.05f, 0.001f, 0.5f}},
            {"min_samples",   {"Min run length (samples)",  8,     1,      64}},
        }
    };
}

ArtifactBuffer ShadowDetectNode::process(const ArtifactBuffer& input,
                                          const NodeParams& params) const
{
    float threshold  = 0.05f;
    int   min_run    = 8;
    if (params.count("threshold"))   threshold = std::get<float>(params.at("threshold"));
    if (params.count("min_samples")) min_run   = std::get<int>(params.at("min_samples"));

    ArtifactBuffer output = input;
    for (auto& artifact : output) {
        auto* ping = std::get_if<core::SidescanPing>(&artifact);
        if (!ping || ping->samples.empty()) continue;

        const int    n         = static_cast<int>(ping->samples.size());
        const float  thresh_amp = threshold * 65535.f;

        // Mark shadow samples: amplitude set to 1 (distinguishable from true zero)
        int run = 0;
        for (int i = 0; i < n; ++i) {
            if (ping->samples[i].amplitude < thresh_amp) {
                ++run;
            } else {
                run = 0;
            }
            if (run >= min_run) {
                for (int j = i - run + 1; j <= i; ++j)
                    ping->samples[j].amplitude = 1;
            }
        }
    }
    return output;
}

} // namespace dolphin::pipeline
