#include "mcp_tools_graph.h"

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

JsonValue LevelSchema() {
    return JsonValue::Object{
        {"type", "object"},
        {"properties", JsonValue::Object{
            {"level", JsonValue::Object{
                {"type", "integer"},
                {"minimum", 1},
            }},
        }},
        {"additionalProperties", false},
    };
}

JsonValue GraphIdSchema(bool required) {
    JsonValue::Object schema{
        {"type", "object"},
        {"properties", JsonValue::Object{
            {"level", JsonValue::Object{
                {"type", "integer"},
                {"minimum", 1},
            }},
            {"graph_id", JsonValue::Object{
                {"type", "string"},
                {"minLength", 1},
                {"maxLength", 64},
            }},
        }},
        {"additionalProperties", false},
    };
    if (required) schema["required"] = JsonValue::Array{JsonValue("graph_id")};
    return schema;
}

JsonValue UnsupportedMutationSchema() {
    return JsonValue::Object{
        {"type", "object"},
        {"description", "Reserved for a future validated game-data implementation."},
        {"properties", JsonValue::Object{
            {"level", JsonValue::Object{{"type", "integer"}, {"minimum", 1}}},
            {"graph_id", JsonValue::Object{{"type", "string"}, {"maxLength", 64}}},
            {"node_id", JsonValue::Object{{"type", "integer"}}},
            {"from_node", JsonValue::Object{{"type", "integer"}}},
            {"to_node", JsonValue::Object{{"type", "integer"}}},
            {"link_type", JsonValue::Object{{"type", "integer"}}},
            {"position", JsonValue::Object{{"type", "array"}, {"maxItems", 3}}},
            {"gamma", JsonValue::Object{{"type", "number"}}},
            {"radius", JsonValue::Object{{"type", "number"}}},
            {"material", JsonValue::Object{{"type", "integer"}}},
            {"criteria", JsonValue::Object{{"type", "string"}, {"maxLength", 128}}},
            {"operation", JsonValue::Object{{"type", "string"}}},
            {"x", JsonValue::Object{{"type", "number"}}},
            {"y", JsonValue::Object{{"type", "number"}}},
            {"z", JsonValue::Object{{"type", "number"}}},
            {"value", JsonValue::Object{{"type", "number"}}},
            {"delta", JsonValue::Object{{"type", "number"}}},
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

std::string FileExtension(std::string_view path) {
    const std::string file_name = FileName(path);
    const std::size_t dot = file_name.find_last_of('.');
    if (dot == std::string::npos) return {};
    return Lower(file_name.substr(dot + 1));
}

bool HasDirectory(std::string_view path, std::string_view directory) {
    const std::string lowered = Lower(std::string(path));
    const std::string needle = "/" + std::string(directory) + "/";
    return lowered.find(needle) != std::string::npos;
}

bool IsGraphFile(const JsonValue& file) {
    if (!file.is_object() || !file.contains("path") || !file.at("path").is_string()) return false;
    const std::string path = file.at("path").as_string();
    const std::string name = Lower(FileName(path));
    return HasDirectory(path, "graphs") && FileExtension(path) == "dat" &&
           name.starts_with("graph") && name.size() > 9;
}

bool IsTerrainFile(const JsonValue& file) {
    if (!file.is_object() || !file.contains("path") || !file.at("path").is_string()) return false;
    const std::string path = file.at("path").as_string();
    if (!HasDirectory(path, "terrain")) return false;
    const std::string extension = FileExtension(path);
    return extension == "hmp" || extension == "lmp" || extension == "ctr" ||
           extension == "cmd" || extension == "bit";
}

bool IsLightmapFile(const JsonValue& file) {
    if (!file.is_object() || !file.contains("path") || !file.at("path").is_string()) return false;
    const std::string path = Lower(file.at("path").as_string());
    return FileExtension(path) == "olm" || HasDirectory(path, "lightmaps") ||
           path.find("lightmap") != std::string::npos;
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
    if (!manifest.is_object() || !manifest.contains("files") || !manifest.at("files").is_array()) {
        return Failure(error, "manifest_failed");
    }
    error.clear();
    return manifest;
}

JsonValue FileDescriptor(const JsonValue& file, std::string_view format) {
    JsonValue::Object descriptor{
        {"path", file.at("path")},
        {"bytes", file.at("bytes")},
        {"format", JsonValue(std::string(format))},
    };
    return descriptor;
}

JsonValue GraphDescriptor(const JsonValue& file) {
    const std::string name = FileName(file.at("path").as_string());
    const std::string stem = name.substr(0, name.size() - 4);
    const std::string id = stem.starts_with("graph") ? stem.substr(5) : stem;
    JsonValue::Object descriptor = FileDescriptor(file, "graph").as_object();
    descriptor["graph_id"] = id;
    return descriptor;
}

JsonValue ManifestSelection(const JsonValue& manifest,
                            bool (*predicate)(const JsonValue&)) {
    JsonValue::Array files;
    for (const auto& file : manifest.at("files").as_array()) {
        if (predicate(file)) files.emplace_back(file);
    }
    return files;
}

bool MatchesGraphId(const JsonValue& file, std::string_view requested) {
    const std::string name = Lower(FileName(file.at("path").as_string()));
    const std::string id = Lower(std::string(requested));
    if (name == "graph" + id + ".dat") return true;
    return id.starts_with("graph") && name == id + ".dat";
}

bool IsSafeGraphId(std::string_view graph_id) {
    return !graph_id.empty() && graph_id.size() <= 64 &&
           graph_id.find_first_of("/:\\") == std::string_view::npos &&
           graph_id.find("..") == std::string_view::npos;
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
            "level", "graph_id", "node_id", "from_node", "to_node", "link_type",
            "position", "gamma", "radius", "material", "criteria", "operation",
            "x", "y", "z", "value", "delta", "expected_revision", "dry_run", "backup"})) {
        return Failure(error, "invalid_arguments");
    }
    return Failure(error, "unsupported_operation");
}

}  // namespace

ToolDefinitionList GraphToolDefinitions() {
    return ToolDefinitionList{
        {"graph_list", LevelSchema()},
        {"graph_get", GraphIdSchema(true)},
        {"graph_node_create", UnsupportedMutationSchema()},
        {"graph_node_update", UnsupportedMutationSchema()},
        {"graph_node_delete", UnsupportedMutationSchema()},
        {"graph_link_create", UnsupportedMutationSchema()},
        {"graph_link_update", UnsupportedMutationSchema()},
        {"graph_link_delete", UnsupportedMutationSchema()},
        {"graph_validate", GraphIdSchema(false)},
        {"terrain_get_metadata", LevelSchema()},
        {"terrain_apply_edit", UnsupportedMutationSchema()},
        {"terrain_validate", LevelSchema()},
        {"lightmap_get", LevelSchema()},
        {"lightmap_rebuild_or_clear", UnsupportedMutationSchema()},
    };
}

JsonValue CallGraphTool(GameDataService& service, std::string_view name,
                        const JsonValue& arguments, std::string& error) {
    error.clear();

    try {
        if (name == "graph_list" || name == "graph_validate" ||
            name == "terrain_get_metadata" || name == "terrain_validate" ||
            name == "lightmap_get") {
            int level = 0;
            const bool graph_with_optional_id = name == "graph_validate";
            const auto allowed = graph_with_optional_id
                ? std::initializer_list<std::string_view>{"level", "graph_id"}
                : std::initializer_list<std::string_view>{"level"};
            if (!ReadLevel(service, arguments, allowed, level, error)) return JsonValue(nullptr);

            const JsonValue manifest = LoadManifest(service, level, error);
            if (!error.empty()) return JsonValue(nullptr);

            if (name == "graph_list") {
                JsonValue::Array graphs;
                for (const auto& file : manifest.at("files").as_array()) {
                    if (IsGraphFile(file)) graphs.emplace_back(GraphDescriptor(file));
                }
                return ManifestResult(manifest, level, "graphs", std::move(graphs));
            }

            if (name == "graph_validate") {
                JsonValue::Array graphs;
                const auto graph_id = arguments.as_object().find("graph_id");
                for (const auto& file : manifest.at("files").as_array()) {
                    if (IsGraphFile(file) &&
                        (graph_id == arguments.as_object().end() ||
                         (graph_id->second.is_string() && MatchesGraphId(file, graph_id->second.as_string())))) {
                        graphs.emplace_back(GraphDescriptor(file));
                    }
                }
                if (graph_id != arguments.as_object().end() && !graph_id->second.is_string()) {
                    return Failure(error, "invalid_arguments");
                }
                if (graph_id != arguments.as_object().end() &&
                    !IsSafeGraphId(graph_id->second.as_string())) {
                    return Failure(error, "invalid_arguments");
                }
                if (graph_id != arguments.as_object().end() && graphs.empty()) {
                    return Failure(error, "unknown_graph");
                }
                JsonValue::Array checks;
                checks.emplace_back(JsonValue::Object{
                    {"name", "graph_manifest"},
                    {"status", "passed"},
                    {"summary", "Graph files are present in the configured level manifest."},
                });
                return JsonValue::Object{
                    {"level", level},
                    {"revision", manifest.at("revision")},
                    {"valid", true},
                    {"validation_scope", "manifest_only"},
                    {"graphs", std::move(graphs)},
                    {"checks", std::move(checks)},
                };
            }

            if (name == "terrain_get_metadata") {
                const JsonValue terrain = ManifestSelection(manifest, IsTerrainFile);
                JsonValue::Array files;
                for (const auto& file : terrain.as_array()) {
                    files.emplace_back(FileDescriptor(file, FileExtension(file.at("path").as_string())));
                }
                return ManifestResult(manifest, level, "terrain", std::move(files));
            }

            if (name == "terrain_validate") {
                const JsonValue terrain = ManifestSelection(manifest, IsTerrainFile);
                return JsonValue::Object{
                    {"level", level},
                    {"revision", manifest.at("revision")},
                    {"valid", true},
                    {"available", !terrain.as_array().empty()},
                    {"validation_scope", "manifest_only"},
                    {"files", terrain},
                };
            }

            const JsonValue lightmaps = ManifestSelection(manifest, IsLightmapFile);
            JsonValue::Array files;
            for (const auto& file : lightmaps.as_array()) {
                files.emplace_back(FileDescriptor(file, FileExtension(file.at("path").as_string())));
            }
            return ManifestResult(manifest, level, "lightmaps", std::move(files));
        }

        if (name == "graph_get") {
            int level = 0;
            if (!ReadLevel(service, arguments, {"level", "graph_id"}, level, error))
                return JsonValue(nullptr);
            const auto graph_id = arguments.as_object().find("graph_id");
            if (graph_id == arguments.as_object().end() || !graph_id->second.is_string() ||
                !IsSafeGraphId(graph_id->second.as_string())) {
                return Failure(error, "invalid_arguments");
            }
            const std::string requested = graph_id->second.as_string();

            const JsonValue manifest = LoadManifest(service, level, error);
            if (!error.empty()) return JsonValue(nullptr);
            for (const auto& file : manifest.at("files").as_array()) {
                if (IsGraphFile(file) && MatchesGraphId(file, requested)) {
                    return JsonValue::Object{
                        {"level", level},
                        {"revision", manifest.at("revision")},
                        {"graph", GraphDescriptor(file)},
                        {"nodes", JsonValue(nullptr)},
                        {"links", JsonValue(nullptr)},
                        {"inspection_scope", "manifest_only"},
                    };
                }
            }
            return Failure(error, "unknown_graph");
        }

        if (name == "graph_node_create" || name == "graph_node_update" ||
            name == "graph_node_delete" || name == "graph_link_create" ||
            name == "graph_link_update" || name == "graph_link_delete" ||
            name == "terrain_apply_edit" || name == "lightmap_rebuild_or_clear") {
            return Unsupported(error, arguments);
        }

        return Failure(error, "unknown_tool");
    } catch (const std::exception&) {
        return Failure(error, "service_error");
    }
}

}  // namespace mcp
