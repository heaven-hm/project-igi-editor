#include "mcp_server.h"

#include "mcp_json_rpc.h"
#include "mcp_tools_ai.h"
#include "mcp_tools_audio.h"
#include "mcp_tools_cutscene.h"
#include "mcp_tools_strings.h"
#include "mcp_tools_assets.h"
#include "mcp_tools_graph.h"
#include "mcp_tools_mission.h"
#include "mcp_tools_objects.h"
#include "mcp_tools_player.h"
#include "mcp_tools_runtime.h"
#include "mcp_tools_session.h"

#include <algorithm>
#include <charconv>
#include <map>

namespace mcp {
namespace {

JsonValue SafeToolError(std::string_view code) {
    static const std::map<std::string_view, std::string_view> summaries{
        {"invalid_arguments", "Tool arguments are invalid."},
        {"unknown_tool", "The requested tool is not available."},
        {"level_not_open", "Open a level before using this operation."},
        {"invalid_level", "The requested level is unavailable."},
        {"unknown_task_id", "The requested task id was not found."},
        {"stale_revision", "The level changed; reload and retry with the new revision."},
        {"validation_failed", "The proposed game-data change failed validation."},
        {"unsupported_operation", "This game-data operation is not supported safely."},
        {"qsc_parse_failed", "The QSC source failed validation."},
        {"qvm_invalid", "The QVM file failed validation."},
        {"qvm_compile_failed", "The QSC source could not be compiled."},
        {"write_failed", "The game-data write failed."},
        {"backup_failed", "The game-data backup failed."},
        {"path_forbidden", "The requested path is outside the configured game data."},
        {"unknown_asset", "The requested asset was not found in the level manifest."},
        {"unknown_graph", "The requested graph was not found in the level manifest."},
        {"game_already_running", "The MCP-managed game is already running."},
        {"game_executable_missing", "igi.exe was not found in the configured game root."},
        {"game_launch_failed", "The game could not be launched through WMI."},
        {"game_not_running", "The MCP-managed game is not running."},
        {"game_not_managed", "IGI is already running, but this MCP session did not launch it."},
        {"game_stop_failed", "The MCP-managed game could not be stopped."},
        {"game_window_unavailable", "The game window is not available for capture."},
        {"screenshot_failed", "The game screenshot could not be written."},
        {"audio_target_missing", "The requested audio destination is not present."},
        {"audio_read_failed", "The requested audio asset could not be read."},
        {"audio_invalid", "The staged audio asset is invalid."},
        {"unknown_string_key", "The requested localized string key was not found."},
        {"unsupported_format", "The requested table format is not supported safely."},
        {"table_parse_failed", "The localized table could not be parsed."},
        {"batch_partial_failure", "A batch operation stopped after a partial write; inspect the returned operation state."},
    };
    const auto it = summaries.find(code);
    return JsonValue::Object{
        {"code", JsonValue(std::string(code))},
        {"summary", JsonValue(it == summaries.end() ? "The operation failed safely." : it->second)},
    };
}

JsonValue DomainResult(JsonValue value, const std::string& error) {
    if (error.empty()) {
        const std::string text = JsonStringify(value);
        return JsonValue::Object{
            {"content", JsonValue::Array{JsonValue::Object{{"type", "text"}, {"text", text}}}},
            {"isError", false},
            {"structuredContent", std::move(value)},
        };
    }
    const JsonValue safe_error = SafeToolError(error);
    return JsonValue::Object{
        {"content", JsonValue::Array{JsonValue::Object{{"type", "text"},
                                                        {"text", JsonStringify(safe_error)}}}},
        {"isError", true},
        {"structuredContent", JsonValue::Object{{"error", std::move(safe_error)}}},
    };
}

const JsonValue::Object& ParamsObject(const JsonRpcRequest& request) {
    static const JsonValue::Object empty;
    if (!request.has_params) return empty;
    if (!request.params.is_object()) throw JsonRpcException(kInvalidParams, "params must be an object");
    return request.params.as_object();
}

bool CandidateRequestId(const JsonValue& value, JsonValue& id) {
    if (!value.is_object()) return false;
    const auto it = value.as_object().find("id");
    if (it == value.as_object().end() ||
        (!it->second.is_null() && !it->second.is_string() && !it->second.is_number()))
        return false;
    id = it->second;
    return true;
}

bool IsStructurallyInvalidRequest(const JsonValue& value) {
    if (!value.is_object()) return true;
    const auto& object = value.as_object();
    const auto version = object.find("jsonrpc");
    if (version == object.end() || !version->second.is_string() ||
        version->second.as_string() != "2.0") return true;
    const auto method = object.find("method");
    if (method == object.end() || !method->second.is_string() ||
        method->second.as_string().empty()) return true;
    const auto id = object.find("id");
    return id != object.end() && !id->second.is_null() &&
           !id->second.is_string() && !id->second.is_number();
}

}  // namespace

std::vector<McpServer::RegisteredTool> McpServer::RegisteredTools() const {
    std::vector<RegisteredTool> tools;
    for (const auto& definition : SessionToolDefinitions()) {
        tools.push_back({definition.name, definition.input_schema, "session"});
    }
    for (const auto& definition : ObjectToolDefinitions()) {
        tools.push_back({definition.name, definition.input_schema, "objects"});
    }
    for (const auto& definition : AiToolDefinitions()) {
        tools.push_back({definition.name, definition.input_schema, "ai"});
    }
    for (const auto& definition : PlayerToolDefinitions()) {
        tools.push_back({definition.name, definition.input_schema, "player"});
    }
    for (const auto& definition : RuntimeToolDefinitions()) {
        tools.push_back({definition.name, definition.input_schema, "runtime"});
    }
    for (const auto& definition : AudioToolDefinitions()) {
        tools.push_back({definition.name, definition.input_schema, "audio"});
    }
    for (const auto& definition : CutsceneToolDefinitions()) {
        tools.push_back({definition.name, definition.input_schema, "cutscene"});
    }
    for (const auto& definition : StringToolDefinitions()) {
        tools.push_back({definition.name, definition.input_schema, "strings"});
    }
    for (const auto& definition : MissionToolDefinitions()) {
        tools.push_back({definition.name, definition.input_schema, "mission"});
    }
    for (const auto& definition : GraphToolDefinitions()) {
        tools.push_back({definition.name, definition.input_schema, "graph"});
    }
    for (const auto& definition : AssetToolDefinitions()) {
        tools.push_back({definition.name, definition.input_schema, "assets"});
    }
    std::sort(tools.begin(), tools.end(), [](const auto& left, const auto& right) {
        return left.name < right.name;
    });
    return tools;
}

JsonValue McpServer::ToolList() const {
    JsonValue::Array tools;
    for (const auto& tool : RegisteredTools()) {
        tools.emplace_back(JsonValue::Object{
            {"name", tool.name},
            {"description", "Game-data operation for Project IGI."},
            {"inputSchema", tool.input_schema},
        });
    }
    return JsonValue::Object{
        {"cacheScope", "public"},
        {"ttlMs", 0},
        {"tools", std::move(tools)},
    };
}

JsonValue McpServer::ResourceList() const {
    return JsonValue::Object{
        {"cacheScope", "public"},
        {"ttlMs", 0},
        {"resources", JsonValue::Array{
            JsonValue::Object{{"uri", "igi://project"}, {"name", "Project manifest"}},
            JsonValue::Object{{"uri", "igi://levels"}, {"name", "Level manifest"}},
        }},
        {"resourceTemplates", JsonValue::Array{
            JsonValue::Object{{"uriTemplate", "igi://level/{level}/manifest"}, {"name", "Level files"}},
            JsonValue::Object{{"uriTemplate", "igi://level/{level}/objects"}, {"name", "Level objects"}},
            JsonValue::Object{{"uriTemplate", "igi://level/{level}/graphs"}, {"name", "Navigation graphs"}},
            JsonValue::Object{{"uriTemplate", "igi://level/{level}/validation"}, {"name", "Validation report"}},
        }},
    };
}

JsonValue McpServer::CallTool(std::string_view name, const JsonValue& arguments, std::string& error) {
    for (const auto& tool : RegisteredTools()) {
        if (tool.name == name) {
            if (tool.domain == "session") return CallSessionTool(service_, name, arguments, error);
            if (tool.domain == "objects") return CallObjectTool(service_, name, arguments, error);
            if (tool.domain == "ai") return CallAiTool(service_, name, arguments, error);
            if (tool.domain == "player") return CallPlayerTool(service_, name, arguments, error);
            if (tool.domain == "runtime") return CallRuntimeTool(service_, name, arguments, error);
            if (tool.domain == "audio") return CallAudioTool(service_, name, arguments, error);
            if (tool.domain == "cutscene") return CallCutsceneTool(service_, name, arguments, error);
            if (tool.domain == "strings") return CallStringTool(service_, name, arguments, error);
            if (tool.domain == "mission") return CallMissionTool(service_, name, arguments, error);
            if (tool.domain == "graph") return CallGraphTool(service_, name, arguments, error);
            if (tool.domain == "assets") return CallAssetTool(service_, name, arguments, error);
        }
    }
    error = "unknown_tool";
    return JsonValue(nullptr);
}

JsonValue McpServer::ReadResource(std::string_view uri, std::string& error) const {
    if (uri == "igi://project") return service_.ProjectInfo();
    if (uri == "igi://levels") return service_.ListLevels(error);
    constexpr std::string_view prefix = "igi://level/";
    if (!uri.starts_with(prefix)) {
        error = "unknown_resource";
        return JsonValue(nullptr);
    }
    const std::size_t id_start = prefix.size();
    const std::size_t slash = uri.find('/', id_start);
    if (slash == std::string_view::npos) {
        error = "unknown_resource";
        return JsonValue(nullptr);
    }
    int level = 0;
    const auto parsed = std::from_chars(uri.data() + id_start, uri.data() + slash, level);
    if (parsed.ec != std::errc{} || parsed.ptr != uri.data() + slash || level < 1) {
        error = "invalid_level";
        return JsonValue(nullptr);
    }
    const std::string_view suffix = uri.substr(slash);
    if (suffix == "/manifest") return service_.LevelManifest(level, error);
    if (suffix == "/objects") return service_.ListObjects(level, error);
    if (suffix == "/graphs") return CallGraphTool(service_, "graph_list",
                                                   JsonValue::Object{{"level", level}}, error);
    if (suffix == "/validation") return service_.ValidateLevel(level, error);
    error = "unknown_resource";
    return JsonValue(nullptr);
}

JsonValue McpServer::Handle(const JsonValue& value) {
    JsonRpcRequest request;
    try {
        request = ParseJsonRpcRequest(value);
        RequireMcp20260728Metadata(request);

        if (request.method == "server/discover") {
            return request.has_id ? MakeJsonRpcResult(request.id, JsonValue::Object{
                {"protocolVersion", "2026-07-28"},
                {"serverInfo", JsonValue::Object{{"name", "project-igi-editor"}, {"version", "mcp-1"}}},
                {"capabilities", JsonValue::Object{{"tools", true}, {"resources", true}}},
                {"cacheScope", "public"},
                {"ttlMs", 0},
            }) : JsonValue(nullptr);
        }
        if (request.method == "tools/list") {
            return request.has_id ? MakeJsonRpcResult(request.id, ToolList()) : JsonValue(nullptr);
        }
        if (request.method == "resources/list") {
            return request.has_id ? MakeJsonRpcResult(request.id, ResourceList()) : JsonValue(nullptr);
        }
        if (request.method == "resources/read") {
            const auto& params = ParamsObject(request);
            const auto it = params.find("uri");
            if (it == params.end() || !it->second.is_string())
                throw JsonRpcException(kInvalidParams, "resources/read requires uri");
            std::string error;
            const JsonValue result = ReadResource(it->second.as_string(), error);
            if (!error.empty()) {
                if (!request.has_id) return JsonValue(nullptr);
                return MakeJsonRpcError(request.id, kInvalidParams, "resource unavailable",
                                        SafeToolError(error));
            }
            return request.has_id ? MakeJsonRpcResult(request.id, JsonValue::Object{
                {"cacheScope", "private"},
                {"ttlMs", 0},
                {"contents", JsonValue::Array{JsonValue::Object{
                    {"uri", it->second}, {"mimeType", "application/json"}, {"text", JsonStringify(result)},
                }}}
            }) : JsonValue(nullptr);
        }
        if (request.method == "tools/call") {
            const auto& params = ParamsObject(request);
            const auto name_it = params.find("name");
            if (name_it == params.end() || !name_it->second.is_string())
                throw JsonRpcException(kInvalidParams, "tools/call requires name");
            const auto args_it = params.find("arguments");
            const JsonValue arguments = args_it == params.end() ? JsonValue::Object{} : args_it->second;
            if (!arguments.is_object()) throw JsonRpcException(kInvalidParams, "tool arguments must be an object");
            std::string error;
            const JsonValue result = CallTool(name_it->second.as_string(), arguments, error);
            if (error == "unknown_tool") throw JsonRpcException(kMethodNotFound, "tool not found");
            if (error == "invalid_arguments") {
                throw JsonRpcException(kInvalidParams, "tool arguments are invalid", SafeToolError(error));
            }
            return request.has_id ? MakeJsonRpcResult(request.id, DomainResult(result, error)) : JsonValue(nullptr);
        }
        throw JsonRpcException(kMethodNotFound, "method not found");
    } catch (const JsonRpcException& exception) {
        JsonValue id;
        const bool has_id = request.has_id || CandidateRequestId(value, id);
        if (request.has_id) id = request.id;
        if (!has_id && IsStructurallyInvalidRequest(value))
            return MakeJsonRpcError(JsonValue(nullptr), exception.code(), exception.what(), exception.data());
        return has_id ? MakeJsonRpcError(id, exception.code(), exception.what(), exception.data())
                      : JsonValue(nullptr);
    } catch (const std::exception&) {
        JsonValue id;
        const bool has_id = request.has_id || CandidateRequestId(value, id);
        if (request.has_id) id = request.id;
        if (!has_id && IsStructurallyInvalidRequest(value))
            return MakeJsonRpcError(JsonValue(nullptr), kInternalError, "internal error");
        return has_id ? MakeJsonRpcError(id, kInternalError, "internal error") : JsonValue(nullptr);
    }
}

}  // namespace mcp
