#include "mcp_tools_mission.h"

#include "../level/qsc_lexer.h"
#include "../level/qsc_parser.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mcp {
namespace {

struct SourceSpan {
    std::size_t begin = 0;
    std::size_t end = 0;
};

struct CallSpan {
    std::string name;
    std::vector<SourceSpan> arguments;
};

struct Patch {
    SourceSpan span;
    std::string replacement;
};

JsonValue StringSchema() {
    return JsonValue::Object{{"type", JsonValue("string")}};
}

JsonValue ObjectSchema(JsonValue::Object properties,
                       std::initializer_list<std::string_view> required = {}) {
    JsonValue::Array required_values;
    for (const std::string_view key : required) required_values.emplace_back(key);
    JsonValue::Object schema{
        {"type", JsonValue("object")},
        {"properties", std::move(properties)},
        {"additionalProperties", JsonValue(false)},
    };
    if (!required_values.empty()) schema["required"] = std::move(required_values);
    return schema;
}

JsonValue FieldsSchema() {
    return JsonValue::Object{
        {"type", JsonValue("object")},
        {"properties", JsonValue::Object{
            {"valid_expression", StringSchema()},
            {"text_resource", StringSchema()},
            {"link_task_id", JsonValue::Object{{"type", JsonValue("integer")}}},
            {"completion_expression", StringSchema()},
            {"failure_expression", StringSchema()},
        }},
        {"additionalProperties", JsonValue(false)},
    };
}

void AddMutationProperties(JsonValue::Object& properties) {
    properties["level"] = JsonValue::Object{{"type", JsonValue("integer")}, {"minimum", JsonValue(1)}};
    properties["dry_run"] = JsonValue::Object{{"type", JsonValue("boolean")}};
    properties["backup"] = JsonValue::Object{{"type", JsonValue("boolean")}};
    properties["expected_revision"] = StringSchema();
}

bool HasOnlyKeys(const JsonValue& value,
                 std::initializer_list<std::string_view> allowed_keys) {
    if (!value.is_object()) return false;
    for (const auto& [key, ignored] : value.as_object()) {
        bool allowed = false;
        for (const std::string_view candidate : allowed_keys) {
            if (key == candidate) {
                allowed = true;
                break;
            }
        }
        if (!allowed) return false;
    }
    return true;
}

bool ReadString(const JsonValue& value, std::string& result, std::size_t maximum = 4096,
                bool allow_empty = true) {
    if (!value.is_string() || value.as_string().size() > maximum ||
        (!allow_empty && value.as_string().empty())) return false;
    for (const unsigned char character : value.as_string()) {
        if (character < 0x20u) return false;
    }
    result = value.as_string();
    return true;
}

bool ReadInteger(const JsonValue& value, std::int64_t& result,
                 std::int64_t minimum = std::numeric_limits<std::int64_t>::min(),
                 std::int64_t maximum = std::numeric_limits<std::int64_t>::max()) {
    if (!value.is_number()) return false;
    const double number = value.as_number();
    if (!std::isfinite(number) || std::trunc(number) != number ||
        number < static_cast<double>(minimum) || number > static_cast<double>(maximum)) return false;
    result = static_cast<std::int64_t>(number);
    return true;
}

bool ReadMutationOptions(const JsonValue& arguments, MutationOptions& options) {
    if (arguments.contains("dry_run")) {
        if (!arguments.at("dry_run").is_bool()) return false;
        options.dry_run = arguments.at("dry_run").as_bool();
    }
    if (arguments.contains("backup")) {
        if (!arguments.at("backup").is_bool()) return false;
        options.backup = arguments.at("backup").as_bool();
    }
    if (arguments.contains("expected_revision")) {
        std::string revision;
        if (!ReadString(arguments.at("expected_revision"), revision, 256, false)) return false;
        options.expected_revision = std::move(revision);
    }
    return true;
}

bool CurrentLevel(GameDataService& service, const JsonValue& arguments, int& level,
                  std::string& error) {
    if (!service.HasOpenLevel()) {
        error = "level_not_open";
        return false;
    }
    const std::int64_t current = service.CurrentRevision().level;
    if (arguments.contains("level")) {
        std::int64_t requested = 0;
        if (!ReadInteger(arguments.at("level"), requested, 1, std::numeric_limits<int>::max()) ||
            requested != current) {
            error = "invalid_arguments";
            return false;
        }
    }
    level = static_cast<int>(current);
    error.clear();
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

std::size_t SkipQuoted(std::string_view source, std::size_t position) {
    ++position;
    bool escaped = false;
    while (position < source.size()) {
        const char character = source[position++];
        if (escaped) escaped = false;
        else if (character == '\\') escaped = true;
        else if (character == '"') break;
    }
    return position;
}

std::size_t SkipComment(std::string_view source, std::size_t position) {
    if (position + 1 < source.size() && source[position + 1] == '*') {
        position += 2;
        while (position + 1 < source.size() &&
               !(source[position] == '*' && source[position + 1] == '/')) ++position;
        return position + 1 < source.size() ? position + 2 : source.size();
    }
    position += 2;
    while (position < source.size() && source[position] != '\n') ++position;
    return position;
}

bool IsIdentifierStart(char character) {
    return (character >= 'A' && character <= 'Z') ||
           (character >= 'a' && character <= 'z') || character == '_';
}

bool IsIdentifierPart(char character) {
    return IsIdentifierStart(character) || (character >= '0' && character <= '9');
}

SourceSpan TrimSpan(std::string_view source, SourceSpan span) {
    while (span.begin < span.end && std::isspace(static_cast<unsigned char>(source[span.begin]))) ++span.begin;
    while (span.end > span.begin && std::isspace(static_cast<unsigned char>(source[span.end - 1]))) --span.end;
    return span;
}

std::vector<CallSpan> ScanCalls(std::string_view source) {
    std::vector<CallSpan> calls;
    for (std::size_t position = 0; position < source.size();) {
        if (source[position] == '"') {
            position = SkipQuoted(source, position);
            continue;
        }
        if (position + 1 < source.size() && source[position] == '/' &&
            (source[position + 1] == '/' || source[position + 1] == '*')) {
            position = SkipComment(source, position);
            continue;
        }
        if (!IsIdentifierStart(source[position])) {
            ++position;
            continue;
        }
        const std::size_t name_begin = position++;
        while (position < source.size() && IsIdentifierPart(source[position])) ++position;
        const std::string name(source.substr(name_begin, position - name_begin));
        std::size_t open = position;
        while (open < source.size() && std::isspace(static_cast<unsigned char>(source[open]))) ++open;
        if (open >= source.size() || source[open] != '(') continue;

        int depth = 1;
        std::size_t cursor = open + 1;
        std::size_t argument_begin = cursor;
        std::vector<SourceSpan> arguments;
        while (cursor < source.size() && depth > 0) {
            if (source[cursor] == '"') {
                cursor = SkipQuoted(source, cursor);
                continue;
            }
            if (cursor + 1 < source.size() && source[cursor] == '/' &&
                (source[cursor + 1] == '/' || source[cursor + 1] == '*')) {
                cursor = SkipComment(source, cursor);
                continue;
            }
            if (source[cursor] == '(') {
                ++depth;
            } else if (source[cursor] == ')') {
                --depth;
                if (depth == 0) break;
            } else if (source[cursor] == ',' && depth == 1) {
                arguments.push_back(TrimSpan(source, {argument_begin, cursor}));
                argument_begin = cursor + 1;
            }
            ++cursor;
        }
        if (depth != 0) continue;
        const SourceSpan last = TrimSpan(source, {argument_begin, cursor});
        if (last.begin != last.end || !arguments.empty()) arguments.push_back(last);
        calls.push_back({name, std::move(arguments)});
    }
    return calls;
}

std::string SpanText(std::string_view source, SourceSpan span) {
    return std::string(source.substr(span.begin, span.end - span.begin));
}

std::string Unquote(std::string value) {
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') return value;
    std::string result;
    bool escaped = false;
    for (std::size_t index = 1; index + 1 < value.size(); ++index) {
        const char character = value[index];
        if (escaped) {
            result.push_back(character);
            escaped = false;
        } else if (character == '\\') {
            escaped = true;
        } else {
            result.push_back(character);
        }
    }
    return result;
}

std::string QuoteQsc(std::string_view value) {
    std::string result = "\"";
    for (const char character : value) {
        if (character == '\\' || character == '"') result.push_back('\\');
        result.push_back(character);
    }
    result.push_back('"');
    return result;
}

std::string ScalarText(const qsc::Node& node) {
    switch (node.kind) {
    case qsc::NodeKind::IntLit: return std::to_string(node.i_val);
    case qsc::NodeKind::FloatLit: return std::to_string(node.f_val);
    case qsc::NodeKind::BoolLit: return node.b_val ? "true" : "false";
    case qsc::NodeKind::StringLit:
    case qsc::NodeKind::IdentLit: return node.s_val;
    case qsc::NodeKind::Unary:
        if (node.children.size() == 1) return node.s_val + ScalarText(*node.children.front());
        break;
    default: break;
    }
    return {};
}

JsonValue ScalarJson(const qsc::Node& node) {
    switch (node.kind) {
    case qsc::NodeKind::IntLit: return JsonValue(node.i_val);
    case qsc::NodeKind::FloatLit: return JsonValue(static_cast<double>(node.f_val));
    case qsc::NodeKind::BoolLit: return JsonValue(node.b_val);
    case qsc::NodeKind::StringLit:
    case qsc::NodeKind::IdentLit: return JsonValue(node.s_val);
    case qsc::NodeKind::Unary:
        if (node.s_val == "-" && node.children.size() == 1) {
            const JsonValue value = ScalarJson(*node.children.front());
            if (value.is_number()) return JsonValue(-value.as_number());
        }
        return JsonValue(ScalarText(node));
    default: return JsonValue(nullptr);
    }
}

void CollectCalls(const qsc::Node& node, std::vector<const qsc::Node*>& calls) {
    if (node.kind == qsc::NodeKind::Call && node.s_val == "DefineComputerObjective") calls.push_back(&node);
    for (const auto& child : node.children) CollectCalls(*child, calls);
}

bool ParseSource(std::string_view source, qsc::ParseResult& parsed, std::string& code) {
    if (source.size() > kMaxJsonMessageBytes) {
        code = "source_too_large";
        return false;
    }
    const qsc::LexResult lexed = qsc::Lex(std::string(source));
    if (!lexed.ok) {
        code = "qsc_lex_failed";
        return false;
    }
    parsed = qsc::Parse(lexed.tokens);
    if (!parsed.ok || !parsed.program) {
        code = "qsc_parse_failed";
        return false;
    }
    code.clear();
    return true;
}

JsonValue ObjectiveJson(const qsc::Node& call, int definition_index) {
    JsonValue::Array objectives;
    for (int slot = 0; slot < 6; ++slot) {
        const std::size_t offset = 4u + static_cast<std::size_t>(slot) * 4u;
        if (offset >= call.children.size()) break;
        const std::string text_resource = ScalarText(*call.children[offset]);
        if (text_resource.empty()) continue;
        JsonValue::Object objective{
            {"index", JsonValue(slot)},
            {"text_resource", JsonValue(text_resource)},
            {"link_task_id", JsonValue(nullptr)},
            {"completion_expression", JsonValue("")},
            {"failure_expression", JsonValue("")},
        };
        if (offset + 1 < call.children.size()) objective["link_task_id"] = ScalarJson(*call.children[offset + 1]);
        if (offset + 2 < call.children.size()) objective["completion_expression"] = ScalarJson(*call.children[offset + 2]);
        if (offset + 3 < call.children.size()) objective["failure_expression"] = ScalarJson(*call.children[offset + 3]);
        objectives.emplace_back(std::move(objective));
    }
    JsonValue::Object result{
        {"definition_index", JsonValue(definition_index)},
        {"valid_expression", call.children.size() > 3 ? ScalarJson(*call.children[3]) : JsonValue("")},
        {"objectives", JsonValue(std::move(objectives))},
    };
    return result;
}

JsonValue ObjectiveListFromSource(std::string_view source, int level,
                                  std::string_view revision, std::string& error) {
    qsc::ParseResult parsed;
    std::string parse_error;
    if (!ParseSource(source, parsed, parse_error)) {
        error = parse_error;
        return JsonValue(nullptr);
    }
    std::vector<const qsc::Node*> definitions;
    CollectCalls(*parsed.program, definitions);
    JsonValue::Array values;
    for (std::size_t index = 0; index < definitions.size(); ++index)
        values.emplace_back(ObjectiveJson(*definitions[index], static_cast<int>(index)));
    return JsonValue::Object{
        {"level", JsonValue(level)},
        {"revision", JsonValue(revision)},
        {"definitions", JsonValue(std::move(values))},
    };
}

}  // namespace

ToolDefinitionList MissionToolDefinitions() {
    JsonValue::Object list{{"level", JsonValue::Object{{"type", JsonValue("integer")}, {"minimum", JsonValue(1)}}}};
    JsonValue::Object update{
        {"level", JsonValue::Object{{"type", JsonValue("integer")}, {"minimum", JsonValue(1)}}},
        {"definition_index", JsonValue::Object{{"type", JsonValue("integer")}}},
        {"objective_index", JsonValue::Object{{"type", JsonValue("integer")}}},
        {"fields", FieldsSchema()},
    };
    AddMutationProperties(update);
    return ToolDefinitionList{
        {"mission_objective_list", ObjectSchema(std::move(list))},
        {"mission_objective_update", ObjectSchema(std::move(update), {"objective_index", "fields"})},
    };
}

JsonValue CallMissionTool(GameDataService& service, std::string_view name,
                          const JsonValue& arguments, std::string& error) {
    error.clear();
    try {
        if (name == "mission_objective_list") {
            if (!HasOnlyKeys(arguments, {"level"})) return Failure(error, "invalid_arguments");
            int level = 0;
            if (!CurrentLevel(service, arguments, level, error)) return JsonValue(nullptr);
            const LevelRevision revision = service.CurrentRevision();
            std::string source;
            std::string domain_error;
            if (!service.LoadCurrentObjectSource(source, domain_error)) return DomainFailure(error, domain_error);
            return ObjectiveListFromSource(source, level, revision.fingerprint, error);
        }

        if (name == "mission_objective_update") {
            if (!HasOnlyKeys(arguments, {"level", "definition_index", "objective_index", "fields", "dry_run", "backup", "expected_revision"}) ||
                !arguments.contains("objective_index") || !arguments.contains("fields") ||
                !HasOnlyKeys(arguments.at("fields"), {"valid_expression", "text_resource", "link_task_id", "completion_expression", "failure_expression"}) ||
                arguments.at("fields").as_object().empty()) return Failure(error, "invalid_arguments");
            std::int64_t objective_index = 0;
            if (!ReadInteger(arguments.at("objective_index"), objective_index, 0, 5)) return Failure(error, "invalid_arguments");
            std::int64_t definition_index = 0;
            if (arguments.contains("definition_index") && !ReadInteger(arguments.at("definition_index"), definition_index, 0, std::numeric_limits<std::int32_t>::max()))
                return Failure(error, "invalid_arguments");
            MutationOptions options;
            if (!ReadMutationOptions(arguments, options)) return Failure(error, "invalid_arguments");

            int level = 0;
            if (!CurrentLevel(service, arguments, level, error)) return JsonValue(nullptr);
            const LevelRevision revision = service.CurrentRevision();
            std::string source;
            std::string domain_error;
            if (!service.LoadCurrentObjectSource(source, domain_error)) return DomainFailure(error, domain_error);
            qsc::ParseResult parsed;
            if (!ParseSource(source, parsed, domain_error)) return Failure(error, domain_error);
            std::vector<const qsc::Node*> definitions;
            CollectCalls(*parsed.program, definitions);
            if (static_cast<std::size_t>(definition_index) >= definitions.size()) return Failure(error, "unknown_objective");
            const qsc::Node& definition = *definitions[static_cast<std::size_t>(definition_index)];
            const std::size_t base = 4u + static_cast<std::size_t>(objective_index) * 4u;
            if (definition.children.size() < base + 4u) return Failure(error, "unsupported_operation");
            const JsonValue before = ObjectiveJson(definition, static_cast<int>(definition_index));

            const auto calls = ScanCalls(source);
            std::size_t occurrence = 0;
            const CallSpan* target = nullptr;
            for (const auto& call : calls) {
                if (call.name != "DefineComputerObjective") continue;
                if (occurrence++ == static_cast<std::size_t>(definition_index)) {
                    target = &call;
                    break;
                }
            }
            if (!target || target->arguments.size() < base + 4u) return Failure(error, "unsupported_operation");

            std::vector<Patch> patches;
            for (const auto& [field, value] : arguments.at("fields").as_object()) {
                std::size_t argument = base;
                std::string replacement;
                if (field == "valid_expression") {
                    argument = 3;
                    std::string text;
                    if (!ReadString(value, text)) return Failure(error, "invalid_arguments");
                    replacement = QuoteQsc(text);
                } else if (field == "text_resource" || field == "completion_expression" || field == "failure_expression") {
                    if (field == "completion_expression") argument += 2;
                    if (field == "failure_expression") argument += 3;
                    std::string text;
                    if (!ReadString(value, text)) return Failure(error, "invalid_arguments");
                    replacement = QuoteQsc(text);
                } else if (field == "link_task_id") {
                    std::int64_t link = 0;
                    if (!ReadInteger(value, link, std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max()))
                        return Failure(error, "invalid_arguments");
                    if (link != -1) {
                        std::string link_error;
                        const JsonValue linked = service.GetObject(level, std::to_string(link), link_error);
                        if (!link_error.empty()) return Failure(error, "unknown_task_id");
                        if (linked.contains("writable") &&
                            (!linked.at("writable").is_bool() || !linked.at("writable").as_bool()))
                            return Failure(error, "ambiguous_task_id");
                    }
                    ++argument;
                    replacement = std::to_string(link);
                }
                if (argument >= target->arguments.size() || target->arguments[argument].begin == target->arguments[argument].end)
                    return Failure(error, "unsupported_operation");
                patches.push_back({target->arguments[argument], std::move(replacement)});
            }
            std::sort(patches.begin(), patches.end(), [](const Patch& left, const Patch& right) {
                return left.span.begin > right.span.begin;
            });
            for (const Patch& patch : patches)
                source.replace(patch.span.begin, patch.span.end - patch.span.begin, patch.replacement);
            if (!ParseSource(source, parsed, domain_error)) return Failure(error, domain_error);
            if (!service.SaveCurrentObjectSource(source, options, domain_error)) return DomainFailure(error, domain_error);

            std::string after_error;
            const JsonValue all_after = ObjectiveListFromSource(source, level, revision.fingerprint, after_error);
            if (!after_error.empty()) return Failure(error, after_error);
            const auto& definitions_after = all_after.at("definitions").as_array();
            JsonValue after = definitions_after[static_cast<std::size_t>(definition_index)];
            JsonValue::Object result{
                {"tool", JsonValue("mission_objective_update")},
                {"changed", JsonValue(true)},
                {"dry_run", JsonValue(options.dry_run)},
                {"definition_index", JsonValue(static_cast<int>(definition_index))},
                {"objective_index", JsonValue(static_cast<int>(objective_index))},
                {"before", before},
                {"after", after},
                {"changed_fields", [&] {
                    JsonValue::Array fields;
                    for (const auto& [field, ignored] : arguments.at("fields").as_object()) fields.emplace_back(field);
                    return JsonValue(std::move(fields));
                }()},
                {"revision_before", JsonValue(revision.fingerprint)},
                {"revision_after", JsonValue(service.CurrentRevision().fingerprint)},
            };
            return result;
        }

        return Failure(error, "unknown_tool");
    } catch (const std::exception&) {
        return Failure(error, "service_error");
    }
}

}  // namespace mcp
