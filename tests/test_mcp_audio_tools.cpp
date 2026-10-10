#include <gtest/gtest.h>

#include "mcp/mcp_tools_audio.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>

namespace {

namespace fs = std::filesystem;

std::string Read(const fs::path& path);

class McpAudioToolsTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / "igi_mcp_audio_tools_test";
        std::error_code error;
        fs::remove_all(root_, error);
        fs::create_directories(root_ / "missions/location0/level1/sounds", error);
        ASSERT_FALSE(error) << error.message();
        Write(root_ / "source.wav", "RIFFsourceWAVE");
        Write(root_ / "missions/location0/level1/sounds/game_music.wav", "RIFFoldWAVE");
        Write(root_ / "missions/location0/level1/sounds/ambience.wav", "RIFFambWAVE");
        std::string open_error;
        scope_ = mcp::ProjectScope::Open(root_, open_error);
        ASSERT_TRUE(scope_.has_value()) << open_error;
        service_ = std::make_unique<mcp::GameDataService>(*scope_);
    }

    static void Write(const fs::path& path, const std::string& text) {
        std::ofstream output(path, std::ios::binary);
        output << text;
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

TEST_F(McpAudioToolsTest, PublishesAudioAndMusicTools) {
    const auto definitions = mcp::AudioToolDefinitions();
    std::set<std::string> names;
    for (const auto& definition : definitions) names.insert(definition.name);

    EXPECT_TRUE(names.contains("audio_list_tracks"));
    EXPECT_TRUE(names.contains("audio_replace_sfx"));
    EXPECT_TRUE(names.contains("audio_set_level_track"));
}

TEST_F(McpAudioToolsTest, ListsLooseTracksAndReplacesMusicThroughTransaction) {
    std::string error;
    const auto listed = mcp::CallAudioTool(
        *service_, "audio_list_tracks", mcp::JsonValue::Object{}, error);
    ASSERT_TRUE(error.empty()) << error;
    ASSERT_TRUE(listed.at("tracks").is_array());
    EXPECT_EQ(listed.at("tracks").as_array().size(), 3u);

    const auto dry_run = mcp::CallAudioTool(
        *service_, "audio_set_level_track",
        mcp::JsonValue::Object{{"level", 1}, {"source_path", "source.wav"}, {"dry_run", true}}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_TRUE(dry_run.at("dry_run").as_bool());
    EXPECT_EQ(Read(root_ / "missions/location0/level1/sounds/game_music.wav"), "RIFFoldWAVE");

    const auto changed = mcp::CallAudioTool(
        *service_, "audio_set_level_track",
        mcp::JsonValue::Object{{"level", 1}, {"source_path", "source.wav"}}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_FALSE(changed.at("dry_run").as_bool());
    EXPECT_EQ(Read(root_ / "missions/location0/level1/sounds/game_music.wav"), "RIFFsourceWAVE");
}

TEST_F(McpAudioToolsTest, RejectsPathsOutsideConfiguredRoot) {
    std::string error;
    const auto result = mcp::CallAudioTool(
        *service_, "audio_replace_sfx",
        mcp::JsonValue::Object{{"source_path", "../source.wav"},
                               {"destination_path", "missions/location0/level1/sounds/ambience.wav"}}, error);
    EXPECT_TRUE(result.is_null());
    EXPECT_EQ(error, "path_forbidden");
}

TEST_F(McpAudioToolsTest, RejectsUnsupportedMissionLayouts) {
    const fs::path forbidden = root_ / "missions/location1/level1/sounds/foreign.wav";
    fs::create_directories(forbidden.parent_path());
    Write(forbidden, "RIFFforeignWAVE");

    std::string error;
    const auto result = mcp::CallAudioTool(
        *service_, "audio_replace_sfx",
        mcp::JsonValue::Object{{"source_path", "source.wav"},
                               {"destination_path", "missions/location1/level1/sounds/foreign.wav"}}, error);
    EXPECT_TRUE(result.is_null());
    EXPECT_EQ(error, "path_forbidden");
}

std::string Read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

}  // namespace
