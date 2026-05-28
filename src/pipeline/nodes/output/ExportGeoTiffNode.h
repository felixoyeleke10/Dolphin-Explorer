#pragma once
#include "pipeline/IProcessingNode.h"

namespace dolphin::pipeline {

// Output node — signals the export service to render a GeoTIFF mosaic.
class ExportGeoTiffNode : public IProcessingNode {
public:
    std::string typeId() const override { return "export_geotiff"; }
    std::string label()  const override { return "Export GeoTIFF"; }
    NodeSchema  schema() const override;
    ArtifactBuffer process(const ArtifactBuffer& input,
                           const NodeParams&     params) const override;
};

} // namespace dolphin::pipeline
