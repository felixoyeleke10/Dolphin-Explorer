#pragma once
#include "pipeline/IProcessingNode.h"

namespace dolphin::pipeline {

// Reduces speckle noise by replacing each sample with the local median.
class SpeckleFilterNode : public IProcessingNode {
public:
    std::string typeId() const override { return "speckle"; }
    std::string label()  const override { return "Speckle Filter"; }
    NodeSchema  schema() const override;
    ArtifactBuffer process(const ArtifactBuffer& input,
                           const NodeParams&     params) const override;
};

} // namespace dolphin::pipeline
