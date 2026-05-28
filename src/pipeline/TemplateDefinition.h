#pragma once

#include <string>
#include <vector>

#include "pipeline/NodeGraph.h"

namespace dolphin::pipeline {

struct ExposedParamBinding {
    std::string public_name;
    std::string internal_node_id;
    std::string internal_param_key;
};

struct TemplateDefinition {
    std::string                      template_id;
    std::string                      label;
    NodeGraph                        subgraph;
    std::vector<ExposedParamBinding> exposed_params;
};

} // namespace dolphin::pipeline
