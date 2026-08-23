#include "pipeline/nodes/correction/SlantRangeNode.h"
#include "pipeline/nodes/correction/BottomDetectNode.h"
#include "core/SidescanGeometry.h"
#include "core/SidescanPing.h"

#include <algorithm>

namespace dolphin::pipeline {

NodeSchema SlantRangeNode::schema() const
{
    return NodeSchema{
        "slant_range", "Slant-Range Correction", "Correction",
        {
            {"auto_detect_bottom", {"Estimate missing altitude", true, false, true}},
            {"detection_threshold", {"Detection threshold (0-1)", 0.65f, 0.0f, 1.0f}},
            {"search_start", {"Search start (%)", 5.0f, 0.0f, 50.0f}},
            {"search_end", {"Search end (%)", 80.0f, 10.0f, 100.0f}},
        }
    };
}

ArtifactBuffer SlantRangeNode::process(const ArtifactBuffer& input,
                                        const NodeParams& params) const
{
    const auto boolParam = [&](const char* key, bool fallback) {
        const auto it = params.find(key);
        return it == params.end() ? fallback : std::get<bool>(it->second);
    };
    const auto floatParam = [&](const char* key, float fallback) {
        const auto it = params.find(key);
        return it == params.end() ? fallback : std::get<float>(it->second);
    };
    const bool auto_detect = boolParam("auto_detect_bottom", true);
    const float threshold = floatParam("detection_threshold", 0.65f);
    const float search_start = floatParam("search_start", 5.0f);
    const float search_end = floatParam("search_end", 80.0f);

    ArtifactBuffer output = input;

    for (auto& artifact : output) {
        auto* ping = std::get_if<core::SidescanPing>(&artifact);
        if (!ping) continue;
        if (core::hasCorrectionFlag(ping->correction_flags,
                                    core::CorrectionFlag::SlantRange))
            continue;
        auto altitude_m = core::sidescanCorrectionAltitudeMetres(*ping);
        if (!altitude_m && auto_detect) {
            auto candidate = *ping;
            if (BottomDetectNode::detectBottom(
                    candidate, threshold, search_start, search_end)) {
                altitude_m = core::sidescanCorrectionAltitudeMetres(candidate);
                if (altitude_m) {
                    *ping = std::move(candidate);
                } else {
                    // Retain the weak pick for QC without destructively masking
                    // samples when it is not trusted enough to drive geometry.
                    ping->bottom_pick = candidate.bottom_pick;
                    ping->qc_flags = candidate.qc_flags;
                }
            }
        }
        if (!altitude_m) continue;

        const bool has_seabed_sample = std::any_of(
            ping->samples.begin(), ping->samples.end(), [&](const auto& sample) {
                return std::isfinite(sample.range_m)
                    && static_cast<double>(sample.range_m) > *altitude_m;
            });
        if (!has_seabed_sample) continue;

        for (auto& sample : ping->samples) {
            if (sample.range_m < 0.f) continue;  // water column marker
            if (!std::isfinite(sample.range_m)) continue;
            const auto ground_m = core::slantToGroundRangeMetres(sample.range_m,
                                                                  *altitude_m);
            sample.range_m = ground_m ? static_cast<float>(*ground_m) : 0.0f;
        }
        ping->correction_flags |= core::CorrectionFlag::SlantRange;
    }

    return output;
}

} // namespace dolphin::pipeline
