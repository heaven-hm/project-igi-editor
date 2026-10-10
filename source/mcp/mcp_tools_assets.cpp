#include "mcp_tools_assets.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace mcp {
namespace {

JsonValue AssetListSchema() {
    return JsonValue::Object{
        {"type", "object"},
        {"properties", JsonValue::Object{
            {"level", JsonValue::Object{{"type", "integer"}, {"minimum", 1}}},
            {"format", JsonValue::Object{
                {"type", "string"},
                {"enum", JsonValue::Array{
                    "dat", "mef", "res", "tex", "spr", "pic", "mtp", "fnt",
                    "lmp", "ctr", "hmp", "cmd", "bit", "graph", "qsc", "qvm", "olm",
                }},
            }},
        }},
        {"additionalProperties", false},
    };
}

JsonValue AssetPathSchema(bool required) {
    JsonValue::Object schema{
        {"type", "object"},
        {"properties", JsonValue::Object{
            {"level", JsonValue::Object{{"type", "integer"}, {"minimum", 1}}},
            {"path", JsonValue::Object{{"type", "string"}, {"minLength", 1}, {"maxLength", 512}}},
        }},
        {"additionalProperties", false},
    };
    if (required) schema["required"] = JsonValue::Array{JsonValue("path")};
    return schema;
}

JsonValue UnsupportedSchema() {
    return JsonValue::Object{
        {"type", "object"},
        {"description", "Reserved for a future validated converter or archive operation."},
        {"properties", JsonValue::Object{
            {"level", JsonValue::Object{{"type", "integer"}, {"minimum", 1}}},
            {"path", JsonValue::Object{{"type", "string"}, {"maxLength", 512}}},
            {"format", JsonValue::Object{{"type", "string"}}},
            {"output_format", JsonValue::Object{{"type", "string"}}},
            {"operation", JsonValue::Object{{"type", "string"}}},
            {"expected_revision", JsonValue::Object{{"type", "string"}}},
            {"dry_run", JsonValue::Object{{"type", "boolean"}}},
            {"backup", JsonValue::Object{{"type", "boolean"}}},
        }},
        {"additionalProperties", false},
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

JsonValue Failure(std::string& error, std::string_view code) {
    error.assign(code);
    return JsonValue(nullptr);
}

JsonValue DomainFailure(std::string& error, const std::string& domain_error) {
    error = domain_error.empty() ? "service_error" : domain_error;
    return JsonValue(nullptr);
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string FileName(std::string_view path) {
    const std::size_t slash = path.find_last_of('/');
    return std::string(path.substr(slash == std::string_view::npos ? 0 : slash + 1));
}

std::string Extension(std::string_view path) {
    const std::string name = FileName(path);
    const std::size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) return {};
    return Lower(name.substr(dot + 1));
}

bool IsSafeRelativePath(std::string_view path) {
    if (path.empty() || path.front() == '/' || path.find(':') != std::string_view::npos)
        return false;
    std::string normalized(path);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    if (normalized.find("//") != std::string::npos) return false;
    std::size_t start = 0;
    while (start <= normalized.size()) {
        const std::size_t end = normalized.find('/', start);
        const std::string_view component(normalized.data() + start,
                                         end == std::string::npos ? normalized.size() - start : end - start);
        if (component == ".." || component.empty()) return false;
        if (end == std::string::npos) break;
        start = end + 1;
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

bool ReadLevel(GameDataService& service, const JsonValue& arguments,
               std::initializer_list<std::string_view> allowed_keys,
               int& level, std::string& error) {
    if (!HasOnlyKeys(arguments, allowed_keys)) {
        error = "invalid_arguments";
        return false;
    }
    const auto it = arguments.as_object().find("level");
    if (it != arguments.as_object().end()) {
        if (!ReadInteger(it->second, level)) {
            error = "invalid_arguments";
            return false;
        }
        return true;
    }
    if (!service.HasOpenLevel()) {
        error = "level_not_open";
        return false;
    }
    try {
        level = service.CurrentRevision().level;
    } catch (const std::exception&) {
        error = "level_not_open";
        return false;
    }
    return true;
}

JsonValue LoadManifest(GameDataService& service, int level, std::string& error) {
    std::string domain_error;
    const JsonValue manifest = service.LevelManifest(level, domain_error);
    if (!domain_error.empty()) return DomainFailure(error, domain_error);
    if (!manifest.is_object() || !manifest.contains("files") || !manifest.at("files").is_array())
        return Failure(error, "manifest_failed");
    error.clear();
    return manifest;
}

std::string AssetFormat(const JsonValue& file) {
    const std::string path = file.at("path").as_string();
    const std::string lowered = Lower(path);
    if (lowered.find("/graphs/") != std::string::npos && Extension(path) == "dat") return "graph";
    return Extension(path);
}

JsonValue AssetDescriptor(const JsonValue& file) {
    return JsonValue::Object{
        {"path", file.at("path")},
        {"bytes", file.at("bytes")},
        {"format", AssetFormat(file)},
    };
}

bool MatchesFormat(const JsonValue& file, std::string_view format) {
    const std::string requested = Lower(std::string(format));
    if (requested == "dat") return Extension(file.at("path").as_string()) == "dat";
    return AssetFormat(file) == requested;
}

bool IsSupportedFormat(std::string_view format) {
    static constexpr std::string_view formats[] = {
        "dat", "mef", "res", "tex", "spr", "pic", "mtp", "fnt",
        "lmp", "ctr", "hmp", "cmd", "bit", "graph", "qsc", "qvm", "olm",
    };
    const std::string lowered = Lower(std::string(format));
    return std::find(std::begin(formats), std::end(formats), lowered) != std::end(formats);
}

JsonValue ManifestResult(const JsonValue& manifest, int level,
                         std::string_view key, JsonValue value) {
    return JsonValue::Object{
        {"level", level},
        {"revision", manifest.at("revision")},
        {std::string(key), std::move(value)},
        {"source", "level_manifest"},
    };
}

JsonValue Unsupported(std::string& error, const JsonValue& arguments) {
    if (!HasOnlyKeys(arguments, {
            "level", "path", "format", "output_format", "operation",
            "expected_revision", "dry_run", "backup"})) {
        return Failure(error, "invalid_arguments");
    }
    return Failure(error, "unsupported_operation");
}

}  // namespace

ToolDefinitionList AssetToolDefinitions() {
    return ToolDefinitionList{
        {"asset_list", AssetListSchema()},
        {"asset_inspect", AssetPathSchema(true)},
        {"asset_validate", AssetPathSchema(false)},
        {"asset_convert", UnsupportedSchema()},
        {"asset_pack_or_update", UnsupportedSchema()},
    };
}

JsonValue CallAssetTool(GameDataService& service, std::string_view name,
                        const JsonValue& arguments, std::string& error) {
    error.clear();

    try {
        if (name == "asset_list") {
            int level = 0;
            if (!ReadLevel(service, arguments, {"level", "format"}, level, error))
                return JsonValue(nullptr);
            const auto format = arguments.as_object().find("format");
            if (format != arguments.as_object().end() && !format->second.is_string())
                return Failure(error, "invalid_arguments");
            if (format != arguments.as_object().end() &&
                !IsSupportedFormat(format->second.as_string()))
                return Failure(error, "invalid_arguments");

            const JsonValue manifest = LoadManifest(service, level, error);
            if (!error.empty()) return JsonValue(nullptr);
            JsonValue::Array assets;
            for (const auto& file : manifest.at("files").as_array()) {
                if (format != arguments.as_object().end() &&
                    !MatchesFormat(file, format->second.as_string())) continue;
                assets.emplace_back(AssetDescriptor(file));
            }
            return ManifestResult(manifest, level, "assets", std::move(assets));
        }

        if (name == "asset_inspect" || name == "asset_validate") {
            int level = 0;
            if (!ReadLevel(service, arguments, {"level", "path"}, level, error))
                return JsonValue(nullptr);
            const auto path = arguments.as_object().find("path");
            if (path != arguments.as_object().end() && !path->second.is_string())
                return Failure(error, "invalid_arguments");
            if (path != arguments.as_object().end() &&
                !IsSafeRelativePath(path->second.as_string()))
                return Failure(error, "path_forbidden");
            if (name == "asset_inspect" &&
                (path == arguments.as_object().end() || !path->second.is_string()))
                return Failure(error, "invalid_arguments");

            const JsonValue manifest = LoadManifest(service, level, error);
            if (!error.empty()) return JsonValue(nullptr);
            JsonValue::Array selected;
            for (const auto& file : manifest.at("files").as_array()) {
                if (path == arguments.as_object().end() || file.at("path").as_string() == path->second.as_string())
                    selected.emplace_back(AssetDescriptor(file));
            }
            if (path != arguments.as_object().end() && selected.empty())
                return Failure(error, "unknown_asset");

            if (name == "asset_inspect") {
                return ManifestResult(manifest, level, "asset", selected.front());
            }

            JsonValue::Array checks;
            for (const auto& asset : selected) {
                checks.emplace_back(JsonValue::Object{
                    {"path", asset.at("path")},
                    {"status", "passed"},
                    {"summary", "Asset is present in the configured level manifest."},
                });
            }
            return JsonValue::Object{
                {"level", level},
                {"revision", manifest.at("revision")},
                {"valid", true},
                {"validation_scope", "manifest_only"},
                {"checks", std::move(checks)},
            };
        }

        if (name == "asset_convert" || name == "asset_pack_or_update")
            return Unsupported(error, arguments);

        return Failure(error, "unknown_tool");
    } catch (const std::exception&) {
        return Failure(error, "service_error");
    }
}

}  // namespace mcp
