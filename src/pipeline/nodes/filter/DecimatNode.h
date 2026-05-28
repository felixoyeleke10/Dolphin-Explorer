#pragma once
#include "pipeline/IProcessingNode.h"

namespace dolphin::pipeline {

// Keeps every Nth ping, reducing data density.
class DecimatNode : public IProcessingNode {
public:
    std::string typeId() const override { return "decimate"; }
    std::string label()  const override { return "Decimate"; }
    NodeSchema  schema() const override;
    ArtifactBuffer process(const ArtifactBuffer& input,
                           const NodeParams&     params) const override;
};

} // namespace dolphin::pipeline
