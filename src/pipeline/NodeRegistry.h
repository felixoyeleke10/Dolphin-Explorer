#pragma once
#include "pipeline/IProcessingNode.h"
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace dolphin::pipeline {

// Singleton factory that maps node type_ids to constructors.
// Enables NodeGraph::fromJson() to reconstruct graphs without coupling to
// specific node types, and lets plugins register new nodes at runtime.
class NodeRegistry {
public:
    static NodeRegistry& instance();

    // Register a zero-argument factory for a node type.
    void registerType(const std::string& type_id,
                      std::function<NodePtr()> factory);

    // Create a node. Returns nullptr if type_id is unknown.
    NodePtr create(const std::string& type_id) const;

    // All known type_ids in registration order.
    std::vector<std::string> allTypeIds() const;
    bool isKnown(const std::string& type_id) const;

private:
    NodeRegistry() = default;
    std::vector<std::string>                              m_order;
    std::map<std::string, std::function<NodePtr()>>       m_factories;
};

// Register all nodes that ship with Dolphin Explorer.
// Call once from main() before any graph is loaded.
void registerBuiltinNodes();

} // namespace dolphin::pipeline
