#include <gtest/gtest.h>

#include "mcp/mcp_tools_player.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>

namespace {

namespace fs = std::filesystem;

class McpPlayerToolsTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / "igi_mcp_player_tools_test";
        std::error_code error;
        fs::remove_all(root_, error);
        fs::create_directories(root_ / "humanplayer", error);
        ASSERT_FALSE(error) << error.message();
        std::ofstream(root_ / "humanplayer/humanplayer.qsc")
            << "DefineHumanPlayerGeneral(1.75, 17.5, 27.0, 0.5, 0.85, 0.85, 0.25, 3.0, 0.5, "
               "0.5, 0.75, 1.0, 0.5, 0.75, 1.0, 0.5, 0.75, 1.0, 0.5, 0.75, 1.0, 1.0, 1.0, "
               "1.0, 1.0, 1.0, 0.5, 0.75, 1.0, 0.5, 0.75, 1.0, 0.5, 0.75, 1.0, 0.5, 0.75, "
               "1.0, 1.0, 1.0, 1.0, 100000.0, 100000.0);\n"
            << "DefineHumanPlayerWeaponCycle(WEAPON_ID_KNIFE, WEAPON_ID_UZI);\n"
            << "DefineHumanPlayerWeaponCategory(1, WEAPON_ID_KNIFE, 1, WEAPON_ID_UZI);\n"
            << "DefineHumanPlayerAmmoLimit(AMMO_ID_556, 200, AMMO_ID_919, 196);\n";

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

TEST_F(McpPlayerToolsTest, PublishesPlayerPhysicsInventoryAndAmmoTools) {
    const auto definitions = mcp::PlayerToolDefinitions();
    std::set<std::string> names;
    for (const auto& definition : definitions) names.insert(definition.name);

    EXPECT_TRUE(names.contains("player_get_physics"));
    EXPECT_TRUE(names.contains("player_set_physics"));
    EXPECT_TRUE(names.contains("player_set_inventory"));
    EXPECT_TRUE(names.contains("player_set_ammo"));
}

TEST_F(McpPlayerToolsTest, ReadsAuthoredPhysicsAndInventory) {
    std::string error;
    const auto physics = mcp::CallPlayerTool(
        *service_, "player_get_physics", mcp::JsonValue::Object{}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_EQ(physics.at("general").at("maximum_health").as_number(), 3.0);
    EXPECT_EQ(physics.at("general").at("jump_horizontal_speed_kmh").as_number(), 17.5);
    EXPECT_EQ(physics.at("general").at("jump_vertical_speed_kmh").as_number(), 27.0);
    EXPECT_EQ(physics.at("weapon_cycle").as_array().size(), 2u);
    EXPECT_EQ(physics.at("ammo_limits").at("AMMO_ID_556").as_number(), 200.0);
}

TEST_F(McpPlayerToolsTest, PersistsPhysicsInventoryAndAmmoThroughQscAndSupportsDryRun) {
    std::string error;
    const auto dry_run = mcp::CallPlayerTool(
        *service_, "player_set_physics",
        mcp::JsonValue::Object{
            {"fields", mcp::JsonValue::Object{{"maximum_health", 30.0},
                                               {"jump_vertical_speed_kmh", 54.0}}},
            {"dry_run", true},
        }, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_TRUE(dry_run.at("dry_run").as_bool());
    EXPECT_EQ(dry_run.at("after").at("general").at("maximum_health").as_number(), 30.0);

    const auto after_dry_run = mcp::CallPlayerTool(
        *service_, "player_get_physics", mcp::JsonValue::Object{}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_EQ(after_dry_run.at("general").at("maximum_health").as_number(), 3.0);

    const auto changed = mcp::CallPlayerTool(
        *service_, "player_set_physics",
        mcp::JsonValue::Object{
            {"fields", mcp::JsonValue::Object{{"maximum_health", 30.0},
                                               {"jump_vertical_speed_kmh", 54.0}}},
        }, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_EQ(changed.at("after").at("general").at("maximum_health").as_number(), 30.0);

    const auto inventory = mcp::CallPlayerTool(
        *service_, "player_set_inventory",
        mcp::JsonValue::Object{{"weapon_cycle", mcp::JsonValue::Array{
            "WEAPON_ID_KNIFE", "WEAPON_ID_M16A2", "WEAPON_ID_MEDIPACK"}}}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_EQ(inventory.at("after").at("weapon_cycle").as_array().size(), 3u);

    const auto ammo = mcp::CallPlayerTool(
        *service_, "player_set_ammo",
        mcp::JsonValue::Object{{"limits", mcp::JsonValue::Object{{"AMMO_ID_556", 250}}}}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_EQ(ammo.at("after").at("ammo_limits").at("AMMO_ID_556").as_number(), 250.0);

    const std::string source = [&] {
        std::ifstream input(root_ / "humanplayer/humanplayer.qsc");
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }();
    EXPECT_NE(source.find("54"), std::string::npos);
    EXPECT_NE(source.find("WEAPON_ID_M16A2"), std::string::npos);
    EXPECT_NE(source.find("AMMO_ID_556, 250"), std::string::npos);
}

TEST_F(McpPlayerToolsTest, RejectsUnknownPhysicsFieldsAndInvalidAmmo) {
    std::string error;
    const auto unknown = mcp::CallPlayerTool(
        *service_, "player_set_physics",
        mcp::JsonValue::Object{{"fields", mcp::JsonValue::Object{{"made_up", 1.0}}}}, error);
    EXPECT_TRUE(unknown.is_null());
    EXPECT_EQ(error, "invalid_arguments");

    const auto invalid = mcp::CallPlayerTool(
        *service_, "player_set_ammo",
        mcp::JsonValue::Object{{"limits", mcp::JsonValue::Object{{"AMMO_ID_556", -1}}}}, error);
    EXPECT_TRUE(invalid.is_null());
    EXPECT_EQ(error, "invalid_arguments");
}

}  // namespace
