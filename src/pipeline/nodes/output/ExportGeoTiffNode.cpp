#include "pipeline/nodes/output/ExportGeoTiffNode.h"

namespace dolphin::pipeline {

NodeSchema ExportGeoTiffNode::schema() const
{
    return NodeSchema{
        "export_geotiff", "Export GeoTIFF", "Output",
        {
            {"resolution_m", {"Resolution (m/px)", 0.25f, 0.01f, 100.0f}},
            {"compress",     {"Compress output",   true,  false,  true}},
        }
    };
}

ArtifactBuffer ExportGeoTiffNode::process(const ArtifactBuffer& input,
                                           const NodeParams&) const
{
    return input;  // Export triggered by the graph runner, not inline
}

} // namespace dolphin::pipeline
