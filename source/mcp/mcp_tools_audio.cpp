#include "mcp_tools_audio.h"
#include "mcp_transaction.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace mcp {
namespace {

JsonValue Failure(std::string& error, std::string_view code) {
    error.assign(code);
    return JsonValue(nullptr);
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

bool ReadString(const JsonValue& value, std::string& result, std::size_t maximum,
                bool allow_empty = false) {
    if (!value.is_string() || value.as_string().size() > maximum ||
        (!allow_empty && value.as_string().empty())) return false;
    for (const unsigned char character : value.as_string()) {
        if (character < 0x20u) return false;
    }
    result = value.as_string();
    return true;
}

bool ReadInteger(const JsonValue& value, int& result, int minimum, int maximum) {
    if (!value.is_number()) return false;
    const double number = value.as_number();
    if (!std::isfinite(number) || std::trunc(number) != number ||
        number < static_cast<double>(minimum) || number > static_cast<double>(maximum)) return false;
    result = static_cast<int>(number);
    return true;
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
        if (component.empty() || component == "..") return false;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}

std::string NormalizedPath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

std::string Extension(std::string_view path) {
    const std::filesystem::path file(path);
    std::string extension = file.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension;
}

bool IsAudioPath(std::string_view path) {
    const std::string extension = Extension(path);
    return extension == ".wav" || extension == ".mp3";
}

bool ReadBinary(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0 || size > 256u * 1024u * 1024u) return false;
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    bytes.resize(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return input.good() || input.eof();
}

bool ResolveAudioPath(GameDataService& service, std::string_view text,
                      std::filesystem::path& absolute, std::filesystem::path& relative,
                      std::string& error) {
    if (!IsSafeRelativePath(text)) {
        error = "path_forbidden";
        return false;
    }
    const std::string normalized = NormalizedPath(std::string(text));
    if (!service.scope().ResolveRelative(normalized, absolute, error)) return false;
    if (!service.scope().RelativeToRoot(absolute, relative, error)) return false;
    if (!service.scope().IsSupportedPath(relative)) {
        error = "path_forbidden";
        return false;
    }
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

JsonValue CommitAudio(GameDataService& service, std::string_view tool,
                      const std::filesystem::path& relative_path,
                      std::vector<std::uint8_t> bytes, std::uintmax_t previous_bytes,
                      const MutationOptions& options, std::string& error) {
    const std::uintmax_t new_bytes = bytes.size();
    const std::vector<std::filesystem::path> tracked{relative_path};
    const std::string before_revision = service.ProjectRevision(tracked, error);
    if (!error.empty()) return JsonValue(nullptr);
    auto transaction = service.BeginProjectMutation(options, tracked, error);
    if (!transaction || !transaction->Stage(relative_path, std::move(bytes), error))
        return JsonValue(nullptr);
    transaction->SetValidator([](const auto& path, const auto& data, std::string& validation_error) {
        if (!IsAudioPath(path.generic_string()) || data.empty()) {
            validation_error = "audio_invalid";
            return false;
        }
        return true;
    });
    if (!transaction->Commit(error)) return JsonValue(nullptr);
    const std::string after_revision = options.dry_run
        ? before_revision : service.ProjectRevision(tracked, error);
    if (!error.empty()) return JsonValue(nullptr);
    return JsonValue::Object{
        {"tool", JsonValue(std::string(tool))},
        {"path", JsonValue(relative_path.generic_string())},
        {"dry_run", options.dry_run},
        {"bytes_before", static_cast<double>(previous_bytes)},
        {"bytes_after", static_cast<double>(new_bytes)},
        {"revision_before", before_revision},
        {"revision_after", after_revision},
    };
}

}  // namespace

ToolDefinitionList AudioToolDefinitions() {
    JsonValue::Object list{
        {"type", JsonValue("object")},
        {"properties", JsonValue::Object{}},
        {"additionalProperties", JsonValue(false)},
    };
    JsonValue::Object replace{
        {"type", JsonValue("object")},
        {"properties", JsonValue::Object{
            {"source_path", JsonValue::Object{{"type", JsonValue("string")}}},
            {"destination_path", JsonValue::Object{{"type", JsonValue("string")}}},
            {"dry_run", JsonValue::Object{{"type", JsonValue("boolean")}}},
            {"backup", JsonValue::Object{{"type", JsonValue("boolean")}}},
            {"expected_revision", JsonValue::Object{{"type", JsonValue("string")}}},
        }},
        {"required", JsonValue::Array{JsonValue("source_path"), JsonValue("destination_path")}},
        {"additionalProperties", JsonValue(false)},
    };
    JsonValue::Object level_track = replace;
    level_track.at("properties").as_object().erase("destination_path");
    level_track.at("properties").as_object()["level"] = JsonValue::Object{{"type", JsonValue("integer")}, {"minimum", JsonValue(1)}, {"maximum", JsonValue(14)}};
    level_track.at("required").as_array().clear();
    level_track.at("required").as_array().emplace_back("level");
    level_track.at("required").as_array().emplace_back("source_path");
    return ToolDefinitionList{
        {"audio_list_tracks", JsonValue(std::move(list))},
        {"audio_replace_sfx", JsonValue(std::move(replace))},
        {"audio_set_level_track", JsonValue(std::move(level_track))},
    };
}

JsonValue CallAudioTool(GameDataService& service, std::string_view name,
                        const JsonValue& arguments, std::string& error) {
    error.clear();
    if (name == "audio_list_tracks") {
        if (!HasOnlyKeys(arguments, {})) return Failure(error, "invalid_arguments");
        JsonValue::Array tracks;
        std::error_code iterator_error;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                 service.scope().root(), std::filesystem::directory_options::skip_permission_denied,
                 iterator_error)) {
            if (iterator_error) break;
            if (!entry.is_regular_file(iterator_error)) continue;
            std::filesystem::path relative;
            if (!service.scope().RelativeToRoot(entry.path(), relative, error)) return JsonValue(nullptr);
            if (!service.scope().IsSupportedPath(relative)) continue;
            if (!IsAudioPath(relative.generic_string())) continue;
            std::error_code size_error;
            const auto size = entry.file_size(size_error);
            if (size_error) continue;
            tracks.emplace_back(JsonValue::Object{{"path", relative.generic_string()},
                                                   {"format", Extension(relative.generic_string()).substr(1)},
                                                   {"bytes", static_cast<double>(size)}});
            if (tracks.size() >= 4096) break;
        }
        return JsonValue::Object{{"tracks", std::move(tracks)}};
    }

    if (name != "audio_replace_sfx" && name != "audio_set_level_track")
        return Failure(error, "unknown_tool");
    const bool level_tool = name == "audio_set_level_track";
    const auto allowed = level_tool
        ? std::initializer_list<std::string_view>{"level", "source_path", "dry_run", "backup", "expected_revision"}
        : std::initializer_list<std::string_view>{"source_path", "destination_path", "dry_run", "backup", "expected_revision"};
    if (!HasOnlyKeys(arguments, allowed) || !arguments.contains("source_path"))
        return Failure(error, "invalid_arguments");
    std::string source_text;
    if (!ReadString(arguments.at("source_path"), source_text, 512) || !IsAudioPath(source_text))
        return Failure(error, "invalid_arguments");
    std::filesystem::path source_absolute;
    std::filesystem::path source_relative;
    if (!ResolveAudioPath(service, source_text, source_absolute, source_relative, error)) return JsonValue(nullptr);
    std::error_code filesystem_error;
    if (!std::filesystem::is_regular_file(source_absolute, filesystem_error))
        return Failure(error, "unknown_asset");

    std::string destination_text;
    if (level_tool) {
        int level = 0;
        if (!arguments.contains("level") || !ReadInteger(arguments.at("level"), level, 1, 14))
            return Failure(error, "invalid_arguments");
        destination_text = "missions/location0/level" + std::to_string(level) + "/sounds/game_music.wav";
    } else {
        if (!arguments.contains("destination_path") ||
            !ReadString(arguments.at("destination_path"), destination_text, 512) ||
            !IsAudioPath(destination_text)) return Failure(error, "invalid_arguments");
    }
    std::filesystem::path destination_absolute;
    std::filesystem::path destination_relative;
    if (!ResolveAudioPath(service, destination_text, destination_absolute, destination_relative, error)) return JsonValue(nullptr);
    if (!std::filesystem::is_regular_file(destination_absolute, filesystem_error))
        return Failure(error, "audio_target_missing");
    std::vector<std::uint8_t> bytes;
    if (!ReadBinary(source_absolute, bytes)) return Failure(error, "audio_read_failed");
    const auto previous_bytes = std::filesystem::file_size(destination_absolute, filesystem_error);
    if (filesystem_error) return Failure(error, "audio_read_failed");
    MutationOptions options;
    if (!ReadMutationOptions(arguments, options)) return Failure(error, "invalid_arguments");
    return CommitAudio(service, name, destination_relative, std::move(bytes), previous_bytes, options, error);
}

}  // namespace mcp
