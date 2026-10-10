#include "mcp_tools_strings.h"

#include "mcp_transaction.h"
#include "../renderer/res_writer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace mcp {
namespace {

struct TableEntry {
    std::string key;
    std::string text;
};

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
        if (character < 0x20u && character != '\t') return false;
    }
    result = value.as_string();
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

std::string Extension(std::string_view path) {
    std::string extension = std::filesystem::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension;
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string Trim(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char character) {
        return std::isspace(character) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char character) {
        return std::isspace(character) != 0;
    }).base();
    if (first >= last) return {};
    return std::string(first, last);
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

bool ReadLineTable(const std::string& source, std::vector<TableEntry>& entries) {
    entries.clear();
    std::size_t begin = 0;
    while (begin <= source.size()) {
        const std::size_t end = source.find('\n', begin);
        std::string line = source.substr(begin, end == std::string::npos ? source.size() - begin : end - begin);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string trimmed = Trim(line);
        if (!trimmed.empty() && trimmed.front() != '#') {
            const std::size_t separator = trimmed.find('=');
            if (separator == std::string::npos) return false;
            const std::string key = Trim(trimmed.substr(0, separator));
            if (key.empty()) return false;
            entries.push_back({key, trimmed.substr(separator + 1)});
        }
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return true;
}

bool EntryMatches(std::string_view resource_name, std::string_view key) {
    const std::string resource = Lower(std::string(resource_name));
    const std::string target = Lower(std::string(key));
    if (resource == target) return true;
    if (resource.size() > target.size() + 1 && resource.ends_with("/" + target)) return true;
    if (resource.starts_with("local:") && resource.substr(6) == target) return true;
    return false;
}

std::string DecodeResourceText(const std::vector<std::uint8_t>& bytes) {
    const auto end = std::find(bytes.begin(), bytes.end(), static_cast<std::uint8_t>(0));
    return std::string(bytes.begin(), end);
}

void AppendU32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    output.push_back(static_cast<std::uint8_t>(value & 0xffu));
    output.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffu));
    output.push_back(static_cast<std::uint8_t>((value >> 16) & 0xffu));
    output.push_back(static_cast<std::uint8_t>((value >> 24) & 0xffu));
}

void AppendChunk(std::vector<std::uint8_t>& output, std::uint32_t fourcc,
                 const std::vector<std::uint8_t>& data, bool last_body) {
    const std::uint32_t padding = (4u - (static_cast<std::uint32_t>(data.size()) % 4u)) % 4u;
    const std::uint32_t skip = last_body ? 0u : 16u + static_cast<std::uint32_t>(data.size()) + padding;
    AppendU32(output, fourcc);
    AppendU32(output, static_cast<std::uint32_t>(data.size()));
    AppendU32(output, 4u);
    AppendU32(output, skip);
    output.insert(output.end(), data.begin(), data.end());
    output.insert(output.end(), padding, 0);
}

bool EncodeRes(const std::vector<RESEntry>& entries, std::vector<std::uint8_t>& output) {
    if (entries.empty()) return false;
    constexpr std::uint32_t ilff = 0x46464c49u;
    constexpr std::uint32_t ires = 0x53455249u;
    constexpr std::uint32_t name = 0x454d414eu;
    constexpr std::uint32_t body = 0x59444f42u;
    output.clear();
    AppendU32(output, ilff);
    AppendU32(output, 0);
    AppendU32(output, 4);
    AppendU32(output, 0);
    AppendU32(output, ires);
    for (std::size_t index = 0; index < entries.size(); ++index) {
        std::vector<std::uint8_t> name_bytes(entries[index].name.begin(), entries[index].name.end());
        name_bytes.push_back(0);
        AppendChunk(output, name, name_bytes, false);
        AppendChunk(output, body, entries[index].data, index + 1 == entries.size());
    }
    if (output.size() > std::numeric_limits<std::uint32_t>::max()) return false;
    const std::uint32_t size = static_cast<std::uint32_t>(output.size());
    output[4] = static_cast<std::uint8_t>(size & 0xffu);
    output[5] = static_cast<std::uint8_t>((size >> 8) & 0xffu);
    output[6] = static_cast<std::uint8_t>((size >> 16) & 0xffu);
    output[7] = static_cast<std::uint8_t>((size >> 24) & 0xffu);
    return true;
}

bool LoadTable(GameDataService& service, const std::string& path,
               std::string& format, std::string& line_source,
               std::vector<TableEntry>& lines, std::vector<RESEntry>& archive,
               std::filesystem::path& relative,
               std::string& error) {
    if (!IsSafeRelativePath(path)) {
        error = "path_forbidden";
        return false;
    }
    std::filesystem::path absolute;
    if (!service.scope().ResolveRelative(path, absolute, error) ||
        !service.scope().RelativeToRoot(absolute, relative, error)) return false;
    if (!service.scope().IsSupportedPath(relative)) {
        error = "path_forbidden";
        return false;
    }
    std::error_code filesystem_error;
    if (!std::filesystem::is_regular_file(absolute, filesystem_error)) {
        error = "unknown_asset";
        return false;
    }
    format = Extension(path);
    if (format == ".str") {
        if (!service.LoadProjectText(relative, line_source, error)) return false;
        return ReadLineTable(line_source, lines);
    }
    if (format == ".res") {
        const RESFile parsed = RES_Parse(absolute.string());
        if (!parsed.valid) {
            error = "table_parse_failed";
            return false;
        }
        archive = parsed.entries;
        return true;
    }
    error = "unsupported_format";
    return false;
}

JsonValue CommitTable(GameDataService& service, const std::filesystem::path& relative,
                      const std::vector<std::uint8_t>& bytes, const MutationOptions& options,
                      std::string_view tool, std::string& error) {
    const std::vector<std::filesystem::path> tracked{relative};
    const std::string before = service.ProjectRevision(tracked, error);
    if (!error.empty()) return JsonValue(nullptr);
    auto transaction = service.BeginProjectMutation(options, tracked, error);
    if (!transaction || !transaction->Stage(relative, bytes, error)) return JsonValue(nullptr);
    transaction->SetValidator([](const auto& path, const auto& data, std::string& validation_error) {
        if (data.empty() || (path.extension() != ".str" && path.extension() != ".res")) {
            validation_error = "table_validation_failed";
            return false;
        }
        return true;
    });
    if (!transaction->Commit(error)) return JsonValue(nullptr);
    const std::string after = options.dry_run ? before : service.ProjectRevision(tracked, error);
    if (!error.empty()) return JsonValue(nullptr);
    return JsonValue::Object{{"tool", JsonValue(std::string(tool))},
                             {"path", relative.generic_string()}, {"dry_run", options.dry_run},
                             {"revision_before", before}, {"revision_after", after}};
}

}  // namespace

ToolDefinitionList StringToolDefinitions() {
    JsonValue::Object get{
        {"type", JsonValue("object")},
        {"properties", JsonValue::Object{
            {"path", JsonValue::Object{{"type", JsonValue("string")}}},
            {"key", JsonValue::Object{{"type", JsonValue("string")}}},
        }},
        {"required", JsonValue::Array{JsonValue("path"), JsonValue("key")}},
        {"additionalProperties", JsonValue(false)},
    };
    JsonValue::Object set = get;
    set.at("properties").as_object()["text"] = JsonValue::Object{{"type", JsonValue("string")}};
    set.at("properties").as_object()["dry_run"] = JsonValue::Object{{"type", JsonValue("boolean")}};
    set.at("properties").as_object()["backup"] = JsonValue::Object{{"type", JsonValue("boolean")}};
    set.at("properties").as_object()["expected_revision"] = JsonValue::Object{{"type", JsonValue("string")}};
    set.at("required").as_array().emplace_back("text");
    return ToolDefinitionList{
        {"string_table_get", JsonValue(get)},
        {"string_table_set", JsonValue(set)},
        {"briefing_get_text", JsonValue(get)},
        {"briefing_set_text", JsonValue(std::move(set))},
    };
}

JsonValue CallStringTool(GameDataService& service, std::string_view name,
                         const JsonValue& arguments, std::string& error) {
    error.clear();
    const bool getter = name == "string_table_get" || name == "briefing_get_text";
    const bool setter = name == "string_table_set" || name == "briefing_set_text";
    if (!getter && !setter) return Failure(error, "unknown_tool");
    const auto keys = setter
        ? std::initializer_list<std::string_view>{"path", "key", "text", "dry_run", "backup", "expected_revision"}
        : std::initializer_list<std::string_view>{"path", "key"};
    if (!HasOnlyKeys(arguments, keys) || !arguments.contains("path") || !arguments.contains("key"))
        return Failure(error, "invalid_arguments");
    std::string path;
    std::string key;
    if (!ReadString(arguments.at("path"), path, 512) ||
        !ReadString(arguments.at("key"), key, 512)) return Failure(error, "invalid_arguments");
    std::string format;
    std::string line_source;
    std::vector<TableEntry> lines;
    std::vector<RESEntry> archive;
    std::filesystem::path relative;
    if (!LoadTable(service, path, format, line_source, lines, archive, relative, error)) {
        if (!error.empty()) return JsonValue(nullptr);
        return Failure(error, "table_parse_failed");
    }

    auto line_entry = std::find_if(lines.begin(), lines.end(), [&](const TableEntry& entry) {
        return entry.key == key;
    });
    auto res_entry = std::find_if(archive.begin(), archive.end(), [&](const RESEntry& entry) {
        return EntryMatches(entry.name, key);
    });
    if (getter) {
        if (format == ".str" && line_entry == lines.end()) return Failure(error, "unknown_string_key");
        if (format == ".res" && res_entry == archive.end()) return Failure(error, "unknown_string_key");
        const std::string text = format == ".str" ? line_entry->text : DecodeResourceText(res_entry->data);
        return JsonValue::Object{{"path", path}, {"format", format.substr(1)}, {"key", key}, {"text", text}};
    }

    std::string text;
    if (!ReadString(arguments.at("text"), text, 65536, true)) return Failure(error, "invalid_arguments");
    MutationOptions options;
    if (!ReadMutationOptions(arguments, options)) return Failure(error, "invalid_arguments");
    std::vector<std::uint8_t> output;
    if (format == ".str") {
        std::string updated;
        std::size_t begin = 0;
        bool replaced = false;
        while (begin <= line_source.size()) {
            const std::size_t end = line_source.find('\n', begin);
            std::string line = line_source.substr(begin, end == std::string::npos ? line_source.size() - begin : end - begin);
            const bool had_cr = !line.empty() && line.back() == '\r';
            if (had_cr) line.pop_back();
            const std::string trimmed = Trim(line);
            const std::size_t separator = trimmed.find('=');
            if (separator != std::string::npos && Trim(trimmed.substr(0, separator)) == key) {
                line = key + "=" + text;
                if (had_cr) line.push_back('\r');
                replaced = true;
            }
            updated += line;
            if (end == std::string::npos) break;
            updated.push_back('\n');
            begin = end + 1;
        }
        if (!replaced) {
            if (!updated.empty() && updated.back() != '\n') updated.push_back('\n');
            updated += key + "=" + text + "\n";
        }
        output.assign(updated.begin(), updated.end());
    } else {
        if (res_entry == archive.end()) {
            archive.push_back({key, {}});
            res_entry = std::prev(archive.end());
        }
        res_entry->data.assign(text.begin(), text.end());
        res_entry->data.push_back(0);
        if (!EncodeRes(archive, output)) return Failure(error, "table_write_failed");
    }
    JsonValue result = CommitTable(service, relative, output, options, name, error);
    if (error.empty()) {
        result["key"] = key;
        result["text"] = text;
    }
    return result;
}

}  // namespace mcp
