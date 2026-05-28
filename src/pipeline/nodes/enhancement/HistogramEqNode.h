#pragma once
#include "pipeline/IProcessingNode.h"

namespace dolphin::pipeline {

// Per-ping histogram equalization with configurable strength blend.
class HistogramEqNode : public IProcessingNode {
public:
    std::string typeId() const override { return "histogram_eq"; }
    std::string label()  const override { return "Histogram Equalization"; }
    NodeSchema  schema() const override;
    ArtifactBuffer process(const ArtifactBuffer& input,
                           const NodeParams&     params) const override;
};

} // namespace dolphin::pipeline
