#pragma once
#include "pipeline/IProcessingNode.h"

namespace dolphin::pipeline {

// Marks acoustic shadow regions by detecting sustained low-amplitude zones.
class ShadowDetectNode : public IProcessingNode {
public:
    std::string typeId() const override { return "shadow_detect"; }
    std::string label()  const override { return "Shadow Detection"; }
    NodeSchema  schema() const override;
    ArtifactBuffer process(const ArtifactBuffer& input,
                           const NodeParams&     params) const override;
};

} // namespace dolphin::pipeline
