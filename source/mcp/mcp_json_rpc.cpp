#include "mcp_json_rpc.h"

#include <utility>

namespace mcp {
namespace {

[[noreturn]] void Invalid(const char* message) {
    throw JsonRpcException(kInvalidRequest, message);
}

const JsonValue& Required(const JsonValue::Object& object, std::string_view key) {
    const auto it = object.find(key);
    if (it == object.end()) Invalid("JSON-RPC request is missing a required field");
    return it->second;
}

}  // namespace

JsonRpcException::JsonRpcException(int code, std::string message, JsonValue data)
    : std::runtime_error(std::move(message)), code_(code), data_(std::move(data)) {}

JsonRpcRequest ParseJsonRpcRequest(const JsonValue& value) {
    if (!value.is_object()) Invalid("JSON-RPC request must be an object");
    const auto& object = value.as_object();

    JsonRpcRequest request;

    if (const auto it = object.find("id"); it != object.end()) {
        if (!it->second.is_null() && !it->second.is_string() && !it->second.is_number())
            Invalid("JSON-RPC id must be a string, number, or null");
        request.id = it->second;
        request.has_id = true;
    }

    const JsonValue& version = Required(object, "jsonrpc");
    if (!version.is_string() || version.as_string() != "2.0") Invalid("jsonrpc must be \"2.0\"");
    request.jsonrpc = version.as_string();

    const JsonValue& method = Required(object, "method");
    if (!method.is_string() || method.as_string().empty()) Invalid("method must be a non-empty string");
    request.method = method.as_string();

    if (const auto it = object.find("params"); it != object.end()) {
        if (!it->second.is_object() && !it->second.is_array())
            throw JsonRpcException(kInvalidParams, "params must be an object or array");
        request.params = it->second;
        request.has_params = true;
        if (it->second.is_object()) {
            const auto metadata = it->second.as_object().find("_meta");
            if (metadata == it->second.as_object().end()) return request;
            if (!metadata->second.is_object())
                throw JsonRpcException(kInvalidParams, "params._meta must be an object");
            request.metadata = metadata->second.as_object();
            request.has_metadata = true;
        }
    }
    return request;
}

bool HasMcp20260728Metadata(const JsonRpcRequest& request) noexcept {
    if (!request.has_metadata) return false;
    const auto it = request.metadata.find(kMcpProtocolVersionMetadataKey);
    return it != request.metadata.end() && it->second.is_string() &&
           it->second.as_string() == "2026-07-28";
}

void RequireMcp20260728Metadata(const JsonRpcRequest& request) {
    if (!request.has_metadata) {
        throw JsonRpcException(kInvalidParams,
            "MCP 2026-07-28 protocol metadata is required");
    }
    const auto it = request.metadata.find(kMcpProtocolVersionMetadataKey);
    if (it == request.metadata.end() || !it->second.is_string()) {
        throw JsonRpcException(kInvalidParams,
            "MCP 2026-07-28 protocol metadata is required");
    }
    if (it->second.as_string() != "2026-07-28") {
        throw JsonRpcException(
            kUnsupportedProtocolVersion, "MCP protocol version is not supported",
            JsonValue::Object{
                {"supported", JsonValue::Array{JsonValue("2026-07-28")}},
                {"requested", it->second},
            });
    }
}

JsonValue MakeJsonRpcResult(const JsonValue& id, JsonValue result) {
    return JsonValue::Object{
        {"jsonrpc", JsonValue("2.0")},
        {"id", id},
        {"result", std::move(result)},
    };
}

JsonValue MakeJsonRpcError(const JsonValue& id, int code, std::string message, JsonValue data) {
    JsonValue::Object error{
        {"code", JsonValue(code)},
        {"message", JsonValue(std::move(message))},
    };
    if (!data.is_null()) error.emplace("data", std::move(data));
    return JsonValue::Object{
        {"jsonrpc", JsonValue("2.0")},
        {"id", id},
        {"error", JsonValue(std::move(error))},
    };
}

}  // namespace mcp
