#include "pipeline/CompoundNode.h"

namespace dolphin::pipeline {

CompoundNode::CompoundNode(TemplateDefinition definition)
    : m_type_id(definition.template_id.empty() ? "compound" : definition.template_id)
    , m_label(definition.label.empty() ? "Compound Node" : definition.label)
    , m_subgraph(std::move(definition.subgraph))
    , m_exposed_params(std::move(definition.exposed_params))
{
}

NodeSchema CompoundNode::schema() const
{
    NodeSchema schema;
    schema.type_id = m_type_id;
    schema.label = m_label;
    schema.category = "Compound";

    for (const auto& binding : m_exposed_params) {
        auto node = m_subgraph.findNode(binding.internal_node_id);
        if (!node)
            continue;

        const auto internal_schema = node->schema();
        auto it = internal_schema.params.find(binding.internal_param_key);
        if (it == internal_schema.params.end())
            continue;

        NodeParam param = it->second;
        if (!binding.public_name.empty())
            param.label = binding.public_name;
        schema.params[binding.public_name] = std::move(param);
    }

    return schema;
}

ArtifactBuffer CompoundNode::process(const ArtifactBuffer& input,
                                     const NodeParams& params) const
{
    NodeGraph local = m_subgraph;

    for (const auto& binding : m_exposed_params) {
        auto node = local.findNode(binding.internal_node_id);
        if (!node)
            continue;

        auto it = params.find(binding.public_name);
        if (it == params.end())
            continue;

        node->params[binding.internal_param_key] = it->second;
    }

    return local.execute(input);
}

} // namespace dolphin::pipeline
