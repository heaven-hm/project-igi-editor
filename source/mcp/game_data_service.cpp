#include "game_data_service.h"

#include "mcp_transaction.h"

#include "../level/qsc_lexer.h"
#include "../level/qsc_parser.h"
#include "../level/qvm_compiler.h"
#include "../level/qvm_decompiler.h"
#include "../level/qvm_parser.h"
#include "../level/task_schema.h"
#include "mcp_task_id.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#ifdef GetObject
#undef GetObject
#endif
#endif

namespace mcp {
namespace {

std::vector<std::filesystem::path> MissionDirectoryCandidatesFor(int mission_id) {
    std::vector<std::filesystem::path> candidates;
    if (mission_id >= 1 && mission_id <= 14) {
        candidates.emplace_back(std::filesystem::path("missions") / "location0" /
                                ("level" + std::to_string(mission_id)));
    }
    return candidates;
}

std::string FoldPath(const std::filesystem::path& path) {
    std::string result = path.generic_string();
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return result;
}

std::string TrimWin32Component(std::string value) {
    while (!value.empty() && (value.back() == '.' || value.back() == ' ')) value.pop_back();
    return value;
}

bool IsWithinRoot(const std::filesystem::path& root, const std::filesystem::path& candidate) {
    const std::string root_text = FoldPath(root.lexically_normal());
    const std::string candidate_text = FoldPath(candidate.lexically_normal());
    if (candidate_text == root_text) return true;
    return candidate_text.size() > root_text.size() &&
           candidate_text.starts_with(root_text) &&
           candidate_text[root_text.size()] == '/';
}

bool HasTraversal(const std::filesystem::path& path) {
    for (const auto& component : path) {
        if (component == "..") return true;
    }
    return false;
}

bool ContainsReparsePoint(const std::filesystem::path& root,
                          const std::filesystem::path& relative_path) {
    std::filesystem::path current = root;
    for (const auto& component : relative_path) {
        current /= component;
#ifdef _WIN32
        const DWORD attributes = GetFileAttributesW(current.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            const DWORD last_error = GetLastError();
            if (last_error == ERROR_FILE_NOT_FOUND || last_error == ERROR_PATH_NOT_FOUND) break;
            return true;
        }
        if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) return true;
#else
        std::error_code link_error;
        if (std::filesystem::is_symlink(current, link_error) && !link_error) return true;
#endif
    }
    return false;
}

bool IsMcpBackupPath(const std::filesystem::path& path) {
    for (const auto& component : path) {
        if (TrimWin32Component(FoldPath(component)) == ".mcp-backups") return true;
    }
    return false;
}

void Mix(std::uint64_t& hash, std::string_view bytes) {
    constexpr std::uint64_t kPrime = 1099511628211ull;
    for (const unsigned char byte : bytes) {
        hash ^= byte;
        hash *= kPrime;
    }
}

bool HashFile(const std::filesystem::path& file, std::uint64_t& hash) {
    std::ifstream input(file, std::ios::binary);
    if (!input) return false;
    char buffer[4096];
    while (input.read(buffer, sizeof(buffer)) || input.gcount() > 0) {
        Mix(hash, std::string_view(buffer, static_cast<std::size_t>(input.gcount())));
    }
    return input.eof();
}

bool ReadBoundedText(const std::filesystem::path& file, std::string& text, std::string& error) {
    std::error_code size_error;
    const auto size = std::filesystem::file_size(file, size_error);
    if (size_error || size > kMaxJsonMessageBytes) {
        error = "source_too_large";
        return false;
    }
    std::ifstream input(file, std::ios::binary);
    if (!input) {
        error = "read_failed";
        return false;
    }
    text.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    if (!input.eof() && input.fail()) {
        error = "read_failed";
        return false;
    }
    error.clear();
    return true;
}

std::string CalculateTrackedRevision(
    const ProjectScope& scope,
    const std::vector<std::filesystem::path>& relative_paths,
    std::string& error) {
    std::vector<std::filesystem::path> normalized_paths;
    normalized_paths.reserve(relative_paths.size());
    for (const auto& relative_path : relative_paths) {
        std::filesystem::path absolute_path;
        if (!scope.ResolveRelative(relative_path, absolute_path, error)) return {};
        if (!std::filesystem::is_regular_file(absolute_path)) {
            error = "file_not_found";
            return {};
        }
        std::filesystem::path normalized;
        if (!scope.RelativeToRoot(absolute_path, normalized, error)) return {};
        normalized_paths.push_back(std::move(normalized));
    }

    std::sort(normalized_paths.begin(), normalized_paths.end(),
              [](const auto& left, const auto& right) {
                  return FoldPath(left) < FoldPath(right);
              });

    std::uint64_t hash = 14695981039346656037ull;
    for (const auto& relative_path : normalized_paths) {
        std::filesystem::path absolute_path;
        if (!scope.ResolveRelative(relative_path, absolute_path, error)) return {};
        Mix(hash, relative_path.generic_string());
        Mix(hash, std::string_view("\0", 1));
        if (!HashFile(absolute_path, hash)) {
            error = "revision_failed";
            return {};
        }
        Mix(hash, std::string_view("\0", 1));
    }

    std::ostringstream fingerprint;
    fingerprint << std::hex << std::setw(16) << std::setfill('0') << hash;
    error.clear();
    return fingerprint.str();
}

bool IsAvailableCatalogId(const ProjectScope& scope, std::string_view value,
                          std::string& error) {
    if (value.empty() || value.size() > 128) {
        error = "unknown_asset_id";
        return false;
    }
    std::filesystem::path catalog_path;
    std::string path_error;
    if (!scope.ResolveRelative("editor/tools/IGIModels.json", catalog_path, path_error)) {
        error = "asset_catalog_unavailable";
        return false;
    }
    std::error_code status_error;
    if (!std::filesystem::is_regular_file(catalog_path, status_error) || status_error) {
        error = "asset_catalog_unavailable";
        return false;
    }
    std::string catalog_text;
    std::string read_error;
    if (!ReadBoundedText(catalog_path, catalog_text, read_error)) {
        error = "asset_catalog_unavailable";
        return false;
    }
    JsonValue catalog;
    JsonError parse_error;
    if (!JsonParse(catalog_text, catalog, parse_error) || !catalog.is_array()) {
        error = "asset_catalog_unavailable";
        return false;
    }
    for (const auto& entry : catalog.as_array()) {
        if (!entry.is_object() || !entry.contains("ModelName") ||
            !entry.contains("ModelId") || !entry.at("ModelName").is_string() ||
            !entry.at("ModelId").is_string()) continue;
        if (entry.at("ModelName").as_string() == value &&
            !entry.at("ModelId").as_string().empty()) {
            error.clear();
            return true;
        }
    }
    error = "unknown_asset_id";
    return false;
}

bool LoadObjectQsc(const ProjectScope& scope, const std::filesystem::path& level_directory,
                  std::string& source, std::string& error) {
    std::filesystem::path level_relative;
    if (!scope.RelativeToRoot(level_directory, level_relative, error)) return false;

    std::filesystem::path qsc_path;
    if (!scope.ResolveRelative(level_relative / "objects.qsc", qsc_path, error)) return false;
    std::error_code status_error;
    if (std::filesystem::is_regular_file(qsc_path, status_error) && !status_error) {
        return ReadBoundedText(qsc_path, source, error);
    }

    std::filesystem::path qvm_path;
    if (!scope.ResolveRelative(level_relative / "objects.qvm", qvm_path, error)) return false;
    const QVMFile qvm = QVM_Parse(qvm_path.string());
    if (!qvm.valid) {
        error = "qvm_invalid";
        return false;
    }
    source = QVM_DecompileToString(qvm);
    if (source.empty() && !qvm.instructions.empty()) {
        error = "qvm_decompile_failed";
        return false;
    }
    error.clear();
    return true;
}

std::string ScalarStableText(const qsc::Node& node) {
    switch (node.kind) {
    case qsc::NodeKind::IntLit: return std::to_string(node.i_val);
    case qsc::NodeKind::FloatLit: return StableNumberText(static_cast<double>(node.f_val));
    case qsc::NodeKind::BoolLit: return node.b_val ? "true" : "false";
    case qsc::NodeKind::StringLit:
    case qsc::NodeKind::IdentLit: return node.s_val;
    case qsc::NodeKind::Unary:
        if (!node.children.empty()) return node.s_val + ScalarStableText(*node.children.front());
        break;
    default: break;
    }
    return {};
}

JsonValue ScalarJsonValue(const qsc::Node& node) {
    switch (node.kind) {
    case qsc::NodeKind::IntLit: return JsonValue(node.i_val);
    case qsc::NodeKind::FloatLit: return JsonValue(static_cast<double>(node.f_val));
    case qsc::NodeKind::BoolLit: return JsonValue(node.b_val);
    case qsc::NodeKind::StringLit:
    case qsc::NodeKind::IdentLit: return JsonValue(node.s_val);
    case qsc::NodeKind::Unary:
        if (node.children.size() == 1 && node.s_val == "-" &&
            (node.children.front()->kind == qsc::NodeKind::IntLit ||
             node.children.front()->kind == qsc::NodeKind::FloatLit)) {
            const JsonValue value = ScalarJsonValue(*node.children.front());
            return JsonValue(-value.as_number());
        }
        return JsonValue(node.s_val +
                         (node.children.empty() ? std::string{} : ScalarStableText(*node.children.front())));
    default: return JsonValue(nullptr);
    }
}

bool ScalarNumber(const qsc::Node& node, double& value) {
    if (node.kind == qsc::NodeKind::IntLit) {
        value = static_cast<double>(node.i_val);
        return true;
    }
    if (node.kind == qsc::NodeKind::FloatLit) {
        value = static_cast<double>(node.f_val);
        return std::isfinite(value);
    }
    if (node.kind == qsc::NodeKind::Unary && node.s_val == "-" && node.children.size() == 1) {
        if (!ScalarNumber(*node.children.front(), value)) return false;
        value = -value;
        return true;
    }
    return false;
}

const qsc::Node* ScalarChild(const qsc::Node& call, std::size_t index) {
    if (index >= call.children.size() || call.children[index]->kind == qsc::NodeKind::Call) return nullptr;
    return call.children[index].get();
}

std::string ChildString(const qsc::Node& call, std::size_t index) {
    const qsc::Node* child = ScalarChild(call, index);
    if (!child) return {};
    if (child->kind == qsc::NodeKind::StringLit || child->kind == qsc::NodeKind::IdentLit) {
        return child->s_val;
    }
    return ScalarStableText(*child);
}

std::string AnonymousTaskSignature(const qsc::Node& call) {
    std::string signature;
    for (std::size_t index = 1; index < call.children.size(); ++index) {
        signature.push_back('|');
        signature += AnonymousArgumentSignature(*call.children[index]);
    }
    return signature;
}

std::size_t NodeSourceOffset(std::string_view source, const qsc::Node& node) {
    if (node.line == 0 || node.col == 0) return std::string_view::npos;
    std::size_t offset = 0;
    uint32_t line = 1;
    while (line < node.line) {
        const std::size_t newline = source.find('\n', offset);
        if (newline == std::string_view::npos) return std::string_view::npos;
        offset = newline + 1;
        ++line;
    }
    return offset + static_cast<std::size_t>(node.col - 1);
}

struct SnapshotRecord {
    const qsc::Node* call = nullptr;
    std::string id;
    std::string base_id;
    std::string parent_id;
    std::vector<std::string> children;
    bool anonymous = false;
    bool ambiguous = false;
};

void RegisterDeclaredSchemas(const qsc::Node& node) {
    if (node.kind == qsc::NodeKind::Call && node.s_val == "Task_DeclareParameters" &&
        !node.children.empty()) {
        std::vector<std::string> arguments;
        arguments.reserve(node.children.size());
        for (const auto& child : node.children) arguments.push_back(ScalarStableText(*child));

        if (!arguments.front().empty() && arguments.size() >= 3) {
            TaskSchemaNS::TaskSchema schema;
            int offset = 3;
            for (std::size_t index = 1; index + 1 < arguments.size(); index += 2) {
                TaskSchemaNS::FieldDef field;
                field.name = arguments[index];
                field.typeName = arguments[index + 1];
                field.argOffset = offset;
                field.argCount = TaskSchemaNS::TypeArgCount(field.typeName);
                offset += field.argCount;
                schema.push_back(std::move(field));
            }
            TaskSchemaNS::RegisterSchema(arguments.front(), std::move(schema));
        }
    }
    for (const auto& child : node.children) RegisterDeclaredSchemas(*child);
}

bool ConfigureSchemasFromProgram(const qsc::Node& program) {
    TaskSchemaNS::ClearRegisteredSchemas();
    RegisterDeclaredSchemas(program);
    return true;
}

void CollectSnapshotRecords(std::string_view source, const qsc::Node& node,
                            const std::string& parent_id,
                            std::vector<SnapshotRecord>& records,
                            std::unordered_map<std::string, int>& id_counts,
                            std::unordered_map<std::string, int>& next_suffix,
                            std::unordered_set<std::string>& used_ids) {
    if (node.kind == qsc::NodeKind::Call && node.s_val == "Task_New") {
        std::string id = ChildString(node, 0);
        if (id.empty()) id = "anonymous";
        const bool anonymous = id == "-1" || id == "anonymous";
        if (anonymous) {
            const std::size_t offset = NodeSourceOffset(source, node);
            const auto marker = offset == std::string_view::npos
                                    ? std::optional<std::string>{}
                                    : AnonymousTaskMarkerBefore(source, offset);
            id = marker.value_or(AnonymousTaskId(parent_id, AnonymousTaskSignature(node)));
        }
        const std::string base_id = id;
        ++id_counts[base_id];
        id = UniqueTaskId(base_id, next_suffix, used_ids);
        records.push_back({&node, id, base_id, parent_id, {}, anonymous, false});
        const std::size_t record_index = records.size() - 1;
        for (const auto& child : node.children) {
            CollectSnapshotRecords(source, *child, id, records, id_counts, next_suffix, used_ids);
        }
        for (std::size_t index = record_index + 1; index < records.size(); ++index) {
            if (records[index].parent_id == id) records[record_index].children.push_back(records[index].id);
        }
        return;
    }
    for (const auto& child : node.children) {
        CollectSnapshotRecords(source, *child, parent_id, records, id_counts, next_suffix, used_ids);
    }
}

JsonValue SnapshotRecordJson(const SnapshotRecord& record) {
    const qsc::Node& call = *record.call;
    JsonValue::Object object;
    object["id"] = record.id;
    object["parent_id"] = record.parent_id.empty() ? JsonValue(nullptr) : JsonValue(record.parent_id);
    object["function"] = call.s_val;
    object["type"] = ChildString(call, 1);
    object["name"] = ChildString(call, 2);
    object["source_line"] = static_cast<int>(call.line);
    object["writable"] = !record.ambiguous;
    object["id_status"] = record.ambiguous ? JsonValue("ambiguous") : JsonValue("stable");

    JsonValue::Array args;
    for (const auto& child : call.children) {
        args.push_back(child->kind == qsc::NodeKind::Call ? JsonValue(nullptr) : ScalarJsonValue(*child));
    }
    object["args"] = std::move(args);

    auto numericVector = [&](std::size_t first) {
        JsonValue::Array values;
        for (std::size_t index = first; index < first + 3; ++index) {
            double value = 0.0;
            if (!ScalarChild(call, index) || !ScalarNumber(*ScalarChild(call, index), value)) return JsonValue::Array{};
            values.emplace_back(value);
        }
        return values;
    };

    int position = -1;
    int rotation = -1;
    int gamma = -1;
    int model = -1;
    const TaskSchemaNS::TaskSchema* schema = TaskSchemaNS::GetSchema(ChildString(call, 1));
    if (schema != nullptr) {
        for (const auto& field : *schema) {
            if (field.name == "Position" && field.typeName == "ObjectPos" && field.argCount == 3)
                position = field.argOffset;
            else if (field.name == "Orientation" && field.typeName == "Real32x9" && field.argCount == 3)
                rotation = field.argOffset;
            else if (field.name == "Gamma" && field.typeName == "Real32" && field.argCount == 1)
                gamma = field.argOffset;
            else if (field.name == "Model" && field.argCount == 1 &&
                     (field.typeName == "String16" || field.typeName == "String32" ||
                      field.typeName == "String256" || field.typeName == "VarString"))
                model = field.argOffset;
        }
    }
    object["position"] = position >= 0 ? numericVector(static_cast<std::size_t>(position))
                                       : JsonValue::Array{};
    if (rotation >= 0) {
        object["rotation_radians"] = numericVector(static_cast<std::size_t>(rotation));
    } else if (gamma >= 0) {
        JsonValue::Array values{0.0, 0.0};
        double value = 0.0;
        if (ScalarChild(call, static_cast<std::size_t>(gamma)) &&
            ScalarNumber(*ScalarChild(call, static_cast<std::size_t>(gamma)), value)) {
            values.emplace_back(value);
            object["rotation_radians"] = std::move(values);
        } else {
            object["rotation_radians"] = JsonValue::Array{};
        }
    } else {
        object["rotation_radians"] = JsonValue::Array{};
    }
    if (model >= 0) {
        const std::string model_id = ChildString(call, static_cast<std::size_t>(model));
        if (!model_id.empty()) object["model_id"] = model_id;
    }

    JsonValue::Array children;
    for (const auto& child : record.children) children.emplace_back(child);
    object["children"] = std::move(children);
    return object;
}

bool ParseSnapshot(const std::string& source, JsonValue::Array& objects, std::string& error) {
    const qsc::LexResult lexed = qsc::Lex(source);
    if (!lexed.ok) {
        error = "qsc_lex_failed";
        return false;
    }
    const qsc::ParseResult parsed = qsc::Parse(lexed.tokens);
    if (!parsed.ok || !parsed.program) {
        error = "qsc_parse_failed";
        return false;
    }
    ConfigureSchemasFromProgram(*parsed.program);
    std::unordered_map<std::string, int> id_counts;
    std::unordered_map<std::string, int> next_suffix;
    std::unordered_set<std::string> used_ids;
    std::vector<SnapshotRecord> records;
    CollectSnapshotRecords(source, *parsed.program, {}, records, id_counts, next_suffix, used_ids);
    for (auto& record : records) {
        const auto count = id_counts.find(record.base_id);
        record.ambiguous = !record.anonymous && count != id_counts.end() && count->second > 1;
    }
    for (const auto& record : records) objects.emplace_back(SnapshotRecordJson(record));
    error.clear();
    return true;
}

}  // namespace

std::optional<ProjectScope> ProjectScope::Open(const std::filesystem::path& project_root,
                                                std::string& error) {
    std::error_code status_error;
    if (project_root.empty() || !std::filesystem::is_directory(project_root, status_error) ||
        status_error) {
        error = "invalid_project_root";
        return std::nullopt;
    }

    std::error_code canonical_error;
    const std::filesystem::path canonical_root = std::filesystem::canonical(project_root, canonical_error);
    if (canonical_error || !std::filesystem::is_directory(canonical_root, status_error) || status_error) {
        error = "invalid_project_root";
        return std::nullopt;
    }

    error.clear();
    return ProjectScope(canonical_root);
}

bool ProjectScope::ResolveRelative(const std::filesystem::path& relative_path,
                                   std::filesystem::path& resolved_path,
                                   std::string& error,
                                   bool allow_internal_backup) const {
    if (relative_path.empty() || relative_path.is_absolute() || relative_path.has_root_name() ||
        HasTraversal(relative_path)) {
        error = "path_forbidden";
        return false;
    }
    if (!allow_internal_backup && IsMcpBackupPath(relative_path)) {
        error = "path_forbidden";
        return false;
    }
    if (ContainsReparsePoint(root_, relative_path)) {
        error = "path_forbidden";
        return false;
    }

    std::error_code canonical_error;
    const std::filesystem::path candidate =
        std::filesystem::weakly_canonical(root_ / relative_path, canonical_error);
    if (canonical_error || !IsWithinRoot(root_, candidate)) {
        error = "path_forbidden";
        return false;
    }

    resolved_path = candidate;
    error.clear();
    return true;
}

bool ProjectScope::RelativeToRoot(const std::filesystem::path& path,
                                  std::filesystem::path& relative_path,
                                  std::string& error) const {
    if (path.empty()) {
        error = "path_forbidden";
        return false;
    }

    std::error_code canonical_error;
    const std::filesystem::path candidate = std::filesystem::weakly_canonical(path, canonical_error);
    if (canonical_error || !IsWithinRoot(root_, candidate)) {
        error = "path_forbidden";
        return false;
    }

    relative_path = candidate.lexically_relative(root_);
    if (relative_path.empty() || HasTraversal(relative_path)) {
        error = "path_forbidden";
        return false;
    }
    error.clear();
    return true;
}

bool ProjectScope::IsSupportedPath(const std::filesystem::path& relative_path) const {
    if (relative_path.empty() || relative_path.is_absolute() || relative_path.has_root_name() ||
        HasTraversal(relative_path)) {
        return false;
    }

    const std::filesystem::path normalized = relative_path.lexically_normal();
    auto component = normalized.begin();
    if (component == normalized.end() || FoldPath(*component) != "missions") return true;
    ++component;
    if (component == normalized.end()) return true;

    const std::string mission_layout = FoldPath(*component);
    return mission_layout == "location0";
}

bool ProjectScope::LevelDirectory(int level, std::filesystem::path& level_directory,
                                  std::string& error) const {
    const std::vector<std::filesystem::path> candidates = MissionDirectoryCandidatesFor(level);
    if (candidates.empty()) {
        error = "invalid_level";
        return false;
    }

    for (const auto& mission_path : candidates) {
        std::filesystem::path candidate_directory;
        std::string candidate_error;
        if (!ResolveRelative(mission_path, candidate_directory, candidate_error)) continue;

        std::filesystem::path objects_qvm;
        if (!ResolveRelative(mission_path / "objects.qvm", objects_qvm, candidate_error)) continue;

        std::error_code status_error;
        if (std::filesystem::is_directory(candidate_directory, status_error) && !status_error &&
            std::filesystem::is_regular_file(objects_qvm, status_error) && !status_error) {
            level_directory = std::move(candidate_directory);
            error.clear();
            return true;
        }
    }
    error = "invalid_level";
    return false;
}

bool GameDataService::CalculateRevision(int level, const std::filesystem::path& level_directory,
                                        LevelRevision& revision, std::string& error) const {
    std::vector<std::filesystem::path> files;
    std::error_code iterator_error;
    for (std::filesystem::recursive_directory_iterator iterator(
             level_directory, std::filesystem::directory_options::skip_permission_denied, iterator_error),
         end;
         iterator != end;
         iterator.increment(iterator_error)) {
        if (iterator_error) {
            error = "revision_failed";
            return false;
        }
        std::error_code type_error;
        if (iterator->is_regular_file(type_error) && !type_error) files.push_back(iterator->path());
    }
    if (iterator_error) {
        error = "revision_failed";
        return false;
    }

    std::sort(files.begin(), files.end(), [](const auto& left, const auto& right) {
        return FoldPath(left) < FoldPath(right);
    });

    std::uint64_t hash = 14695981039346656037ull;
    for (const auto& file : files) {
        std::filesystem::path relative;
        if (!scope_.RelativeToRoot(file, relative, error)) return false;
        if (IsMcpBackupPath(relative)) continue;
        Mix(hash, relative.generic_string());
        Mix(hash, std::string_view("\0", 1));
        if (!HashFile(file, hash)) {
            error = "revision_failed";
            return false;
        }
        Mix(hash, std::string_view("\0", 1));
    }

    std::ostringstream fingerprint;
    fingerprint << std::hex << std::setw(16) << std::setfill('0') << hash;
    revision = LevelRevision{level, fingerprint.str()};
    error.clear();
    return true;
}

bool GameDataService::OpenLevel(int level, std::string& error) {
    std::lock_guard<std::mutex> lock(*mutation_mutex_);
    TaskSchemaNS::ClearRegisteredSchemas();
    std::filesystem::path level_directory;
    if (!scope_.LevelDirectory(level, level_directory, error)) return false;

    std::string source;
    std::string schema_error;
    if (LoadObjectQsc(scope_, level_directory, source, schema_error)) {
        const qsc::LexResult lexed = qsc::Lex(source);
        const qsc::ParseResult parsed = lexed.ok ? qsc::Parse(lexed.tokens) : qsc::ParseResult{};
        if (lexed.ok && parsed.ok && parsed.program) ConfigureSchemasFromProgram(*parsed.program);
    }

    LevelRevision revision;
    if (!CalculateRevision(level, level_directory, revision, error)) return false;
    current_revision_ = std::move(revision);
    error.clear();
    return true;
}

bool GameDataService::RefreshRevisionUnlocked(std::string& error) {
    if (!current_revision_) {
        error = "level_not_open";
        return false;
    }

    std::filesystem::path level_directory;
    if (!scope_.LevelDirectory(current_revision_->level, level_directory, error)) return false;
    LevelRevision revision;
    if (!CalculateRevision(current_revision_->level, level_directory, revision, error)) return false;
    current_revision_ = std::move(revision);
    error.clear();
    return true;
}

bool GameDataService::RefreshRevision(std::string& error) {
    std::lock_guard<std::mutex> lock(*mutation_mutex_);
    return RefreshRevisionUnlocked(error);
}

LevelRevision GameDataService::CurrentRevision() const {
    std::lock_guard<std::mutex> lock(*mutation_mutex_);
    if (!current_revision_) throw std::logic_error("level is not open");
    return *current_revision_;
}

JsonValue GameDataService::ProjectInfo() const {
    std::lock_guard<std::mutex> lock(*mutation_mutex_);
    JsonValue::Object info;
    info["schema_version"] = "1";
    info["project_type"] = "igi1";
    info["root_kind"] = "configured_game_root";
    info["protocol_profile"] = "2026-07-28";
    info["game_data_only"] = true;
    info["capabilities"] = JsonValue::Object{
        {"levels", true}, {"reload", true}, {"validate", true}, {"snapshots", true}};
    if (current_revision_) {
        info["current_level"] = current_revision_->level;
        info["current_revision"] = current_revision_->fingerprint;
    } else {
        info["current_level"] = JsonValue(nullptr);
        info["current_revision"] = JsonValue(nullptr);
    }
    return info;
}

bool GameDataService::HasOpenLevel() const {
    std::lock_guard<std::mutex> lock(*mutation_mutex_);
    return current_revision_.has_value();
}

JsonValue GameDataService::ListLevels(std::string& error) const {
    std::lock_guard<std::mutex> lock(*mutation_mutex_);
    static constexpr int kMissionIds[] = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
    JsonValue::Array levels;
    for (const int level : kMissionIds) {
        std::filesystem::path directory;
        std::string level_error;
        if (!scope_.LevelDirectory(level, directory, level_error)) continue;
        std::filesystem::path relative;
        if (!scope_.RelativeToRoot(directory, relative, error)) return JsonValue(nullptr);
        const std::string relative_text = relative.generic_string();
        const std::string layout = "location0";

        JsonValue::Object entry;
        entry["level"] = level;
        entry["layout"] = layout;
        entry["relative_path"] = relative_text;
        entry["available"] = true;
        JsonValue::Object files;
        std::error_code status_error;
        files["objects_qvm"] = std::filesystem::is_regular_file(directory / "objects.qvm", status_error) &&
                                 !status_error;
        status_error.clear();
        files["mission_qvm"] = std::filesystem::is_regular_file(directory / "mission.qvm", status_error) &&
                                 !status_error;
        status_error.clear();
        files["terrain"] = std::filesystem::is_directory(directory / "terrain", status_error) &&
                             !status_error;
        status_error.clear();
        files["graphs"] = std::filesystem::is_directory(directory / "graphs", status_error) &&
                           !status_error;
        entry["files"] = std::move(files);
        levels.emplace_back(std::move(entry));
    }
    error.clear();
    return JsonValue(std::move(levels));
}

JsonValue GameDataService::LevelManifest(int level, std::string& error) const {
    std::lock_guard<std::mutex> lock(*mutation_mutex_);
    std::filesystem::path level_directory;
    if (!scope_.LevelDirectory(level, level_directory, error)) return JsonValue(nullptr);
    LevelRevision revision;
    if (!CalculateRevision(level, level_directory, revision, error)) return JsonValue(nullptr);

    JsonValue::Array files;
    std::vector<std::filesystem::path> paths;
    std::error_code iterator_error;
    for (std::filesystem::recursive_directory_iterator iterator(
             level_directory, std::filesystem::directory_options::skip_permission_denied, iterator_error),
         end;
         iterator != end; iterator.increment(iterator_error)) {
        if (iterator_error) {
            error = "manifest_failed";
            return JsonValue(nullptr);
        }
        std::filesystem::path relative;
        if (!scope_.RelativeToRoot(iterator->path(), relative, error)) return JsonValue(nullptr);
        if (IsMcpBackupPath(relative)) continue;
        std::error_code type_error;
        if (iterator->is_regular_file(type_error) && !type_error) paths.push_back(iterator->path());
    }
    if (iterator_error) {
        error = "manifest_failed";
        return JsonValue(nullptr);
    }
    std::sort(paths.begin(), paths.end(), [this](const auto& left, const auto& right) {
        return FoldPath(left) < FoldPath(right);
    });
    for (const auto& path : paths) {
        std::filesystem::path relative;
        if (!scope_.RelativeToRoot(path, relative, error)) return JsonValue(nullptr);
        std::error_code size_error;
        const auto size = std::filesystem::file_size(path, size_error);
        if (size_error) {
            error = "manifest_failed";
            return JsonValue(nullptr);
        }
        files.emplace_back(JsonValue::Object{
            {"path", relative.generic_string()}, {"bytes", static_cast<double>(size)}});
    }

    JsonValue::Object manifest;
    manifest["level"] = level;
    std::filesystem::path level_relative;
    if (!scope_.RelativeToRoot(level_directory, level_relative, error)) return JsonValue(nullptr);
    manifest["relative_path"] = level_relative.generic_string();
    manifest["revision"] = revision.fingerprint;
    manifest["files"] = std::move(files);
    error.clear();
    return manifest;
}

JsonValue GameDataService::ListObjects(int level, std::string& error) const {
    std::lock_guard<std::mutex> lock(*mutation_mutex_);
    std::filesystem::path level_directory;
    if (!scope_.LevelDirectory(level, level_directory, error)) return JsonValue(nullptr);
    LevelRevision revision;
    if (!CalculateRevision(level, level_directory, revision, error)) return JsonValue(nullptr);
    std::string source;
    if (!LoadObjectQsc(scope_, level_directory, source, error)) return JsonValue(nullptr);
    JsonValue::Array objects;
    if (!ParseSnapshot(source, objects, error)) return JsonValue(nullptr);
    JsonValue::Object result;
    result["level"] = level;
    result["revision"] = revision.fingerprint;
    result["objects"] = std::move(objects);
    error.clear();
    return result;
}

JsonValue GameDataService::GetObject(int level, std::string_view task_id, std::string& error) const {
    if (task_id.empty() || task_id.size() > 128) {
        error = "invalid_task_id";
        return JsonValue(nullptr);
    }
    const JsonValue snapshot = ListObjects(level, error);
    if (!error.empty()) return JsonValue(nullptr);
    for (const auto& object : snapshot.at("objects").as_array()) {
        if (object.at("id").as_string() == task_id) {
            error.clear();
            return object;
        }
    }
    error = "unknown_task_id";
    return JsonValue(nullptr);
}

JsonValue GameDataService::ObjectSnapshotFromSource(int level, std::string_view source,
                                                    std::string_view task_id,
                                                    std::string& error) const {
    return ObjectSnapshotFromSource(level, source, task_id, 0, error);
}

JsonValue GameDataService::ObjectSnapshotFromSource(int level, std::string_view source,
                                                    std::string_view task_id, int source_line,
                                                    std::string& error) const {
    std::lock_guard<std::mutex> lock(*mutation_mutex_);
    if (task_id.empty() || task_id.size() > 128) {
        error = "invalid_task_id";
        return JsonValue(nullptr);
    }
    JsonValue::Array objects;
    if (!ParseSnapshot(std::string(source), objects, error)) return JsonValue(nullptr);
    for (const auto& object : objects) {
        if (object.at("id").as_string() == task_id ||
            (source_line > 0 && object.at("source_line").as_number() == source_line)) {
            error.clear();
            return object;
        }
    }
    error = "unknown_task_id";
    return JsonValue(nullptr);
}

bool GameDataService::IsAvailablePickupId(std::string_view pickup_id, std::string& error) const {
    return IsAvailableCatalogId(scope_, pickup_id, error);
}

bool GameDataService::IsAvailableModelId(std::string_view model_id, std::string& error) const {
    if (model_id.empty() || model_id.size() > 128) {
        error = "unknown_asset_id";
        return false;
    }
    std::filesystem::path catalog_path;
    std::string path_error;
    if (!scope_.ResolveRelative("editor/tools/IGIModels.json", catalog_path, path_error)) {
        error = "asset_catalog_unavailable";
        return false;
    }
    std::string catalog_text;
    std::string read_error;
    if (!ReadBoundedText(catalog_path, catalog_text, read_error)) {
        error = "asset_catalog_unavailable";
        return false;
    }
    JsonValue catalog;
    JsonError parse_error;
    if (!JsonParse(catalog_text, catalog, parse_error) || !catalog.is_array()) {
        error = "asset_catalog_unavailable";
        return false;
    }
    for (const auto& entry : catalog.as_array()) {
        if (!entry.is_object() || !entry.contains("ModelId") ||
            !entry.at("ModelId").is_string()) continue;
        if (entry.at("ModelId").as_string() == model_id && !model_id.empty()) {
            error.clear();
            return true;
        }
    }
    error = "unknown_asset_id";
    return false;
}

bool GameDataService::IsAvailableWeaponId(std::string_view weapon_id, std::string& error) const {
    return IsAvailableCatalogId(scope_, weapon_id, error);
}

bool GameDataService::IsAvailableAmmoId(std::string_view ammo_id, std::string& error) const {
    return IsAvailableCatalogId(scope_, ammo_id, error);
}

JsonValue GameDataService::ValidateLevel(int level, std::string& error) const {
    std::lock_guard<std::mutex> lock(*mutation_mutex_);
    std::filesystem::path level_directory;
    if (!scope_.LevelDirectory(level, level_directory, error)) return JsonValue(nullptr);
    LevelRevision revision;
    if (!CalculateRevision(level, level_directory, revision, error)) return JsonValue(nullptr);

    JsonValue::Array checks;
    JsonValue::Array failures;
    bool valid = true;
    const auto addCheck = [&](std::string name, bool passed, std::string code,
                              std::string summary) {
        checks.emplace_back(JsonValue::Object{
            {"name", std::move(name)},
            {"status", passed ? JsonValue("passed") : JsonValue("failed")},
            {"code", code.empty() ? JsonValue(nullptr) : JsonValue(std::move(code))},
            {"summary", std::move(summary)},
        });
        if (!passed) {
            valid = false;
            failures.emplace_back(JsonValue::Object{
                {"code", checks.back().at("code")},
                {"field", JsonValue(nullptr)},
                {"summary", checks.back().at("summary")},
            });
        }
    };

    std::filesystem::path level_relative;
    if (!scope_.RelativeToRoot(level_directory, level_relative, error)) return JsonValue(nullptr);
    std::filesystem::path qvm_path;
    if (!scope_.ResolveRelative(level_relative / "objects.qvm", qvm_path, error)) return JsonValue(nullptr);
    const QVMFile qvm = QVM_Parse(qvm_path.string());
    addCheck("objects_qvm", qvm.valid, qvm.valid ? "" : "qvm_invalid",
             qvm.valid ? "objects.qvm parsed" : "objects.qvm failed validation");

    std::string source;
    std::string source_error;
    const bool source_loaded = LoadObjectQsc(scope_, level_directory, source, source_error);
    if (!source_loaded) {
        addCheck("objects_qsc", false, source_error, "objects source could not be loaded");
    } else {
        JsonValue::Array ignored_objects;
        const bool parsed = ParseSnapshot(source, ignored_objects, source_error);
        addCheck("objects_qsc", parsed, parsed ? "" : source_error,
                 parsed ? "objects source lexed and parsed" : "objects source failed validation");
    }

    JsonValue::Object result;
    result["level"] = level;
    result["revision"] = revision.fingerprint;
    result["valid"] = valid;
    result["checks"] = std::move(checks);
    result["errors"] = std::move(failures);
    error.clear();
    return result;
}

bool GameDataService::LoadCurrentObjectSource(std::string& source, std::string& error) const {
    std::lock_guard<std::mutex> lock(*mutation_mutex_);
    if (!current_revision_) {
        error = "level_not_open";
        return false;
    }
    std::filesystem::path level_directory;
    if (!scope_.LevelDirectory(current_revision_->level, level_directory, error)) return false;
    if (!LoadObjectQsc(scope_, level_directory, source, error)) return false;
    const qsc::LexResult lexed = qsc::Lex(source);
    const qsc::ParseResult parsed = lexed.ok ? qsc::Parse(lexed.tokens) : qsc::ParseResult{};
    if (!lexed.ok || !parsed.ok || !parsed.program) {
        error = "qsc_parse_failed";
        return false;
    }
    ConfigureSchemasFromProgram(*parsed.program);
    error.clear();
    return true;
}

bool GameDataService::LoadProjectText(const std::filesystem::path& relative_path,
                                      std::string& text, std::string& error) const {
    std::lock_guard<std::mutex> lock(*mutation_mutex_);
    std::filesystem::path absolute_path;
    if (!scope_.ResolveRelative(relative_path, absolute_path, error)) return false;
    return ReadBoundedText(absolute_path, text, error);
}

std::string GameDataService::ProjectRevision(
    const std::vector<std::filesystem::path>& relative_paths,
    std::string& error) const {
    std::lock_guard<std::mutex> lock(*mutation_mutex_);
    if (relative_paths.empty()) {
        error = "invalid_arguments";
        return {};
    }
    return CalculateTrackedRevision(scope_, relative_paths, error);
}

std::unique_ptr<Transaction> GameDataService::BeginProjectMutation(
    const MutationOptions& options,
    const std::vector<std::filesystem::path>& tracked_paths,
    std::string& error) {
    if (tracked_paths.empty()) {
        error = "invalid_arguments";
        return nullptr;
    }

    std::unique_lock<std::mutex> mutation_lock(*mutation_mutex_);
    const std::string baseline_revision =
        CalculateTrackedRevision(scope_, tracked_paths, error);
    if (!error.empty()) return nullptr;
    if (options.expected_revision && *options.expected_revision != baseline_revision) {
        error = "stale_revision";
        return nullptr;
    }

    auto committed_revision = std::make_shared<std::string>();
    auto transaction = std::make_unique<Transaction>(
        scope_, options, std::filesystem::path{}, mutation_mutex_, std::move(mutation_lock));
    transaction->SetCommitGuard([this, tracked_paths, baseline_revision](std::string& guard_error) {
        const std::string current_revision =
            CalculateTrackedRevision(scope_, tracked_paths, guard_error);
        if (!guard_error.empty()) return false;
        if (current_revision != baseline_revision) {
            guard_error = "stale_revision";
            return false;
        }
        return true;
    });
    transaction->SetCommitObserver([this, tracked_paths, committed_revision]() {
        std::string ignored;
        *committed_revision = CalculateTrackedRevision(scope_, tracked_paths, ignored);
    });
    transaction->SetRollbackGuard([this, tracked_paths, committed_revision](std::string& rollback_error) {
        if (committed_revision->empty()) {
            rollback_error = "rollback_unavailable";
            return false;
        }
        const std::string current_revision =
            CalculateTrackedRevision(scope_, tracked_paths, rollback_error);
        if (!rollback_error.empty()) return false;
        if (current_revision != *committed_revision) {
            rollback_error = "stale_revision";
            return false;
        }
        return true;
    });
    error.clear();
    return transaction;
}

bool GameDataService::SaveCurrentObjectSource(std::string_view source,
                                              const MutationOptions& options,
                                              std::string& error) {
    if (!HasOpenLevel()) {
        error = "level_not_open";
        return false;
    }
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
    ConfigureSchemasFromProgram(*parsed.program);
    const qvm::CompileResult compiled = qvm::Compile(*parsed.program);
    if (!compiled.ok || compiled.binary.empty()) {
        error = "qvm_compile_failed";
        return false;
    }

    const LevelRevision revision = CurrentRevision();
    if (revision.level <= 0) {
        error = "level_not_open";
        return false;
    }
    std::filesystem::path level_directory;
    if (!scope_.LevelDirectory(revision.level, level_directory, error)) return false;
    std::filesystem::path level_relative;
    if (!scope_.RelativeToRoot(level_directory, level_relative, error)) return false;

    auto transaction = BeginMutation(options, error);
    if (!transaction) return false;
    const std::vector<std::uint8_t> source_bytes(source.begin(), source.end());
    if (!transaction->Stage(level_relative / "objects.qsc", source_bytes, error) ||
        !transaction->Stage(level_relative / "objects.qvm", compiled.binary, error)) {
        return false;
    }
    transaction->SetValidator([source = std::string(source)](const auto&, const auto&, std::string& validation_error) {
        const qsc::LexResult check_lex = qsc::Lex(source);
        const qsc::ParseResult check_parse = check_lex.ok ? qsc::Parse(check_lex.tokens) : qsc::ParseResult{};
        if (!check_lex.ok || !check_parse.ok || !check_parse.program) {
            validation_error = "qsc_validation_failed";
            return false;
        }
        return true;
    });
    transaction->SetPostValidator([](const auto& relative_path, const auto& absolute_path,
                                     std::string& validation_error) {
        if (relative_path.filename() == "objects.qsc") {
            std::string text;
            if (!ReadBoundedText(absolute_path, text, validation_error)) return false;
            const qsc::LexResult lexed = qsc::Lex(text);
            const qsc::ParseResult parsed = lexed.ok ? qsc::Parse(lexed.tokens) : qsc::ParseResult{};
            if (!lexed.ok || !parsed.ok || !parsed.program) {
                validation_error = "qsc_post_validation_failed";
                return false;
            }
            return true;
        }
        if (relative_path.filename() == "objects.qvm") {
            const QVMFile qvm = QVM_Parse(absolute_path.string());
            if (!qvm.valid) {
                validation_error = "qvm_post_validation_failed";
                return false;
            }
        }
        return true;
    });
    return transaction->Commit(error);
}

std::unique_ptr<Transaction> GameDataService::BeginMutation(const MutationOptions& options,
                                                             std::string& error) {
    std::unique_lock<std::mutex> mutation_lock(*mutation_mutex_);
    if (!current_revision_) {
        error = "level_not_open";
        return nullptr;
    }

    const std::string cached_revision = current_revision_->fingerprint;
    if (!RefreshRevisionUnlocked(error)) return nullptr;
    if (current_revision_->fingerprint != cached_revision) {
        error = "stale_revision";
        return nullptr;
    }
    if (options.expected_revision && *options.expected_revision != current_revision_->fingerprint) {
        error = "stale_revision";
        return nullptr;
    }

    std::filesystem::path level_directory;
    if (!scope_.LevelDirectory(current_revision_->level, level_directory, error)) return nullptr;
    std::filesystem::path allowed_prefix;
    if (!scope_.RelativeToRoot(level_directory, allowed_prefix, error)) return nullptr;
    error.clear();
    const std::string baseline_revision = current_revision_->fingerprint;
    const auto committed_revision = std::make_shared<std::string>();
    auto transaction = std::make_unique<Transaction>(scope_, options, std::move(allowed_prefix),
                                                      mutation_mutex_, std::move(mutation_lock));
    transaction->SetCommitGuard([this, baseline_revision](std::string& guard_error) {
        if (!RefreshRevisionUnlocked(guard_error)) return false;
        if (!current_revision_ || current_revision_->fingerprint != baseline_revision) {
            guard_error = "stale_revision";
            return false;
        }
        return true;
    });
    transaction->SetCommitObserver([this, committed_revision]() {
        std::string ignored;
        if (RefreshRevisionUnlocked(ignored) && current_revision_)
            *committed_revision = current_revision_->fingerprint;
    });
    transaction->SetRollbackGuard([this, committed_revision](std::string& rollback_error) {
        if (committed_revision->empty()) {
            rollback_error = "rollback_unavailable";
            return false;
        }
        if (!RefreshRevisionUnlocked(rollback_error)) return false;
        if (!current_revision_ || current_revision_->fingerprint != *committed_revision) {
            rollback_error = "stale_revision";
            return false;
        }
        return true;
    });
    return transaction;
}

}  // namespace mcp
