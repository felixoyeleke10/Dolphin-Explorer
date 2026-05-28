// NodeGraph.Traversal.cpp — graph algorithms: topo sort, cycle detection, dirty propagation.
#include "pipeline/NodeGraph.h"
#include <map>
#include <set>

namespace dolphin::pipeline {

std::vector<std::string> NodeGraph::downstreamOf(const std::string& id) const
{
    std::vector<std::string> result;
    for (const auto& e : m_edges)
        if (e.from_node == id) result.push_back(e.to_node);
    return result;
}

std::vector<std::string> NodeGraph::topoSort() const
{
    std::map<std::string, int> in_degree;
    for (const auto& n : m_nodes) in_degree[n->instance_id] = 0;
    for (const auto& e : m_edges) in_degree[e.to_node]++;

    std::vector<std::string> queue, order;
    for (const auto& [id, deg] : in_degree)
        if (deg == 0) queue.push_back(id);

    while (!queue.empty()) {
        std::string cur = queue.back(); queue.pop_back();
        order.push_back(cur);
        for (const auto& e : m_edges) {
            if (e.from_node == cur)
                if (--in_degree[e.to_node] == 0)
                    queue.push_back(e.to_node);
        }
    }
    if (order.size() != m_nodes.size()) return {};
    return order;
}

void NodeGraph::markDirtyRecursive(const std::string& id,
                                   std::set<std::string>& visited)
{
    if (!visited.insert(id).second) return;
    m_dirty[id] = true;
    m_cache.erase(id);
    for (const auto& ds : downstreamOf(id))
        markDirtyRecursive(ds, visited);
}

void NodeGraph::markDirty(const std::string& id)
{
    std::set<std::string> visited;
    markDirtyRecursive(id, visited);
}

void NodeGraph::markAllDirty()
{
    for (const auto& n : m_nodes) m_dirty[n->instance_id] = true;
    m_cache.clear();
}

bool NodeGraph::wouldCreateCycle(const std::string& from,
                                 const std::string& to) const
{
    std::set<std::string> visited;
    std::vector<std::string> stack{to};
    while (!stack.empty()) {
        std::string cur = stack.back(); stack.pop_back();
        if (!visited.insert(cur).second) continue;
        if (cur == from) return true;
        for (const auto& e : m_edges)
            if (e.from_node == cur) stack.push_back(e.to_node);
    }
    return false;
}

} // namespace dolphin::pipeline
