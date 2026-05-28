#include "pipeline/nodes/input/MBESInputNode.h"

namespace dolphin::pipeline {

NodeSchema MBESInputNode::schema() const
{
    return NodeSchema{
        "mbes_input", "MBES Input", "DataIn",
        {
            {"swath_mode", {"Swath Mode", std::string{"Full"}, std::string{}, std::string{},
                            {"Full", "Port", "Starboard", "Nadir"}}},
        }
    };
}

ArtifactBuffer MBESInputNode::process(const ArtifactBuffer& input,
                                       const NodeParams&) const
{
    return input;
}

} // namespace dolphin::pipeline
