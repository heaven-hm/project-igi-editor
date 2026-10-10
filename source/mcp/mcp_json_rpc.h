#pragma once

#include "mcp_json.h"

#include <stdexcept>
#include <string>

namespace mcp {

inline constexpr int kParseError = -32700;
inline constexpr int kInvalidRequest = -32600;
inline constexpr int kMethodNotFound = -32601;
inline constexpr int kInvalidParams = -32602;
inline constexpr int kInternalError = -32603;
inline constexpr int kUnsupportedProtocolVersion = -32022;
inline constexpr std::string_view kMcpProtocolVersionMetadataKey =
    "io.modelcontextprotocol/protocolVersion";
inline constexpr std::string_view kMcpClientInfoMetadataKey =
    "io.modelcontextprotocol/clientInfo";
inline constexpr std::string_view kMcpClientCapabilitiesMetadataKey =
    "io.modelcontextprotocol/clientCapabilities";

struct JsonRpcRequest {
    std::string jsonrpc;
    JsonValue id;
    bool has_id = false;
    std::string method;
    JsonValue params;
    bool has_params = false;
    JsonValue::Object metadata;
    bool has_metadata = false;
};

class JsonRpcException : public std::runtime_error {
public:
    JsonRpcException(int code, std::string message, JsonValue data = JsonValue(nullptr));

    int code() const noexcept { return code_; }
    const JsonValue& data() const noexcept { return data_; }

private:
    int code_;
    JsonValue data_;
};

JsonRpcRequest ParseJsonRpcRequest(const JsonValue& value);
bool HasMcp20260728Metadata(const JsonRpcRequest& request) noexcept;
void RequireMcp20260728Metadata(const JsonRpcRequest& request);
JsonValue MakeJsonRpcResult(const JsonValue& id, JsonValue result);
JsonValue MakeJsonRpcError(const JsonValue& id, int code, std::string message,
                           JsonValue data = JsonValue(nullptr));

}  // namespace mcp
