#pragma once
#include "pipeline/IProcessingNode.h"

namespace dolphin::pipeline {

// Zeros samples outside [near_m, far_m] across-track range.
class ClipRangeNode : public IProcessingNode {
public:
    std::string typeId() const override { return "clip_range"; }
    std::string label()  const override { return "Clip Range"; }
    NodeSchema  schema() const override;
    ArtifactBuffer process(const ArtifactBuffer& input,
                           const NodeParams&     params) const override;
};

} // namespace dolphin::pipeline
