#pragma once
#include "pipeline/IProcessingNode.h"

namespace dolphin::pipeline {

// Simple band-pass filter using moving-average approximation
class BandPassNode : public IProcessingNode {
public:
    std::string typeId() const override { return "bpf"; }
    std::string label()  const override { return "Band-Pass Filter"; }
    NodeSchema  schema() const override;
    ArtifactBuffer process(const ArtifactBuffer& input,
                           const NodeParams&     params) const override;
};

} // namespace dolphin::pipeline
