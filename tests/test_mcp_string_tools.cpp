#include <gtest/gtest.h>

#include "mcp/mcp_tools_strings.h"
#include "../source/renderer/res_writer.h"
#include "../source/renderer/res_compiler.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>

namespace {

namespace fs = std::filesystem;

class McpStringToolsTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / "igi_mcp_string_tools_test";
        std::error_code error;
        fs::remove_all(root_, error);
        fs::create_directories(root_, error);
        ASSERT_FALSE(error) << error.message();
        std::ofstream(root_ / "level5.str")
            << "# mission strings\nMISSION_TITLE=Insertion\nBRIEFING_1=Reach the base.\n";
        std::string archive_error;
        ASSERT_TRUE(RES_WriteEntries(
            {RESEntry{"LOCAL:english/M16", std::vector<std::uint8_t>{'o', 'l', 'd', 0}}},
            (root_ / "location0.res").string(), archive_error)) << archive_error;
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

TEST_F(McpStringToolsTest, PublishesStringAndBriefingTools) {
    const auto definitions = mcp::StringToolDefinitions();
    std::set<std::string> names;
    for (const auto& definition : definitions) names.insert(definition.name);

    EXPECT_TRUE(names.contains("string_table_get"));
    EXPECT_TRUE(names.contains("string_table_set"));
    EXPECT_TRUE(names.contains("briefing_get_text"));
    EXPECT_TRUE(names.contains("briefing_set_text"));
}

TEST_F(McpStringToolsTest, ReadsAndUpdatesLineStringTableWithDryRun) {
    std::string error;
    const auto current = mcp::CallStringTool(
        *service_, "briefing_get_text",
        mcp::JsonValue::Object{{"path", "level5.str"}, {"key", "BRIEFING_1"}}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_EQ(current.at("text").as_string(), "Reach the base.");

    const auto dry_run = mcp::CallStringTool(
        *service_, "briefing_set_text",
        mcp::JsonValue::Object{{"path", "level5.str"}, {"key", "BRIEFING_1"},
                               {"text", "Reach the tower."}, {"dry_run", true}}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_TRUE(dry_run.at("dry_run").as_bool());
    EXPECT_EQ(mcp::CallStringTool(*service_, "briefing_get_text",
                                  mcp::JsonValue::Object{{"path", "level5.str"}, {"key", "BRIEFING_1"}}, error)
                  .at("text").as_string(), "Reach the base.");

    const auto changed = mcp::CallStringTool(
        *service_, "string_table_set",
        mcp::JsonValue::Object{{"path", "level5.str"}, {"key", "WEAPON_NAME"},
                               {"text", "M16A2"}}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_FALSE(changed.at("dry_run").as_bool());
    EXPECT_EQ(mcp::CallStringTool(*service_, "string_table_get",
                                  mcp::JsonValue::Object{{"path", "level5.str"}, {"key", "WEAPON_NAME"}}, error)
                  .at("text").as_string(), "M16A2");
}

TEST_F(McpStringToolsTest, RejectsUnknownTableFormat) {
    std::ofstream(root_ / "table.bin") << "raw";
    std::string error;
    const auto result = mcp::CallStringTool(
        *service_, "string_table_get",
        mcp::JsonValue::Object{{"path", "table.bin"}, {"key", "x"}}, error);
    EXPECT_TRUE(result.is_null());
    EXPECT_EQ(error, "unsupported_format");
}

TEST_F(McpStringToolsTest, ReadsAndRewritesIresLocalizedEntries) {
    std::string error;
    const auto current = mcp::CallStringTool(
        *service_, "string_table_get",
        mcp::JsonValue::Object{{"path", "location0.res"}, {"key", "M16"}}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_EQ(current.at("text").as_string(), "old");

    const auto changed = mcp::CallStringTool(
        *service_, "string_table_set",
        mcp::JsonValue::Object{{"path", "location0.res"}, {"key", "M16"},
                               {"text", "M16A2"}}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_EQ(changed.at("text").as_string(), "M16A2");
    const auto reread = mcp::CallStringTool(
        *service_, "string_table_get",
        mcp::JsonValue::Object{{"path", "location0.res"}, {"key", "M16"}}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_EQ(reread.at("text").as_string(), "M16A2");
}

TEST_F(McpStringToolsTest, RejectsUnsupportedMissionLayouts) {
    const fs::path forbidden = root_ / "missions/location2/strings.str";
    fs::create_directories(forbidden.parent_path());
    std::ofstream(forbidden) << "KEY=foreign\n";

    std::string error;
    const auto result = mcp::CallStringTool(
        *service_, "string_table_get",
        mcp::JsonValue::Object{{"path", "missions/location2/strings.str"}, {"key", "KEY"}}, error);
    EXPECT_TRUE(result.is_null());
    EXPECT_EQ(error, "path_forbidden");
}

}  // namespace
