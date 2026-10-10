#include <gtest/gtest.h>

#include "mcp/mcp_tools_runtime.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>

namespace {

namespace fs = std::filesystem;

class McpRuntimeToolsTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / "igi_mcp_runtime_tools_test";
        std::error_code error;
        fs::remove_all(root_, error);
        fs::create_directories(root_ / "missions/location0/level1", error);
        ASSERT_FALSE(error) << error.message();
        std::string open_error;
        scope_ = mcp::ProjectScope::Open(root_, open_error);
        ASSERT_TRUE(scope_.has_value()) << open_error;
        service_ = std::make_unique<mcp::GameDataService>(*scope_);
    }

    void TearDown() override {
        service_.reset();
        std::error_code error;
        fs::remove_all(root_, error);
    }

    fs::path root_;
    std::optional<mcp::ProjectScope> scope_;
    std::unique_ptr<mcp::GameDataService> service_;
};

TEST_F(McpRuntimeToolsTest, PublishesLiveGameTools) {
    const auto definitions = mcp::RuntimeToolDefinitions();
    std::set<std::string> names;
    for (const auto& definition : definitions) names.insert(definition.name);

    EXPECT_TRUE(names.contains("game_launch"));
    EXPECT_TRUE(names.contains("game_stop"));
    EXPECT_TRUE(names.contains("game_get_status"));
    EXPECT_TRUE(names.contains("game_capture_screenshot"));
}

TEST_F(McpRuntimeToolsTest, ValidatesLaunchAndScreenshotArgumentsBeforeSideEffects) {
    std::string error;
    const auto invalid_level = mcp::CallRuntimeTool(
        *service_, "game_launch", mcp::JsonValue::Object{{"level", 0}}, error);
    EXPECT_TRUE(invalid_level.is_null());
    EXPECT_EQ(error, "invalid_arguments");

    const auto outside_path = mcp::CallRuntimeTool(
        *service_, "game_capture_screenshot",
        mcp::JsonValue::Object{{"path", "../outside.png"}}, error);
    EXPECT_TRUE(outside_path.is_null());
    EXPECT_EQ(error, "path_forbidden");
}

TEST_F(McpRuntimeToolsTest, ReportsNoManagedGameWithoutLaunchingOne) {
    std::string error;
    const auto status = mcp::CallRuntimeTool(
        *service_, "game_get_status", mcp::JsonValue::Object{}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_FALSE(status.at("running").as_bool());

    const auto stopped = mcp::CallRuntimeTool(
        *service_, "game_stop", mcp::JsonValue::Object{}, error);
    EXPECT_TRUE(stopped.is_null());
    EXPECT_EQ(error, "game_not_running");
}

}  // namespace
