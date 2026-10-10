#include "mcp_tools_ai.h"

#include "../level/qsc_lexer.h"
#include "../level/qsc_parser.h"
#include "mcp_task_id.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
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
    SourceSpan full;
    int source_line = 0;
    std::vector<SourceSpan> arguments;
    std::string id;
    std::string parent_id;
    std::string type;
};

struct Patch {
    SourceSpan span;
    std::string replacement;
};

JsonValue EmptyObjectSchema() {
    return JsonValue::Object{
        {"type", JsonValue("object")},
        {"properties", JsonValue::Object{}},
        {"additionalProperties", JsonValue(false)},
    };
}

void AddMutationSchemaProperties(JsonValue::Object& properties) {
    properties["level"] = JsonValue::Object{{"type", JsonValue("integer")}, {"minimum", JsonValue(1)}};
    properties["dry_run"] = JsonValue::Object{{"type", JsonValue("boolean")}};
    properties["backup"] = JsonValue::Object{{"type", JsonValue("boolean")}};
    properties["expected_revision"] = JsonValue::Object{{"type", JsonValue("string")}};
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

JsonValue StringSchema() {
    return JsonValue::Object{{"type", JsonValue("string")}};
}

JsonValue FieldsSchema(std::initializer_list<std::string_view> fields) {
    JsonValue::Object properties;
    for (const std::string_view field : fields) properties[std::string(field)] = JsonValue::Object{};
    return JsonValue::Object{
        {"type", JsonValue("object")},
        {"properties", std::move(properties)},
        {"additionalProperties", JsonValue(false)},
    };
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

bool ReadString(const JsonValue& value, std::string& result, std::size_t maximum = 4096) {
    if (!value.is_string() || value.as_string().empty() || value.as_string().size() > maximum)
        return false;
    for (const unsigned char character : value.as_string()) {
        if (character < 0x20u) return false;
    }
    result = value.as_string();
    return true;
}

bool ReadOptionalString(const JsonValue& value, std::string& result, std::size_t maximum = 4096) {
    if (!value.is_string() || value.as_string().size() > maximum) return false;
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
        number < static_cast<double>(minimum) || number > static_cast<double>(maximum)) {
        return false;
    }
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
        if (!ReadString(arguments.at("expected_revision"), revision, 256)) return false;
        options.expected_revision = std::move(revision);
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

bool IsIdentifierStart(char character) {
    return (character >= 'A' && character <= 'Z') ||
           (character >= 'a' && character <= 'z') || character == '_';
}

bool IsIdentifierPart(char character) {
    return IsIdentifierStart(character) || (character >= '0' && character <= '9');
}

std::size_t SkipQuoted(std::string_view source, std::size_t position) {
    ++position;
    bool escaped = false;
    while (position < source.size()) {
        const char character = source[position++];
        if (escaped) {
            escaped = false;
        } else if (character == '\\') {
            escaped = true;
        } else if (character == '"') {
            break;
        }
    }
    return position;
}

std::size_t SkipComment(std::string_view source, std::size_t position) {
    if (position + 1 >= source.size() || source[position] != '/') return position;
    if (source[position + 1] == '/') {
        position += 2;
        while (position < source.size() && source[position] != '\n') ++position;
        return position;
    }
    if (source[position + 1] == '*') {
        position += 2;
        while (position + 1 < source.size() &&
               !(source[position] == '*' && source[position + 1] == '/')) ++position;
        return position + 1 < source.size() ? position + 2 : source.size();
    }
    return position;
}

SourceSpan TrimSpan(std::string_view source, SourceSpan span) {
    while (span.begin < span.end && std::isspace(static_cast<unsigned char>(source[span.begin]))) ++span.begin;
    while (span.end > span.begin && std::isspace(static_cast<unsigned char>(source[span.end - 1]))) --span.end;
    return span;
}

std::string Unquote(std::string value);
std::string TrimmedText(std::string_view source, SourceSpan span);
std::string AnonymousTaskSignature(std::string_view source, const std::vector<SourceSpan>& arguments);

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
        std::size_t close = open + 1;
        std::size_t argument_begin = close;
        std::vector<SourceSpan> arguments;
        while (close < source.size() && depth > 0) {
            if (source[close] == '"') {
                close = SkipQuoted(source, close);
                continue;
            }
            if (close + 1 < source.size() && source[close] == '/' &&
                (source[close + 1] == '/' || source[close + 1] == '*')) {
                close = SkipComment(source, close);
                continue;
            }
            if (source[close] == '(') {
                ++depth;
            } else if (source[close] == ')') {
                --depth;
                if (depth == 0) break;
            } else if (source[close] == ',' && depth == 1) {
                arguments.push_back(TrimSpan(source, {argument_begin, close}));
                argument_begin = close + 1;
            }
            ++close;
        }
        if (depth != 0) continue;
        const SourceSpan last = TrimSpan(source, {argument_begin, close});
        if (last.begin != last.end || !arguments.empty()) arguments.push_back(last);
        const int source_line = static_cast<int>(
            std::count(source.begin(), source.begin() + name_begin, '\n')) + 1;
        calls.push_back({name, {name_begin, close + 1}, source_line, std::move(arguments)});
    }
    std::unordered_map<std::string, int> next_suffix;
    std::unordered_set<std::string> used_ids;
    for (std::size_t index = 0; index < calls.size(); ++index) {
        CallSpan& call = calls[index];
        if (call.name != "Task_New" || call.arguments.size() < 2) continue;
        call.id = Unquote(TrimmedText(source, call.arguments[0]));
        call.type = Unquote(TrimmedText(source, call.arguments[1]));
        if (call.id.empty()) call.id = "anonymous";
        std::size_t parent_end = std::numeric_limits<std::size_t>::max();
        for (std::size_t parent = 0; parent < index; ++parent) {
            if (calls[parent].name == "Task_New" && calls[parent].full.begin < call.full.begin &&
                calls[parent].full.end > call.full.end && calls[parent].full.end < parent_end) {
                call.parent_id = calls[parent].id;
                parent_end = calls[parent].full.end;
            }
        }
        if (call.id == "-1" || call.id == "anonymous") {
            const auto marker = AnonymousTaskMarkerBefore(source, call.full.begin);
            call.id = marker.value_or(AnonymousTaskId(call.parent_id, AnonymousTaskSignature(source, call.arguments)));
        }
        call.id = UniqueTaskId(call.id, next_suffix, used_ids);
    }
    return calls;
}

std::string SpanText(std::string_view source, SourceSpan span) {
    return std::string(source.substr(span.begin, span.end - span.begin));
}

std::string TrimmedText(std::string_view source, SourceSpan span) {
    return SpanText(source, TrimSpan(source, span));
}

std::string AnonymousTaskSignature(std::string_view source, const std::vector<SourceSpan>& arguments) {
    std::string signature;
    for (std::size_t index = 1; index < arguments.size(); ++index) {
        signature.push_back('|');
        signature += AnonymousArgumentSignature(TrimmedText(source, arguments[index]));
    }
    return signature;
}

std::string Unquote(std::string value) {
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') return value;
    std::string result;
    result.reserve(value.size() - 2);
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

const CallSpan* FindTaskCallInSource(const std::vector<CallSpan>& calls,
                                     std::string_view task_id, std::string_view task_type) {
    for (const auto& call : calls) {
        if (call.name == "Task_New" && call.id == task_id && call.type == task_type) {
            return &call;
        }
    }
    return nullptr;
}

bool AddArgumentPatch(const CallSpan& call, std::size_t index,
                      std::string replacement, std::vector<Patch>& patches) {
    if (index >= call.arguments.size() || call.arguments[index].begin == call.arguments[index].end)
        return false;
    patches.push_back({call.arguments[index], std::move(replacement)});
    return true;
}

bool ApplyPatches(std::string& source, std::vector<Patch> patches) {
    std::sort(patches.begin(), patches.end(), [](const Patch& left, const Patch& right) {
        return left.span.begin > right.span.begin;
    });
    for (std::size_t index = 1; index < patches.size(); ++index) {
        if (patches[index - 1].span.begin < patches[index].span.end) return false;
    }
    for (const Patch& patch : patches) {
        source.replace(patch.span.begin, patch.span.end - patch.span.begin, patch.replacement);
    }
    return true;
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

bool ValidateQsc(std::string_view source, std::string& code) {
    if (source.size() > kMaxJsonMessageBytes) {
        code = "source_too_large";
        return false;
    }
    const qsc::LexResult lexed = qsc::Lex(std::string(source));
    if (!lexed.ok) {
        code = "qsc_lex_failed";
        return false;
    }
    const qsc::ParseResult parsed = qsc::Parse(lexed.tokens);
    if (!parsed.ok || !parsed.program) {
        code = "qsc_parse_failed";
        return false;
    }
    code.clear();
    return true;
}

bool IsAiObject(std::string_view type) {
    return type == "HumanAI" || type == "HumanSoldier" || type == "HumanSoldierFemale" ||
           type == "HumanPlayer" || type == "AISquad";
}

JsonValue ScriptValidationResult(std::string_view source) {
    std::string code;
    const bool valid = ValidateQsc(source, code);
    JsonValue::Object result{{"valid", valid}};
    if (!valid) result["error"] = code;
    return result;
}

JsonValue MakeMutationResult(const JsonValue& before, std::string_view task_id,
                             bool dry_run, const LevelRevision& before_revision,
                             const LevelRevision& after_revision,
                             std::string_view tool, JsonValue changed_fields,
                             JsonValue after) {
    return JsonValue::Object{
        {"tool", JsonValue(tool)},
        {"changed", JsonValue(true)},
        {"dry_run", JsonValue(dry_run)},
        {"task_id", JsonValue(task_id)},
        {"changed_fields", std::move(changed_fields)},
        {"revision_before", JsonValue(before_revision.fingerprint)},
        {"revision_after", JsonValue(after_revision.fingerprint)},
        {"before", before},
        {"after", std::move(after)},
    };
}

JsonValue MakeFieldMutationResult(std::string_view operation, bool dry_run,
                                  const LevelRevision& before_revision,
                                  const LevelRevision& after_revision,
                                  JsonValue changed_fields, JsonValue before,
                                  JsonValue after) {
    return JsonValue::Object{
        {"tool", JsonValue(operation)},
        {"changed", JsonValue(true)},
        {"dry_run", JsonValue(dry_run)},
        {"changed_fields", std::move(changed_fields)},
        {"revision_before", JsonValue(before_revision.fingerprint)},
        {"revision_after", JsonValue(after_revision.fingerprint)},
        {"before", std::move(before)},
        {"after", std::move(after)},
    };
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

bool ReadTaskId(const JsonValue& arguments, std::string& task_id) {
    return arguments.contains("task_id") && ReadString(arguments.at("task_id"), task_id, 128);
}

bool ReadWeaponId(const JsonValue& value, std::string& weapon_id) {
    if (!ReadString(value, weapon_id, 64)) return false;
    return weapon_id.starts_with("WEAPON_ID_");
}

bool IsWritableObject(const JsonValue& object) {
    return !object.contains("writable") ||
           (object.at("writable").is_bool() && object.at("writable").as_bool());
}

bool GraphIdExists(GameDataService& service, int level, std::int64_t graph_id,
                   std::string& error) {
    const JsonValue manifest = service.LevelManifest(level, error);
    if (!error.empty() || !manifest.is_object() || !manifest.contains("files")) return false;
    const std::string expected = "graph" + std::to_string(graph_id) + ".dat";
    for (const auto& file : manifest.at("files").as_array()) {
        if (!file.is_object() || !file.contains("path") || !file.at("path").is_string()) continue;
        const std::string path = file.at("path").as_string();
        const std::size_t slash = path.find_last_of("/\\");
        const std::string name = path.substr(slash == std::string::npos ? 0 : slash + 1);
        if (name == expected) {
            error.clear();
            return true;
        }
    }
    error = "unknown_graph";
    return false;
}

JsonValue WeaponListResult(const JsonValue& snapshot) {
    std::map<std::string, std::set<std::string>> weapons;
    for (const auto& object : snapshot.at("objects").as_array()) {
        const std::string task_id = object.at("id").as_string();
        for (const auto& argument : object.at("args").as_array()) {
            if (!argument.is_string() || !argument.as_string().starts_with("WEAPON_ID_")) continue;
            weapons[argument.as_string()].insert(task_id);
        }
    }
    JsonValue::Array values;
    for (const auto& [weapon_id, task_ids] : weapons) {
        JsonValue::Array ids;
        for (const std::string& task_id : task_ids) ids.emplace_back(task_id);
        values.emplace_back(JsonValue::Object{
            {"weapon_id", JsonValue(weapon_id)},
            {"task_ids", JsonValue(std::move(ids))},
        });
    }
    return JsonValue::Object{
        {"level", snapshot.at("level")},
        {"revision", snapshot.at("revision")},
        {"weapons", JsonValue(std::move(values))},
        {"source", JsonValue("level_snapshot")},
    };
}

}  // namespace

ToolDefinitionList AiToolDefinitions() {
    JsonValue::Object ai_get{{"level", JsonValue::Object{{"type", JsonValue("integer")}, {"minimum", JsonValue(1)}}}, {"task_id", StringSchema()}};
    JsonValue::Object ai_update{{"task_id", StringSchema()}, {"fields", FieldsSchema({"ai_type", "team", "graph_id"})}};
    AddMutationSchemaProperties(ai_update);
    JsonValue::Object set_script{{"task_id", StringSchema()}, {"source", StringSchema()}};
    AddMutationSchemaProperties(set_script);
    JsonValue::Object validate_script{{"source", StringSchema()}};
    JsonValue::Object compile_script{{"level", JsonValue::Object{{"type", JsonValue("integer")}, {"minimum", JsonValue(1)}}}, {"source", StringSchema()}, {"expected_revision", StringSchema()}};
    JsonValue::Object list_weapons{{"level", JsonValue::Object{{"type", JsonValue("integer")}, {"minimum", JsonValue(1)}}}};
    const JsonValue loadout_item = ObjectSchema(
        JsonValue::Object{{"task_id", StringSchema()}, {"weapon_id", StringSchema()}},
        {"task_id", "weapon_id"});
    JsonValue::Object loadout{{"task_id", StringSchema()}, {"loadout", JsonValue::Object{
        {"type", JsonValue("array")}, {"items", loadout_item}}}};
    AddMutationSchemaProperties(loadout);
    const JsonValue batch_item = ObjectSchema(
        JsonValue::Object{{"task_id", StringSchema()}, {"loadout", JsonValue::Object{
            {"type", JsonValue("array")}, {"minItems", JsonValue(1)}, {"items", loadout_item}}}},
        {"task_id", "loadout"});
    JsonValue::Object batch{{"operations", JsonValue::Object{
        {"type", JsonValue("array")}, {"minItems", JsonValue(1)}, {"items", batch_item}}}};
    AddMutationSchemaProperties(batch);
    JsonValue::Object pickup{{"task_id", StringSchema()}, {"fields", FieldsSchema({"weapon_id", "ammo_id", "count"})}};
    AddMutationSchemaProperties(pickup);
    return ToolDefinitionList{
        {"ai_get", ObjectSchema(std::move(ai_get), {"task_id"})},
        {"ai_update", ObjectSchema(std::move(ai_update), {"task_id", "fields"})},
        {"ai_set_script", ObjectSchema(std::move(set_script), {"task_id", "source"})},
        {"ai_validate_script", ObjectSchema(std::move(validate_script), {"source"})},
        {"ai_compile_script", ObjectSchema(std::move(compile_script), {"source"})},
        {"ai_list_weapons", ObjectSchema(std::move(list_weapons))},
        {"ai_set_weapon_loadout", ObjectSchema(std::move(loadout), {"task_id", "loadout"})},
        {"ai_batch_set_loadout", ObjectSchema(std::move(batch), {"operations"})},
        {"pickup_create_or_update", ObjectSchema(std::move(pickup), {"task_id", "fields"})},
    };
}

JsonValue CallAiTool(GameDataService& service, std::string_view name,
                     const JsonValue& arguments, std::string& error) {
    error.clear();
    try {
        if (name == "ai_validate_script") {
            if (!HasOnlyKeys(arguments, {"source"}) || !arguments.contains("source"))
                return Failure(error, "invalid_arguments");
            std::string source;
            if (!ReadOptionalString(arguments.at("source"), source, kMaxJsonMessageBytes))
                return Failure(error, "invalid_arguments");
            return ScriptValidationResult(source);
        }

        if (name == "ai_compile_script") {
            if (!HasOnlyKeys(arguments, {"level", "source", "expected_revision"}) || !arguments.contains("source"))
                return Failure(error, "invalid_arguments");
            std::string source;
            if (!ReadOptionalString(arguments.at("source"), source, kMaxJsonMessageBytes))
                return Failure(error, "invalid_arguments");
            const JsonValue validation = ScriptValidationResult(source);
            if (!validation.at("valid").as_bool()) {
                JsonValue::Object result = validation.as_object();
                result["compiled"] = false;
                return result;
            }
            int level = 0;
            if (!CurrentLevel(service, arguments, level, error)) return JsonValue(nullptr);
            MutationOptions options;
            options.dry_run = true;
            options.backup = false;
            if (arguments.contains("expected_revision")) {
                if (!ReadMutationOptions(arguments, options)) return Failure(error, "invalid_arguments");
            }
            std::string compile_error;
            if (!service.SaveCurrentObjectSource(source, options, compile_error)) {
                if (compile_error == "qvm_compile_failed" || compile_error == "validation_failed") {
                    return JsonValue::Object{{"valid", true}, {"compiled", false}, {"error", compile_error}};
                }
                return DomainFailure(error, compile_error);
            }
            return JsonValue::Object{{"valid", true}, {"compiled", true}, {"dry_run", true}};
        }

        if (name == "ai_list_weapons") {
            if (!HasOnlyKeys(arguments, {"level"})) return Failure(error, "invalid_arguments");
            int level = 0;
            if (!CurrentLevel(service, arguments, level, error)) return JsonValue(nullptr);
            const LevelRevision revision = service.CurrentRevision();
            std::string domain_error;
            const JsonValue snapshot = service.ListObjects(level, domain_error);
            if (!domain_error.empty()) return DomainFailure(error, domain_error);
            return WeaponListResult(snapshot);
        }

        if (name == "ai_get") {
            if (!HasOnlyKeys(arguments, {"level", "task_id"})) return Failure(error, "invalid_arguments");
            std::string task_id;
            if (!ReadTaskId(arguments, task_id)) return Failure(error, "invalid_arguments");
            int level = 0;
            if (!CurrentLevel(service, arguments, level, error)) return JsonValue(nullptr);
            std::string domain_error;
            const JsonValue object = service.GetObject(level, task_id, domain_error);
            if (!domain_error.empty()) return DomainFailure(error, domain_error);
            if (!IsAiObject(object.at("type").as_string())) return Failure(error, "unsupported_operation");
            return object;
        }

        if (name == "ai_set_script") {
            if (!HasOnlyKeys(arguments, {"level", "task_id", "source", "dry_run", "backup", "expected_revision"}) ||
                !arguments.contains("source"))
                return Failure(error, "invalid_arguments");
            std::string task_id;
            std::string source;
            if (!ReadTaskId(arguments, task_id) || !ReadOptionalString(arguments.at("source"), source, kMaxJsonMessageBytes))
                return Failure(error, "invalid_arguments");
            MutationOptions ignored_options;
            if (!ReadMutationOptions(arguments, ignored_options)) return Failure(error, "invalid_arguments");
            std::string domain_error;
            int level = 0;
            if (!CurrentLevel(service, arguments, level, error)) return JsonValue(nullptr);
            const JsonValue object = service.GetObject(level, task_id, domain_error);
            if (!domain_error.empty()) return DomainFailure(error, domain_error);
            if (!IsAiObject(object.at("type").as_string())) return Failure(error, "unsupported_operation");
            const JsonValue validation = ScriptValidationResult(source);
            if (!validation.at("valid").as_bool()) return validation;
            return Failure(error, "unsupported_operation");
        }

        if (name == "ai_update") {
            if (!HasOnlyKeys(arguments, {"level", "task_id", "fields", "dry_run", "backup", "expected_revision"}) ||
                !arguments.contains("fields") || !HasOnlyKeys(arguments.at("fields"), {"ai_type", "team", "graph_id"}))
                return Failure(error, "invalid_arguments");
            std::string task_id;
            if (!ReadTaskId(arguments, task_id) || arguments.at("fields").as_object().empty())
                return Failure(error, "invalid_arguments");
            MutationOptions options;
            if (!ReadMutationOptions(arguments, options)) return Failure(error, "invalid_arguments");
            int level = 0;
            if (!CurrentLevel(service, arguments, level, error)) return JsonValue(nullptr);
            const LevelRevision revision_before = service.CurrentRevision();
            std::string domain_error;
            const JsonValue before = service.GetObject(level, task_id, domain_error);
            if (!domain_error.empty()) return DomainFailure(error, domain_error);
            if (!IsWritableObject(before)) return Failure(error, "ambiguous_task_id");
            const std::string type = before.at("type").as_string();
            if (!IsAiObject(type)) return Failure(error, "unsupported_operation");

            std::string source;
            if (!service.LoadCurrentObjectSource(source, domain_error)) return DomainFailure(error, domain_error);
            const auto calls = ScanCalls(source);
            const CallSpan* call = FindTaskCallInSource(calls, task_id, type);
            if (!call) return Failure(error, "unsupported_operation");
            std::vector<Patch> patches;
            for (const auto& [field, value] : arguments.at("fields").as_object()) {
                if (field == "ai_type") {
                    std::string ai_type;
                    if (type != "HumanAI") return Failure(error, "unsupported_operation");
                    if (!ReadString(value, ai_type, 256))
                        return Failure(error, "invalid_arguments");
                    if (!AddArgumentPatch(*call, 3, QuoteQsc(ai_type), patches))
                        return Failure(error, "unsupported_operation");
                } else if (field == "graph_id") {
                    std::int64_t graph_id = 0;
                    if (type != "HumanAI") return Failure(error, "unsupported_operation");
                    if (!ReadInteger(value, graph_id, 0, std::numeric_limits<std::int32_t>::max()))
                        return Failure(error, "invalid_arguments");
                    if (!GraphIdExists(service, level, graph_id, domain_error)) {
                        if (!domain_error.empty()) return DomainFailure(error, domain_error);
                        return Failure(error, "unknown_graph");
                    }
                    if (!AddArgumentPatch(*call, 4, std::to_string(graph_id), patches))
                        return Failure(error, "unsupported_operation");
                } else if (field == "team") {
                    std::int64_t team = 0;
                    if (type != "HumanSoldier" && type != "HumanSoldierFemale" && type != "HumanPlayer")
                        return Failure(error, "unsupported_operation");
                    if (!ReadInteger(value, team, std::numeric_limits<std::int32_t>::min(),
                                     std::numeric_limits<std::int32_t>::max()))
                        return Failure(error, "invalid_arguments");
                    if (!AddArgumentPatch(*call, 8, std::to_string(team), patches))
                        return Failure(error, "unsupported_operation");
                }
            }
            if (!ApplyPatches(source, std::move(patches))) return Failure(error, "unsupported_operation");
            if (before.at("id").is_string() && before.at("id").as_string().starts_with("anon-"))
                AddAnonymousTaskMarker(source, call->full.begin, task_id);
            if (!options.dry_run && !ValidateQsc(source, domain_error)) return Failure(error, domain_error);
            if (!service.SaveCurrentObjectSource(source, options, domain_error)) return DomainFailure(error, domain_error);
            const LevelRevision revision_after = service.CurrentRevision();
            JsonValue::Array changed_fields;
            for (const auto& [field, ignored] : arguments.at("fields").as_object()) changed_fields.emplace_back(field);
            const JsonValue after = service.ObjectSnapshotFromSource(
                level, source, task_id, call->source_line, domain_error);
            if (!domain_error.empty()) return DomainFailure(error, domain_error);
            return MakeMutationResult(before, task_id, options.dry_run, revision_before,
                                      revision_after, "ai_update", std::move(changed_fields), after);
        }

        if (name == "ai_set_weapon_loadout") {
            if (!HasOnlyKeys(arguments, {"level", "task_id", "loadout", "dry_run", "backup", "expected_revision"}) ||
                !arguments.contains("loadout") || !arguments.at("loadout").is_array())
                return Failure(error, "invalid_arguments");
            std::string parent_id;
            if (!ReadTaskId(arguments, parent_id) || arguments.at("loadout").as_array().empty())
                return Failure(error, "invalid_arguments");
            MutationOptions options;
            if (!ReadMutationOptions(arguments, options)) return Failure(error, "invalid_arguments");
            int level = 0;
            if (!CurrentLevel(service, arguments, level, error)) return JsonValue(nullptr);
            const LevelRevision revision = service.CurrentRevision();
            std::string domain_error;
            const JsonValue parent = service.GetObject(level, parent_id, domain_error);
            if (!domain_error.empty()) return DomainFailure(error, domain_error);
            if (!IsWritableObject(parent)) return Failure(error, "ambiguous_task_id");
            if (parent.at("type").as_string() != "HumanSoldier" &&
                parent.at("type").as_string() != "HumanSoldierFemale" &&
                parent.at("type").as_string() != "HumanSoldierRPG" &&
                parent.at("type").as_string() != "HumanAI" &&
                parent.at("type").as_string() != "HumanPlayer") return Failure(error, "unsupported_operation");
            std::string source;
            if (!service.LoadCurrentObjectSource(source, domain_error)) return DomainFailure(error, domain_error);
            const auto calls = ScanCalls(source);
            std::vector<Patch> patches;
            std::set<std::string> seen;
            std::vector<std::string> child_ids;
            std::vector<std::pair<std::size_t, std::string>> anonymous_markers;
            JsonValue::Array before_loadout;
            for (const auto& item : arguments.at("loadout").as_array()) {
                if (!HasOnlyKeys(item, {"task_id", "weapon_id"})) return Failure(error, "invalid_arguments");
                std::string child_id;
                std::string weapon_id;
                if (!ReadTaskId(item, child_id) || !item.contains("weapon_id") || !ReadWeaponId(item.at("weapon_id"), weapon_id) ||
                    !seen.insert(child_id).second) return Failure(error, "invalid_arguments");
                if (!service.IsAvailableWeaponId(weapon_id, domain_error))
                    return DomainFailure(error, domain_error);
                const JsonValue child = service.GetObject(level, child_id, domain_error);
                if (!domain_error.empty()) return DomainFailure(error, domain_error);
                child_ids.push_back(child_id);
                before_loadout.emplace_back(JsonValue::Object{{"task_id", child_id}, {"object", child}});
                if (!IsWritableObject(child)) return Failure(error, "ambiguous_task_id");
                if (child.at("parent_id").is_null() || child.at("parent_id").as_string() != parent_id ||
                    !child.at("type").as_string().starts_with("Gun")) return Failure(error, "unsupported_operation");
                const CallSpan* call = FindTaskCallInSource(calls, child_id, child.at("type").as_string());
                if (!call) return Failure(error, "unsupported_operation");
                if (child_id.starts_with("anon-")) anonymous_markers.emplace_back(call->full.begin, child_id);
                std::size_t weapon_argument = call->arguments.size();
                for (std::size_t index = 3; index < call->arguments.size(); ++index) {
                    if (Unquote(TrimmedText(source, call->arguments[index])).starts_with("WEAPON_ID_")) {
                        weapon_argument = index;
                        break;
                    }
                }
                if (weapon_argument == call->arguments.size() || !AddArgumentPatch(*call, weapon_argument, QuoteQsc(weapon_id), patches))
                    return Failure(error, "unsupported_operation");
            }
            if (!ApplyPatches(source, std::move(patches))) return Failure(error, "unsupported_operation");
            if (parent_id.starts_with("anon-")) {
                const CallSpan* parent_call = FindTaskCallInSource(calls, parent_id, parent.at("type").as_string());
                if (parent_call) anonymous_markers.emplace_back(parent_call->full.begin, parent_id);
            }
            std::sort(anonymous_markers.begin(), anonymous_markers.end(),
                      [](const auto& left, const auto& right) { return left.first > right.first; });
            for (const auto& [position, id] : anonymous_markers)
                AddAnonymousTaskMarker(source, position, id);
            if (!service.SaveCurrentObjectSource(source, options, domain_error)) return DomainFailure(error, domain_error);
            const LevelRevision revision_after = service.CurrentRevision();
            JsonValue::Array changed_fields;
            for (const std::string& child_id : child_ids) changed_fields.emplace_back(child_id);
            JsonValue after = options.dry_run
                ? service.ObjectSnapshotFromSource(level, source, parent_id, domain_error)
                : service.GetObject(level, parent_id, domain_error);
            if (!domain_error.empty()) return DomainFailure(error, domain_error);
            JsonValue::Array after_loadout;
            for (const std::string& child_id : child_ids) {
                JsonValue child_after = options.dry_run
                    ? service.ObjectSnapshotFromSource(level, source, child_id, domain_error)
                    : service.GetObject(level, child_id, domain_error);
                if (!domain_error.empty()) return DomainFailure(error, domain_error);
                after_loadout.emplace_back(JsonValue::Object{{"task_id", child_id}, {"object", child_after}});
            }
            JsonValue before_snapshot = parent;
            before_snapshot["loadout"] = JsonValue(std::move(before_loadout));
            after["loadout"] = JsonValue(std::move(after_loadout));
            JsonValue result = MakeFieldMutationResult("ai_set_weapon_loadout", options.dry_run, revision,
                                                       revision_after, std::move(changed_fields),
                                                       before_snapshot, after);
            result["task_id"] = parent_id;
            result["loadout_count"] = static_cast<int>(seen.size());
            return result;
        }

        if (name == "ai_batch_set_loadout") {
            if (!HasOnlyKeys(arguments, {"operations", "dry_run", "backup", "expected_revision"}) ||
                !arguments.contains("operations") || !arguments.at("operations").is_array() ||
                arguments.at("operations").as_array().empty()) return Failure(error, "invalid_arguments");
            for (const auto& operation : arguments.at("operations").as_array()) {
                if (!HasOnlyKeys(operation, {"task_id", "loadout"}) ||
                    !operation.contains("task_id") || !operation.contains("loadout") ||
                    !operation.at("loadout").is_array() || operation.at("loadout").as_array().empty())
                    return Failure(error, "invalid_arguments");
            }
            MutationOptions options;
            if (!ReadMutationOptions(arguments, options)) return Failure(error, "invalid_arguments");
            JsonValue::Array preview;
            for (const auto& operation : arguments.at("operations").as_array()) {
                JsonValue::Object preview_arguments = operation.as_object();
                preview_arguments["dry_run"] = true;
                preview_arguments["backup"] = false;
                if (arguments.contains("expected_revision"))
                    preview_arguments["expected_revision"] = arguments.at("expected_revision");
                std::string preview_error;
                const JsonValue result = CallAiTool(service, "ai_set_weapon_loadout",
                                                    preview_arguments, preview_error);
                if (!preview_error.empty()) {
                    error = preview_error;
                    return JsonValue(nullptr);
                }
                preview.emplace_back(result);
            }
            if (options.dry_run) {
                return JsonValue::Object{{"tool", "ai_batch_set_loadout"}, {"dry_run", true},
                                         {"operations", std::move(preview)}};
            }
            JsonValue::Array applied;
            bool first = true;
            std::size_t operation_index = 0;
            for (const auto& operation : arguments.at("operations").as_array()) {
                JsonValue::Object applied_arguments = operation.as_object();
                applied_arguments["backup"] = options.backup;
                if (first && arguments.contains("expected_revision"))
                    applied_arguments["expected_revision"] = arguments.at("expected_revision");
                first = false;
                std::string applied_error;
                const JsonValue result = CallAiTool(service, "ai_set_weapon_loadout",
                                                    applied_arguments, applied_error);
                if (!applied_error.empty()) {
                    error = "batch_partial_failure";
                    return JsonValue::Object{
                        {"tool", "ai_batch_set_loadout"},
                        {"dry_run", false},
                        {"applied", std::move(applied)},
                        {"failed_index", static_cast<int>(operation_index)},
                        {"failed_error", applied_error},
                    };
                }
                applied.emplace_back(result);
                ++operation_index;
            }
            return JsonValue::Object{{"tool", "ai_batch_set_loadout"}, {"dry_run", false},
                                     {"operations", std::move(applied)}};
        }

        if (name == "pickup_create_or_update") {
            if (!HasOnlyKeys(arguments, {"level", "task_id", "fields", "dry_run", "backup", "expected_revision"}) ||
                !arguments.contains("fields") || !HasOnlyKeys(arguments.at("fields"), {"weapon_id", "ammo_id", "count"}) ||
                arguments.at("fields").as_object().empty()) return Failure(error, "invalid_arguments");
            std::string task_id;
            if (!ReadTaskId(arguments, task_id)) return Failure(error, "invalid_arguments");
            MutationOptions options;
            if (!ReadMutationOptions(arguments, options)) return Failure(error, "invalid_arguments");
            int level = 0;
            if (!CurrentLevel(service, arguments, level, error)) return JsonValue(nullptr);
            const LevelRevision revision = service.CurrentRevision();
            std::string domain_error;
            const JsonValue before = service.GetObject(level, task_id, domain_error);
            if (!domain_error.empty()) return DomainFailure(error, domain_error);
            if (!IsWritableObject(before)) return Failure(error, "ambiguous_task_id");
            const std::string type = before.at("type").as_string();
            if (type != "GunPickup" && type != "AmmoPickup") return Failure(error, "unsupported_operation");
            std::string source;
            if (!service.LoadCurrentObjectSource(source, domain_error)) return DomainFailure(error, domain_error);
            const auto calls = ScanCalls(source);
            const CallSpan* call = FindTaskCallInSource(calls, task_id, type);
            if (!call) return Failure(error, "unsupported_operation");
            std::vector<Patch> patches;
            for (const auto& [field, value] : arguments.at("fields").as_object()) {
                if (field == "weapon_id") {
                    std::string id;
                    if (type != "GunPickup") return Failure(error, "unsupported_operation");
                    if (!ReadWeaponId(value, id)) return Failure(error, "invalid_arguments");
                    if (!service.IsAvailableWeaponId(id, domain_error))
                        return DomainFailure(error, domain_error);
                    if (!AddArgumentPatch(*call, 9, QuoteQsc(id), patches))
                        return Failure(error, "unsupported_operation");
                } else if (field == "ammo_id") {
                    std::string id;
                    if (type != "AmmoPickup") return Failure(error, "unsupported_operation");
                    if (!ReadString(value, id, 64) || !id.starts_with("AMMO_ID_"))
                        return Failure(error, "invalid_arguments");
                    if (!service.IsAvailableAmmoId(id, domain_error))
                        return DomainFailure(error, domain_error);
                    if (!AddArgumentPatch(*call, 9, QuoteQsc(id), patches))
                        return Failure(error, "unsupported_operation");
                } else if (field == "count") {
                    std::int64_t count = 0;
                    if (!ReadInteger(value, count, 0, std::numeric_limits<std::int16_t>::max()))
                        return Failure(error, "invalid_arguments");
                    if (!AddArgumentPatch(*call, 10, std::to_string(count), patches))
                        return Failure(error, "unsupported_operation");
                }
            }
            if (!ApplyPatches(source, std::move(patches)))
                return Failure(error, "unsupported_operation");
            if (before.at("id").is_string() && before.at("id").as_string().starts_with("anon-"))
                AddAnonymousTaskMarker(source, call->full.begin, task_id);
            if (!service.SaveCurrentObjectSource(source, options, domain_error))
                return domain_error.empty() ? Failure(error, "unsupported_operation") : DomainFailure(error, domain_error);
            const LevelRevision revision_after = service.CurrentRevision();
            JsonValue::Array changed_fields;
            for (const auto& [field, ignored] : arguments.at("fields").as_object()) changed_fields.emplace_back(field);
            const JsonValue after = service.ObjectSnapshotFromSource(
                level, source, task_id, call->source_line, domain_error);
            if (!domain_error.empty()) return DomainFailure(error, domain_error);
            return MakeMutationResult(before, task_id, options.dry_run, revision,
                                      revision_after, "pickup_create_or_update",
                                      std::move(changed_fields), after);
        }

        return Failure(error, "unknown_tool");
    } catch (const std::exception&) {
        return Failure(error, "service_error");
    }
}

}  // namespace mcp
