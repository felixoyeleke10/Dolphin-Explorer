#pragma once
#include "pipeline/IProcessingNode.h"

namespace dolphin::pipeline {

// Source node — marks the entry point of the graph; passes artifacts through unchanged.
class SidescanInputNode : public IProcessingNode {
public:
    std::string typeId() const override { return "sss_input"; }
    std::string label()  const override { return "Sidescan Input"; }
    NodeSchema  schema() const override;
    ArtifactBuffer process(const ArtifactBuffer& input,
                           const NodeParams&     params) const override;
};

} // namespace dolphin::pipeline
