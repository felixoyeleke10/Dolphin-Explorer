#include "pipeline/NodeGraph.h"
#include "pipeline/NodeRegistry.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <variant>

using dolphin::pipeline::NodeGraph;
using dolphin::pipeline::NodeGroup;
using dolphin::pipeline::NodeRegistry;

namespace {

int gPassed = 0;
int gFailed = 0;

void check(bool condition, const char* expression, const char* file, int line)
{
    if (condition) {
        ++gPassed;
    } else {
        ++gFailed;
        std::fprintf(stderr, "FAIL  %s:%d  %s\n", file, line, expression);
    }
}

#define CHECK(expression) check((expression), #expression, __FILE__, __LINE__)

dolphin::pipeline::NodePtr makeNode(const std::string& type,
                                    const std::string& id)
{
    auto node = NodeRegistry::instance().create(type);
    CHECK(node != nullptr);
    if (node) node->instance_id = id;
    return node;
}

NodeGraph makeStableGraph()
{
    NodeGraph graph;
    auto node = makeNode("tvg", "stable");
    if (node) {
        node->params["spreading"] = 12.0f;
        graph.addNode(std::move(node));
        graph.setNodePosition("stable", 5.0f, 7.0f);
        graph.addGroup(NodeGroup{"stable_group", "Stable", {"stable"}});
    }
    return graph;
}

void expectRejectedWithoutMutation(NodeGraph& graph, const std::string& json)
{
    const std::string before = graph.toJson();
    CHECK(!graph.fromJson(json));
    CHECK(graph.toJson() == before);
}

void testRoundTripPreservesPortsGroupsAndLayout()
{
    NodeGraph graph;
    auto input = makeNode("sss_input", "input");
    auto merge = makeNode("merge", "merge");
    if (!input || !merge) return;

    input->params["channel"] = std::string{"Port"};
    graph.addNode(std::move(input));
    graph.addNode(std::move(merge));
    CHECK(graph.addEdge("input", "merge", 1));
    graph.setNodePosition("input", -14.5f, 20.25f);
    graph.setNodePosition("merge", 120.0f, 45.0f);
    graph.addGroup(NodeGroup{"grp_processing", "Processing", {"input", "merge"}});

    const std::string serialized = graph.toJson();
    CHECK(serialized.find("\"to_port\": 1") != std::string::npos);
    CHECK(serialized.find("\"groups\"") != std::string::npos);

    NodeGraph loaded;
    CHECK(loaded.fromJson(serialized));
    CHECK(loaded.nodes().size() == 2);
    CHECK(loaded.edges().size() == 1);
    if (loaded.edges().size() == 1) {
        CHECK(loaded.edges()[0].from_node == "input");
        CHECK(loaded.edges()[0].to_node == "merge");
        CHECK(loaded.edges()[0].to_port == 1);
    }

    const auto loadedInput = loaded.findNode("input");
    CHECK(loadedInput != nullptr);
    if (loadedInput) {
        const auto channel = loadedInput->params.find("channel");
        CHECK(channel != loadedInput->params.end());
        if (channel != loadedInput->params.end())
            CHECK(std::get<std::string>(channel->second) == "Port");
    }

    CHECK(loaded.hasNodePosition("input"));
    const auto inputPosition = loaded.nodePosition("input");
    CHECK(std::fabs(inputPosition.first - (-14.5f)) < 0.001f);
    CHECK(std::fabs(inputPosition.second - 20.25f) < 0.001f);

    CHECK(loaded.groups().size() == 1);
    if (loaded.groups().size() == 1) {
        CHECK(loaded.groups()[0].id == "grp_processing");
        CHECK(loaded.groups()[0].label == "Processing");
        CHECK(loaded.groups()[0].node_ids.size() == 2);
        if (loaded.groups()[0].node_ids.size() == 2) {
            CHECK(loaded.groups()[0].node_ids[0] == "input");
            CHECK(loaded.groups()[0].node_ids[1] == "merge");
        }
    }

    // Legacy optional collections and edge ports remain readable.
    NodeGraph legacy;
    CHECK(legacy.fromJson(R"json({
        "nodes": [
            {"id": "a", "type": "sss_input"},
            {"id": "b", "type": "merge", "params": {}}
        ],
        "edges": [{"from": "a", "to": "b"}]
    })json"));
    CHECK(legacy.edges().size() == 1);
    if (legacy.edges().size() == 1) CHECK(legacy.edges()[0].to_port == 0);
}

void testUnknownTypeDoesNotMutateExistingGraph()
{
    NodeGraph graph = makeStableGraph();
    expectRejectedWithoutMutation(graph, R"json({
        "version": 2,
        "nodes": [{"id": "unknown", "type": "not_registered", "params": {}}]
    })json");
}

void testMalformedContentAndDanglingReferencesAreRejected()
{
    NodeGraph graph = makeStableGraph();

    expectRejectedWithoutMutation(graph,
        R"json({"version": 2, "nodes": {}})json");
    expectRejectedWithoutMutation(graph,
        R"json({"version": 2, "nodes": [42]})json");
    expectRejectedWithoutMutation(graph, R"json({
        "version": 2,
        "nodes": [{"id": "", "type": "tvg"}]
    })json");
    expectRejectedWithoutMutation(graph, R"json({
        "version": 2,
        "nodes": [
            {"id": "dup", "type": "tvg"},
            {"id": "dup", "type": "tvg"}
        ]
    })json");
    expectRejectedWithoutMutation(graph, R"json({
        "version": 2,
        "nodes": [{"id": "bad_params", "type": "tvg", "params": []}]
    })json");
    expectRejectedWithoutMutation(graph, R"json({
        "version": 2,
        "nodes": [{"id": "bad_params", "type": "tvg",
                   "params": {"spreading": "wide"}}]
    })json");
    expectRejectedWithoutMutation(graph, R"json({
        "version": 2,
        "nodes": [{"id": "bad_params", "type": "tvg",
                   "params": {"unknown_setting": 1}}]
    })json");

    expectRejectedWithoutMutation(graph, R"json({
        "version": 2,
        "nodes": [{"id": "a", "type": "sss_input"}],
        "edges": [{"from": "a", "to": "missing"}]
    })json");
    expectRejectedWithoutMutation(graph, R"json({
        "version": 2,
        "nodes": [
            {"id": "a", "type": "sss_input"},
            {"id": "b", "type": "merge"}
        ],
        "edges": [{"from": "a", "to": "b", "to_port": 2}]
    })json");
    expectRejectedWithoutMutation(graph, R"json({
        "version": 2,
        "nodes": [{"id": "a", "type": "tvg"}],
        "layout": [{"id": "missing", "x": 1, "y": 2}]
    })json");
    expectRejectedWithoutMutation(graph, R"json({
        "version": 2,
        "nodes": [{"id": "a", "type": "tvg"}],
        "groups": [{"id": "g", "label": "Broken", "node_ids": ["missing"]}]
    })json");
    expectRejectedWithoutMutation(graph, R"json({
        "version": 2,
        "nodes": [{"id": "a", "type": "tvg"}],
        "groups": [
            {"id": "g", "label": "One", "node_ids": ["a"]},
            {"id": "g", "label": "Two", "node_ids": []}
        ]
    })json");
}

} // namespace

int main()
{
    dolphin::pipeline::registerBuiltinNodes();

    testRoundTripPreservesPortsGroupsAndLayout();
    testUnknownTypeDoesNotMutateExistingGraph();
    testMalformedContentAndDanglingReferencesAreRejected();

    std::printf("%d passed, %d failed\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
