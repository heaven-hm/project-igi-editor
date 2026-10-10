#include <gtest/gtest.h>

#include "mcp/mcp_server.h"
#include "mcp/mcp_json_rpc.h"

#include <filesystem>
#include <fstream>
#include <set>

namespace {

namespace fs = std::filesystem;

class McpServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / "igi_mcp_server_test";
        std::error_code error;
        fs::remove_all(root_, error);
        const fs::path level = root_ / "missions/location0/level1";
        fs::create_directories(level, error);
        ASSERT_FALSE(error) << error.message();
        std::ofstream(level / "objects.qvm", std::ios::binary) << "fixture-qvm";
        std::ofstream(level / "objects.qsc", std::ios::binary)
            << "Task_New(1, \"Building\", \"Test\", 0, 0, 0, 0, 0, 0, \"300_01_1\");\n";
        std::string open_error;
        scope_ = mcp::ProjectScope::Open(root_, open_error);
        ASSERT_TRUE(scope_.has_value()) << open_error;
    }

    void TearDown() override {
        std::error_code error;
        fs::remove_all(root_, error);
    }

    mcp::JsonValue Request(std::string method, mcp::JsonValue params = mcp::JsonValue::Object{}) {
        return mcp::JsonValue::Object{
            {"jsonrpc", "2.0"}, {"id", 1}, {"method", std::move(method)},
            {"params", std::move(params)},
        };
    }

    void AddMetadata(mcp::JsonValue& request) {
        request["params"]["_meta"] = mcp::JsonValue::Object{
            {std::string(mcp::kMcpProtocolVersionMetadataKey), "2026-07-28"}};
    }

    fs::path root_;
    std::optional<mcp::ProjectScope> scope_;
};

TEST_F(McpServerTest, ListsToolsAndHandlesProjectInfoWithStatelessMetadata) {
    mcp::GameDataService service(*scope_);
    mcp::McpServer server(service);
    auto request = Request("tools/list");
    AddMetadata(request);
    const auto response = server.Handle(request);
    ASSERT_TRUE(response.at("result").at("tools").is_array());
    EXPECT_EQ(response.at("result").at("cacheScope").as_string(), "public");
    EXPECT_EQ(response.at("result").at("ttlMs").as_number(), 0);
    std::set<std::string> names;
    for (const auto& tool : response.at("result").at("tools").as_array()) {
        names.insert(tool.at("name").as_string());
    }
    for (const std::string required : {
             "project_info", "project_list_levels", "level_open", "level_reload", "level_validate",
             "task_list", "task_get", "object_set_transform", "ai_get", "mission_objective_list",
             "graph_list", "terrain_get_metadata", "asset_list", "player_get_physics",
             "game_launch", "audio_list_tracks", "string_table_get", "cutscene_list"}) {
        EXPECT_TRUE(names.contains(required)) << required;
    }
    EXPECT_GE(names.size(), 46u);

    bool found_loadout = false;
    for (const auto& tool : response.at("result").at("tools").as_array()) {
        if (tool.at("name").as_string() != "ai_set_weapon_loadout") continue;
        found_loadout = true;
        const auto& item_schema = tool.at("inputSchema").at("properties")
                                      .at("loadout").at("items");
        EXPECT_EQ(item_schema.at("type").as_string(), "object");
        EXPECT_FALSE(item_schema.at("additionalProperties").as_bool());
        ASSERT_TRUE(item_schema.at("required").is_array());
        EXPECT_EQ(item_schema.at("required").as_array().size(), 2u);
    }
    EXPECT_TRUE(found_loadout);

    auto info_request = Request("tools/call", mcp::JsonValue::Object{
        {"name", "project_info"}, {"arguments", mcp::JsonValue::Object{}}});
    AddMetadata(info_request);
    const auto info = server.Handle(info_request);
    EXPECT_FALSE(info.at("result").at("isError").as_bool());
    EXPECT_EQ(info.at("result").at("structuredContent").at("protocol_profile").as_string(),
              "2026-07-28");
}

TEST_F(McpServerTest, IncludesCacheMetadataOnDiscoveryAndResourceResults) {
    mcp::GameDataService service(*scope_);
    mcp::McpServer server(service);

    auto discover = Request("server/discover");
    AddMetadata(discover);
    const auto discover_response = server.Handle(discover);
    EXPECT_EQ(discover_response.at("result").at("cacheScope").as_string(), "public");
    EXPECT_EQ(discover_response.at("result").at("ttlMs").as_number(), 0);

    auto resources = Request("resources/list");
    AddMetadata(resources);
    const auto resources_response = server.Handle(resources);
    EXPECT_EQ(resources_response.at("result").at("cacheScope").as_string(), "public");
    EXPECT_EQ(resources_response.at("result").at("ttlMs").as_number(), 0);

    auto read = Request("resources/read", mcp::JsonValue::Object{{"uri", "igi://project"}});
    AddMetadata(read);
    const auto read_response = server.Handle(read);
    EXPECT_EQ(read_response.at("result").at("cacheScope").as_string(), "private");
    EXPECT_EQ(read_response.at("result").at("ttlMs").as_number(), 0);
}

TEST_F(McpServerTest, ReturnsUnsupportedProtocolVersionErrorData) {
    mcp::GameDataService service(*scope_);
    mcp::McpServer server(service);
    auto request = Request("tools/list");
    AddMetadata(request);
    request["params"]["_meta"][std::string(mcp::kMcpProtocolVersionMetadataKey)] = "2025-11-25";
    const auto response = server.Handle(request);
    EXPECT_EQ(response.at("error").at("code").as_number(), mcp::kUnsupportedProtocolVersion);
    EXPECT_EQ(response.at("error").at("data").at("requested").as_string(), "2025-11-25");
}

TEST_F(McpServerTest, RequiresProtocolMetadataAndReturnsSafeDomainErrors) {
    mcp::GameDataService service(*scope_);
    mcp::McpServer server(service);
    const auto missing = server.Handle(Request("tools/list"));
    EXPECT_EQ(missing.at("error").at("code").as_number(), mcp::kInvalidParams);

    auto call = Request("tools/call", mcp::JsonValue::Object{
        {"name", "level_reload"}, {"arguments", mcp::JsonValue::Object{}}});
    AddMetadata(call);
    const auto response = server.Handle(call);
    EXPECT_TRUE(response.at("result").at("isError").as_bool());
    EXPECT_EQ(response.at("result").at("structuredContent").at("error").at("code").as_string(),
              "level_not_open");
}

TEST_F(McpServerTest, PreservesCandidateIdForMalformedJsonRpcRequests) {
    mcp::GameDataService service(*scope_);
    mcp::McpServer server(service);
    const auto response = server.Handle(mcp::JsonValue::Object{
        {"jsonrpc", "1.0"}, {"id", 7}, {"method", "tools/list"},
    });
    EXPECT_EQ(response.at("error").at("code").as_number(), mcp::kInvalidRequest);
    EXPECT_EQ(response.at("id").as_number(), 7);
}

TEST_F(McpServerTest, ReturnsNullIdForStructurallyInvalidRequestWithoutId) {
    mcp::GameDataService service(*scope_);
    mcp::McpServer server(service);
    const auto response = server.Handle(mcp::JsonValue::Object{
        {"jsonrpc", "2.0"},
    });
    EXPECT_EQ(response.at("error").at("code").as_number(), mcp::kInvalidRequest);
    EXPECT_TRUE(response.at("id").is_null());
}

TEST_F(McpServerTest, ReturnsInvalidParamsForToolArgumentFailures) {
    mcp::GameDataService service(*scope_);
    mcp::McpServer server(service);
    auto request = Request("tools/call", mcp::JsonValue::Object{
        {"name", "project_info"},
        {"arguments", mcp::JsonValue::Object{{"unexpected", true}}},
    });
    AddMetadata(request);

    const auto response = server.Handle(request);
    ASSERT_TRUE(response.contains("error")) << "tool argument failures must be JSON-RPC errors";
    EXPECT_EQ(response.at("error").at("code").as_number(), mcp::kInvalidParams);
    EXPECT_EQ(response.at("id").as_number(), 1);
    EXPECT_EQ(response.at("error").at("data").at("code").as_string(), "invalid_arguments");
}

}  // namespace
