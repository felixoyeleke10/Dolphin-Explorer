// NodeGraph.Serialization.cpp — NodeGraph::toJson() and NodeGraph::fromJson().
#include "pipeline/NodeGraph.h"
#include "pipeline/NodeRegistry.h"
#include "util/Json.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <set>
#include <string_view>
#include <type_traits>
#include <variant>

namespace dolphin::pipeline {
namespace {

constexpr int kGraphFormatVersion = 2;

util::JsonValue valueToJson(const Value& v)
{
    return std::visit([](const auto& x) -> util::JsonValue {
        using T = std::decay_t<decltype(x)>;
        if constexpr (std::is_same_v<T, bool>)        return util::JsonValue(x);
        if constexpr (std::is_same_v<T, int>)         return util::JsonValue(x);
        if constexpr (std::is_same_v<T, float>)       return util::JsonValue(x);
        if constexpr (std::is_same_v<T, double>)      return util::JsonValue(x);
        if constexpr (std::is_same_v<T, std::string>) return util::JsonValue(x);
        return util::JsonValue();
    }, v);
}

bool hasOnlyKeys(const util::JsonValue& object,
                 std::initializer_list<std::string_view> allowed)
{
    if (!object.isObject()) return false;
    for (const auto& [key, value] : object.items()) {
        (void)value;
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end())
            return false;
    }
    return true;
}

bool readIntegral(const util::JsonValue& json, int& value)
{
    if (!json.isNumber()) return false;
    const double number = json.asDouble();
    if (!std::isfinite(number) || std::trunc(number) != number
            || number < static_cast<double>(std::numeric_limits<int>::min())
            || number > static_cast<double>(std::numeric_limits<int>::max())) {
        return false;
    }
    value = static_cast<int>(number);
    return true;
}

template <typename T>
bool inSchemaRange(T value, const NodeParam& spec)
{
    const auto* minimum = std::get_if<T>(&spec.min_value);
    const auto* maximum = std::get_if<T>(&spec.max_value);
    return minimum && maximum && value >= *minimum && value <= *maximum;
}

bool jsonToValue(const util::JsonValue& json,
                 const NodeParam& spec,
                 Value& value)
{
    return std::visit([&](const auto& defaultValue) -> bool {
        using T = std::decay_t<decltype(defaultValue)>;

        if constexpr (std::is_same_v<T, bool>) {
            if (!json.isBool()) return false;
            value = json.asBool();
            return true;
        } else if constexpr (std::is_same_v<T, int>) {
            int parsed = 0;
            if (!readIntegral(json, parsed) || !inSchemaRange(parsed, spec))
                return false;
            value = parsed;
            return true;
        } else if constexpr (std::is_same_v<T, float>) {
            if (!json.isNumber()) return false;
            const double parsed = json.asDouble();
            constexpr double maxFloat =
                static_cast<double>(std::numeric_limits<float>::max());
            if (!std::isfinite(parsed) || parsed < -maxFloat || parsed > maxFloat)
                return false;
            const float converted = static_cast<float>(parsed);
            if (!inSchemaRange(converted, spec)) return false;
            value = converted;
            return true;
        } else if constexpr (std::is_same_v<T, double>) {
            if (!json.isNumber()) return false;
            const double parsed = json.asDouble();
            if (!std::isfinite(parsed) || !inSchemaRange(parsed, spec))
                return false;
            value = parsed;
            return true;
        } else if constexpr (std::is_same_v<T, std::string>) {
            if (!json.isString()) return false;
            const std::string& parsed = json.asString();
            if (!spec.options.empty()
                    && std::find(spec.options.begin(), spec.options.end(), parsed)
                        == spec.options.end()) {
                return false;
            }
            value = parsed;
            return true;
        }
        return false;
    }, spec.default_value);
}

bool readPositionCoordinate(const util::JsonValue& json, float& value)
{
    if (!json.isNumber()) return false;
    const double number = json.asDouble();
    constexpr double maxFloat =
        static_cast<double>(std::numeric_limits<float>::max());
    if (!std::isfinite(number) || number < -maxFloat || number > maxFloat)
        return false;
    value = static_cast<float>(number);
    return true;
}

} // namespace

std::string NodeGraph::toJson() const
{
    util::JsonValue root = util::JsonValue::object();
    root["version"] = util::JsonValue(kGraphFormatVersion);

    util::JsonValue nodesArr = util::JsonValue::array();
    for (const auto& node : m_nodes) {
        util::JsonValue serializedNode = util::JsonValue::object();
        serializedNode["id"]   = util::JsonValue(node->instance_id);
        serializedNode["type"] = util::JsonValue(node->typeId());

        // Serialize all current params (merge schema defaults + instance overrides).
        const auto schema = node->schema();
        util::JsonValue params = util::JsonValue::object();
        for (const auto& [key, spec] : schema.params)
            params[key] = valueToJson(spec.default_value);
        for (const auto& [key, value] : node->params)
            params[key] = valueToJson(value);

        serializedNode["params"] = std::move(params);
        nodesArr.push(std::move(serializedNode));
    }
    root["nodes"] = std::move(nodesArr);

    util::JsonValue edgesArr = util::JsonValue::array();
    for (const auto& edge : m_edges) {
        util::JsonValue serializedEdge = util::JsonValue::object();
        serializedEdge["from"] = util::JsonValue(edge.from_node);
        serializedEdge["to"] = util::JsonValue(edge.to_node);
        serializedEdge["to_port"] = util::JsonValue(edge.to_port);
        edgesArr.push(std::move(serializedEdge));
    }
    root["edges"] = std::move(edgesArr);

    util::JsonValue layoutArr = util::JsonValue::array();
    for (const auto& [id, position] : m_positions) {
        util::JsonValue entry = util::JsonValue::object();
        entry["id"] = util::JsonValue(id);
        entry["x"] = util::JsonValue(static_cast<double>(position.first));
        entry["y"] = util::JsonValue(static_cast<double>(position.second));
        layoutArr.push(std::move(entry));
    }
    root["layout"] = std::move(layoutArr);

    util::JsonValue groupsArr = util::JsonValue::array();
    for (const auto& group : m_groups) {
        util::JsonValue serializedGroup = util::JsonValue::object();
        serializedGroup["id"] = util::JsonValue(group.id);
        serializedGroup["label"] = util::JsonValue(group.label);
        util::JsonValue nodeIds = util::JsonValue::array();
        for (const auto& nodeId : group.node_ids)
            nodeIds.push(util::JsonValue(nodeId));
        serializedGroup["node_ids"] = std::move(nodeIds);
        groupsArr.push(std::move(serializedGroup));
    }
    root["groups"] = std::move(groupsArr);

    return root.dump();
}

bool NodeGraph::fromJson(const std::string& json)
{
    const util::JsonValue root = util::parseJson(json);
    if (!root.isObject()
            || !hasOnlyKeys(root, {"version", "nodes", "edges", "layout", "groups"})) {
        return false;
    }

    if (root.has("version")) {
        int version = 0;
        if (!readIntegral(root.get("version"), version)
                || version < 1 || version > kGraphFormatVersion) {
            return false;
        }
    }

    // Nodes are the only required collection. All other collections and the
    // version field were absent from some legacy graph documents.
    if (!root.has("nodes") || !root.get("nodes").isArray()) return false;

    NodeGraph parsedGraph;
    auto& registry = NodeRegistry::instance();
    std::set<std::string> nodeIds;

    for (const auto& serializedNode : root.get("nodes").elements()) {
        if (!serializedNode.isObject()
                || !hasOnlyKeys(serializedNode, {"id", "type", "params"})
                || !serializedNode.get("id").isString()
                || !serializedNode.get("type").isString()) {
            return false;
        }

        const std::string instanceId = serializedNode.get("id").asString();
        const std::string typeId = serializedNode.get("type").asString();
        if (instanceId.empty() || typeId.empty()
                || !nodeIds.insert(instanceId).second) {
            return false;
        }

        NodePtr node = registry.create(typeId);
        if (!node) return false;
        node->instance_id = instanceId;

        const auto schema = node->schema();
        for (const auto& [key, spec] : schema.params)
            node->params[key] = spec.default_value;

        if (serializedNode.has("params")) {
            const auto& paramsJson = serializedNode.get("params");
            if (!paramsJson.isObject()) return false;
            for (const auto& [key, serializedValue] : paramsJson.items()) {
                const auto specIt = schema.params.find(key);
                if (specIt == schema.params.end()) return false;
                Value parsedValue;
                if (!jsonToValue(serializedValue, specIt->second, parsedValue))
                    return false;
                node->params[key] = std::move(parsedValue);
            }
        }

        parsedGraph.addNode(std::move(node));
    }

    if (root.has("edges")) {
        const auto& edgesJson = root.get("edges");
        if (!edgesJson.isArray()) return false;

        std::set<std::pair<std::string, int>> occupiedInputPorts;
        for (const auto& serializedEdge : edgesJson.elements()) {
            if (!serializedEdge.isObject()
                    || !hasOnlyKeys(serializedEdge, {"from", "to", "to_port"})
                    || !serializedEdge.get("from").isString()
                    || !serializedEdge.get("to").isString()) {
                return false;
            }

            const std::string from = serializedEdge.get("from").asString();
            const std::string to = serializedEdge.get("to").asString();
            int toPort = 0; // Legacy edges had no explicit port.
            if (serializedEdge.has("to_port")
                    && !readIntegral(serializedEdge.get("to_port"), toPort)) {
                return false;
            }

            const NodePtr fromNode = parsedGraph.findNode(from);
            const NodePtr toNode = parsedGraph.findNode(to);
            if (from.empty() || to.empty() || !fromNode || !toNode
                    || toPort < 0 || toPort >= toNode->inputCount()
                    || !occupiedInputPorts.emplace(to, toPort).second
                    || !parsedGraph.addEdge(from, to, toPort)) {
                return false;
            }
        }
    }

    if (root.has("layout")) {
        const auto& layoutJson = root.get("layout");
        if (!layoutJson.isArray()) return false;

        std::set<std::string> positionedNodes;
        for (const auto& serializedPosition : layoutJson.elements()) {
            if (!serializedPosition.isObject()
                    || !hasOnlyKeys(serializedPosition, {"id", "x", "y"})
                    || !serializedPosition.get("id").isString()) {
                return false;
            }

            const std::string id = serializedPosition.get("id").asString();
            float x = 0.0f;
            float y = 0.0f;
            if (id.empty() || !parsedGraph.findNode(id)
                    || !positionedNodes.insert(id).second
                    || !readPositionCoordinate(serializedPosition.get("x"), x)
                    || !readPositionCoordinate(serializedPosition.get("y"), y)) {
                return false;
            }
            parsedGraph.setNodePosition(id, x, y);
        }
    }

    if (root.has("groups")) {
        const auto& groupsJson = root.get("groups");
        if (!groupsJson.isArray()) return false;

        std::set<std::string> groupIds;
        for (const auto& serializedGroup : groupsJson.elements()) {
            if (!serializedGroup.isObject()
                    || !hasOnlyKeys(serializedGroup, {"id", "label", "node_ids"})
                    || !serializedGroup.get("id").isString()
                    || !serializedGroup.get("label").isString()
                    || !serializedGroup.get("node_ids").isArray()) {
                return false;
            }

            NodeGroup group;
            group.id = serializedGroup.get("id").asString();
            group.label = serializedGroup.get("label").asString();
            if (group.id.empty() || !groupIds.insert(group.id).second)
                return false;

            std::set<std::string> groupedNodeIds;
            for (const auto& serializedNodeId
                    : serializedGroup.get("node_ids").elements()) {
                if (!serializedNodeId.isString()) return false;
                const std::string nodeId = serializedNodeId.asString();
                if (nodeId.empty() || !parsedGraph.findNode(nodeId)
                        || !groupedNodeIds.insert(nodeId).second) {
                    return false;
                }
                group.node_ids.push_back(nodeId);
            }
            parsedGraph.addGroup(std::move(group));
        }
    }

    // Commit only after every object and cross-reference has been validated.
    m_nodes.swap(parsedGraph.m_nodes);
    m_edges.swap(parsedGraph.m_edges);
    m_groups.swap(parsedGraph.m_groups);
    m_dirty.swap(parsedGraph.m_dirty);
    m_cache.swap(parsedGraph.m_cache);
    m_positions.swap(parsedGraph.m_positions);
    return true;
}

} // namespace dolphin::pipeline
