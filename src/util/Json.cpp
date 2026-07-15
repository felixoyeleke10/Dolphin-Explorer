#include "util/Json.h"
#include <sstream>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <utility>

namespace dolphin::util {

// -- Statics -------------------------------------------------------------------

static const JsonValue s_null_value;

const JsonValue& JsonValue::get(const std::string& key) const
{
    auto it = m_obj.find(key);
    return it != m_obj.end() ? it->second : s_null_value;
}

// -- Serialiser ----------------------------------------------------------------

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
        default:
            if (c < 0x20) {
                static constexpr char kHex[] = "0123456789abcdef";
                out += "\\u00";
                out += kHex[(c >> 4) & 0x0f];
                out += kHex[c & 0x0f];
            } else {
                out += static_cast<char>(c);
            }
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
        // JSON has no representation for NaN or infinity. Emitting either as
        // a bare token would create a manifest that cannot be read back.
        if (!std::isfinite(m_num))
            return "null";
        // Emit without trailing .000000 noise when the value is integral.
        // Threshold is 2^53 (9.007e15) — the largest integer exactly representable
        // as a double.  This covers microsecond timestamps (~1.7e15 for year 2024)
        // which previously fell through to the lossy ostringstream path.
        if (m_num >= -9.007199254740992e15 && m_num <= 9.007199254740992e15
            && m_num == static_cast<double>(static_cast<int64_t>(m_num))) {
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

// -- Parser --------------------------------------------------------------------

struct Parser {
    const std::string& src;
    size_t pos = 0;
    bool failed = false;

    void skipWs() {
        while (pos < src.size()) {
            const char c = src[pos];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
                break;
            ++pos;
        }
    }
    char peek() {
        skipWs();
        return pos < src.size() ? src[pos] : '\0';
    }

    bool consume(char expected) {
        skipWs();
        if (pos >= src.size() || src[pos] != expected) {
            failed = true;
            return false;
        }
        ++pos;
        return true;
    }

    static int hexDigit(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    bool parseHexQuad(uint32_t& value) {
        if (src.size() - pos < 4) {
            failed = true;
            return false;
        }
        value = 0;
        for (int i = 0; i < 4; ++i) {
            const int digit = hexDigit(src[pos++]);
            if (digit < 0) {
                failed = true;
                return false;
            }
            value = (value << 4) | static_cast<uint32_t>(digit);
        }
        return true;
    }

    static void appendUtf8(std::string& out, uint32_t codepoint) {
        if (codepoint <= 0x7f) {
            out += static_cast<char>(codepoint);
        } else if (codepoint <= 0x7ff) {
            out += static_cast<char>(0xc0 | (codepoint >> 6));
            out += static_cast<char>(0x80 | (codepoint & 0x3f));
        } else if (codepoint <= 0xffff) {
            out += static_cast<char>(0xe0 | (codepoint >> 12));
            out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (codepoint & 0x3f));
        } else {
            out += static_cast<char>(0xf0 | (codepoint >> 18));
            out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f));
            out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (codepoint & 0x3f));
        }
    }

    bool parseString(std::string& out) {
        if (!consume('"'))
            return false;

        while (pos < src.size()) {
            const unsigned char c = static_cast<unsigned char>(src[pos++]);
            if (c == '"')
                return true;
            if (c < 0x20) {
                failed = true;
                return false;
            }
            if (c != '\\') {
                out += static_cast<char>(c);
                continue;
            }

            if (pos >= src.size()) {
                failed = true;
                return false;
            }
            const char escaped = src[pos++];
            switch (escaped) {
            case '"': out += '"';  break;
            case '\\': out += '\\'; break;
            case '/':  out += '/';  break;
            case 'b':  out += '\b'; break;
            case 'f':  out += '\f'; break;
            case 'n':  out += '\n'; break;
            case 'r':  out += '\r'; break;
            case 't':  out += '\t'; break;
            case 'u': {
                uint32_t codepoint = 0;
                if (!parseHexQuad(codepoint))
                    return false;
                if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
                    if (src.size() - pos < 6 || src[pos] != '\\' || src[pos + 1] != 'u') {
                        failed = true;
                        return false;
                    }
                    pos += 2;
                    uint32_t low = 0;
                    if (!parseHexQuad(low))
                        return false;
                    if (low < 0xdc00 || low > 0xdfff) {
                        failed = true;
                        return false;
                    }
                    codepoint = 0x10000
                        + ((codepoint - 0xd800) << 10)
                        + (low - 0xdc00);
                } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
                    failed = true;
                    return false;
                }
                appendUtf8(out, codepoint);
                break;
            }
            default:
                failed = true;
                return false;
            }
        }

        failed = true; // Unterminated string.
        return false;
    }

    bool parseLiteral(const char* literal) {
        for (size_t i = 0; literal[i] != '\0'; ++i) {
            if (pos >= src.size() || src[pos] != literal[i]) {
                failed = true;
                return false;
            }
            ++pos;
        }
        return true;
    }

    JsonValue parseNumber() {
        skipWs();
        const size_t start = pos;

        if (pos < src.size() && src[pos] == '-')
            ++pos;
        if (pos >= src.size()) {
            failed = true;
            return {};
        }

        if (src[pos] == '0') {
            ++pos;
            if (pos < src.size()
                && std::isdigit(static_cast<unsigned char>(src[pos]))) {
                failed = true; // Leading zero.
                return {};
            }
        } else if (src[pos] >= '1' && src[pos] <= '9') {
            do {
                ++pos;
            } while (pos < src.size()
                     && std::isdigit(static_cast<unsigned char>(src[pos])));
        } else {
            failed = true;
            return {};
        }

        if (pos < src.size() && src[pos] == '.') {
            ++pos;
            const size_t fractionStart = pos;
            while (pos < src.size()
                   && std::isdigit(static_cast<unsigned char>(src[pos]))) {
                ++pos;
            }
            if (pos == fractionStart) {
                failed = true;
                return {};
            }
        }

        if (pos < src.size() && (src[pos] == 'e' || src[pos] == 'E')) {
            ++pos;
            if (pos < src.size() && (src[pos] == '+' || src[pos] == '-'))
                ++pos;
            const size_t exponentStart = pos;
            while (pos < src.size()
                   && std::isdigit(static_cast<unsigned char>(src[pos]))) {
                ++pos;
            }
            if (pos == exponentStart) {
                failed = true;
                return {};
            }
        }

        const std::string number = src.substr(start, pos - start);
        size_t converted = 0;
        const double value = std::stod(number, &converted);
        if (converted != number.size() || !std::isfinite(value)) {
            failed = true;
            return {};
        }
        return JsonValue(value);
    }

    JsonValue parseValue() {
        const char c = peek();
        if (c == '"') {
            std::string value;
            return parseString(value) ? JsonValue(value) : JsonValue{};
        }
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == 't') return parseLiteral("true") ? JsonValue(true) : JsonValue{};
        if (c == 'f') return parseLiteral("false") ? JsonValue(false) : JsonValue{};
        if (c == 'n') return parseLiteral("null") ? JsonValue() : JsonValue{};
        if (c == '-' || (c >= '0' && c <= '9'))
            return parseNumber();

        failed = true;
        return {};
    }

    JsonValue parseObject() {
        if (!consume('{'))
            return {};
        JsonValue obj = JsonValue::object();
        if (peek() == '}') {
            consume('}');
            return obj;
        }

        while (!failed) {
            std::string key;
            if (!parseString(key) || !consume(':'))
                return {};

            JsonValue value = parseValue();
            if (failed)
                return {};
            obj[key] = std::move(value);

            const char separator = peek();
            if (separator == '}') {
                consume('}');
                return obj;
            }
            if (separator != ',') {
                failed = true;
                return {};
            }
            consume(',');
        }
        return {};
    }

    JsonValue parseArray() {
        if (!consume('['))
            return {};
        JsonValue arr = JsonValue::array();
        if (peek() == ']') {
            consume(']');
            return arr;
        }

        while (!failed) {
            JsonValue value = parseValue();
            if (failed)
                return {};
            arr.push(std::move(value));

            const char separator = peek();
            if (separator == ']') {
                consume(']');
                return arr;
            }
            if (separator != ',') {
                failed = true;
                return {};
            }
            consume(',');
        }
        return {};
    }
};

JsonValue parseJson(const std::string& text)
{
    try {
        Parser p{text};
        JsonValue value = p.parseValue();
        p.skipWs();
        return !p.failed && p.pos == text.size() ? value : JsonValue{};
    } catch (...) {
        // Malformed or out-of-range input follows the documented Null result
        // contract instead of leaking numeric-conversion exceptions.
        return JsonValue{};
    }
}

} // namespace dolphin::util
