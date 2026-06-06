// NodeGraph.cpp — node/edge/group CRUD and layout.
//
// Traversal algorithms: NodeGraph.Traversal.cpp
// Execution engine:     NodeGraph.Execution.cpp
// JSON serialisation:   NodeGraph.Serialization.cpp

#include "pipeline/NodeGraph.h"
#include <algorithm>

namespace dolphin::pipeline {

NodeGraph::NodeGraph() = default;

// -- Groups --------------------------------------------------------------------

NodeGroup* NodeGraph::addGroup(NodeGroup group)
{
    if (group.id.empty())
        group.id = "grp_" + std::to_string(m_groups.size());
    m_groups.push_back(std::move(group));
    return &m_groups.back();
}

void NodeGraph::removeGroup(const std::string& id)
{
    m_groups.erase(std::remove_if(m_groups.begin(), m_groups.end(),
        [&](const NodeGroup& g){ return g.id == id; }), m_groups.end());
}

NodeGroup* NodeGraph::findGroup(const std::string& id)
{
    for (auto& g : m_groups) if (g.id == id) return &g;
    return nullptr;
}

const NodeGroup* NodeGraph::findGroup(const std::string& id) const
{
    for (const auto& g : m_groups) if (g.id == id) return &g;
    return nullptr;
}

// -- Nodes ---------------------------------------------------------------------

void NodeGraph::addNode(NodePtr node)
{
    m_nodes.push_back(node);
    m_dirty[node->instance_id] = true;
}

void NodeGraph::removeNode(const std::string& id)
{
    m_nodes.erase(std::remove_if(m_nodes.begin(), m_nodes.end(),
        [&](const NodePtr& n){ return n->instance_id == id; }), m_nodes.end());
    m_edges.erase(std::remove_if(m_edges.begin(), m_edges.end(),
        [&](const Edge& e){ return e.from_node == id || e.to_node == id; }),
        m_edges.end());
    m_dirty.erase(id);
    m_cache.erase(id);
    m_positions.erase(id);
}

NodePtr NodeGraph::findNode(const std::string& id) const
{
    for (const auto& n : m_nodes) if (n->instance_id == id) return n;
    return nullptr;
}

// -- Edges ---------------------------------------------------------------------

bool NodeGraph::addEdge(const std::string& from, const std::string& to, int to_port)
{
    if (from.empty() || to.empty() || from == to) return false;
    if (!findNode(from) || !findNode(to)) return false;
    removeEdgeToPort(to, to_port);
    if (wouldCreateCycle(from, to)) return false;
    m_edges.push_back({from, to, to_port});
    markDirty(to);
    return true;
}

void NodeGraph::removeEdge(const std::string& from, const std::string& to)
{
    m_edges.erase(std::remove_if(m_edges.begin(), m_edges.end(),
        [&](const Edge& e){ return e.from_node == from && e.to_node == to; }),
        m_edges.end());
    markDirty(to);
}

void NodeGraph::removeEdgeToPort(const std::string& to, int to_port)
{
    m_edges.erase(std::remove_if(m_edges.begin(), m_edges.end(),
        [&](const Edge& e){ return e.to_node == to && e.to_port == to_port; }),
        m_edges.end());
}

// -- Layout --------------------------------------------------------------------

void NodeGraph::setNodePosition(const std::string& id, float x, float y)
{
    m_positions[id] = {x, y};
}

std::pair<float,float> NodeGraph::nodePosition(const std::string& id) const
{
    auto it = m_positions.find(id);
    return (it != m_positions.end()) ? it->second : std::make_pair(0.f, 0.f);
}

bool NodeGraph::hasNodePosition(const std::string& id) const
{
    return m_positions.count(id) > 0;
}

} // namespace dolphin::pipeline
