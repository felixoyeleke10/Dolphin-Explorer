#pragma once
#include <string>
#include <vector>
#include <map>
#include <vector>
#include <variant>
#include <memory>
#include "core/Artifact.h"

namespace dolphin::pipeline {

// A buffer of heterogeneous survey artifacts flowing between nodes.
// Nodes use std::get_if<> to handle types they understand and pass others through.
using ArtifactBuffer = std::vector<core::Artifact>;

// Parameter value types
using Value = std::variant<bool, int, float, double, std::string>;

struct NodeParam {
    std::string label;
    Value       default_value;
    Value       min_value;
    Value       max_value;
    std::vector<std::string> options;
};

struct NodeSchema {
    std::string                      type_id;    // "tvg", "bpf", etc.
    std::string                      label;
    std::string                      category;   // palette grouping
    std::map<std::string, NodeParam> params;
};

using NodeParams = std::map<std::string, Value>;

class IProcessingNode {
public:
    virtual ~IProcessingNode() = default;

    virtual std::string    typeId()      const = 0;
    virtual std::string    label()       const = 0;
    virtual NodeSchema     schema()      const = 0;
    virtual int            inputCount()  const { return 1; }  // override to 2 for merge nodes

    // Pure function — same input + params always yields same output.
    // Nodes should transform artifacts of their modality and pass others through unchanged.
    virtual ArtifactBuffer process(const ArtifactBuffer& input,
                                    const NodeParams&      params) const = 0;

    // Instance-level params (edited by user in the node graph)
    NodeParams  params;
    std::string instance_id;    // unique within a graph
};

using NodePtr = std::shared_ptr<IProcessingNode>;

} // namespace dolphin::pipeline
