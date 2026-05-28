#pragma once
#include "pipeline/IProcessingNode.h"

namespace dolphin::pipeline {

// Stretches amplitude range by clipping to [low_pct, high_pct] percentiles.
class ContrastEnhanceNode : public IProcessingNode {
public:
    std::string typeId() const override { return "contrast_enhance"; }
    std::string label()  const override { return "Contrast Enhance"; }
    NodeSchema  schema() const override;
    ArtifactBuffer process(const ArtifactBuffer& input,
                           const NodeParams&     params) const override;
};

} // namespace dolphin::pipeline
