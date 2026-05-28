#include "pipeline/nodes/filter/SpeckleFilterNode.h"
#include "core/SidescanPing.h"
#include <algorithm>
#include <vector>

namespace dolphin::pipeline {

NodeSchema SpeckleFilterNode::schema() const
{
    return NodeSchema{
        "speckle", "Speckle Filter", "Filter",
        {
            {"radius",     {"Radius (samples)", 2,  1, 20}},
            {"iterations", {"Iterations",       1,  1,  5}},
        }
    };
}

ArtifactBuffer SpeckleFilterNode::process(const ArtifactBuffer& input,
                                           const NodeParams& params) const
{
    int radius     = 2;
    int iterations = 1;
    if (params.count("radius"))     radius     = std::get<int>(params.at("radius"));
    if (params.count("iterations")) iterations = std::get<int>(params.at("iterations"));
    radius     = std::max(1, radius);
    iterations = std::max(1, std::min(5, iterations));

    ArtifactBuffer output = input;
    for (auto& artifact : output) {
        auto* ping = std::get_if<core::SidescanPing>(&artifact);
        if (!ping || ping->samples.empty()) continue;

        const int n = static_cast<int>(ping->samples.size());
        std::vector<float> buf(n);

        for (int iter = 0; iter < iterations; ++iter) {
            for (int i = 0; i < n; ++i) {
                int lo = std::max(0, i - radius);
                int hi = std::min(n - 1, i + radius);
                std::vector<float> window;
                window.reserve(hi - lo + 1);
                for (int j = lo; j <= hi; ++j)
                    window.push_back(static_cast<float>(ping->samples[j].amplitude));
                std::nth_element(window.begin(),
                                 window.begin() + window.size() / 2,
                                 window.end());
                buf[i] = window[window.size() / 2];
            }
            for (int i = 0; i < n; ++i)
                ping->samples[i].amplitude = static_cast<uint16_t>(
                    std::clamp(buf[i], 0.f, 65535.f));
        }
    }
    return output;
}

} // namespace dolphin::pipeline
