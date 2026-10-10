#include <gtest/gtest.h>

#include "mcp/mcp_tools_cutscene.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>

namespace {

namespace fs = std::filesystem;

class McpCutsceneToolsTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / "igi_mcp_cutscene_tools_test";
        std::error_code error;
        fs::remove_all(root_, error);
        const fs::path level = root_ / "missions/location0/level1";
        fs::create_directories(level, error);
        ASSERT_FALSE(error) << error.message();
        std::ofstream(level / "objects.qvm", std::ios::binary) << "fixture-qvm";
        std::ofstream(level / "objects.qsc", std::ios::binary)
            << "Task_DeclareParameters(\"CutScene\", \"Position\", \"ObjectPos\", \"Orientation\", \"Real32x9\", \"Run\", \"VarString\", \"Reset\", \"VarString\", \"Time delta (seconds)\", \"VarString\", \"Start time (seconds)\", \"Real32\", \"Initial run\", \"bool8\", \"Time scale\", \"Real32\", \"Viewport height factor\", \"Real32\", \"Viewport height factor fade in time\", \"Real32\", \"Viewport height factor fade out time\", \"Real32\", \"Time of day\", \"Real32\", \"Start expression\", \"VarString\", \"Stop expression\", \"VarString\");\n"
               "Task_DeclareParameters(\"StatusMessage\", \"Position\", \"ObjectPos\", \"Orientation\", \"Real32x9\", \"Send\", \"VarString\", \"Text\", \"VarString\", \"Sprite\", \"String256\", \"Sound\", \"String16\", \"Is send once\", \"bool8\", \"Cutscene message\", \"bool8\", \"Duration\", \"Real32\");\n"
               "Task_New(1200, \"CutScene\", \"Intro\", 1, 2, 3, 0, 0, 0, \"run\", \"reset\", \"delta\", 0, FALSE, 1, 0.7, 0, 0, 0, \"start\", \"stop\");\n"
               "Task_New(1201, \"StatusMessage\", \"Line\", 1, 2, 3, 0, 0, 0, \"send\", \"old dialogue\", \"\", \"voice\", TRUE, TRUE, 2);\n";
        std::string open_error;
        scope_ = mcp::ProjectScope::Open(root_, open_error);
        ASSERT_TRUE(scope_.has_value()) << open_error;
        service_ = std::make_unique<mcp::GameDataService>(*scope_);
        ASSERT_TRUE(service_->OpenLevel(1, open_error)) << open_error;
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

TEST_F(McpCutsceneToolsTest, PublishesCoherentCutsceneTools) {
    const auto definitions = mcp::CutsceneToolDefinitions();
    std::set<std::string> names;
    for (const auto& definition : definitions) names.insert(definition.name);

    EXPECT_TRUE(names.contains("cutscene_list"));
    EXPECT_TRUE(names.contains("cutscene_get"));
    EXPECT_TRUE(names.contains("cutscene_edit_camera"));
    EXPECT_TRUE(names.contains("cutscene_set_dialogue"));
}

TEST_F(McpCutsceneToolsTest, ListsCameraStateAndEditsDialogueThroughExistingTaskSchema) {
    std::string error;
    const auto listed = mcp::CallCutsceneTool(
        *service_, "cutscene_list", mcp::JsonValue::Object{}, error);
    ASSERT_TRUE(error.empty()) << error;
    ASSERT_EQ(listed.at("cutscenes").as_array().size(), 1u);
    EXPECT_EQ(listed.at("cutscenes").as_array()[0].at("id").as_string(), "1200");

    const auto camera = mcp::CallCutsceneTool(
        *service_, "cutscene_edit_camera",
        mcp::JsonValue::Object{{"task_id", "1200"},
                               {"position", mcp::JsonValue::Array{10.0, 20.0, 30.0}},
                               {"rotation_radians", mcp::JsonValue::Array{0.1, 0.2, 0.3}}}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_FALSE(camera.is_null());

    const auto dry_run_camera = mcp::CallCutsceneTool(
        *service_, "cutscene_edit_camera",
        mcp::JsonValue::Object{{"task_id", "1200"},
                               {"position", mcp::JsonValue::Array{40.0, 50.0, 60.0}},
                               {"dry_run", true}}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_TRUE(dry_run_camera.at("dry_run").as_bool());
    EXPECT_EQ(dry_run_camera.at("cutscene").at("position").as_array()[0].as_number(), 40.0);

    const auto persisted_camera = service_->GetObject(1, "1200", error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_EQ(persisted_camera.at("position").as_array()[0].as_number(), 10.0);

    const auto dialogue = mcp::CallCutsceneTool(
        *service_, "cutscene_set_dialogue",
        mcp::JsonValue::Object{{"task_id", "1201"}, {"text", "new dialogue"}}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_FALSE(dialogue.is_null());
    const auto object = service_->GetObject(1, "1201", error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_EQ(object.at("args").as_array()[10].as_string(), "new dialogue");
}

}  // namespace
