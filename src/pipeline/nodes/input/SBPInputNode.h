#pragma once
#include "pipeline/IProcessingNode.h"

namespace dolphin::pipeline {

class SBPInputNode : public IProcessingNode {
public:
    std::string typeId() const override { return "sbp_input"; }
    std::string label()  const override { return "SBP Input"; }
    NodeSchema  schema() const override;
    ArtifactBuffer process(const ArtifactBuffer& input,
                           const NodeParams&     params) const override;
};

} // namespace dolphin::pipeline
