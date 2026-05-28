#pragma once

#include <string>
#include <vector>

#include "pipeline/IProcessingNode.h"
#include "pipeline/NodeGraph.h"
#include "pipeline/TemplateDefinition.h"

namespace dolphin::pipeline {

class CompoundNode : public IProcessingNode {
public:
    CompoundNode() = default;
    explicit CompoundNode(TemplateDefinition definition);

    std::string typeId() const override { return m_type_id; }
    std::string label() const override { return m_label; }
    NodeSchema  schema() const override;
    ArtifactBuffer process(const ArtifactBuffer& input,
                           const NodeParams& params) const override;

    const NodeGraph& subgraph() const { return m_subgraph; }
    NodeGraph&       subgraph()       { return m_subgraph; }
    const std::vector<ExposedParamBinding>& exposedParams() const { return m_exposed_params; }

private:
    std::string                      m_type_id = "compound";
    std::string                      m_label = "Compound Node";
    NodeGraph                        m_subgraph;
    std::vector<ExposedParamBinding> m_exposed_params;
};

} // namespace dolphin::pipeline
