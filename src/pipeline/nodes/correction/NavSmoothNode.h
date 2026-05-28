#pragma once
#include "pipeline/IProcessingNode.h"

namespace dolphin::pipeline {

// Smooths navigation track with a running-average window.
class NavSmoothNode : public IProcessingNode {
public:
    std::string typeId() const override { return "nav_smooth"; }
    std::string label()  const override { return "Nav Smooth"; }
    NodeSchema  schema() const override;
    ArtifactBuffer process(const ArtifactBuffer& input,
                           const NodeParams&     params) const override;
};

} // namespace dolphin::pipeline
