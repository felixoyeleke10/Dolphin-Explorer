#include "pipeline/nodes/filter/DecimatNode.h"
#include <algorithm>

namespace dolphin::pipeline {

NodeSchema DecimatNode::schema() const
{
    return NodeSchema{
        "decimate", "Decimate", "Filter",
        {
            {"factor", {"Keep every Nth ping", 2, 2, 32}},
        }
    };
}

ArtifactBuffer DecimatNode::process(const ArtifactBuffer& input,
                                     const NodeParams& params) const
{
    int factor = 2;
    if (params.count("factor")) factor = std::get<int>(params.at("factor"));
    factor = std::max(2, factor);

    ArtifactBuffer output;
    output.reserve(input.size() / factor + 1);
    for (int i = 0; i < static_cast<int>(input.size()); ++i)
        if (i % factor == 0) output.push_back(input[i]);
    return output;
}

} // namespace dolphin::pipeline
