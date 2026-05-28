// NodeGraph.Execution.cpp — execute() with caching and per-node timing.
#include "pipeline/NodeGraph.h"
#include "pipeline/GraphJob.h"
#include <chrono>
#include <set>
#include <stdexcept>

namespace dolphin::pipeline {

namespace {

static int64_t nowUs()
{
    using namespace std::chrono;
    return duration_cast<microseconds>(
        steady_clock::now().time_since_epoch()).count();
}

void appendUnique(ArtifactBuffer& dst, const ArtifactBuffer& src)
{
    std::set<std::pair<int, uint64_t>> seen;
    for (const auto& a : dst)
        seen.emplace(static_cast<int>(core::artifactType(a)), core::artifactId(a));
    for (const auto& a : src) {
        if (seen.insert({static_cast<int>(core::artifactType(a)), core::artifactId(a)}).second)
            dst.push_back(a);
    }
}

} // namespace

ArtifactBuffer NodeGraph::execute(const ArtifactBuffer& source)
{
    GraphJob dummy;
    return execute(source, dummy);
}

ArtifactBuffer NodeGraph::execute(const ArtifactBuffer& source, GraphJob& job)
{
    auto order = topoSort();
    if (order.empty()) {
        if (!m_nodes.empty()) {
            NodeResult r;
            r.node_id = r.node_type = "graph";
            r.failed = true; r.error = "graph contains a cycle";
            r.input_count = r.output_count = source.size();
            job.node_results.push_back(std::move(r));
        }
        return source;
    }

    ArtifactBuffer last_output = source;

    for (const auto& id : order) {
        NodeResult result;
        result.node_id = id;
        NodePtr node = findNode(id);
        if (!node) continue;
        result.node_type = node->typeId();

        if (!m_dirty[id] && m_cache.count(id)) {
            result.cache_hit    = true;
            result.input_count  = last_output.size();
            result.output_count = m_cache[id].size();
            last_output = m_cache[id];
            job.node_results.push_back(std::move(result));
            continue;
        }

        ArtifactBuffer input;
        bool has_upstream = false;
        for (const auto& e : m_edges) {
            if (e.to_node != id) continue;
            has_upstream = true;
            auto it = m_cache.find(e.from_node);
            if (it != m_cache.end()) appendUnique(input, it->second);
        }
        if (!has_upstream) input = source;
        result.input_count = input.size();

        const int64_t t0 = nowUs();
        try {
            ArtifactBuffer output = node->process(input, node->params);
            result.output_count = output.size();
            m_cache[id] = output;
            m_dirty[id] = false;
            last_output = std::move(output);
        } catch (const std::exception& ex) {
            result.failed = true; result.error = ex.what();
            m_cache[id] = input; last_output = input;
        } catch (...) {
            result.failed = true; result.error = "unknown exception";
            m_cache[id] = input; last_output = input;
        }
        result.duration_us = nowUs() - t0;
        job.node_results.push_back(std::move(result));
    }

    return last_output;
}

} // namespace dolphin::pipeline
