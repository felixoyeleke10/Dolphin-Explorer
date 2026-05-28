// NodeGraph.Serialization.cpp — NodeGraph::toJson() and NodeGraph::fromJson().
#include "pipeline/NodeGraph.h"
#include "pipeline/NodeRegistry.h"
#include "util/Json.h"

#include <variant>

namespace dolphin::pipeline {

static util::JsonValue valueToJson(const Value& v)
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

static Value jsonToValue(const util::JsonValue& jv, const Value& default_val)
{
    return std::visit([&jv](const auto& def) -> Value {
        using T = std::decay_t<decltype(def)>;
        if (jv.isNull()) return def;
        if constexpr (std::is_same_v<T, bool>)        return jv.asBool();
        if constexpr (std::is_same_v<T, int>)         return jv.asInt();
        if constexpr (std::is_same_v<T, float>)       return jv.asFloat();
        if constexpr (std::is_same_v<T, double>)      return jv.asDouble();
        if constexpr (std::is_same_v<T, std::string>) return jv.asString();
        return def;
    }, default_val);
}

std::string NodeGraph::toJson() const
{
    util::JsonValue root = util::JsonValue::object();
    root["version"] = util::JsonValue(1);

    util::JsonValue nodes_arr = util::JsonValue::array();
    for (auto& node : m_nodes) {
        util::JsonValue n = util::JsonValue::object();
        n["id"]   = util::JsonValue(node->instance_id);
        n["type"] = util::JsonValue(node->typeId());

        // Serialise all current params (merge schema defaults + instance overrides)
        auto schema = node->schema();
        util::JsonValue params = util::JsonValue::object();

        for (auto& [key, sp] : schema.params)
            params[key] = valueToJson(sp.default_value);

        for (auto& [key, val] : node->params)
            params[key] = valueToJson(val);

        n["params"] = std::move(params);
        nodes_arr.push(std::move(n));
    }
    root["nodes"] = std::move(nodes_arr);

    util::JsonValue edges_arr = util::JsonValue::array();
    for (auto& edge : m_edges) {
        util::JsonValue e = util::JsonValue::object();
        e["from"] = util::JsonValue(edge.from_node);
        e["to"]   = util::JsonValue(edge.to_node);
        edges_arr.push(std::move(e));
    }
    root["edges"] = std::move(edges_arr);

    util::JsonValue layout_arr = util::JsonValue::array();
    for (const auto& [id, pos] : m_positions) {
        util::JsonValue entry = util::JsonValue::object();
        entry["id"] = util::JsonValue(id);
        entry["x"]  = util::JsonValue(static_cast<double>(pos.first));
        entry["y"]  = util::JsonValue(static_cast<double>(pos.second));
        layout_arr.push(std::move(entry));
    }
    root["layout"] = std::move(layout_arr);

    return root.dump();
}

bool NodeGraph::fromJson(const std::string& json)
{
    util::JsonValue root = util::parseJson(json);
    if (!root.isObject()) return false;

    m_nodes.clear();
    m_edges.clear();
    m_dirty.clear();
    m_cache.clear();
    m_positions.clear();

    auto& registry = NodeRegistry::instance();

    for (auto& jn : root.get("nodes").elements()) {
        std::string type_id     = jn.get("type").asString();
        std::string instance_id = jn.get("id").asString();

        NodePtr node = registry.create(type_id);
        if (!node) continue;

        node->instance_id = instance_id;

        auto schema = node->schema();
        for (auto& [key, sp] : schema.params)
            node->params[key] = sp.default_value;

        auto& params_json = jn.get("params");
        if (params_json.isObject()) {
            for (auto& [key, jv] : params_json.items()) {
                if (node->params.count(key)) {
                    node->params[key] = jsonToValue(jv, node->params[key]);
                }
            }
        }

        addNode(node);
    }

    for (auto& je : root.get("edges").elements()) {
        std::string from = je.get("from").asString();
        std::string to   = je.get("to").asString();
        if (!from.empty() && !to.empty() && !addEdge(from, to)) {
            m_nodes.clear();
            m_edges.clear();
            m_dirty.clear();
            m_cache.clear();
            return false;
        }
    }

    for (auto& jpos : root.get("layout").elements()) {
        const std::string id = jpos.get("id").asString();
        const float x = jpos.get("x").asFloat();
        const float y = jpos.get("y").asFloat();
        if (!id.empty()) setNodePosition(id, x, y);
    }

    return true;
}

} // namespace dolphin::pipeline
