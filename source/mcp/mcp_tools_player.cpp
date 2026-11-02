#include "mcp_tools_player.h"

#include "mcp_transaction.h"

#include "../level/qsc_lexer.h"
#include "../level/qsc_parser.h"
#include "../level/qvm_compiler.h"
#include "../level/qvm_parser.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mcp {
namespace {

constexpr std::string_view kPlayerQsc = "humanplayer/humanplayer.qsc";

struct Span {
    std::size_t begin = 0;
    std::size_t end = 0;
};

struct Call {
    std::string name;
    Span full;
    std::vector<Span> arguments;
};

struct Patch {
    Span span;
    std::string replacement;
};

JsonValue EmptyObjectSchema() {
    return JsonValue::Object{
        {"type", JsonValue("object")},
        {"properties", JsonValue::Object{}},
        {"additionalProperties", JsonValue(false)},
    };
}

void AddMutationProperties(JsonValue::Object& properties) {
    properties["dry_run"] = JsonValue::Object{{"type", JsonValue("boolean")}};
    properties["backup"] = JsonValue::Object{{"type", JsonValue("boolean")}};
    properties["expected_revision"] = JsonValue::Object{{"type", JsonValue("string")}};
}

JsonValue ObjectSchema(JsonValue::Object properties,
                       std::initializer_list<std::string_view> required = {}) {
    JsonValue::Array required_values;
    for (const auto key : required) required_values.emplace_back(key);
    JsonValue::Object result{
        {"type", JsonValue("object")},
        {"properties", std::move(properties)},
        {"additionalProperties", JsonValue(false)},
    };
    if (!required_values.empty()) result["required"] = std::move(required_values);
    return result;
}

bool HasOnlyKeys(const JsonValue& value,
                 std::initializer_list<std::string_view> allowed_keys) {
    if (!value.is_object()) return false;
    for (const auto& [key, ignored] : value.as_object()) {
        bool allowed = false;
        for (const auto candidate : allowed_keys) {
            if (key == candidate) {
                allowed = true;
                break;
            }
        }
        if (!allowed) return false;
    }
    return true;
}

bool ReadMutationOptions(const JsonValue& value, MutationOptions& options) {
    if (value.contains("dry_run")) {
        if (!value.at("dry_run").is_bool()) return false;
        options.dry_run = value.at("dry_run").as_bool();
    }
    if (value.contains("backup")) {
        if (!value.at("backup").is_bool()) return false;
        options.backup = value.at("backup").as_bool();
    }
    if (value.contains("expected_revision")) {
        if (!value.at("expected_revision").is_string() ||
            value.at("expected_revision").as_string().empty() ||
            value.at("expected_revision").as_string().size() > 256) return false;
        options.expected_revision = value.at("expected_revision").as_string();
    }
    return true;
}

JsonValue Failure(std::string& error, std::string_view code) {
    error.assign(code);
    return JsonValue(nullptr);
}

JsonValue DomainFailure(std::string& error, std::string_view code) {
    error.assign(code);
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

bool IsIdentifierStart(char character) {
    return std::isalpha(static_cast<unsigned char>(character)) || character == '_';
}

bool IsIdentifierPart(char character) {
    return std::isalnum(static_cast<unsigned char>(character)) || character == '_';
}

Span TrimSpan(std::string_view source, Span span) {
    while (span.begin < span.end &&
           std::isspace(static_cast<unsigned char>(source[span.begin]))) ++span.begin;
    while (span.end > span.begin &&
           std::isspace(static_cast<unsigned char>(source[span.end - 1]))) --span.end;
    return span;
}

std::vector<Call> ScanCalls(std::string_view source) {
    std::vector<Call> calls;
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
        while (open < source.size() &&
               std::isspace(static_cast<unsigned char>(source[open]))) ++open;
        if (open >= source.size() || source[open] != '(') continue;

        int depth = 1;
        std::size_t cursor = open + 1;
        std::size_t argument_begin = cursor;
        std::vector<Span> arguments;
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
            if (source[cursor] == '(') ++depth;
            else if (source[cursor] == ')') {
                --depth;
                if (depth == 0) break;
            } else if (source[cursor] == ',' && depth == 1) {
                arguments.push_back(TrimSpan(source, {argument_begin, cursor}));
                argument_begin = cursor + 1;
            }
            ++cursor;
        }
        if (depth != 0) break;
        const Span last = TrimSpan(source, {argument_begin, cursor});
        if (last.begin != last.end || !arguments.empty()) arguments.push_back(last);
        calls.push_back({name, {name_begin, cursor + 1}, std::move(arguments)});
        position = cursor + 1;
    }
    return calls;
}

std::string Text(std::string_view source, Span span) {
    return std::string(source.substr(span.begin, span.end - span.begin));
}

bool ReadNumber(std::string_view source, Span span, float& value) {
    const std::string text = Text(source, TrimSpan(source, span));
    if (text.empty()) return false;
    char* end = nullptr;
    value = std::strtof(text.c_str(), &end);
    return end != text.c_str() && *end == '\0' && std::isfinite(value);
}

bool ReadInteger(std::string_view source, Span span, std::int64_t& value) {
    const std::string text = Text(source, TrimSpan(source, span));
    if (text.empty()) return false;
    char* end = nullptr;
    const long long parsed = std::strtoll(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0') return false;
    value = parsed;
    return true;
}

std::string ReadIdentifier(std::string_view source, Span span) {
    return Text(source, TrimSpan(source, span));
}

std::string FormatNumber(double value) {
    std::ostringstream output;
    output << std::setprecision(9) << std::defaultfloat << value;
    return output.str();
}

bool ApplyPatches(std::string& source, std::vector<Patch> patches) {
    std::sort(patches.begin(), patches.end(), [](const Patch& left, const Patch& right) {
        return left.span.begin > right.span.begin;
    });
    for (const Patch& patch : patches) {
        if (patch.span.begin > patch.span.end || patch.span.end > source.size()) return false;
        source.replace(patch.span.begin, patch.span.end - patch.span.begin, patch.replacement);
    }
    return true;
}

bool AddArgumentPatch(const Call& call, std::size_t index, std::string replacement,
                      std::vector<Patch>& patches) {
    if (index >= call.arguments.size()) return false;
    patches.push_back({call.arguments[index], std::move(replacement)});
    return true;
}

const Call* FindCall(const std::vector<Call>& calls, std::string_view name) {
    for (const auto& call : calls) {
        if (call.name == name) return &call;
    }
    return nullptr;
}

bool ParseQscAndCompile(std::string_view source, std::vector<std::uint8_t>& binary,
                        std::string& error) {
    const qsc::LexResult lexed = qsc::Lex(std::string(source));
    if (!lexed.ok) {
        error = "qsc_lex_failed";
        return false;
    }
    const qsc::ParseResult parsed = qsc::Parse(lexed.tokens);
    if (!parsed.ok || !parsed.program) {
        error = "qsc_parse_failed";
        return false;
    }
    const qvm::CompileResult compiled = qvm::Compile(*parsed.program);
    if (!compiled.ok || compiled.binary.empty()) {
        error = "qvm_compile_failed";
        return false;
    }
    binary = compiled.binary;
    error.clear();
    return true;
}

bool ReadFloatArguments(std::string_view source, const Call& call,
                        std::vector<float>& values) {
    values.clear();
    values.reserve(call.arguments.size());
    for (const Span argument : call.arguments) {
        float value = 0.0f;
        if (!ReadNumber(source, argument, value)) return false;
        values.push_back(value);
    }
    return true;
}

JsonValue GeneralSnapshot(const std::vector<float>& values) {
    JsonValue::Array raw;
    for (const float value : values) raw.emplace_back(static_cast<double>(value));
    JsonValue::Object general{
        {"raw_arguments", std::move(raw)},
        {"delta_translation_scale", static_cast<double>(values[0])},
        {"jump_horizontal_speed_kmh", static_cast<double>(values[1])},
        {"jump_vertical_speed_kmh", static_cast<double>(values[2])},
        {"speed3_kmh", static_cast<double>(values[3])},
        {"distance4_m", static_cast<double>(values[4])},
        {"distance5_m", static_cast<double>(values[5])},
        {"duration6_seconds", static_cast<double>(values[6])},
        {"maximum_health", static_cast<double>(values[7])},
        {"value8", static_cast<double>(values[8])},
    };
    JsonValue::Array first_table;
    JsonValue::Array second_table;
    for (std::size_t index = 9; index < 26; ++index)
        first_table.emplace_back(static_cast<double>(values[index]));
    for (std::size_t index = 26; index < 43; ++index)
        second_table.emplace_back(static_cast<double>(values[index]));
    general["first_table"] = std::move(first_table);
    general["second_table"] = std::move(second_table);
    return general;
}

bool ReadConfig(std::string_view source, JsonValue& snapshot, std::string& error) {
    const auto calls = ScanCalls(source);
    const Call* general = FindCall(calls, "DefineHumanPlayerGeneral");
    const Call* cycle = FindCall(calls, "DefineHumanPlayerWeaponCycle");
    const Call* ammo = FindCall(calls, "DefineHumanPlayerAmmoLimit");
    if (!general || !cycle || !ammo || general->arguments.size() != 43 ||
        ammo->arguments.size() % 2 != 0) {
        error = "player_config_invalid";
        return false;
    }

    std::vector<float> values;
    if (!ReadFloatArguments(source, *general, values)) {
        error = "player_config_invalid";
        return false;
    }
    JsonValue::Array weapon_cycle;
    for (const Span argument : cycle->arguments) {
        const std::string id = ReadIdentifier(source, argument);
        if (!id.starts_with("WEAPON_ID_") || id.size() <= 10) {
            error = "player_config_invalid";
            return false;
        }
        weapon_cycle.emplace_back(id);
    }
    JsonValue::Object ammo_limits;
    for (std::size_t index = 0; index < ammo->arguments.size(); index += 2) {
        const std::string id = ReadIdentifier(source, ammo->arguments[index]);
        std::int64_t count = 0;
        if (!id.starts_with("AMMO_ID_") || id.size() <= 8 ||
            !ReadInteger(source, ammo->arguments[index + 1], count) || count < 0) {
            error = "player_config_invalid";
            return false;
        }
        ammo_limits[id] = static_cast<double>(count);
    }

    snapshot = JsonValue::Object{
        {"relative_path", JsonValue(std::string(kPlayerQsc))},
        {"general", GeneralSnapshot(values)},
        {"weapon_cycle", std::move(weapon_cycle)},
        {"ammo_limits", std::move(ammo_limits)},
    };
    error.clear();
    return true;
}

bool ReadPlayerSource(GameDataService& service, std::string& source, JsonValue& snapshot,
                      std::string& error) {
    if (!service.LoadProjectText(kPlayerQsc, source, error)) return false;
    return ReadConfig(source, snapshot, error);
}

JsonValue MutationResult(std::string_view tool, bool dry_run,
                         std::string_view revision_before, std::string_view revision_after,
                         JsonValue before, JsonValue after) {
    return JsonValue::Object{
        {"tool", JsonValue(std::string(tool))},
        {"dry_run", JsonValue(dry_run)},
        {"revision_before", JsonValue(std::string(revision_before))},
        {"revision_after", JsonValue(std::string(revision_after))},
        {"before", std::move(before)},
        {"after", std::move(after)},
    };
}

JsonValue CommitPlayerSource(GameDataService& service, std::string_view tool,
                             std::string source, const JsonValue& before,
                             const MutationOptions& options, std::string& error) {
    std::vector<std::uint8_t> qvm;
    if (!ParseQscAndCompile(source, qvm, error)) return JsonValue(nullptr);
    const std::vector<std::filesystem::path> tracked_paths{std::filesystem::path(kPlayerQsc)};
    const std::string revision_before = service.ProjectRevision(tracked_paths, error);
    if (!error.empty()) return JsonValue(nullptr);
    auto transaction = service.BeginProjectMutation(options, tracked_paths, error);
    if (!transaction) return JsonValue(nullptr);

    const std::vector<std::uint8_t> qsc_bytes(source.begin(), source.end());
    if (!transaction->Stage(std::filesystem::path(kPlayerQsc), qsc_bytes, error) ||
        !transaction->Stage(std::filesystem::path("humanplayer/humanplayer.qvm"), qvm, error))
        return JsonValue(nullptr);
    transaction->SetValidator([](const auto& relative_path, const auto& bytes,
                                 std::string& validation_error) {
        if (relative_path.extension() == ".qsc") {
            const std::string text(bytes.begin(), bytes.end());
            const qsc::LexResult lexed = qsc::Lex(text);
            const qsc::ParseResult parsed = lexed.ok ? qsc::Parse(lexed.tokens) : qsc::ParseResult{};
            if (!lexed.ok || !parsed.ok || !parsed.program) {
                validation_error = "qsc_validation_failed";
                return false;
            }
        }
        return true;
    });
    transaction->SetPostValidator([](const auto& relative_path, const auto& absolute_path,
                                     std::string& validation_error) {
        if (relative_path.extension() == ".qsc") {
            std::ifstream input(absolute_path, std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(input)),
                                   std::istreambuf_iterator<char>());
            const qsc::LexResult lexed = qsc::Lex(text);
            const qsc::ParseResult parsed = lexed.ok ? qsc::Parse(lexed.tokens) : qsc::ParseResult{};
            if (!lexed.ok || !parsed.ok || !parsed.program) {
                validation_error = "qsc_post_validation_failed";
                return false;
            }
        } else if (relative_path.extension() == ".qvm" && !QVM_Parse(absolute_path.string()).valid) {
            validation_error = "qvm_post_validation_failed";
            return false;
        }
        return true;
    });
    if (!transaction->Commit(error)) return JsonValue(nullptr);

    JsonValue after;
    if (!ReadConfig(source, after, error)) return JsonValue(nullptr);
    const std::string revision_after = options.dry_run
        ? revision_before
        : service.ProjectRevision(tracked_paths, error);
    if (!error.empty()) return JsonValue(nullptr);
    return MutationResult(tool, options.dry_run, revision_before, revision_after,
                          before, std::move(after));
}

}  // namespace

ToolDefinitionList PlayerToolDefinitions() {
    JsonValue::Object physics_fields;
    for (const std::string_view field : {
             "delta_translation_scale", "jump_horizontal_speed_kmh",
             "jump_vertical_speed_kmh", "speed3_kmh", "distance4_m", "distance5_m",
             "duration6_seconds", "maximum_health", "value8"}) {
        physics_fields[std::string(field)] = JsonValue::Object{{"type", JsonValue("number")}};
    }
    JsonValue::Object set_physics{{"fields", ObjectSchema(physics_fields)}};
    AddMutationProperties(set_physics);

    JsonValue::Object set_inventory{
        {"weapon_cycle", JsonValue::Object{
            {"type", JsonValue("array")},
            {"minItems", JsonValue(1)},
            {"items", JsonValue::Object{{"type", JsonValue("string")}}},
        }},
    };
    AddMutationProperties(set_inventory);

    JsonValue::Object set_ammo{
        {"limits", JsonValue::Object{
            {"type", JsonValue("object")},
            {"minProperties", JsonValue(1)},
            {"additionalProperties", JsonValue::Object{
                {"type", JsonValue("integer")}, {"minimum", JsonValue(0)},
            }},
        }},
    };
    AddMutationProperties(set_ammo);

    return ToolDefinitionList{
        {"player_get_physics", EmptyObjectSchema()},
        {"player_set_physics", ObjectSchema(std::move(set_physics), {"fields"})},
        {"player_set_inventory", ObjectSchema(std::move(set_inventory), {"weapon_cycle"})},
        {"player_set_ammo", ObjectSchema(std::move(set_ammo), {"limits"})},
    };
}

JsonValue CallPlayerTool(GameDataService& service, std::string_view name,
                         const JsonValue& arguments, std::string& error) {
    error.clear();
    if (name == "player_get_physics") {
        if (!HasOnlyKeys(arguments, {})) return Failure(error, "invalid_arguments");
        std::string source;
        JsonValue snapshot;
        if (!ReadPlayerSource(service, source, snapshot, error)) return JsonValue(nullptr);
        return snapshot;
    }

    if (name == "player_set_physics") {
        if (!HasOnlyKeys(arguments, {"fields", "dry_run", "backup", "expected_revision"}) ||
            !arguments.contains("fields") || !arguments.at("fields").is_object() ||
            arguments.at("fields").as_object().empty()) return Failure(error, "invalid_arguments");
        MutationOptions options;
        if (!ReadMutationOptions(arguments, options)) return Failure(error, "invalid_arguments");
        std::string source;
        JsonValue before;
        if (!ReadPlayerSource(service, source, before, error)) return JsonValue(nullptr);
        const auto calls = ScanCalls(source);
        const Call* general = FindCall(calls, "DefineHumanPlayerGeneral");
        if (!general || general->arguments.size() != 43) return DomainFailure(error, "player_config_invalid");
        std::vector<Patch> patches;
        const std::map<std::string, std::size_t> indices{
            {"delta_translation_scale", 0}, {"jump_horizontal_speed_kmh", 1},
            {"jump_vertical_speed_kmh", 2}, {"speed3_kmh", 3}, {"distance4_m", 4},
            {"distance5_m", 5}, {"duration6_seconds", 6}, {"maximum_health", 7},
            {"value8", 8},
        };
        for (const auto& [field, value] : arguments.at("fields").as_object()) {
            const auto index = indices.find(field);
            if (index == indices.end() || !value.is_number() ||
                !std::isfinite(value.as_number()) ||
                (field == "maximum_health" && value.as_number() <= 0.0) ||
                !AddArgumentPatch(*general, index->second, FormatNumber(value.as_number()), patches))
                return Failure(error, "invalid_arguments");
        }
        if (!ApplyPatches(source, std::move(patches))) return DomainFailure(error, "write_failed");
        return CommitPlayerSource(service, name, std::move(source), before, options, error);
    }

    if (name == "player_set_inventory") {
        if (!HasOnlyKeys(arguments, {"weapon_cycle", "dry_run", "backup", "expected_revision"}) ||
            !arguments.contains("weapon_cycle") || !arguments.at("weapon_cycle").is_array() ||
            arguments.at("weapon_cycle").as_array().empty()) return Failure(error, "invalid_arguments");
        MutationOptions options;
        if (!ReadMutationOptions(arguments, options)) return Failure(error, "invalid_arguments");
        std::string source;
        JsonValue before;
        if (!ReadPlayerSource(service, source, before, error)) return JsonValue(nullptr);
        const auto calls = ScanCalls(source);
        const Call* cycle = FindCall(calls, "DefineHumanPlayerWeaponCycle");
        if (!cycle) return DomainFailure(error, "player_config_invalid");
        std::string replacement = "DefineHumanPlayerWeaponCycle(";
        for (std::size_t index = 0; index < arguments.at("weapon_cycle").as_array().size(); ++index) {
            const auto& item = arguments.at("weapon_cycle").as_array()[index];
            if (!item.is_string() || !item.as_string().starts_with("WEAPON_ID_") ||
                item.as_string().size() <= 10) return Failure(error, "invalid_arguments");
            if (index > 0) replacement += ", ";
            replacement += item.as_string();
        }
        replacement += ")";
        std::vector<Patch> patches{{cycle->full, std::move(replacement)}};
        if (!ApplyPatches(source, std::move(patches))) return DomainFailure(error, "write_failed");
        return CommitPlayerSource(service, name, std::move(source), before, options, error);
    }

    if (name == "player_set_ammo") {
        if (!HasOnlyKeys(arguments, {"limits", "dry_run", "backup", "expected_revision"}) ||
            !arguments.contains("limits") || !arguments.at("limits").is_object() ||
            arguments.at("limits").as_object().empty()) return Failure(error, "invalid_arguments");
        MutationOptions options;
        if (!ReadMutationOptions(arguments, options)) return Failure(error, "invalid_arguments");
        std::string source;
        JsonValue before;
        if (!ReadPlayerSource(service, source, before, error)) return JsonValue(nullptr);
        const auto calls = ScanCalls(source);
        const Call* ammo = FindCall(calls, "DefineHumanPlayerAmmoLimit");
        if (!ammo || ammo->arguments.size() % 2 != 0) return DomainFailure(error, "player_config_invalid");
        std::vector<std::string> ids;
        std::vector<std::int64_t> counts;
        for (std::size_t index = 0; index < ammo->arguments.size(); index += 2) {
            ids.push_back(ReadIdentifier(source, ammo->arguments[index]));
            std::int64_t count = 0;
            if (!ReadInteger(source, ammo->arguments[index + 1], count))
                return DomainFailure(error, "player_config_invalid");
            counts.push_back(count);
        }
        for (const auto& [id, value] : arguments.at("limits").as_object()) {
            if (!id.starts_with("AMMO_ID_") || id.size() <= 8 || !value.is_number() ||
                !std::isfinite(value.as_number()) || std::trunc(value.as_number()) != value.as_number() ||
                value.as_number() < 0.0 || value.as_number() > std::numeric_limits<std::int32_t>::max())
                return Failure(error, "invalid_arguments");
            const auto existing = std::find(ids.begin(), ids.end(), id);
            if (existing == ids.end()) {
                ids.push_back(id);
                counts.push_back(static_cast<std::int64_t>(value.as_number()));
            } else {
                counts[static_cast<std::size_t>(existing - ids.begin())] =
                    static_cast<std::int64_t>(value.as_number());
            }
        }
        std::string replacement = "DefineHumanPlayerAmmoLimit(";
        for (std::size_t index = 0; index < ids.size(); ++index) {
            if (index > 0) replacement += ", ";
            replacement += ids[index] + ", " + std::to_string(counts[index]);
        }
        replacement += ")";
        std::vector<Patch> patches{{ammo->full, std::move(replacement)}};
        if (!ApplyPatches(source, std::move(patches))) return DomainFailure(error, "write_failed");
        return CommitPlayerSource(service, name, std::move(source), before, options, error);
    }

    return Failure(error, "unknown_tool");
}

}  // namespace mcp
