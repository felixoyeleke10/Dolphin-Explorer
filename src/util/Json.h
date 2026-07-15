#pragma once
#include <cmath>
#include <limits>
#include <string>
#include <vector>
#include <map>

namespace dolphin::util {

// Minimal JSON value — covers everything the project manifest and graph
// serializer needs. The parser validates the complete grammar; numbers remain
// IEEE-754 doubles, so integer identities and ranges need caller validation.
class JsonValue {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    JsonValue()                          : m_type(Type::Null)   {}
    JsonValue(bool v)                    : m_type(Type::Bool),   m_bool(v) {}
    JsonValue(int v)                     : m_type(Type::Number), m_num(static_cast<double>(v)) {}
    JsonValue(float v)                   : m_type(Type::Number), m_num(static_cast<double>(v)) {}
    JsonValue(double v)                  : m_type(Type::Number), m_num(v) {}
    JsonValue(const char* v)             : m_type(Type::String), m_str(v) {}
    JsonValue(const std::string& v)      : m_type(Type::String), m_str(v) {}

    static JsonValue object() { JsonValue v; v.m_type = Type::Object; return v; }
    static JsonValue array()  { JsonValue v; v.m_type = Type::Array;  return v; }

    Type type()     const { return m_type; }
    bool isNull()   const { return m_type == Type::Null; }
    bool isBool()   const { return m_type == Type::Bool; }
    bool isNumber() const { return m_type == Type::Number; }
    bool isString() const { return m_type == Type::String; }
    bool isArray()  const { return m_type == Type::Array; }
    bool isObject() const { return m_type == Type::Object; }

    bool               asBool()   const { return m_bool; }
    double             asDouble() const { return m_num; }
    int asInt() const {
        if (!std::isfinite(m_num)) return 0;
        if (m_num <= static_cast<double>(std::numeric_limits<int>::min()))
            return std::numeric_limits<int>::min();
        if (m_num >= static_cast<double>(std::numeric_limits<int>::max()))
            return std::numeric_limits<int>::max();
        return static_cast<int>(m_num);
    }
    float asFloat() const {
        if (!std::isfinite(m_num)) return 0.0f;
        constexpr double max_float =
            static_cast<double>(std::numeric_limits<float>::max());
        if (m_num <= -max_float) return -std::numeric_limits<float>::max();
        if (m_num >= max_float) return std::numeric_limits<float>::max();
        return static_cast<float>(m_num);
    }
    const std::string& asString() const { return m_str; }

    // Object interface
    bool has(const std::string& key) const { return m_obj.count(key) > 0; }
    const JsonValue& get(const std::string& key) const;
    JsonValue& operator[](const std::string& key) { m_type = Type::Object; return m_obj[key]; }
    const std::map<std::string, JsonValue>& items() const { return m_obj; }

    // Array interface
    void push(JsonValue v) { m_type = Type::Array; m_arr.push_back(std::move(v)); }
    size_t           size()         const { return m_arr.size(); }
    const JsonValue& at(size_t i)   const { return m_arr[i]; }
    const std::vector<JsonValue>&   elements() const { return m_arr; }

    // Serialise to pretty-printed JSON string
    std::string dump(int indent = 2, int depth = 0) const;

private:
    Type        m_type = Type::Null;
    bool        m_bool = false;
    double      m_num  = 0.0;
    std::string m_str;
    std::vector<JsonValue>           m_arr;
    std::map<std::string, JsonValue> m_obj;
};

// Returns Null on parse error
JsonValue parseJson(const std::string& text);

} // namespace dolphin::util
