#pragma once
#include "pipeline/IProcessingNode.h"

namespace dolphin::pipeline {

// Output node — signals the export service to write a contact CSV report.
class ExportCsvNode : public IProcessingNode {
public:
    std::string typeId() const override { return "export_csv"; }
    std::string label()  const override { return "Export CSV"; }
    NodeSchema  schema() const override;
    ArtifactBuffer process(const ArtifactBuffer& input,
                           const NodeParams&     params) const override;
};

} // namespace dolphin::pipeline
