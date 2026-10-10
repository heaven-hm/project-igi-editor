#include "mcp_json.h"

#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace mcp {
namespace {

bool IsContinuation(unsigned char c) {
    return (c & 0xc0u) == 0x80u;
}

bool DecodeUtf8(std::string_view input, size_t& index, uint32_t& codepoint) {
    const auto byte = [&](size_t offset) {
        return static_cast<unsigned char>(input[index + offset]);
    };
    const size_t remaining = input.size() - index;
    const unsigned char first = byte(0);
    if (first <= 0x7fu) {
        codepoint = first;
        ++index;
        return true;
    }
    if (first >= 0xc2u && first <= 0xdfu) {
        if (remaining < 2 || !IsContinuation(byte(1))) return false;
        codepoint = ((first & 0x1fu) << 6) | (byte(1) & 0x3fu);
        index += 2;
        return true;
    }
    if (first >= 0xe0u && first <= 0xefu) {
        if (remaining < 3 || !IsContinuation(byte(1)) || !IsContinuation(byte(2))) return false;
        if (first == 0xe0u && byte(1) < 0xa0u) return false;
        if (first == 0xedu && byte(1) >= 0xa0u) return false;
        codepoint = ((first & 0x0fu) << 12) |
                    ((byte(1) & 0x3fu) << 6) |
                    (byte(2) & 0x3fu);
        index += 3;
        return true;
    }
    if (first >= 0xf0u && first <= 0xf4u) {
        if (remaining < 4 || !IsContinuation(byte(1)) ||
            !IsContinuation(byte(2)) || !IsContinuation(byte(3))) return false;
        if (first == 0xf0u && byte(1) < 0x90u) return false;
        if (first == 0xf4u && byte(1) >= 0x90u) return false;
        codepoint = ((first & 0x07u) << 18) |
                    ((byte(1) & 0x3fu) << 12) |
                    ((byte(2) & 0x3fu) << 6) |
                    (byte(3) & 0x3fu);
        index += 4;
        return true;
    }
    return false;
}

bool IsValidUtf8(std::string_view value) {
    size_t index = 0;
    uint32_t codepoint = 0;
    while (index < value.size()) {
        if (!DecodeUtf8(value, index, codepoint)) return false;
    }
    return true;
}

void AppendUtf8(std::string& output, uint32_t codepoint) {
    if (codepoint <= 0x7fu) {
        output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ffu) {
        output.push_back(static_cast<char>(0xc0u | (codepoint >> 6)));
        output.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
    } else if (codepoint <= 0xffffu) {
        output.push_back(static_cast<char>(0xe0u | (codepoint >> 12)));
        output.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3fu)));
        output.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
    } else {
        output.push_back(static_cast<char>(0xf0u | (codepoint >> 18)));
        output.push_back(static_cast<char>(0x80u | ((codepoint >> 12) & 0x3fu)));
        output.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3fu)));
        output.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
    }
}

class Parser {
public:
    explicit Parser(std::string_view input) : input_(input) {}

    struct JsonParseFailure {
        const char* message;
        size_t offset;
    };

    JsonValue Parse() {
        SkipWhitespace();
        JsonValue value = ParseValue(0);
        SkipWhitespace();
        if (index_ != input_.size()) Fail("trailing input");
        return value;
    }

private:
    std::string_view input_;
    size_t index_ = 0;

    [[noreturn]] void Fail(const char* message) const {
        throw JsonParseFailure{message, index_};
    }

    void SkipWhitespace() {
        while (index_ < input_.size()) {
            const char c = input_[index_];
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
            ++index_;
        }
    }

    bool Consume(char expected) {
        if (index_ < input_.size() && input_[index_] == expected) {
            ++index_;
            return true;
        }
        return false;
    }

    JsonValue ParseValue(size_t depth) {
        SkipWhitespace();
        if (index_ >= input_.size()) Fail("unexpected end of input");
        switch (input_[index_]) {
        case 'n': return ParseLiteral("null", JsonValue(nullptr));
        case 't': return ParseLiteral("true", JsonValue(true));
        case 'f': return ParseLiteral("false", JsonValue(false));
        case '"': return JsonValue(ParseString());
        case '[': return ParseArray(depth);
        case '{': return ParseObject(depth);
        default:
            if (input_[index_] == '-' || (input_[index_] >= '0' && input_[index_] <= '9'))
                return ParseNumber();
            Fail("invalid value");
        }
    }

    JsonValue ParseLiteral(std::string_view literal, JsonValue value) {
        if (input_.substr(index_, literal.size()) != literal) Fail("invalid literal");
        index_ += literal.size();
        return value;
    }

    static int HexValue(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    uint32_t ParseUnicodeEscape() {
        if (index_ + 4 > input_.size()) Fail("incomplete unicode escape");
        uint32_t value = 0;
        for (size_t i = 0; i < 4; ++i) {
            const int hex = HexValue(input_[index_ + i]);
            if (hex < 0) Fail("invalid unicode escape");
            value = (value << 4) | static_cast<uint32_t>(hex);
        }
        index_ += 4;
        return value;
    }

    std::string ParseString() {
        if (!Consume('"')) Fail("expected string");
        std::string output;
        while (index_ < input_.size()) {
            const unsigned char c = static_cast<unsigned char>(input_[index_++]);
            if (c == '"') return output;
            if (c < 0x20u) Fail("unescaped control character in string");
            if (c == '\\') {
                if (index_ >= input_.size()) Fail("incomplete string escape");
                switch (input_[index_++]) {
                case '"': output.push_back('"'); break;
                case '\\': output.push_back('\\'); break;
                case '/': output.push_back('/'); break;
                case 'b': output.push_back('\b'); break;
                case 'f': output.push_back('\f'); break;
                case 'n': output.push_back('\n'); break;
                case 'r': output.push_back('\r'); break;
                case 't': output.push_back('\t'); break;
                case 'u': {
                    const uint32_t first = ParseUnicodeEscape();
                    uint32_t codepoint = first;
                    if (first >= 0xd800u && first <= 0xdbffu) {
                        if (index_ + 6 > input_.size() || input_[index_] != '\\' || input_[index_ + 1] != 'u')
                            Fail("unpaired high surrogate");
                        index_ += 2;
                        const uint32_t second = ParseUnicodeEscape();
                        if (second < 0xdc00u || second > 0xdfffu) Fail("invalid surrogate pair");
                        codepoint = 0x10000u + ((first - 0xd800u) << 10) + (second - 0xdc00u);
                    } else if (first >= 0xdc00u && first <= 0xdfffu) {
                        Fail("unpaired low surrogate");
                    }
                    AppendUtf8(output, codepoint);
                    break;
                }
                default: Fail("invalid string escape");
                }
                continue;
            }
            if (c < 0x80u) {
                output.push_back(static_cast<char>(c));
                continue;
            }
            const size_t first_index = index_ - 1;
            index_ = first_index;
            uint32_t codepoint = 0;
            if (!DecodeUtf8(input_, index_, codepoint)) Fail("invalid UTF-8 in string");
            output.append(input_.substr(first_index, index_ - first_index));
        }
        Fail("unterminated string");
    }

    JsonValue ParseNumber() {
        const size_t start = index_;
        if (Consume('-') && index_ >= input_.size()) Fail("incomplete number");
        if (index_ < input_.size() && input_[index_] == '0') {
            ++index_;
            if (index_ < input_.size() && input_[index_] >= '0' && input_[index_] <= '9')
                Fail("leading zero in number");
        } else {
            if (index_ >= input_.size() || input_[index_] < '1' || input_[index_] > '9') Fail("invalid number");
            while (index_ < input_.size() && input_[index_] >= '0' && input_[index_] <= '9') ++index_;
        }
        if (Consume('.')) {
            const size_t fraction_start = index_;
            while (index_ < input_.size() && input_[index_] >= '0' && input_[index_] <= '9') ++index_;
            if (index_ == fraction_start) Fail("missing fraction digits");
        }
        if (index_ < input_.size() && (input_[index_] == 'e' || input_[index_] == 'E')) {
            ++index_;
            if (index_ < input_.size() && (input_[index_] == '+' || input_[index_] == '-')) ++index_;
            const size_t exponent_start = index_;
            while (index_ < input_.size() && input_[index_] >= '0' && input_[index_] <= '9') ++index_;
            if (index_ == exponent_start) Fail("missing exponent digits");
        }
        const std::string token(input_.substr(start, index_ - start));
        char* end = nullptr;
        const double number = std::strtod(token.c_str(), &end);
        if (end != token.c_str() + token.size() || !std::isfinite(number)) Fail("number is not finite");
        return JsonValue(number);
    }

    JsonValue ParseArray(size_t depth) {
        if (depth >= kMaxJsonNestingDepth) Fail("maximum nesting depth exceeded");
        Consume('[');
        JsonValue::Array values;
        SkipWhitespace();
        if (Consume(']')) return JsonValue(std::move(values));
        while (true) {
            values.push_back(ParseValue(depth + 1));
            SkipWhitespace();
            if (Consume(']')) break;
            if (!Consume(',')) Fail("expected comma in array");
            SkipWhitespace();
            if (index_ < input_.size() && input_[index_] == ']') Fail("trailing comma in array");
        }
        return JsonValue(std::move(values));
    }

    JsonValue ParseObject(size_t depth) {
        if (depth >= kMaxJsonNestingDepth) Fail("maximum nesting depth exceeded");
        Consume('{');
        JsonValue::Object values;
        SkipWhitespace();
        if (Consume('}')) return JsonValue(std::move(values));
        while (true) {
            SkipWhitespace();
            if (index_ >= input_.size() || input_[index_] != '"') Fail("object key must be a string");
            const std::string key = ParseString();
            if (!IsValidUtf8(key)) Fail("invalid UTF-8 in object key");
            SkipWhitespace();
            if (!Consume(':')) Fail("expected colon after object key");
            if (values.contains(key)) Fail("duplicate object key");
            values.emplace(key, ParseValue(depth + 1));
            SkipWhitespace();
            if (Consume('}')) break;
            if (!Consume(',')) Fail("expected comma in object");
            SkipWhitespace();
            if (index_ < input_.size() && input_[index_] == '}') Fail("trailing comma in object");
        }
        return JsonValue(std::move(values));
    }
};

void EnsureOutputBudget(const std::string& output) {
    if (output.size() > kMaxJsonMessageBytes)
        throw std::invalid_argument("JSON output exceeds maximum size");
}

void StringifyString(std::string_view value, std::string& output) {
    if (!IsValidUtf8(value)) throw std::invalid_argument("JSON strings must be valid UTF-8");
    output.push_back('"');
    for (unsigned char c : value) {
        switch (c) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (c < 0x20u) {
                std::ostringstream escaped;
                escaped << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
                output += escaped.str();
            } else {
                output.push_back(static_cast<char>(c));
            }
            break;
        }
    }
    output.push_back('"');
    EnsureOutputBudget(output);
}

void StringifyValue(const JsonValue& value, std::string& output, size_t depth) {
    if (depth > kMaxJsonNestingDepth)
        throw std::invalid_argument("JSON output exceeds maximum nesting depth");
    switch (value.type()) {
    case JsonType::Null: output += "null"; break;
    case JsonType::Boolean: output += value.as_bool() ? "true" : "false"; break;
    case JsonType::Number: {
        std::ostringstream number;
        number.imbue(std::locale::classic());
        number << std::setprecision(std::numeric_limits<double>::max_digits10) << value.as_number();
        output += number.str();
        break;
    }
    case JsonType::String: StringifyString(value.as_string(), output); break;
    case JsonType::Array: {
        output.push_back('[');
        bool first = true;
        for (const auto& child : value.as_array()) {
            if (!first) output.push_back(',');
            first = false;
            StringifyValue(child, output, depth + 1);
        }
        output.push_back(']');
        break;
    }
    case JsonType::Object: {
        output.push_back('{');
        bool first = true;
        for (const auto& [key, child] : value.as_object()) {
            if (!first) output.push_back(',');
            first = false;
            StringifyString(key, output);
            output.push_back(':');
            StringifyValue(child, output, depth + 1);
        }
        output.push_back('}');
        break;
    }
    }
    EnsureOutputBudget(output);
}

}  // namespace

JsonValue::JsonValue() : value_(nullptr) {}
JsonValue::JsonValue(std::nullptr_t) : value_(nullptr) {}
JsonValue::JsonValue(bool value) : value_(value) {}

JsonValue::JsonValue(double value) : value_(value) {
    if (!std::isfinite(value)) throw std::invalid_argument("JSON numbers must be finite");
}

JsonValue::JsonValue(float value) : JsonValue(static_cast<double>(value)) {}

JsonValue::JsonValue(const char* value) : JsonValue(std::string(value ? value : "")) {}

JsonValue::JsonValue(std::string value) : value_(std::move(value)) {
    if (!IsValidUtf8(std::get<std::string>(value_))) throw std::invalid_argument("JSON strings must be valid UTF-8");
}

JsonValue::JsonValue(std::string_view value) : JsonValue(std::string(value)) {}
JsonValue::JsonValue(Array value) : value_(std::move(value)) {}
JsonValue::JsonValue(Object value) : value_(std::move(value)) {}

JsonType JsonValue::type() const noexcept {
    switch (value_.index()) {
    case 0: return JsonType::Null;
    case 1: return JsonType::Boolean;
    case 2: return JsonType::Number;
    case 3: return JsonType::String;
    case 4: return JsonType::Array;
    default: return JsonType::Object;
    }
}

bool JsonValue::is_null() const noexcept { return type() == JsonType::Null; }
bool JsonValue::is_bool() const noexcept { return type() == JsonType::Boolean; }
bool JsonValue::is_number() const noexcept { return type() == JsonType::Number; }
bool JsonValue::is_string() const noexcept { return type() == JsonType::String; }
bool JsonValue::is_array() const noexcept { return type() == JsonType::Array; }
bool JsonValue::is_object() const noexcept { return type() == JsonType::Object; }

bool JsonValue::as_bool() const { return std::get<bool>(value_); }
double JsonValue::as_number() const { return std::get<double>(value_); }
const std::string& JsonValue::as_string() const { return std::get<std::string>(value_); }
const JsonValue::Array& JsonValue::as_array() const { return std::get<Array>(value_); }
JsonValue::Array& JsonValue::as_array() { return std::get<Array>(value_); }
const JsonValue::Object& JsonValue::as_object() const { return std::get<Object>(value_); }
JsonValue::Object& JsonValue::as_object() { return std::get<Object>(value_); }

bool JsonValue::contains(std::string_view key) const {
    if (!is_object()) return false;
    return as_object().find(key) != as_object().end();
}

const JsonValue& JsonValue::at(std::string_view key) const { return as_object().at(std::string(key)); }
JsonValue& JsonValue::at(std::string_view key) { return as_object().at(std::string(key)); }

JsonValue& JsonValue::operator[](std::string_view key) {
    return as_object()[std::string(key)];
}

bool JsonParse(std::string_view input, JsonValue& output, JsonError& error) {
    error = {};
    if (input.size() > kMaxJsonMessageBytes) {
        error.message = "JSON message exceeds maximum size";
        error.offset = kMaxJsonMessageBytes;
        return false;
    }
    try {
        output = Parser(input).Parse();
        return true;
    } catch (const Parser::JsonParseFailure& failure) {
        error.message = failure.message;
        error.offset = failure.offset;
        return false;
    } catch (const std::exception& exception) {
        error.message = exception.what();
        return false;
    }
}

std::string JsonStringify(const JsonValue& value) {
    std::string output;
    StringifyValue(value, output, 0);
    return output;
}

}  // namespace mcp
