#include <gtest/gtest.h>

#include "mcp/game_data_service.h"

#include <filesystem>
#include <fstream>
#include <set>
#include <string>

namespace {

namespace fs = std::filesystem;

class McpGameSnapshotTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / "igi_mcp_snapshot_test";
        std::error_code error;
        fs::remove_all(root_, error);
        const fs::path level = root_ / "missions/location0/level1";
        fs::create_directories(level, error);
        ASSERT_FALSE(error) << error.message();
        std::ofstream(level / "objects.qvm", std::ios::binary) << "fixture-qvm";
        std::ofstream(level / "objects.qsc", std::ios::binary)
            << "Task_New(100, \"Building\", \"Hangar\", 1.0, 2.0, 3.0, 0.1, 0.2, 0.3, \"300_01_1\", "
               "Task_New(-1, \"GunPickup\", \"Uzi\", 4, 5, 6, 0, 0, 0, \"WEAPON_ID_UZI\", 10));\n"
               "Task_New(200, \"Door\", \"Gate\", 7, 8, 9, 0, 0, 0, 0, 0, 0, \"340_01_1\");\n";

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

TEST_F(McpGameSnapshotTest, ReturnsRedactedProjectInfoAndDeterministicLevels) {
    mcp::GameDataService service(*scope_);
    const mcp::JsonValue info = service.ProjectInfo();
    EXPECT_EQ(info.at("project_type").as_string(), "igi1");
    EXPECT_EQ(info.at("protocol_profile").as_string(), "2026-07-28");
    EXPECT_FALSE(info.contains("absolute_path"));

    std::string error;
    const mcp::JsonValue levels = service.ListLevels(error);
    ASSERT_TRUE(error.empty()) << error;
    ASSERT_EQ(levels.as_array().size(), 1u);
    EXPECT_EQ(levels.as_array().front().at("relative_path").as_string(),
              "missions/location0/level1");
}

TEST_F(McpGameSnapshotTest, BuildsStableObjectSnapshotWithHierarchyAndGameFields) {
    mcp::GameDataService service(*scope_);
    std::string error;
    const mcp::JsonValue snapshot = service.ListObjects(1, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_EQ(snapshot.at("level").as_number(), 1);
    ASSERT_EQ(snapshot.at("objects").as_array().size(), 3u);

    const auto& objects = snapshot.at("objects").as_array();
    EXPECT_EQ(objects[0].at("id").as_string(), "100");
    EXPECT_EQ(objects[0].at("type").as_string(), "Building");
    EXPECT_EQ(objects[0].at("model_id").as_string(), "300_01_1");
    EXPECT_EQ(objects[0].at("position").as_array()[0].as_number(), 1.0);
    ASSERT_EQ(objects[0].at("children").as_array().size(), 1u);
    const std::string child_id = objects[0].at("children").as_array()[0].as_string();
    EXPECT_TRUE(child_id.starts_with("anon-"));
    EXPECT_EQ(objects[1].at("parent_id").as_string(), "100");

    const mcp::JsonValue child = service.GetObject(1, child_id, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_EQ(child.at("name").as_string(), "Uzi");
    EXPECT_EQ(child.at("args").as_array()[9].as_string(), "WEAPON_ID_UZI");
}

TEST_F(McpGameSnapshotTest, ReturnsManifestWithRootRelativeFilesOnly) {
    mcp::GameDataService service(*scope_);
    std::string error;
    const mcp::JsonValue manifest = service.LevelManifest(1, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_EQ(manifest.at("level").as_number(), 1);
    ASSERT_EQ(manifest.at("files").as_array().size(), 2u);
    for (const auto& file : manifest.at("files").as_array()) {
        EXPECT_TRUE(file.at("path").as_string().starts_with("missions/location0/level1/"));
        EXPECT_EQ(file.at("path").as_string().find(":\\"), std::string::npos);
    }
}

TEST_F(McpGameSnapshotTest, ValidatesQvmAndQscWithStableCheckResults) {
    mcp::GameDataService service(*scope_);
    std::string error;
    const mcp::JsonValue validation = service.ValidateLevel(1, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_FALSE(validation.at("valid").as_bool());
    ASSERT_EQ(validation.at("checks").as_array().size(), 2u);
    EXPECT_EQ(validation.at("checks").as_array()[0].at("name").as_string(), "objects_qvm");
    EXPECT_EQ(validation.at("checks").as_array()[0].at("status").as_string(), "failed");
    EXPECT_EQ(validation.at("checks").as_array()[1].at("name").as_string(), "objects_qsc");
    EXPECT_EQ(validation.at("checks").as_array()[1].at("status").as_string(), "passed");
}

TEST_F(McpGameSnapshotTest, RejectsUnknownTaskIdWithoutLeakingFilesystemDetails) {
    mcp::GameDataService service(*scope_);
    std::string error;
    const mcp::JsonValue object = service.GetObject(1, "missing", error);
    EXPECT_TRUE(object.is_null());
    EXPECT_EQ(error, "unknown_task_id");
    EXPECT_EQ(error.find(root_.string()), std::string::npos);
}

TEST_F(McpGameSnapshotTest, KeepsGeneratedTaskIdsUniqueWhenTheyMeetExplicitIds) {
    const fs::path objects = root_ / "missions/location0/level1/objects.qsc";
    std::ofstream(objects, std::ios::trunc)
        << "Task_New(1, \"Building\", \"First\", 1, 2, 3, 0, 0, 0, \"300_01_1\");\n"
           "Task_New(1, \"Building\", \"Second\", 4, 5, 6, 0, 0, 0, \"300_01_1\");\n"
           "Task_New(\"1#1\", \"Building\", \"Explicit suffix\", 7, 8, 9, 0, 0, 0, \"300_01_1\");\n";
    mcp::GameDataService service(*scope_);
    std::string error;
    ASSERT_TRUE(service.OpenLevel(1, error)) << error;
    const auto snapshot = service.ListObjects(1, error);
    ASSERT_TRUE(error.empty()) << error;
    std::set<std::string> ids;
    for (const auto& object : snapshot.at("objects").as_array()) {
        ids.insert(object.at("id").as_string());
    }
    EXPECT_EQ(ids.size(), snapshot.at("objects").as_array().size());
}

}  // namespace
