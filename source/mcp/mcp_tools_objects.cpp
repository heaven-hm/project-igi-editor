#include "mcp_tools_objects.h"

#include "../level/qsc_parser.h"
#include "../level/task_schema.h"
#include "mcp_task_id.h"

#include <array>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace mcp {
namespace {

using Object = JsonValue::Object;

JsonValue StringSchema() {
    return Object{{"type", "string"}, {"minLength", 1}, {"maxLength", 256}};
}

JsonValue NumberVectorSchema() {
    return Object{{"type", "array"}, {"minItems", 3}, {"maxItems", 3},
                  {"items", Object{{"type", "number"}}}};
}

Object MutationProperties() {
    return Object{
        {"level", Object{{"type", "integer"}, {"minimum", 1}}},
        {"expected_revision", Object{{"type", "string"}, {"minLength", 1}}},
        {"dry_run", Object{{"type", "boolean"}}},
        {"backup", Object{{"type", "boolean"}}},
    };
}

JsonValue Schema(Object properties, JsonValue::Array required = {}) {
    return Object{{"type", "object"}, {"properties", std::move(properties)},
                  {"required", std::move(required)}, {"additionalProperties", false}};
}

bool HasOnlyKeys(const JsonValue& value,
                 std::initializer_list<std::string_view> allowed) {
    if (!value.is_object()) return false;
    for (const auto& [key, ignored] : value.as_object()) {
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) return false;
    }
    return true;
}

bool ReadInteger(const JsonValue& value, int& result) {
    if (!value.is_number()) return false;
    const double number = value.as_number();
    if (!std::isfinite(number) || std::trunc(number) != number ||
        number < 1.0 || number > static_cast<double>(std::numeric_limits<int>::max())) {
        return false;
    }
    result = static_cast<int>(number);
    return true;
}

bool ReadNonNegativeInteger(const JsonValue& value, int& result) {
    if (!value.is_number()) return false;
    const double number = value.as_number();
    if (!std::isfinite(number) || std::trunc(number) != number ||
        number < 0.0 || number > static_cast<double>(std::numeric_limits<int>::max())) {
        return false;
    }
    result = static_cast<int>(number);
    return true;
}

bool ReadTaskId(const JsonValue& arguments, std::string& task_id) {
    if (!arguments.is_object() || !arguments.contains("task_id") ||
        !arguments.at("task_id").is_string()) return false;
    task_id = arguments.at("task_id").as_string();
    return !task_id.empty() && task_id.size() <= 128;
}

bool ReadNonEmptyString(const JsonValue& value, std::string& result) {
    if (!value.is_string()) return false;
    result = value.as_string();
    if (result.empty() || result.size() > 256) return false;
    for (const unsigned char character : result) {
        if (character < 0x20) return false;
    }
    return true;
}

bool ReadMutationOptions(const JsonValue& arguments, MutationOptions& options,
                         std::string& error) {
    if (!arguments.is_object()) {
        error = "invalid_arguments";
        return false;
    }
    if (arguments.contains("expected_revision")) {
        if (!arguments.at("expected_revision").is_string() ||
            arguments.at("expected_revision").as_string().empty()) {
            error = "invalid_arguments";
            return false;
        }
        options.expected_revision = arguments.at("expected_revision").as_string();
    }
    if (arguments.contains("dry_run")) {
        if (!arguments.at("dry_run").is_bool()) {
            error = "invalid_arguments";
            return false;
        }
        options.dry_run = arguments.at("dry_run").as_bool();
    }
    if (arguments.contains("backup")) {
        if (!arguments.at("backup").is_bool()) {
            error = "invalid_arguments";
            return false;
        }
        options.backup = arguments.at("backup").as_bool();
    }
    error.clear();
    return true;
}

bool ReadFiniteVector(const JsonValue& value, std::array<double, 3>& result) {
    if (!value.is_array() || value.as_array().size() != 3) return false;
    for (std::size_t index = 0; index < 3; ++index) {
        if (!value.as_array()[index].is_number()) return false;
        result[index] = value.as_array()[index].as_number();
        if (!std::isfinite(result[index]) || std::abs(result[index]) > 1.0e12) return false;
    }
    return true;
}

JsonValue Failure(std::string& error, std::string_view code) {
    error.assign(code);
    return JsonValue(nullptr);
}

JsonValue DomainFailure(std::string& error, const std::string& domain_error) {
    error = domain_error.empty() ? "service_error" : domain_error;
    return JsonValue(nullptr);
}

struct ArgSpan {
    std::size_t begin = 0;
    std::size_t end = 0;
};

struct CallSpan {
    std::size_t begin = 0;
    std::size_t open = 0;
    std::size_t end = 0;
    int source_line = 0;
    std::vector<ArgSpan> args;
    std::string type;
    std::string name;
    std::string id;
    std::string parent_id;
};

bool IsIdentifierCharacter(char value) {
    return std::isalnum(static_cast<unsigned char>(value)) || value == '_' || value == '.';
}

std::size_t SkipString(const std::string& source, std::size_t position) {
    ++position;
    while (position < source.size()) {
        if (source[position] == '\\') {
            position += std::min<std::size_t>(2, source.size() - position);
        } else if (source[position++] == '"') {
            break;
        }
    }
    return position;
}

std::size_t SkipTrivia(const std::string& source, std::size_t position) {
    while (position < source.size()) {
        if (std::isspace(static_cast<unsigned char>(source[position]))) {
            ++position;
        } else if (source[position] == '/' && position + 1 < source.size() &&
                   source[position + 1] == '/') {
            position += 2;
            while (position < source.size() && source[position] != '\n') ++position;
        } else if (source[position] == '/' && position + 1 < source.size() &&
                   source[position + 1] == '*') {
            position += 2;
            while (position + 1 < source.size() &&
                   !(source[position] == '*' && source[position + 1] == '/')) ++position;
            if (position + 1 < source.size()) position += 2;
        } else {
            break;
        }
    }
    return position;
}

bool FindClosingParen(const std::string& source, std::size_t open, std::size_t& close) {
    int depth = 0;
    for (std::size_t position = open; position < source.size();) {
        if (source[position] == '"') {
            position = SkipString(source, position);
            continue;
        }
        if (source[position] == '/' && position + 1 < source.size() &&
            source[position + 1] == '/') {
            position = SkipTrivia(source, position);
            continue;
        }
        if (source[position] == '/' && position + 1 < source.size() &&
            source[position + 1] == '*') {
            position = SkipTrivia(source, position);
            continue;
        }
        if (source[position] == '(') ++depth;
        if (source[position] == ')' && --depth == 0) {
            close = position;
            return true;
        }
        ++position;
    }
    return false;
}

std::vector<ArgSpan> SplitArguments(const std::string& source, std::size_t open,
                                    std::size_t close) {
    std::vector<ArgSpan> result;
    std::size_t start = open + 1;
    int depth = 0;
    for (std::size_t position = start; position < close;) {
        if (source[position] == '"') {
            position = SkipString(source, position);
            continue;
        }
        if (source[position] == '/' && position + 1 < close &&
            (source[position + 1] == '/' || source[position + 1] == '*')) {
            position = SkipTrivia(source, position);
            continue;
        }
        if (source[position] == '(' || source[position] == '{') ++depth;
        if (source[position] == ')' || source[position] == '}') --depth;
        if (source[position] == ',' && depth == 0) {
            result.push_back({start, position});
            start = position + 1;
        }
        ++position;
    }
    if (start < close || !result.empty()) result.push_back({start, close});
    return result;
}

std::size_t TrimLeft(const std::string& source, std::size_t begin, std::size_t end) {
    while (begin < end && std::isspace(static_cast<unsigned char>(source[begin]))) ++begin;
    return begin;
}

std::size_t TrimRight(const std::string& source, std::size_t begin, std::size_t end) {
    while (end > begin && std::isspace(static_cast<unsigned char>(source[end - 1]))) --end;
    return end;
}

struct Scalar {
    enum class Kind { Invalid, Number, Boolean, String, Identifier } kind = Kind::Invalid;
    double number = 0.0;
    bool boolean = false;
    std::string text;
};

bool ParseScalar(std::string_view text, Scalar& result) {
    qsc::LexResult lexed = qsc::Lex(std::string(text) + ";");
    if (!lexed.ok) return false;
    qsc::ParseResult parsed = qsc::Parse(lexed.tokens);
    if (!parsed.ok || !parsed.program || parsed.program->children.size() != 1 ||
        parsed.program->children[0]->kind != qsc::NodeKind::ExprStmt) return false;
    const qsc::Node* node = parsed.program->children[0]->children.empty()
                                ? nullptr
                                : parsed.program->children[0]->children[0].get();
    if (!node) return false;
    if (node->kind == qsc::NodeKind::IntLit || node->kind == qsc::NodeKind::FloatLit) {
        result.kind = Scalar::Kind::Number;
        result.number = node->kind == qsc::NodeKind::IntLit
                            ? static_cast<double>(node->i_val)
                            : static_cast<double>(node->f_val);
        return std::isfinite(result.number);
    }
    if (node->kind == qsc::NodeKind::Unary && node->s_val == "-" &&
        node->children.size() == 1) {
        Scalar child;
        if (!ParseScalar(text.substr(text.find('-') + 1), child) ||
            child.kind != Scalar::Kind::Number) return false;
        result.kind = Scalar::Kind::Number;
        result.number = -child.number;
        return std::isfinite(result.number);
    }
    if (node->kind == qsc::NodeKind::BoolLit) {
        result.kind = Scalar::Kind::Boolean;
        result.boolean = node->b_val;
        return true;
    }
    if (node->kind == qsc::NodeKind::StringLit) {
        result.kind = Scalar::Kind::String;
        result.text = node->s_val;
        return true;
    }
    if (node->kind == qsc::NodeKind::IdentLit) {
        result.kind = Scalar::Kind::Identifier;
        result.text = node->s_val;
        return true;
    }
    return false;
}

std::string StableScalarText(const Scalar& value) {
    if (value.kind == Scalar::Kind::Number) {
        return StableNumberText(value.number);
    }
    if (value.kind == Scalar::Kind::Boolean) return value.boolean ? "true" : "false";
    return value.text;
}

std::string AnonymousTaskSignature(const std::string& source, const std::vector<ArgSpan>& args) {
    std::string signature;
    for (std::size_t index = 1; index < args.size(); ++index) {
        const std::size_t begin = TrimLeft(source, args[index].begin, args[index].end);
        const std::size_t end = TrimRight(source, begin, args[index].end);
        signature.push_back('|');
        signature += AnonymousArgumentSignature(std::string_view(source).substr(begin, end - begin));
    }
    return signature;
}

bool ScanTaskCalls(const std::string& source, std::vector<CallSpan>& calls) {
    for (std::size_t position = 0; position < source.size();) {
        if (source[position] == '"') {
            position = SkipString(source, position);
            continue;
        }
        if (source[position] == '/' && position + 1 < source.size() &&
            (source[position + 1] == '/' || source[position + 1] == '*')) {
            position = SkipTrivia(source, position);
            continue;
        }
        if (!std::isalpha(static_cast<unsigned char>(source[position])) &&
            source[position] != '_') {
            ++position;
            continue;
        }
        const std::size_t word_begin = position++;
        while (position < source.size() && IsIdentifierCharacter(source[position])) ++position;
        if (source.substr(word_begin, position - word_begin) != "Task_New") continue;
        const std::size_t open = SkipTrivia(source, position);
        if (open >= source.size() || source[open] != '(') continue;
        std::size_t close = 0;
        if (!FindClosingParen(source, open, close)) return false;
        const int source_line = static_cast<int>(
            std::count(source.begin(), source.begin() + word_begin, '\n')) + 1;
        calls.push_back({word_begin, open, close, source_line,
                         SplitArguments(source, open, close)});
    }

    std::sort(calls.begin(), calls.end(), [](const CallSpan& left, const CallSpan& right) {
        return left.begin < right.begin;
    });
    std::unordered_map<std::string, int> next_suffix;
    std::unordered_set<std::string> used_ids;
    for (std::size_t index = 0; index < calls.size(); ++index) {
        CallSpan& call = calls[index];
        for (std::size_t argument_index = 0; argument_index < call.args.size(); ++argument_index) {
            const auto& argument = call.args[argument_index];
            const std::size_t begin = TrimLeft(source, argument.begin, argument.end);
            const std::size_t end = TrimRight(source, begin, argument.end);
            Scalar value;
            if (begin < end && ParseScalar(std::string_view(source).substr(begin, end - begin), value)) {
                const std::string text = value.kind == Scalar::Kind::String ||
                                                 value.kind == Scalar::Kind::Identifier
                                             ? value.text : StableScalarText(value);
                if (argument_index == 0) call.id = text.empty() ? "anonymous" : text;
                if (argument_index == 1) call.type = text;
                if (argument_index == 2) call.name = text;
            }
        }
        std::size_t parent_end = std::numeric_limits<std::size_t>::max();
        for (std::size_t parent = 0; parent < index; ++parent) {
            if (calls[parent].begin < call.begin && calls[parent].end > call.end &&
                calls[parent].end < parent_end) {
                call.parent_id = calls[parent].id;
                parent_end = calls[parent].end;
            }
        }
        if (call.id == "-1" || call.id == "anonymous") {
            const auto marker = AnonymousTaskMarkerBefore(source, call.begin);
            call.id = marker.value_or(AnonymousTaskId(call.parent_id, AnonymousTaskSignature(source, call.args)));
        }
        call.id = UniqueTaskId(call.id, next_suffix, used_ids);
    }
    return true;
}

CallSpan* FindCall(std::vector<CallSpan>& calls, std::string_view task_id) {
    for (auto& call : calls) {
        if (call.id == task_id) return &call;
    }
    return nullptr;
}

struct Layout {
    std::array<int, 3> position = {-1, -1, -1};
    std::array<int, 3> rotation = {-1, -1, -1};
    int model = -1;
};

Layout LayoutFor(const std::string& type) {
    Layout layout;
    const TaskSchemaNS::TaskSchema* schema = TaskSchemaNS::GetSchema(type);
    if (schema == nullptr) return layout;
    for (const auto& field : *schema) {
        if (field.typeName == "ObjectPos" && field.argCount == 3) {
            for (int component = 0; component < 3; ++component)
                layout.position[component] = field.argOffset + component;
        } else if (field.typeName == "Real32x9" && field.argCount == 3) {
            for (int component = 0; component < 3; ++component)
                layout.rotation[component] = field.argOffset + component;
        } else if (field.name == "Model" && field.argCount == 1 &&
                   (field.typeName == "String16" || field.typeName == "String32" ||
                    field.typeName == "String256" ||
                    field.typeName == "VarString")) {
            layout.model = field.argOffset;
        }
    }
    return layout;
}

bool ValidArg(const CallSpan& call, int index) {
    return index >= 0 && static_cast<std::size_t>(index) < call.args.size();
}

std::size_t ArgBegin(const std::string& source, const ArgSpan& span) {
    return TrimLeft(source, span.begin, span.end);
}

std::size_t ArgEnd(const std::string& source, const ArgSpan& span) {
    const std::size_t begin = ArgBegin(source, span);
    return TrimRight(source, begin, span.end);
}

struct Replacement {
    std::size_t begin = 0;
    std::size_t end = 0;
    std::string text;
    std::string field;
};

enum class ExpectedKind { Any, Number, Boolean, String };

ExpectedKind ExpectedKindForType(std::string_view type_name) {
    if (type_name == "Bool" || type_name == "bool8" || type_name == "Boolean" ||
        type_name == "PushButton")
        return ExpectedKind::Boolean;
    if (type_name == "String16" || type_name == "String32" || type_name == "String256" ||
        type_name == "VarString" || type_name == "String" || type_name == "EnumString32" ||
        type_name == "DropDownCombo" || type_name == "Graph" || type_name == "AnimData")
        return ExpectedKind::String;
    if (type_name == "ObjectPos" || type_name == "Real32x9" || type_name == "RGB" ||
        type_name == "Real32x3" || type_name == "Real64x3" || type_name == "Colour" ||
        type_name == "Real32" || type_name == "Real64" || type_name == "Angle" ||
        type_name == "Degrees" || type_name == "RangeReal32" || type_name == "Int8" ||
        type_name == "Int32" || type_name == "Int16" || type_name == "Integer" ||
        type_name == "EnumInt32" || type_name == "TrainPos1D")
        return ExpectedKind::Number;
    return ExpectedKind::Any;
}

const TaskSchemaNS::FieldDef* FieldForParameter(const CallSpan& call, int index) {
    const TaskSchemaNS::TaskSchema* schema = TaskSchemaNS::GetSchema(call.type);
    if (schema == nullptr) return nullptr;
    for (const auto& field : *schema) {
        if (index >= field.argOffset && index < field.argOffset + field.argCount)
            return &field;
    }
    return nullptr;
}

bool ExistingKindMatches(const Scalar& existing, ExpectedKind expected) {
    if (expected == ExpectedKind::Any) return true;
    if (expected == ExpectedKind::Number) return existing.kind == Scalar::Kind::Number;
    if (expected == ExpectedKind::Boolean)
        return existing.kind == Scalar::Kind::Boolean || existing.kind == Scalar::Kind::Identifier;
    return existing.kind == Scalar::Kind::String || existing.kind == Scalar::Kind::Identifier;
}

bool ValueMatches(const JsonValue& value, const Scalar& existing, ExpectedKind expected) {
    if (!ExistingKindMatches(existing, expected)) return false;
    if (expected == ExpectedKind::Number) return value.is_number();
    if (expected == ExpectedKind::Boolean) return value.is_bool();
    if (expected == ExpectedKind::String) return value.is_string();
    switch (existing.kind) {
        case Scalar::Kind::Number: return value.is_number();
        case Scalar::Kind::Boolean: return value.is_bool();
        case Scalar::Kind::String: return value.is_string();
        case Scalar::Kind::Identifier: return value.is_string() || value.is_bool();
        default: return false;
    }
}

bool IsIntegerType(std::string_view type_name) {
    return type_name == "Int8" || type_name == "Int16" || type_name == "Int32" ||
           type_name == "Integer";
}

bool SchemaFieldValueMatches(const JsonValue& value, const Scalar& existing,
                             const TaskSchemaNS::FieldDef& field) {
    const ExpectedKind expected = ExpectedKindForType(field.typeName);
    if (expected == ExpectedKind::Any || !ValueMatches(value, existing, expected)) return false;
    if (expected == ExpectedKind::String) {
        const std::size_t maximum = field.typeName == "String16" ? 16 :
                                    field.typeName == "String32" ? 32 : 256;
        return value.is_string() && value.as_string().size() <= maximum;
    }
    if (!value.is_number()) return true;
    const double number = value.as_number();
    if (!std::isfinite(number)) return false;
    if (field.typeName == "RGB" || field.typeName == "Colour")
        return number >= 0.0 && number <= 1.0;
    if (!IsIntegerType(field.typeName)) return true;
    if (std::trunc(number) != number) return false;
    if (field.typeName == "Int8") return number >= -128.0 && number <= 127.0;
    if (field.typeName == "Int16") return number >= -32768.0 && number <= 32767.0;
    return number >= static_cast<double>(std::numeric_limits<std::int32_t>::min()) &&
           number <= static_cast<double>(std::numeric_limits<std::int32_t>::max());
}

bool SchemaAcceptsCall(const std::string& source, const CallSpan& call,
                       const TaskSchemaNS::TaskSchema& schema) {
    for (const auto& field : schema) {
        if (ExpectedKindForType(field.typeName) == ExpectedKind::Any) return false;
        for (int offset = 0; offset < field.argCount; ++offset) {
            const int index = field.argOffset + offset;
            if (!ValidArg(call, index)) return false;
            const ArgSpan& span = call.args[static_cast<std::size_t>(index)];
            const std::size_t begin = ArgBegin(source, span);
            const std::size_t end = ArgEnd(source, span);
            Scalar existing;
            if (begin >= end || !ParseScalar(std::string_view(source).substr(begin, end - begin), existing) ||
                !ExistingKindMatches(existing, ExpectedKindForType(field.typeName))) return false;
        }
    }
    return true;
}

bool ValidateSchemaParameterValue(const std::string& source, const CallSpan& call, int index,
                                  const JsonValue& value, const TaskSchemaNS::FieldDef*& field,
                                  ExpectedKind& expected, std::string& error) {
    field = FieldForParameter(call, index);
    if (field == nullptr || index < 3 || !ValidArg(call, index)) {
        error = "unsupported_operation";
        return false;
    }
    const ArgSpan& span = call.args[static_cast<std::size_t>(index)];
    const std::size_t begin = ArgBegin(source, span);
    const std::size_t end = ArgEnd(source, span);
    Scalar existing;
    if (begin >= end || !ParseScalar(std::string_view(source).substr(begin, end - begin), existing) ||
        !SchemaFieldValueMatches(value, existing, *field)) {
        error = "unsupported_operation";
        return false;
    }
    expected = ExpectedKindForType(field->typeName);
    return true;
}

bool ValidateReplacementValue(const std::string& source, const CallSpan& call, int index,
                              const JsonValue& value, ExpectedKind expected,
                              std::string& error) {
    if (!ValidArg(call, index)) {
        error = "unsupported_operation";
        return false;
    }
    const ArgSpan& span = call.args[static_cast<std::size_t>(index)];
    const std::size_t begin = ArgBegin(source, span);
    const std::size_t end = ArgEnd(source, span);
    Scalar existing;
    if (begin >= end || !ParseScalar(std::string_view(source).substr(begin, end - begin), existing) ||
        !ValueMatches(value, existing, expected)) {
        error = "unsupported_operation";
        return false;
    }
    return true;
}

bool ValidateFieldStringLength(const JsonValue& value, const TaskSchemaNS::FieldDef* field) {
    if (field == nullptr || !value.is_string()) return false;
    const std::size_t maximum = field->typeName == "String16" ? 16 :
                                field->typeName == "String32" ? 32 : 256;
    return value.as_string().size() <= maximum;
}

bool AddReplacement(const std::string& source, const CallSpan& call, int index,
                    std::string text, std::string field,
                    std::vector<Replacement>& replacements, std::string& error,
                    ExpectedKind expected = ExpectedKind::Any) {
    if (!ValidArg(call, index)) {
        error = "unsupported_operation";
        return false;
    }
    const ArgSpan& span = call.args[static_cast<std::size_t>(index)];
    const std::size_t begin = ArgBegin(source, span);
    const std::size_t end = ArgEnd(source, span);
    if (begin >= end) {
        error = "unsupported_operation";
        return false;
    }
    Scalar existing;
    if (!ParseScalar(std::string_view(source).substr(begin, end - begin), existing)) {
        error = "unsupported_operation";
        return false;
    }
    if (!ExistingKindMatches(existing, expected)) {
        error = "unsupported_operation";
        return false;
    }
    replacements.push_back({begin, end, std::move(text), std::move(field)});
    return true;
}

bool IsReal64Type(std::string_view type_name) {
    return type_name == "Real64" || type_name == "Real64x3";
}

std::string FormatNumber(double value, bool real64 = false) {
    std::ostringstream output;
    output << std::setprecision(real64 ? 17 : 9) << (real64 ? value : static_cast<float>(value));
    return output.str();
}

std::string FormatString(std::string_view value) {
    std::string result = "\"";
    for (const char character : value) {
        switch (character) {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += character; break;
        }
    }
    result += '"';
    return result;
}

bool AddVectorReplacements(const std::string& source, const CallSpan& call,
                           const std::array<int, 3>& indices, const std::array<double, 3>& values,
                           std::string_view field, std::vector<Replacement>& replacements,
                           std::string& error) {
    for (int component = 0; component < 3; ++component) {
        if (indices[component] < 0) {
            error = "unsupported_operation";
            return false;
        }
        const TaskSchemaNS::FieldDef* schema_field = FieldForParameter(call, indices[component]);
        if (!AddReplacement(source, call, indices[component],
                            FormatNumber(values[component], schema_field != nullptr &&
                                                             IsReal64Type(schema_field->typeName)),
                            std::string(field) + "[" + std::to_string(component) + "]",
                            replacements, error, ExpectedKind::Number)) return false;
    }
    return true;
}

bool ApplyReplacements(std::string& source, std::vector<Replacement>& replacements,
                       std::vector<std::string>& fields, std::string& error) {
    std::sort(replacements.begin(), replacements.end(),
              [](const Replacement& left, const Replacement& right) {
                  return left.begin > right.begin;
              });
    for (std::size_t index = 1; index < replacements.size(); ++index) {
        if (replacements[index - 1].begin < replacements[index].end) {
            error = "unsupported_operation";
            return false;
        }
    }
    for (const auto& replacement : replacements) {
        source.replace(replacement.begin, replacement.end - replacement.begin, replacement.text);
        fields.push_back(replacement.field);
    }
    std::reverse(fields.begin(), fields.end());
    error.clear();
    return true;
}

bool CurrentLevel(GameDataService& service, const JsonValue& arguments, int& level,
                  std::string& error) {
    LevelRevision current_revision;
    try {
        current_revision = service.CurrentRevision();
    } catch (...) {
        error = "level_not_open";
        return false;
    }
    level = current_revision.level;
    if (arguments.contains("level")) {
        int requested = 0;
        if (!ReadInteger(arguments.at("level"), requested) || requested != current_revision.level) {
            error = "invalid_arguments";
            return false;
        }
    }
    error.clear();
    return true;
}

JsonValue MutationResult(GameDataService& service, int level, std::string_view tool,
                         std::string_view task_id, const JsonValue& before,
                         int source_line, std::size_t call_begin, const MutationOptions& options,
                         std::string source,
                         std::vector<Replacement> replacements, std::string& error) {
    const LevelRevision revision_before = service.CurrentRevision();
    std::vector<std::string> fields;
    if (!ApplyReplacements(source, replacements, fields, error)) return JsonValue(nullptr);
    if (before.at("id").is_string() && before.at("id").as_string().starts_with("anon-"))
        AddAnonymousTaskMarker(source, call_begin, task_id);
    if (!service.SaveCurrentObjectSource(source, options, error)) {
        if (error.empty()) error = "save_failed";
        return JsonValue(nullptr);
    }

    JsonValue after;
    LevelRevision revision_after = revision_before;
    if (options.dry_run) {
        std::string object_error;
        after = service.ObjectSnapshotFromSource(level, source, task_id, source_line, object_error);
        if (!object_error.empty()) {
            error = object_error;
            return JsonValue(nullptr);
        }
    } else {
        revision_after = service.CurrentRevision();
        std::string object_error;
        after = service.ObjectSnapshotFromSource(level, source, task_id, source_line, object_error);
        if (!object_error.empty()) {
            error = object_error;
            return JsonValue(nullptr);
        }
    }
    return Object{
        {"tool", std::string(tool)},
        {"task_id", std::string(task_id)},
        {"dry_run", options.dry_run},
        {"changed", true},
        {"changed_fields", [&] {
             JsonValue::Array values;
             for (const auto& field : fields) values.emplace_back(field);
             return JsonValue(values);
         }()},
        {"revision_before", revision_before.fingerprint},
        {"revision_after", revision_after.fingerprint},
        {"before", before},
        {"after", after},
    };
}

JsonValue ReadObject(GameDataService& service, const JsonValue& arguments,
                     std::string_view tool, std::string& error) {
    std::string task_id;
    if (!ReadTaskId(arguments, task_id)) return Failure(error, "invalid_arguments");
    int level = 0;
    if (!CurrentLevel(service, arguments, level, error)) return JsonValue(nullptr);
    std::string domain_error;
    const JsonValue object = service.GetObject(level, task_id, domain_error);
    if (!domain_error.empty()) return DomainFailure(error, domain_error);
    if (tool == "task_get") return object;
    return Object{{"level", level}, {"object", object}};
}

bool PrepareTarget(GameDataService& service, const JsonValue& arguments, int& level,
                   std::string& task_id, JsonValue& before, std::string& source,
                   CallSpan*& call, std::vector<CallSpan>& calls, MutationOptions& options,
                   std::string& error) {
    if (!ReadTaskId(arguments, task_id)) {
        error = "invalid_arguments";
        return false;
    }
    if (!CurrentLevel(service, arguments, level, error)) return false;
    if (!ReadMutationOptions(arguments, options, error)) return false;
    std::string domain_error;
    before = service.GetObject(level, task_id, domain_error);
    if (!domain_error.empty()) {
        error = domain_error;
        return false;
    }
    if (!service.LoadCurrentObjectSource(source, error)) return false;
    if (!ScanTaskCalls(source, calls)) {
        error = "qsc_parse_failed";
        return false;
    }
    call = FindCall(calls, task_id);
    if (!call) {
        error = "unknown_task_id";
        return false;
    }
    if (before.contains("writable") && !before.at("writable").as_bool()) {
        error = "ambiguous_task_id";
        return false;
    }
    if (TaskSchemaNS::GetSchema(call->type) == nullptr) {
        error = "unsupported_operation";
        return false;
    }
    return true;
}

JsonValue UnsupportedStructural(GameDataService& service, const JsonValue& arguments,
                                std::string_view tool, bool require_parent,
                                std::string& error) {
    std::string task_id;
    if (!ReadTaskId(arguments, task_id)) return Failure(error, "invalid_arguments");
    if (require_parent && (!arguments.contains("parent_id") ||
                           !arguments.at("parent_id").is_string() ||
                           arguments.at("parent_id").as_string().empty())) {
        return Failure(error, "invalid_arguments");
    }
    int level = 0;
    if (!CurrentLevel(service, arguments, level, error)) return JsonValue(nullptr);
    MutationOptions options;
    if (!ReadMutationOptions(arguments, options, error)) return JsonValue(nullptr);
    std::string domain_error;
    const JsonValue object = service.GetObject(level, task_id, domain_error);
    if (!domain_error.empty()) return DomainFailure(error, domain_error);
    (void)object;
    return Failure(error, "unsupported_operation");
}

}  // namespace

ToolDefinitionList ObjectToolDefinitions() {
    Object properties = MutationProperties();
    properties["task_id"] = StringSchema();

    Object transform = MutationProperties();
    transform["task_id"] = StringSchema();
    transform["position"] = NumberVectorSchema();
    transform["rotation_radians"] = NumberVectorSchema();
    transform["scale"] = Object{{"type", "number"}};

    Object update = MutationProperties();
    update["task_id"] = StringSchema();
    update["name"] = StringSchema();
    update["position"] = NumberVectorSchema();
    update["rotation_radians"] = NumberVectorSchema();
    update["model_id"] = StringSchema();
    update["type"] = StringSchema();
    update["parameter_index"] = Object{{"type", "integer"}, {"minimum", 0}};
    update["value"] = Object{};

    Object create = MutationProperties();
    create["task_id"] = StringSchema();
    create["type"] = StringSchema();
    create["parent_id"] = StringSchema();
    create["name"] = StringSchema();

    Object schema = Object{{"task_id", StringSchema()}, {"type", StringSchema()}};
    return ToolDefinitionList{
        {"task_list", Schema(Object{{"level", properties["level"]}})},
        {"task_get", Schema(Object{{"level", properties["level"]}, {"task_id", StringSchema()}},
                             JsonValue::Array{JsonValue("task_id")})},
        {"task_create", Schema(create, JsonValue::Array{JsonValue("task_id"), JsonValue("type")})},
        {"task_update", Schema(update, JsonValue::Array{JsonValue("task_id")})},
        {"task_delete", Schema(properties, JsonValue::Array{JsonValue("task_id")})},
        {"task_duplicate", Schema(Object{{"task_id", StringSchema()}, {"new_task_id", StringSchema()},
                                          {"parent_id", StringSchema()},
                                          {"expected_revision", properties["expected_revision"]},
                                          {"dry_run", properties["dry_run"]},
                                          {"backup", properties["backup"]}},
                                   JsonValue::Array{JsonValue("task_id"), JsonValue("new_task_id")})},
        {"task_reparent", Schema(Object{{"task_id", StringSchema()}, {"parent_id", StringSchema()},
                                          {"expected_revision", properties["expected_revision"]},
                                          {"dry_run", properties["dry_run"]},
                                          {"backup", properties["backup"]}},
                                   JsonValue::Array{JsonValue("task_id"), JsonValue("parent_id")})},
        {"object_set_transform", Schema(transform, JsonValue::Array{JsonValue("task_id")})},
        {"object_set_model", Schema(Object{{"task_id", StringSchema()}, {"model_id", StringSchema()},
                                            {"expected_revision", properties["expected_revision"]},
                                            {"dry_run", properties["dry_run"]},
                                            {"backup", properties["backup"]}},
                                     JsonValue::Array{JsonValue("task_id"), JsonValue("model_id")})},
        {"object_set_type", Schema(Object{{"task_id", StringSchema()}, {"type", StringSchema()},
                                           {"expected_revision", properties["expected_revision"]},
                                           {"dry_run", properties["dry_run"]},
                                           {"backup", properties["backup"]}},
                                    JsonValue::Array{JsonValue("task_id"), JsonValue("type")})},
        {"object_set_parameter", Schema(Object{{"task_id", StringSchema()},
                                                  {"parameter_index", Object{{"type", "integer"}, {"minimum", 0}}},
                                                  {"value", Object{}},
                                                  {"expected_revision", properties["expected_revision"]},
                                                  {"dry_run", properties["dry_run"]},
                                                  {"backup", properties["backup"]}},
                                           JsonValue::Array{JsonValue("task_id"), JsonValue("parameter_index"),
                                                            JsonValue("value")})},
        {"object_get_schema", Schema(schema)},
    };
}

JsonValue CallObjectTool(GameDataService& service, std::string_view name,
                         const JsonValue& arguments, std::string& error) {
    error.clear();
    try {
        if (name == "task_list") {
            if (!HasOnlyKeys(arguments, {"level"})) return Failure(error, "invalid_arguments");
            int level = 0;
            if (!CurrentLevel(service, arguments, level, error)) return JsonValue(nullptr);
            std::string domain_error;
            const JsonValue result = service.ListObjects(level, domain_error);
            return domain_error.empty() ? result : DomainFailure(error, domain_error);
        }

        if (name == "task_get") {
            if (!HasOnlyKeys(arguments, {"level", "task_id"})) return Failure(error, "invalid_arguments");
            return ReadObject(service, arguments, name, error);
        }

        if (name == "object_get_schema") {
            if (!HasOnlyKeys(arguments, {"level", "task_id", "type"}) ||
                (arguments.contains("task_id") == arguments.contains("type"))) {
                return Failure(error, "invalid_arguments");
            }
            int level = 0;
            if (!CurrentLevel(service, arguments, level, error)) return JsonValue(nullptr);
            std::string type;
            std::string task_id;
            if (arguments.contains("task_id")) {
                if (!ReadTaskId(arguments, task_id)) return Failure(error, "invalid_arguments");
                std::string domain_error;
                const JsonValue object = service.GetObject(level, task_id, domain_error);
                if (!domain_error.empty()) return DomainFailure(error, domain_error);
                type = object.at("type").as_string();
            } else if (!ReadNonEmptyString(arguments.at("type"), type)) {
                return Failure(error, "invalid_arguments");
            } else {
                std::string source;
                std::string domain_error;
                if (!service.LoadCurrentObjectSource(source, domain_error))
                    return DomainFailure(error, domain_error);
            }
            if (TaskSchemaNS::GetSchema(type) == nullptr)
                return Failure(error, "unsupported_operation");
            const Layout layout = LayoutFor(type);
            const auto indices = [](const std::array<int, 3>& values) {
                JsonValue::Array result;
                for (std::size_t index = 0; index < 3; ++index) {
                    const int value = values[index];
                    if (value >= 0) result.emplace_back(value);
                }
                return JsonValue(result);
            };
            Object fields{
                {"task_id", Object{{"type", "string"}, {"read_only", true}}},
                {"type", Object{{"type", "string"}, {"parameter_index", 1}}},
                {"name", Object{{"type", "string"}, {"parameter_index", 2}}},
                {"scale", Object{{"supported", false}, {"reason", "non_persistent"}}},
            };
            if (layout.position[0] >= 0) {
                fields["position"] = Object{{"type", "array"},
                                              {"parameter_indices", indices(layout.position)}};
            }
            if (layout.rotation[0] >= 0) {
                fields["rotation_radians"] = Object{{"type", "array"},
                                                      {"parameter_indices", indices(layout.rotation)}};
            }
            if (layout.model >= 0) {
                fields["model_id"] = Object{{"type", "string"},
                                              {"parameter_index", layout.model}};
            }
            return Object{
                {"type", type},
                {"stable_id_required", true},
                {"persistent", true},
                {"fields", std::move(fields)},
            };
        }

        if (name == "task_create") {
            if (!HasOnlyKeys(arguments, {"level", "task_id", "type", "parent_id", "name",
                                          "expected_revision", "dry_run", "backup"})) {
                return Failure(error, "invalid_arguments");
            }
            std::string task_id;
            if (!ReadTaskId(arguments, task_id) || !arguments.contains("type")) {
                return Failure(error, "invalid_arguments");
            }
            std::string type;
            if (!ReadNonEmptyString(arguments.at("type"), type)) return Failure(error, "invalid_arguments");
            int level = 0;
            if (!CurrentLevel(service, arguments, level, error)) return JsonValue(nullptr);
            MutationOptions options;
            if (!ReadMutationOptions(arguments, options, error)) return JsonValue(nullptr);
            return Failure(error, "unsupported_operation");
        }

        if (name == "task_delete") {
            if (!HasOnlyKeys(arguments, {"level", "task_id", "expected_revision", "dry_run", "backup"}))
                return Failure(error, "invalid_arguments");
            return UnsupportedStructural(service, arguments, name, false, error);
        }
        if (name == "task_duplicate") {
            if (!HasOnlyKeys(arguments, {"level", "task_id", "new_task_id", "parent_id",
                                          "expected_revision", "dry_run", "backup"}) ||
                !arguments.contains("new_task_id") || !arguments.at("new_task_id").is_string() ||
                arguments.at("new_task_id").as_string().empty()) return Failure(error, "invalid_arguments");
            return UnsupportedStructural(service, arguments, name, false, error);
        }
        if (name == "task_reparent") {
            if (!HasOnlyKeys(arguments, {"level", "task_id", "parent_id", "expected_revision",
                                          "dry_run", "backup"})) return Failure(error, "invalid_arguments");
            return UnsupportedStructural(service, arguments, name, true, error);
        }

        if (name == "object_set_transform" || name == "object_set_model" ||
            name == "object_set_type" || name == "object_set_parameter" || name == "task_update") {
            const bool update = name == "task_update";
            if (name == "object_set_transform" &&
                !HasOnlyKeys(arguments, {"level", "task_id", "position", "rotation_radians", "scale",
                                          "expected_revision", "dry_run", "backup"}))
                return Failure(error, "invalid_arguments");
            if (name == "object_set_model" &&
                !HasOnlyKeys(arguments, {"level", "task_id", "model_id", "expected_revision", "dry_run", "backup"}))
                return Failure(error, "invalid_arguments");
            if (name == "object_set_type" &&
                !HasOnlyKeys(arguments, {"level", "task_id", "type", "expected_revision", "dry_run", "backup"}))
                return Failure(error, "invalid_arguments");
            if (name == "object_set_parameter" &&
                !HasOnlyKeys(arguments, {"level", "task_id", "parameter_index", "value",
                                          "expected_revision", "dry_run", "backup"}))
                return Failure(error, "invalid_arguments");
            if (update &&
                !HasOnlyKeys(arguments, {"level", "task_id", "name", "position", "rotation_radians",
                                          "model_id", "type", "parameter_index", "value",
                                          "expected_revision", "dry_run", "backup"}))
                return Failure(error, "invalid_arguments");

            int level = 0;
            std::string task_id;
            JsonValue before;
            std::string source;
            std::vector<CallSpan> calls;
            CallSpan* call = nullptr;
            MutationOptions options;
            if (!PrepareTarget(service, arguments, level, task_id, before, source, call, calls,
                               options, error)) return JsonValue(nullptr);
            std::vector<Replacement> replacements;

            if (name == "object_set_transform" || update) {
                if (arguments.contains("scale")) return Failure(error, "unsupported_operation");
                if (arguments.contains("position")) {
                    std::array<double, 3> values{};
                    if (!ReadFiniteVector(arguments.at("position"), values)) return Failure(error, "invalid_arguments");
                    const Layout layout = LayoutFor(call->type);
                    if (!AddVectorReplacements(source, *call, layout.position, values, "position",
                                               replacements, error)) return JsonValue(nullptr);
                }
                if (arguments.contains("rotation_radians")) {
                    std::array<double, 3> values{};
                    if (!ReadFiniteVector(arguments.at("rotation_radians"), values)) return Failure(error, "invalid_arguments");
                    const Layout layout = LayoutFor(call->type);
                    if (!AddVectorReplacements(source, *call, layout.rotation, values, "rotation_radians",
                                               replacements, error)) return JsonValue(nullptr);
                }
                if (name == "object_set_transform" && !arguments.contains("position") &&
                    !arguments.contains("rotation_radians")) return Failure(error, "invalid_arguments");
            }

            if (name == "object_set_model" || (update && arguments.contains("model_id"))) {
                if (name == "object_set_model" && !arguments.contains("model_id"))
                    return Failure(error, "invalid_arguments");
                std::string model;
                if (!ReadNonEmptyString(arguments.at("model_id"), model)) return Failure(error, "invalid_arguments");
                const int index = LayoutFor(call->type).model;
                const TaskSchemaNS::FieldDef* model_field = FieldForParameter(*call, index);
                if (!ValidateFieldStringLength(JsonValue(model), model_field))
                    return Failure(error, "unsupported_operation");
                std::string catalog_error;
                if (!service.IsAvailableModelId(model, catalog_error))
                    return DomainFailure(error, catalog_error);
                if (!ValidateReplacementValue(source, *call, index, JsonValue(model),
                                              ExpectedKind::String, error) ||
                    !AddReplacement(source, *call, index, FormatString(model), "model_id",
                                    replacements, error, ExpectedKind::String)) return JsonValue(nullptr);
            }
            if (name == "object_set_type" || (update && arguments.contains("type"))) {
                if (name == "object_set_type" && !arguments.contains("type"))
                    return Failure(error, "invalid_arguments");
                std::string type;
                const TaskSchemaNS::TaskSchema* destination_schema = nullptr;
                if (!ReadNonEmptyString(arguments.at("type"), type)) return Failure(error, "invalid_arguments");
                destination_schema = TaskSchemaNS::GetSchema(type);
                if (destination_schema == nullptr || !SchemaAcceptsCall(source, *call, *destination_schema))
                    return Failure(error, "unsupported_operation");
                if (!ValidateReplacementValue(source, *call, 1, JsonValue(type),
                                              ExpectedKind::String, error) ||
                    !AddReplacement(source, *call, 1, FormatString(type), "type", replacements,
                                    error, ExpectedKind::String))
                    return JsonValue(nullptr);
            }
            if (update && arguments.contains("name")) {
                std::string task_name;
                if (!ReadNonEmptyString(arguments.at("name"), task_name) ||
                    !ValidateReplacementValue(source, *call, 2, JsonValue(task_name),
                                              ExpectedKind::String, error) ||
                    !AddReplacement(source, *call, 2, FormatString(task_name), "name", replacements,
                                    error, ExpectedKind::String))
                    return JsonValue(nullptr);
            }
            if (name == "object_set_parameter" || (update && arguments.contains("parameter_index"))) {
                if (name == "object_set_parameter" &&
                    (!arguments.contains("parameter_index") || !arguments.contains("value")))
                    return Failure(error, "invalid_arguments");
                int index = -1;
                if (!ReadNonNegativeInteger(arguments.at("parameter_index"), index) ||
                    !arguments.contains("value")) return Failure(error, "invalid_arguments");
                if (index == 0) return Failure(error, "unsupported_operation");
                const JsonValue& value = arguments.at("value");
                const TaskSchemaNS::FieldDef* field = nullptr;
                ExpectedKind expected = ExpectedKind::Any;
                if (!ValidateSchemaParameterValue(source, *call, index, value, field, expected, error))
                    return JsonValue(nullptr);
                std::string replacement;
                if (value.is_string()) {
                    std::string text;
                    if (!ReadNonEmptyString(value, text)) return Failure(error, "invalid_arguments");
                    replacement = FormatString(text);
                }
                else if (value.is_bool()) replacement = value.as_bool() ? "TRUE" : "FALSE";
                else if (value.is_number() && std::isfinite(value.as_number()))
                    replacement = FormatNumber(value.as_number(), field != nullptr &&
                                                             IsReal64Type(field->typeName));
                else return Failure(error, "invalid_arguments");
                if (!AddReplacement(source, *call, index, replacement,
                                    "parameter[" + std::to_string(index) + "]", replacements, error,
                                    expected))
                    return JsonValue(nullptr);
            }
            if (replacements.empty()) return Failure(error, "invalid_arguments");
            return MutationResult(service, level, name, task_id, before, call->source_line, call->begin,
                                  options, source, std::move(replacements), error);
        }

        return Failure(error, "unknown_tool");
    } catch (...) {
        return Failure(error, "service_error");
    }
}

}  // namespace mcp
