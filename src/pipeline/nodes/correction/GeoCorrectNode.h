#pragma once
#include "pipeline/IProcessingNode.h"

namespace dolphin::pipeline {

// Applies a vessel-heading layback offset to each ping's navigation fix.
class GeoCorrectNode : public IProcessingNode {
public:
    std::string typeId() const override { return "geocorrect"; }
    std::string label()  const override { return "Layback Correction"; }
    NodeSchema  schema() const override;
    ArtifactBuffer process(const ArtifactBuffer& input,
                           const NodeParams&     params) const override;
};

} // namespace dolphin::pipeline
