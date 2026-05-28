#include "util/Json.h"
#include <sstream>
#include <cctype>
#include <cstdint>

namespace dolphin::util {

// ── Statics ───────────────────────────────────────────────────────────────────

static const JsonValue s_null_value;

const JsonValue& JsonValue::get(const std::string& key) const
{
    auto it = m_obj.find(key);
    return it != m_obj.end() ? it->second : s_null_value;
}

// ── Serialiser ────────────────────────────────────────────────────────────────

static std::string escapeStr(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:   out += static_cast<char>(c);
        }
    }
    out += '"';
    return out;
}

std::string JsonValue::dump(int indent, int depth) const
{
    std::string pad(static_cast<size_t>(indent * depth), ' ');
    std::string cpd(static_cast<size_t>(indent * (depth + 1)), ' ');

    switch (m_type) {
    case Type::Null:   return "null";
    case Type::Bool:   return m_bool ? "true" : "false";
    case Type::Number: {
        // Emit without trailing .000000 noise when the value is integral
        if (m_num == static_cast<double>(static_cast<int64_t>(m_num))
            && m_num >= -1e15 && m_num <= 1e15) {
            return std::to_string(static_cast<int64_t>(m_num));
        }
        std::ostringstream ss;
        ss << m_num;
        // Ensure there is a decimal point so JSON parsers treat it as float
        std::string s = ss.str();
        if (s.find('.') == std::string::npos && s.find('e') == std::string::npos)
            s += ".0";
        return s;
    }
    case Type::String: return escapeStr(m_str);
    case Type::Array: {
        if (m_arr.empty()) return "[]";
        std::string out = "[\n";
        for (size_t i = 0; i < m_arr.size(); ++i) {
            out += cpd + m_arr[i].dump(indent, depth + 1);
            if (i + 1 < m_arr.size()) out += ',';
            out += '\n';
        }
        out += pad + ']';
        return out;
    }
    case Type::Object: {
        if (m_obj.empty()) return "{}";
        std::string out = "{\n";
        size_t i = 0;
        for (auto& [k, v] : m_obj) {
            out += cpd + escapeStr(k) + ": " + v.dump(indent, depth + 1);
            if (++i < m_obj.size()) out += ',';
            out += '\n';
        }
        out += pad + '}';
        return out;
    }
    }
    return "null";
}

// ── Parser ────────────────────────────────────────────────────────────────────

struct Parser {
    const std::string& src;
    size_t pos = 0;

    void skipWs() {
        while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos])))
            ++pos;
    }
    char peek() { skipWs(); return pos < src.size() ? src[pos]    : '\0'; }
    char take() { skipWs(); return pos < src.size() ? src[pos++]  : '\0'; }

    std::string parseString() {
        take(); // opening "
        std::string s;
        while (pos < src.size()) {
            char c = src[pos++];
            if (c == '"') break;
            if (c == '\\' && pos < src.size()) {
                char e = src[pos++];
                switch (e) {
                case '"': s += '"';  break; case '\\': s += '\\'; break;
                case '/': s += '/';  break; case 'n':  s += '\n'; break;
                case 'r': s += '\r'; break; case 't':  s += '\t'; break;
                default:  s += e;
                }
            } else { s += c; }
        }
        return s;
    }

    JsonValue parseValue() {
        char c = peek();
        if (c == '"') return JsonValue(parseString());
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == 't') { pos += 4; return JsonValue(true);  } // true
        if (c == 'f') { pos += 5; return JsonValue(false); } // false
        if (c == 'n') { pos += 4; return JsonValue();      } // null
        // Number
        skipWs();
        std::string ns;
        if (pos < src.size() && src[pos] == '-') ns += src[pos++];
        while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos])))
            ns += src[pos++];
        if (pos < src.size() && src[pos] == '.') {
            ns += src[pos++];
            while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos])))
                ns += src[pos++];
        }
        if (pos < src.size() && (src[pos] == 'e' || src[pos] == 'E')) {
            ns += src[pos++];
            if (pos < src.size() && (src[pos] == '+' || src[pos] == '-'))
                ns += src[pos++];
            while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos])))
                ns += src[pos++];
        }
        if (ns.empty()) return JsonValue();
        return JsonValue(std::stod(ns));
    }

    JsonValue parseObject() {
        take(); // {
        JsonValue obj = JsonValue::object();
        skipWs();
        if (peek() == '}') { take(); return obj; }
        while (pos < src.size()) {
            skipWs();
            std::string key = parseString();
            skipWs(); take(); // :
            obj[key] = parseValue();
            skipWs();
            char sep = peek();
            if (sep == '}') { take(); break; }
            if (sep == ',') { take(); }
        }
        return obj;
    }

    JsonValue parseArray() {
        take(); // [
        JsonValue arr = JsonValue::array();
        skipWs();
        if (peek() == ']') { take(); return arr; }
        while (pos < src.size()) {
            arr.push(parseValue());
            skipWs();
            char sep = peek();
            if (sep == ']') { take(); break; }
            if (sep == ',') { take(); }
        }
        return arr;
    }
};

JsonValue parseJson(const std::string& text)
{
    Parser p{text};
    return p.parseValue();
}

} // namespace dolphin::util
