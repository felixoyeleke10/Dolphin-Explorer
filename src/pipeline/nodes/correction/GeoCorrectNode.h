#pragma once
#include "pipeline/IProcessingNode.h"

namespace dolphin::pipeline {

// Applies layback correction and assigns geo-referenced positions to each sample.
class GeoCorrectNode : public IProcessingNode {
public:
    std::string typeId() const override { return "geocorrect"; }
    std::string label()  const override { return "Geo-Correction"; }
    NodeSchema  schema() const override;
    ArtifactBuffer process(const ArtifactBuffer& input,
                           const NodeParams&     params) const override;
};

} // namespace dolphin::pipeline
