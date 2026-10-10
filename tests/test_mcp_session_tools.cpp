#include <gtest/gtest.h>

#include "mcp/mcp_tools_session.h"

#include <filesystem>
#include <fstream>

namespace {

namespace fs = std::filesystem;

class McpSessionToolsTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / "igi_mcp_session_tools_test";
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

    fs::path root_;
    std::optional<mcp::ProjectScope> scope_;
};

TEST_F(McpSessionToolsTest, ReturnsSortedGameDataOnlyDefinitions) {
    const auto definitions = mcp::SessionToolDefinitions();
    ASSERT_EQ(definitions.size(), 5u);
    EXPECT_EQ(definitions[0].name, "level_open");
    EXPECT_EQ(definitions[1].name, "level_reload");
    EXPECT_EQ(definitions[2].name, "level_validate");
    EXPECT_EQ(definitions[3].name, "project_info");
    EXPECT_EQ(definitions[4].name, "project_list_levels");
    for (const auto& definition : definitions) {
        EXPECT_EQ(definition.name.find("font"), std::string::npos);
        EXPECT_EQ(definition.name.find("panel"), std::string::npos);
    }
}

TEST_F(McpSessionToolsTest, OpensReloadsAndValidatesTheCurrentLevel) {
    mcp::GameDataService service(*scope_);
    std::string error;
    const auto open = mcp::CallSessionTool(
        service, "level_open", mcp::JsonValue::Object{{"level", 1}}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_TRUE(open.at("opened").as_bool());
    EXPECT_EQ(open.at("relative_path").as_string(), "missions/location0/level1");
    EXPECT_EQ(open.at("revision").as_string(), open.at("manifest").at("revision").as_string());

    const auto reload = mcp::CallSessionTool(
        service, "level_reload", mcp::JsonValue::Object{}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_TRUE(reload.at("reloaded").as_bool());
    EXPECT_FALSE(reload.at("changed_externally").as_bool());

    const auto validation = mcp::CallSessionTool(
        service, "level_validate", mcp::JsonValue::Object{{"level", 1}}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_FALSE(validation.at("valid").as_bool());
}

TEST_F(McpSessionToolsTest, RejectsWrongArgumentsAndReportsLevelNotOpen) {
    mcp::GameDataService service(*scope_);
    std::string error;
    const auto invalid = mcp::CallSessionTool(
        service, "level_open", mcp::JsonValue::Object{{"level", 1}, {"extra", true}}, error);
    EXPECT_TRUE(invalid.is_null());
    EXPECT_EQ(error, "invalid_arguments");

    const auto reload = mcp::CallSessionTool(
        service, "level_reload", mcp::JsonValue::Object{}, error);
    EXPECT_TRUE(reload.is_null());
    EXPECT_EQ(error, "level_not_open");

    const auto unknown = mcp::CallSessionTool(
        service, "editor_font_size", mcp::JsonValue::Object{}, error);
    EXPECT_TRUE(unknown.is_null());
    EXPECT_EQ(error, "unknown_tool");
}

}  // namespace
