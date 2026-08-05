#include "pipeline/nodes/correction/BottomDetectNode.h"
#include <algorithm>
#include <cmath>

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

    // Require the crossing to persist for a short run of samples rather than
    // accepting the first one: an isolated noise spike, multipath return, or
    // biological scatter ahead of the true bottom would otherwise be picked.
    // A few samples of range bias for genuinely sharp returns is the cost of
    // rejecting single/double-sample glitches.
    constexpr int kPersistSamples = 3;
    int run = 0;
    for (int i = search_start; i < search_end; ++i) {
        if (ping.samples[i].amplitude >= thresh_amp) {
            if (++run >= kPersistSamples) return i - kPersistSamples + 1;
        } else {
            run = 0;
        }
    }

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
        detectBottom(*ping, threshold, search_start, search_end);
    }

    return output;
}

bool BottomDetectNode::detectBottom(core::SidescanPing& ping, float threshold,
                                    float search_start_pct, float search_end_pct)
{
    if (ping.bottom_pick.source == 2 && ping.bottom_pick.valid()) return true;
    const int count = static_cast<int>(ping.samples.size());
    if (count == 0) return false;
    threshold = std::clamp(threshold, 0.0f, 1.0f);
    search_start_pct = std::clamp(search_start_pct, 0.0f, 99.0f);
    search_end_pct = std::clamp(search_end_pct, search_start_pct + 0.01f, 100.0f);
    const int begin = std::clamp(
        static_cast<int>(search_start_pct * 0.01f * count), 0, count - 1);
    const int end = std::clamp(
        static_cast<int>(search_end_pct * 0.01f * count), begin + 1, count);

    BottomDetectNode detector;
    const int index = detector.findBottom(ping, threshold, begin, end);
    float range_m = index >= 0 ? ping.samples[static_cast<size_t>(index)].range_m : -1.0f;
    if (!(std::isfinite(range_m) && range_m > 0.0f)
        && index >= 0 && count > 1 && std::isfinite(ping.slant_range_m)
        && ping.slant_range_m > ping.blanking_m) {
        const float blanking_m = std::max(0.0f, ping.blanking_m);
        range_m = blanking_m + (ping.slant_range_m - blanking_m)
            * static_cast<float>(index) / static_cast<float>(count - 1);
    }
    if (index < 0 || !std::isfinite(range_m) || range_m <= 0.0f) {
        ping.bottom_pick = {};
        ping.bottom_pick.range_m = -1.0f;
        ping.qc_flags |= core::QcFlag::BottomLost;
        return false;
    }

    uint16_t peak = 0;
    for (int i = begin; i < end; ++i)
        peak = std::max(peak, ping.samples[static_cast<size_t>(i)].amplitude);
    ping.bottom_pick.range_m = range_m;
    // Confidence reflects how distinctly the return stands out from the
    // local background (the water-column noise floor ahead of the pick),
    // not merely how close it is to the search window's own peak — that
    // ratio is guaranteed >= threshold by construction and can't tell a
    // strong return from a barely-qualifying one.
    double background_sum = 0.0;
    int background_n = 0;
    for (int i = begin; i < index; ++i) {
        background_sum += ping.samples[static_cast<size_t>(i)].amplitude;
        ++background_n;
    }
    const double background = background_n > 0 ? background_sum / background_n : 0.0;
    const double contrast = peak > 0 ? (static_cast<double>(peak) - background) / peak : 0.0;
    ping.bottom_pick.confidence = static_cast<float>(std::clamp(contrast, 0.0, 1.0));
    ping.bottom_pick.source = 1;
    for (int i = 0; i < index; ++i)
        ping.samples[static_cast<size_t>(i)].range_m = -1.0f;
    return true;
}

} // namespace dolphin::pipeline
