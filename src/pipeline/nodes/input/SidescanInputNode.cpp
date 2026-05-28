#include "pipeline/nodes/input/SidescanInputNode.h"

namespace dolphin::pipeline {

NodeSchema SidescanInputNode::schema() const
{
    return NodeSchema{
        "sss_input", "Sidescan Input", "DataIn",
        {
            {"channel", {"Channel", std::string{"Both"}, std::string{}, std::string{},
                         {"Both", "Port", "Starboard"}}},
        }
    };
}

ArtifactBuffer SidescanInputNode::process(const ArtifactBuffer& input,
                                            const NodeParams&) const
{
    return input;
}

} // namespace dolphin::pipeline
