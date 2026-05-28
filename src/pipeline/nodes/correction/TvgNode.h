#pragma once
#include "pipeline/IProcessingNode.h"

namespace dolphin::pipeline {

// Time-Varied Gain: compensates for acoustic spreading loss
class TvgNode : public IProcessingNode {
public:
    std::string typeId() const override { return "tvg"; }
    std::string label()  const override { return "TVG"; }
    NodeSchema  schema() const override;
    ArtifactBuffer process(const ArtifactBuffer& input,
                           const NodeParams&     params) const override;
};

} // namespace dolphin::pipeline
