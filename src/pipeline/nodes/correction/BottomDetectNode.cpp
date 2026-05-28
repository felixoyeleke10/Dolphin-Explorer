#include "pipeline/nodes/correction/BottomDetectNode.h"
#include <algorithm>

namespace dolphin::pipeline {

NodeSchema BottomDetectNode::schema() const
{
    return NodeSchema{
        "bottom_detect", "Bottom Detection", "Analysis",
        {
            {"threshold",    {"Threshold (0-1)", 0.65f, 0.0f, 1.0f}},
            {"search_start", {"Search start (%)", 5.0f,  0.0f, 50.0f}},
            {"search_end",   {"Search end (%)",   80.0f, 10.0f, 100.0f}},
        }
    };
}

int BottomDetectNode::findBottom(const core::SidescanPing& ping, float threshold,
                                  int search_start, int search_end) const
{
    if (ping.samples.empty()) return -1;

    uint16_t max_amp = 0;
    for (int i = search_start; i < search_end; ++i)
        max_amp = std::max(max_amp, ping.samples[i].amplitude);

    if (max_amp == 0) return -1;

    uint16_t thresh_amp = static_cast<uint16_t>(threshold * max_amp);
    for (int i = search_start; i < search_end; ++i)
        if (ping.samples[i].amplitude >= thresh_amp) return i;

    return -1;
}

ArtifactBuffer BottomDetectNode::process(const ArtifactBuffer& input,
                                          const NodeParams& params) const
{
    float threshold    = 0.65f;
    float search_start = 5.0f;
    float search_end   = 80.0f;

    if (params.count("threshold"))    threshold    = std::get<float>(params.at("threshold"));
    if (params.count("search_start")) search_start = std::get<float>(params.at("search_start"));
    if (params.count("search_end"))   search_end   = std::get<float>(params.at("search_end"));

    ArtifactBuffer output = input;

    for (auto& artifact : output) {
        auto* ping = std::get_if<core::SidescanPing>(&artifact);
        if (!ping || ping->samples.empty()) continue;

        int n  = static_cast<int>(ping->samples.size());
        int s0 = std::clamp(static_cast<int>(search_start / 100.f * n), 0, n - 1);
        int s1 = std::clamp(static_cast<int>(search_end   / 100.f * n), s0 + 1, n);

        int bottom_idx = findBottom(*ping, threshold, s0, s1);
        if (bottom_idx >= 0) {
            ping->bottom_pick.range_m    = ping->samples[bottom_idx].range_m;
            ping->bottom_pick.confidence = threshold; // threshold used as proxy score
            ping->bottom_pick.source     = 1;         // auto-detected
            for (int i = 0; i < bottom_idx; ++i)
                ping->samples[i].range_m = -1.f;     // water column marker
        } else {
            ping->bottom_pick.range_m    = -1.f;
            ping->bottom_pick.confidence =  0.f;
            ping->bottom_pick.source     =  0;
            ping->qc_flags |= core::QcFlag::BottomLost;
        }
    }

    return output;
}

} // namespace dolphin::pipeline
