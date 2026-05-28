#include "pipeline/nodes/output/ExportCsvNode.h"

namespace dolphin::pipeline {

NodeSchema ExportCsvNode::schema() const
{
    return NodeSchema{
        "export_csv", "Export CSV", "Output",
        {
            {"delimiter",      {"Delimiter",      std::string{","}, std::string{}, std::string{}}},
            {"include_header", {"Include header", true, false, true}},
        }
    };
}

ArtifactBuffer ExportCsvNode::process(const ArtifactBuffer& input,
                                       const NodeParams&) const
{
    return input;  // Export triggered by the graph runner, not inline
}

} // namespace dolphin::pipeline
