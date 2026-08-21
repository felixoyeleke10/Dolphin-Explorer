#pragma once
#include "pipeline/IProcessingNode.h"

namespace dolphin::pipeline {

// Angle-range correction using altitude-derived grazing angle.
class ArcNode : public IProcessingNode {
public:
    std::string typeId() const override { return "arc"; }
    std::string label() const override { return "Angle-Range Correction"; }
    NodeSchema schema() const override;
    ArtifactBuffer process(const ArtifactBuffer& input,
                           const NodeParams& params) const override;
};

} // namespace dolphin::pipeline
