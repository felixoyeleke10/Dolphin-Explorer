#pragma once
#include "pipeline/IProcessingNode.h"

namespace dolphin::pipeline {

class MBESInputNode : public IProcessingNode {
public:
    std::string typeId() const override { return "mbes_input"; }
    std::string label()  const override { return "MBES Input"; }
    NodeSchema  schema() const override;
    ArtifactBuffer process(const ArtifactBuffer& input,
                           const NodeParams&     params) const override;
};

} // namespace dolphin::pipeline
