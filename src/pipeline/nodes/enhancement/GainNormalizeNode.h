#pragma once
#include "pipeline/IProcessingNode.h"

namespace dolphin::pipeline {

// Applies one stable line-level gain per channel to a target mean.
class GainNormalizeNode : public IProcessingNode {
public:
    std::string typeId() const override { return "gain_normalize"; }
    std::string label()  const override { return "Gain Normalize"; }
    NodeSchema  schema() const override;
    ArtifactBuffer process(const ArtifactBuffer& input,
                           const NodeParams&     params) const override;
};

} // namespace dolphin::pipeline
