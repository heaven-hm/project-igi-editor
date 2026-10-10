#include "mcp/mcp_json_rpc.h"

#include <gtest/gtest.h>

namespace {

using mcp::JsonValue;

JsonValue MakeRequest(bool include_id = true) {
    JsonValue::Object params;
    params.emplace("_meta", JsonValue::Object{
                                  {"io.modelcontextprotocol/protocolVersion", JsonValue("2026-07-28")},
                                  {"io.modelcontextprotocol/clientInfo", JsonValue::Object{{"name", JsonValue("test")}}},
                              });

    JsonValue::Object request;
    request.emplace("jsonrpc", JsonValue("2.0"));
    request.emplace("method", JsonValue("tools/list"));
    request.emplace("params", JsonValue(std::move(params)));
    if (include_id) {
        request.emplace("id", JsonValue(7));
    }
    return JsonValue(std::move(request));
}

TEST(McpJsonRpcTest, ParsesStatelessRequestMetadata) {
    const mcp::JsonRpcRequest request = mcp::ParseJsonRpcRequest(MakeRequest());

    EXPECT_EQ(request.jsonrpc, "2.0");
    ASSERT_TRUE(request.has_id);
    EXPECT_EQ(request.id.as_number(), 7.0);
    EXPECT_EQ(request.method, "tools/list");
    ASSERT_TRUE(request.has_params);
    ASSERT_TRUE(request.has_metadata);
    EXPECT_EQ(request.metadata.at("io.modelcontextprotocol/protocolVersion").as_string(), "2026-07-28");
    EXPECT_TRUE(mcp::HasMcp20260728Metadata(request));
    EXPECT_NO_THROW(mcp::RequireMcp20260728Metadata(request));
}

TEST(McpJsonRpcTest, AcceptsNotificationsWithoutSessionLifecycle) {
    const mcp::JsonRpcRequest request = mcp::ParseJsonRpcRequest(MakeRequest(false));
    EXPECT_FALSE(request.has_id);
    EXPECT_EQ(request.method, "tools/list");
}

TEST(McpJsonRpcTest, AcceptsArrayParamsAsGenericJsonRpc) {
    JsonValue::Object request{{"jsonrpc", JsonValue("2.0")},
                              {"method", JsonValue("example")},
                              {"params", JsonValue::Array{JsonValue(1), JsonValue("x")}}};
    EXPECT_TRUE(mcp::ParseJsonRpcRequest(JsonValue(std::move(request))).params.is_array());
}

TEST(McpJsonRpcTest, ReportsUnsupportedProtocolVersionWithNegotiationData) {
    auto request = MakeRequest();
    request["params"]["_meta"][std::string(mcp::kMcpProtocolVersionMetadataKey)] = "2025-11-25";
    const auto parsed = mcp::ParseJsonRpcRequest(request);
    try {
        mcp::RequireMcp20260728Metadata(parsed);
        FAIL() << "expected unsupported protocol version";
    } catch (const mcp::JsonRpcException& exception) {
        EXPECT_EQ(exception.code(), mcp::kUnsupportedProtocolVersion);
        EXPECT_EQ(exception.data().at("supported").as_array().size(), 1u);
        EXPECT_EQ(exception.data().at("supported").as_array()[0].as_string(), "2026-07-28");
        EXPECT_EQ(exception.data().at("requested").as_string(), "2025-11-25");
    }
}

TEST(McpJsonRpcTest, RejectsInvalidRequestsAndParams) {
    JsonValue::Object invalid_version{{"jsonrpc", JsonValue("1.0")},
                                      {"method", JsonValue("tools/list")}};
    EXPECT_THROW(mcp::ParseJsonRpcRequest(JsonValue(std::move(invalid_version))),
                 mcp::JsonRpcException);

    JsonValue::Object invalid_id{{"jsonrpc", JsonValue("2.0")},
                                 {"id", JsonValue(true)},
                                 {"method", JsonValue("tools/list")}};
    EXPECT_THROW(mcp::ParseJsonRpcRequest(JsonValue(std::move(invalid_id))),
                 mcp::JsonRpcException);

    JsonValue::Object invalid_params{{"jsonrpc", JsonValue("2.0")},
                                     {"method", JsonValue("tools/call")},
                                     {"params", JsonValue(3)}};
    try {
        (void)mcp::ParseJsonRpcRequest(JsonValue(std::move(invalid_params)));
        FAIL() << "expected invalid params";
    } catch (const mcp::JsonRpcException& exception) {
        EXPECT_EQ(exception.code(), mcp::kInvalidParams);
    }
}

TEST(McpJsonRpcTest, BuildsDeterministicResultAndErrorResponses) {
    const JsonValue id(11);
    const JsonValue result = mcp::MakeJsonRpcResult(
        id, JsonValue::Object{{"count", JsonValue(2)}, {"ok", JsonValue(true)}});
    EXPECT_EQ(mcp::JsonStringify(result),
              R"({"id":11,"jsonrpc":"2.0","result":{"count":2,"ok":true}})");

    const JsonValue error = mcp::MakeJsonRpcError(
        id, mcp::kInvalidParams, "Invalid params", JsonValue::Object{{"field", JsonValue("id")}});
    EXPECT_EQ(mcp::JsonStringify(error),
              R"({"error":{"code":-32602,"data":{"field":"id"},"message":"Invalid params"},"id":11,"jsonrpc":"2.0"})");
}

}  // namespace
