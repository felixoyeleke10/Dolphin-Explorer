#pragma once
#include "pipeline/IProcessingNode.h"

namespace dolphin::pipeline {

// Normalizes each ping's mean amplitude to a target value.
class GainNormalizeNode : public IProcessingNode {
public:
    std::string typeId() const override { return "gain_normalize"; }
    std::string label()  const override { return "Gain Normalize"; }
    NodeSchema  schema() const override;
    ArtifactBuffer process(const ArtifactBuffer& input,
                           const NodeParams&     params) const override;
};

} // namespace dolphin::pipeline
