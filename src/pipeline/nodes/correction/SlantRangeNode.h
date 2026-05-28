#pragma once
#include "pipeline/IProcessingNode.h"

namespace dolphin::pipeline {

// Converts slant range to across-track ground range using sensor altitude.
class SlantRangeNode : public IProcessingNode {
public:
    std::string typeId() const override { return "slant_range"; }
    std::string label()  const override { return "Slant-Range Correction"; }
    NodeSchema  schema() const override;
    ArtifactBuffer process(const ArtifactBuffer& input,
                           const NodeParams&     params) const override;
};

} // namespace dolphin::pipeline
