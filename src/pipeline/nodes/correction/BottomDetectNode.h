#pragma once
#include "pipeline/IProcessingNode.h"
#include "core/SidescanPing.h"

namespace dolphin::pipeline {

// Detects seabed return by finding the first amplitude peak above threshold.
// Sets sample.range_m = -1 for samples above the seabed (water column marker).
// Only processes SidescanPing artifacts; all others pass through unchanged.
class BottomDetectNode : public IProcessingNode {
public:
    std::string    typeId() const override { return "bottom_detect"; }
    std::string    label()  const override { return "Bottom Detection"; }
    NodeSchema     schema() const override;
    ArtifactBuffer process(const ArtifactBuffer& input,
                           const NodeParams&     params) const override;

    // Estimates the first seabed return and records an automatic bottom pick.
    // Manual picks are never overwritten. Percentage bounds are clamped.
    static bool detectBottom(core::SidescanPing& ping, float threshold,
                             float search_start_pct, float search_end_pct);

private:
    int findBottom(const core::SidescanPing& ping, float threshold,
                   int search_start, int search_end) const;
};

} // namespace dolphin::pipeline
