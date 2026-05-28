#include "pipeline/nodes/input/SBPInputNode.h"

namespace dolphin::pipeline {

NodeSchema SBPInputNode::schema() const
{
    return NodeSchema{
        "sbp_input", "SBP Input", "DataIn",
        {
            {"channel", {"Channel", std::string{"Both"}, std::string{}, std::string{},
                         {"Both", "Low Freq", "High Freq"}}},
        }
    };
}

ArtifactBuffer SBPInputNode::process(const ArtifactBuffer& input,
                                      const NodeParams&) const
{
    return input;
}

} // namespace dolphin::pipeline
