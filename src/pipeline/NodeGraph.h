#pragma once
#include <vector>
#include <string>
#include <map>
#include <set>
#include <functional>
#include "pipeline/IProcessingNode.h"
#include "pipeline/GraphJob.h"

namespace dolphin::pipeline {

struct Edge {
    std::string from_node;  // instance_id
    std::string to_node;
    int         to_port = 0;  // 0 = primary input, 1 = secondary (merge B)
};

struct NodeGroup {
    std::string              id;
    std::string              label;
    std::vector<std::string> node_ids;
};

// Dirty-tracking callback — called when graph output changes
using GraphOutputFn = std::function<void(const ArtifactBuffer&)>;

class NodeGraph {
public:
    NodeGraph();

    void       addNode(NodePtr node);
    void       removeNode(const std::string& instance_id);
    bool       addEdge(const std::string& from, const std::string& to, int to_port = 0);
    void       removeEdge(const std::string& from, const std::string& to);
    void       removeEdgeToPort(const std::string& to, int to_port);  // disconnect a specific input port

    // Mark a node (and all downstream) dirty
    void       markDirty(const std::string& instance_id);
    void       markAllDirty();

    // Execute the graph — returns final output.
    // The two-argument form also populates a GraphJob with per-node timing.
    ArtifactBuffer execute(const ArtifactBuffer& source);
    ArtifactBuffer execute(const ArtifactBuffer& source, struct GraphJob& job);

    // Serialise to JSON (includes node params and edges).
    // fromJson requires the NodeRegistry to be populated (call registerBuiltinNodes first).
    std::string toJson() const;
    bool        fromJson(const std::string& json);

    // Node access
    NodePtr     findNode(const std::string& instance_id) const;
    const std::vector<NodePtr>& nodes() const { return m_nodes; }
    const std::vector<Edge>&    edges() const { return m_edges; }

    // Layout — node positions for the visual editor (canvas coordinates).
    void setNodePosition(const std::string& id, float x, float y);
    std::pair<float,float> nodePosition(const std::string& id) const;
    bool hasNodePosition(const std::string& id) const;

    // Groups
    NodeGroup* addGroup(NodeGroup group);
    void       removeGroup(const std::string& id);
    NodeGroup* findGroup(const std::string& id);
    const NodeGroup* findGroup(const std::string& id) const;
    const std::vector<NodeGroup>& groups() const { return m_groups; }
    std::vector<NodeGroup>&       groups()       { return m_groups; }

private:
    std::vector<NodePtr>   m_nodes;
    std::vector<Edge>      m_edges;
    std::vector<NodeGroup> m_groups;
    std::map<std::string, bool>              m_dirty;
    std::map<std::string, ArtifactBuffer>    m_cache;
    std::map<std::string, std::pair<float,float>> m_positions;

    std::vector<std::string> topoSort() const;
    std::vector<std::string> downstreamOf(const std::string& id) const;
    void       markDirtyRecursive(const std::string& instance_id,
                                  std::set<std::string>& visited);
    bool       wouldCreateCycle(const std::string& from,
                                const std::string& to) const;
};

} // namespace dolphin::pipeline
