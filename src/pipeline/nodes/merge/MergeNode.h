#pragma once
#include "pipeline/IProcessingNode.h"

namespace dolphin::pipeline {

// Combines two input streams (port A + port B) into one artifact buffer.
class MergeNode : public IProcessingNode {
public:
    std::string    typeId()     const override { return "merge"; }
    std::string    label()      const override { return "Merge"; }
    int            inputCount() const override { return 2; }
    NodeSchema     schema()     const override;
    ArtifactBuffer process(const ArtifactBuffer& input, const NodeParams&) const override;
};

// Weighted blend of two inputs — applies a mix ratio to artifact buffers.
class BlendNode : public IProcessingNode {
public:
    std::string    typeId()     const override { return "blend"; }
    std::string    label()      const override { return "Blend"; }
    int            inputCount() const override { return 2; }
    NodeSchema     schema()     const override;
    ArtifactBuffer process(const ArtifactBuffer& input, const NodeParams& params) const override;
};

// N-input merge — user configures how many inputs (2–8) via the "inputs" param.
// inputCount() reads instance params at runtime.
class MultiMergeNode : public IProcessingNode {
public:
    std::string    typeId()     const override { return "multi_merge"; }
    std::string    label()      const override { return "MultiMerge"; }
    int            inputCount() const override;
    NodeSchema     schema()     const override;
    ArtifactBuffer process(const ArtifactBuffer& input, const NodeParams&) const override;
};

} // namespace dolphin::pipeline
