#include "mcp_tools_session.h"

#include <cmath>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <utility>

namespace mcp {
namespace {

JsonValue EmptyObjectSchema() {
    return JsonValue::Object{
        {"type", JsonValue("object")},
        {"properties", JsonValue::Object{}},
        {"additionalProperties", JsonValue(false)},
    };
}

JsonValue LevelSchema() {
    return JsonValue::Object{
        {"type", JsonValue("object")},
        {"properties", JsonValue::Object{
            {"level", JsonValue::Object{
                {"type", JsonValue("integer")},
                {"minimum", JsonValue(1)},
            }},
        }},
        {"required", JsonValue::Array{JsonValue("level")}},
        {"additionalProperties", JsonValue(false)},
    };
}

bool HasOnlyKeys(const JsonValue& arguments,
                 std::initializer_list<std::string_view> allowed_keys) {
    if (!arguments.is_object()) return false;
    for (const auto& [key, ignored] : arguments.as_object()) {
        bool allowed = false;
        for (const std::string_view allowed_key : allowed_keys) {
            if (key == allowed_key) {
                allowed = true;
                break;
            }
        }
        if (!allowed) return false;
    }
    return true;
}

bool ReadLevel(const JsonValue& arguments, int& level) {
    if (!HasOnlyKeys(arguments, {"level"})) return false;
    const auto it = arguments.as_object().find("level");
    if (it == arguments.as_object().end() || !it->second.is_number()) return false;

    const double value = it->second.as_number();
    if (!std::isfinite(value) || std::trunc(value) != value ||
        value < 1.0 || value > static_cast<double>(std::numeric_limits<int>::max())) {
        return false;
    }
    level = static_cast<int>(value);
    return true;
}

bool RequireObjectWithoutArguments(const JsonValue& arguments) {
    return HasOnlyKeys(arguments, {});
}

JsonValue Failure(std::string& error, std::string_view code) {
    error.assign(code);
    return JsonValue(nullptr);
}

JsonValue DomainFailure(std::string& error, const std::string& domain_error) {
    error = domain_error.empty() ? "service_error" : domain_error;
    return JsonValue(nullptr);
}

JsonValue RevisionResult(const LevelRevision& revision) {
    return JsonValue::Object{
        {"level", JsonValue(revision.level)},
        {"revision", JsonValue(revision.fingerprint)},
    };
}

}  // namespace

ToolDefinitionList SessionToolDefinitions() {
    return ToolDefinitionList{
        {"level_open", LevelSchema()},
        {"level_reload", EmptyObjectSchema()},
        {"level_validate", LevelSchema()},
        {"project_info", EmptyObjectSchema()},
        {"project_list_levels", EmptyObjectSchema()},
    };
}

JsonValue CallSessionTool(GameDataService& service, std::string_view name,
                          const JsonValue& arguments, std::string& error) {
    error.clear();

    try {
        if (name == "project_info") {
            if (!RequireObjectWithoutArguments(arguments))
                return Failure(error, "invalid_arguments");
            return service.ProjectInfo();
        }

        if (name == "project_list_levels") {
            if (!RequireObjectWithoutArguments(arguments))
                return Failure(error, "invalid_arguments");
            std::string domain_error;
            const JsonValue result = service.ListLevels(domain_error);
            if (!domain_error.empty()) return DomainFailure(error, domain_error);
            return result;
        }

        if (name == "level_open") {
            int level = 0;
            if (!ReadLevel(arguments, level)) return Failure(error, "invalid_arguments");

            std::string domain_error;
            if (!service.OpenLevel(level, domain_error))
                return DomainFailure(error, domain_error);
            const JsonValue manifest = service.LevelManifest(level, domain_error);
            if (!domain_error.empty()) return DomainFailure(error, domain_error);

            JsonValue::Object result{
                {"level", manifest.at("level")},
                {"revision", manifest.at("revision")},
            };
            result["opened"] = true;
            result["relative_path"] = manifest.at("relative_path");
            result["manifest"] = manifest;
            return result;
        }

        if (name == "level_reload") {
            if (!RequireObjectWithoutArguments(arguments))
                return Failure(error, "invalid_arguments");

            if (!service.HasOpenLevel()) return DomainFailure(error, "level_not_open");
            const LevelRevision before = service.CurrentRevision();
            std::string domain_error;
            if (!service.RefreshRevision(domain_error))
                return DomainFailure(error, domain_error);
            const LevelRevision after = service.CurrentRevision();
            JsonValue::Object result = RevisionResult(after).as_object();
            result["revision_before"] = before.fingerprint;
            result["revision_after"] = after.fingerprint;
            result["changed_externally"] = before.fingerprint != after.fingerprint;
            result["reloaded"] = true;
            return result;
        }

        if (name == "level_validate") {
            int level = 0;
            if (!ReadLevel(arguments, level)) return Failure(error, "invalid_arguments");

            std::string domain_error;
            const JsonValue result = service.ValidateLevel(level, domain_error);
            if (!domain_error.empty()) return DomainFailure(error, domain_error);
            return result;
        }

        return Failure(error, "unknown_tool");
    } catch (const std::exception&) {
        return Failure(error, "service_error");
    }
}

}  // namespace mcp
