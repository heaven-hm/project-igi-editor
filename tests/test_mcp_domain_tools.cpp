#include <gtest/gtest.h>

#include "mcp/mcp_tools_ai.h"
#include "mcp/mcp_tools_assets.h"
#include "mcp/mcp_tools_graph.h"
#include "mcp/mcp_tools_mission.h"
#include "mcp/mcp_tools_objects.h"
#include "mcp/mcp_task_id.h"
#include "level/task_schema.h"

#include <filesystem>
#include <fstream>

namespace {

namespace fs = std::filesystem;

class McpDomainToolsTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / "igi_mcp_domain_tools_test";
        std::error_code error;
        fs::remove_all(root_, error);
        const fs::path level = root_ / "missions/location0/level1";
        fs::create_directories(level, error);
        ASSERT_FALSE(error) << error.message();
        fs::create_directories(root_ / "editor/tools", error);
        ASSERT_FALSE(error) << error.message();
        std::ofstream(root_ / "editor/tools/IGIModels.json")
            << "[{\"ModelName\":\"MODEL_300\",\"ModelId\":\"300_01_1\"},"
               "{\"ModelName\":\"MODEL_301\",\"ModelId\":\"301_01_1\"},"
               "{\"ModelName\":\"MODEL_302\",\"ModelId\":\"302_01_1\"},"
               "{\"ModelName\":\"MODEL_304\",\"ModelId\":\"304_01_1\"},"
               "{\"ModelName\":\"WEAPON_ID_UZI\",\"ModelId\":\"111_01_1\"},"
               "{\"ModelName\":\"WEAPON_ID_UZIX2\",\"ModelId\":\"111_04_1\"}]";
        std::ofstream(level / "objects.qvm", std::ios::binary) << "fixture-qvm";
        std::ofstream(level / "objects.qsc", std::ios::binary)
        << "Task_New(100, \"Building\", \"Hangar\", 1, 2, 3, 0, 0, 0, \"300_01_1\");\n"
           "Task_DeclareParameters(\"Real64Task\", \"Value\", \"Real64\");\n"
           "Task_DeclareParameters(\"CustomLayoutTask\", \"Position\", \"Real64x3\", \"Model\", \"String16\");\n"
           "Task_New(160, \"Real64Task\", \"Precision\", 0.123456789012);\n"
           "Task_New(161, \"CustomLayoutTask\", \"Layout\", 1, 2, 3, \"302_01_1\");\n"
           "Task_New(101, \"HumanSoldier\", \"Guard\", 4, 5, 6, 0, \"200_01_1\", 0, -1, 0);\n"
           "Task_New(101, \"HumanSoldier\", \"Guard duplicate\", 14, 15, 16, 0, \"200_01_2\", 0, -1, 0);\n"
           "Task_New(103, \"HumanSoldier\", \"Unique guard\", 24, 25, 26, 0, \"200_01_4\", 0, -1, 0);\n"
               "Task_New(-1, \"HumanSoldier\", \"Anonymous guard\", 17, 18, 19, 0, \"200_01_3\", 0, -1, 0);\n"
               "Task_New(-1, \"Building\", \"Anonymous\", 7, 8, 9, 0, 0, 0, \"302_01_1\"); Task_New(-1, \"Building\", \"Anonymous duplicate\", 7, 8, 9, 0, 0, 0, \"302_01_1\");\n"
               "Task_New(150, \"HumanSoldier\", \"Loadout\", 4, 5, 6, 0, \"200_01_1\", 0, -1, 0, Task_New(151, \"GunUzi\", \"\", 4, 5, 6, 0, 0, 0, \"WEAPON_ID_UZI\"), Task_New(152, \"GunUzi\", \"\", 4, 5, 6, 0, 0, 0, \"WEAPON_ID_UZIX2\"));\n"
               "Task_New(170, \"HumanSoldierRPG\", \"Sniper\", 4, 5, 6, 0, \"200_01_1\", 0, -1, 0, Task_New(171, \"GunRPG\", \"\", 4, 5, 6, 0, 0, 0, \"WEAPON_ID_UZI\"));\n"
               "Task_New(102, \"UnknownTask\", \"Unknown\", 1, 2, 3, 0, 0, 0, \"303_01_1\");\n"
               "DefineComputerObjective(1, 0, 0, \"TRUE\", \"obj.text\", 100, \"TRUE\", \"FALSE\");\n";
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

TEST_F(McpDomainToolsTest, PersistsTransformAndModelWithoutAcceptingScale) {
    std::string error;
    const auto transformed = mcp::CallObjectTool(
        *service_, "object_set_transform",
        mcp::JsonValue::Object{
            {"task_id", "100"},
            {"position", mcp::JsonValue::Array{10.0, 20.0, 30.0}},
            {"rotation_radians", mcp::JsonValue::Array{0.1, 0.2, 0.3}},
        }, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_FALSE(transformed.is_null());

    const auto model = mcp::CallObjectTool(
        *service_, "object_set_model",
        mcp::JsonValue::Object{{"task_id", "100"}, {"model_id", "301_01_1"}}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_FALSE(model.is_null());

    const auto object = service_->GetObject(1, "100", error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_EQ(object.at("model_id").as_string(), "301_01_1");
    EXPECT_EQ(object.at("position").as_array()[0].as_number(), 10.0);

    const auto scale = mcp::CallObjectTool(
        *service_, "object_set_transform",
        mcp::JsonValue::Object{{"task_id", "100"}, {"scale", 2.0}}, error);
    EXPECT_TRUE(scale.is_null());
    EXPECT_EQ(error, "unsupported_operation");
}

TEST_F(McpDomainToolsTest, DryRunReturnsStagedObjectWithoutWriting) {
    std::string error;
    const auto before = service_->GetObject(1, "100", error);
    ASSERT_TRUE(error.empty()) << error;
    const auto result = mcp::CallObjectTool(
        *service_, "object_set_model",
        mcp::JsonValue::Object{{"task_id", "100"}, {"model_id", "301_01_1"}, {"dry_run", true}}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_TRUE(result.at("dry_run").as_bool());
    EXPECT_EQ(result.at("before").at("model_id").as_string(), before.at("model_id").as_string());
    EXPECT_EQ(result.at("after").at("model_id").as_string(), "301_01_1");
    const auto after = service_->GetObject(1, "100", error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_EQ(after.at("model_id").as_string(), before.at("model_id").as_string());
}

TEST_F(McpDomainToolsTest, EnforcesDeclaredStringLength) {
    std::string error;
    const auto result = mcp::CallObjectTool(
        *service_, "object_set_model",
        mcp::JsonValue::Object{{"task_id", "100"}, {"model_id", "12345678901234567"}}, error);
    EXPECT_TRUE(result.is_null());
    EXPECT_EQ(error, "unsupported_operation");
}

TEST_F(McpDomainToolsTest, RejectsMissingStandaloneMutationFields) {
    std::string error;
    const auto missing_model = mcp::CallObjectTool(
        *service_, "object_set_model", mcp::JsonValue::Object{{"task_id", "100"}}, error);
    EXPECT_TRUE(missing_model.is_null());
    EXPECT_EQ(error, "invalid_arguments");

    const auto missing_type = mcp::CallObjectTool(
        *service_, "object_set_type", mcp::JsonValue::Object{{"task_id", "100"}}, error);
    EXPECT_TRUE(missing_type.is_null());
    EXPECT_EQ(error, "invalid_arguments");

    const auto missing_parameter = mcp::CallObjectTool(
        *service_, "object_set_parameter", mcp::JsonValue::Object{{"task_id", "100"}}, error);
    EXPECT_TRUE(missing_parameter.is_null());
    EXPECT_EQ(error, "invalid_arguments");
}

TEST_F(McpDomainToolsTest, ReturnsLevelNotOpenBeforeSavingSource) {
    mcp::GameDataService unopened(*scope_);
    std::string error;
    EXPECT_FALSE(unopened.SaveCurrentObjectSource("not parsed", {}, error));
    EXPECT_EQ(error, "level_not_open");
}

TEST_F(McpDomainToolsTest, PreservesReal64ParameterPrecision) {
    std::string error;
    const auto result = mcp::CallObjectTool(
        *service_, "object_set_parameter",
        mcp::JsonValue::Object{{"task_id", "160"}, {"parameter_index", 3},
                               {"value", 0.123456789012}}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_FALSE(result.is_null());

    std::string source;
    ASSERT_TRUE(service_->LoadCurrentObjectSource(source, error)) << error;
    EXPECT_NE(source.find("0.123456789012"), std::string::npos);
}

TEST_F(McpDomainToolsTest, RejectsModelIdsMissingFromTheCatalog) {
    std::string error;
    const auto result = mcp::CallObjectTool(
        *service_, "object_set_model",
        mcp::JsonValue::Object{{"task_id", "100"}, {"model_id", "999_01_1"}}, error);
    EXPECT_TRUE(result.is_null());
    EXPECT_EQ(error, "unknown_asset_id");
}

TEST_F(McpDomainToolsTest, ReturnsAfterSnapshotWhenRenamingAnonymousTask) {
    std::string error;
    const auto snapshot = service_->ListObjects(1, error);
    ASSERT_TRUE(error.empty()) << error;
    std::string anonymous_id;
    for (const auto& object : snapshot.at("objects").as_array()) {
        if (object.at("type").as_string() == "Building" &&
            object.at("id").as_string().starts_with("anon-")) {
            anonymous_id = object.at("id").as_string();
            break;
        }
    }
    ASSERT_FALSE(anonymous_id.empty());

    const auto result = mcp::CallObjectTool(
        *service_, "task_update",
        mcp::JsonValue::Object{{"task_id", anonymous_id}, {"name", "Renamed"}}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_FALSE(result.is_null());
    EXPECT_EQ(result.at("after").at("name").as_string(), "Renamed");
    EXPECT_EQ(result.at("after").at("id").as_string(), anonymous_id);
    const auto reread = service_->GetObject(1, anonymous_id, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_EQ(reread.at("name").as_string(), "Renamed");
}

TEST_F(McpDomainToolsTest, KeepsAnonymousIdAcrossInsertedLines) {
    std::string error;
    const auto before = service_->ListObjects(1, error);
    ASSERT_TRUE(error.empty()) << error;
    std::string anonymous_id;
    for (const auto& object : before.at("objects").as_array()) {
        if (object.at("type").as_string() == "HumanSoldier" &&
            object.at("name").as_string() == "Anonymous guard") {
            anonymous_id = object.at("id").as_string();
            break;
        }
    }
    ASSERT_FALSE(anonymous_id.empty());

    const fs::path qsc = root_ / "missions/location0/level1/objects.qsc";
    std::ifstream input(qsc, std::ios::binary);
    const std::string source((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    std::ofstream(qsc, std::ios::binary)
        << "Task_New(200, \"Building\", \"Inserted sibling\", 20, 21, 22, 0, 0, 0, \"304_01_1\");\n"
        << source;

    const auto after = service_->ListObjects(1, error);
    ASSERT_TRUE(error.empty()) << error;
    for (const auto& object : after.at("objects").as_array()) {
        if (object.at("name").as_string() == "Anonymous guard") {
            EXPECT_EQ(object.at("id").as_string(), anonymous_id);
            return;
        }
    }
    FAIL() << "anonymous task disappeared after inserting a line";
}

TEST_F(McpDomainToolsTest, KeepsSameLineAnonymousTasksAddressable) {
    std::string error;
    const auto snapshot = service_->ListObjects(1, error);
    ASSERT_TRUE(error.empty()) << error;
    std::vector<std::string> ids;
    for (const auto& object : snapshot.at("objects").as_array()) {
        if (object.at("type").as_string() == "Building" &&
            object.at("id").as_string().starts_with("anon-")) {
            ids.push_back(object.at("id").as_string());
            EXPECT_TRUE(object.at("writable").as_bool());
        }
    }
    ASSERT_EQ(ids.size(), 2u);
    EXPECT_NE(ids[0], ids[1]);
}

TEST_F(McpDomainToolsTest, KeepsAnonymousIdAcrossMutableFields) {
    std::string error;
    const auto snapshot = service_->ListObjects(1, error);
    ASSERT_TRUE(error.empty()) << error;
    std::string anonymous_id;
    for (const auto& object : snapshot.at("objects").as_array()) {
        if (object.at("type").as_string() == "Building" &&
            object.at("id").as_string().starts_with("anon-")) {
            anonymous_id = object.at("id").as_string();
            break;
        }
    }
    ASSERT_FALSE(anonymous_id.empty());

    const auto transformed = mcp::CallObjectTool(
        *service_, "object_set_transform",
        mcp::JsonValue::Object{{"task_id", anonymous_id},
                               {"position", mcp::JsonValue::Array{70.0, 80.0, 90.0}}}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_FALSE(transformed.is_null());

    const auto reread = service_->GetObject(1, anonymous_id, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_EQ(reread.at("id").as_string(), anonymous_id);
    EXPECT_EQ(reread.at("position").as_array()[0].as_number(), 70.0);
}

TEST_F(McpDomainToolsTest, IgnoresBlockCommentedTaskCallsForAiMutation) {
    const fs::path qsc = root_ / "missions/location0/level1/objects.qsc";
    std::ofstream(qsc, std::ios::binary)
        << "/* Task_New(-1, \"HumanSoldier\", \"Commented\", 17, 18, 19, 0, \"200_01_3\", 0, -1, 0); */\n"
           "Task_New(-1, \"HumanSoldier\", \"Live\", 17, 18, 19, 0, \"200_01_3\", 0, -1, 0);\n";
    std::string error;
    ASSERT_TRUE(service_->RefreshRevision(error)) << error;
    const auto snapshot = service_->ListObjects(1, error);
    ASSERT_TRUE(error.empty()) << error;
    std::string anonymous_id;
    for (const auto& object : snapshot.at("objects").as_array()) {
        if (object.at("name").as_string() == "Live") {
            anonymous_id = object.at("id").as_string();
            break;
        }
    }
    ASSERT_FALSE(anonymous_id.empty());

    const auto result = mcp::CallAiTool(
        *service_, "ai_update",
        mcp::JsonValue::Object{{"task_id", anonymous_id},
                               {"fields", mcp::JsonValue::Object{{"team", 3}}}}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_FALSE(result.is_null());
    const auto after = service_->GetObject(1, anonymous_id, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_EQ(after.at("args").as_array()[8].as_number(), 3.0);
}

TEST(McpTaskIdTest, PreservesNestedArgumentIdentityInSignatures) {
    EXPECT_NE(mcp::AnonymousArgumentSignature("Call(1)"),
              mcp::AnonymousArgumentSignature("Call(2)"));
    EXPECT_NE(mcp::AnonymousArgumentSignature("1 + 2"),
              mcp::AnonymousArgumentSignature("1 + 3"));
    EXPECT_NE(mcp::AnonymousArgumentSignature("16777216"),
              mcp::AnonymousArgumentSignature("16777217"));
    EXPECT_NE(mcp::AnonymousArgumentSignature("1"),
              mcp::AnonymousArgumentSignature("1.0"));
    EXPECT_NE(mcp::AnonymousArgumentSignature("true"),
              mcp::AnonymousArgumentSignature("\"true\""));
    EXPECT_NE(mcp::AnonymousArgumentSignature("name"),
              mcp::AnonymousArgumentSignature("\"name\""));
}

TEST_F(McpDomainToolsTest, ReturnsInvalidArgumentsForMalformedAiFields) {
    std::string error;
    const auto result = mcp::CallAiTool(
        *service_, "ai_update",
        mcp::JsonValue::Object{{"task_id", "103"},
                               {"fields", mcp::JsonValue::Object{{"team", "three"}}}}, error);
    EXPECT_TRUE(result.is_null());
    EXPECT_EQ(error, "invalid_arguments");
}

TEST_F(McpDomainToolsTest, ReturnsBeforeAndAfterForWeaponLoadoutDryRun) {
    std::string error;
    const auto result = mcp::CallAiTool(
        *service_, "ai_set_weapon_loadout",
        mcp::JsonValue::Object{
            {"task_id", "150"},
            {"loadout", mcp::JsonValue::Array{
                mcp::JsonValue::Object{{"task_id", "152"}, {"weapon_id", "WEAPON_ID_UZI"}},
                mcp::JsonValue::Object{{"task_id", "151"}, {"weapon_id", "WEAPON_ID_UZIX2"}}}},
            {"dry_run", true},
        }, error);
    ASSERT_TRUE(error.empty()) << error;
    ASSERT_TRUE(result.at("before").contains("loadout"));
    ASSERT_TRUE(result.at("after").contains("loadout"));
    ASSERT_EQ(result.at("before").at("loadout").as_array().size(), 2u);
    ASSERT_EQ(result.at("after").at("loadout").as_array().size(), 2u);
    EXPECT_EQ(result.at("before").at("loadout").as_array()[0].at("task_id").as_string(), "152");
    EXPECT_EQ(result.at("before").at("loadout").as_array()[0].at("object")
                  .at("args").as_array().back().as_string(), "WEAPON_ID_UZIX2");
    EXPECT_EQ(result.at("before").at("loadout").as_array()[1].at("task_id").as_string(), "151");
    EXPECT_EQ(result.at("before").at("loadout").as_array()[1].at("object")
                  .at("args").as_array().back().as_string(), "WEAPON_ID_UZI");
    EXPECT_EQ(result.at("after").at("loadout").as_array()[0].at("task_id").as_string(), "152");
    EXPECT_EQ(result.at("after").at("loadout").as_array()[0].at("object")
                  .at("args").as_array().back().as_string(), "WEAPON_ID_UZI");
    EXPECT_EQ(result.at("after").at("loadout").as_array()[1].at("task_id").as_string(), "151");
    EXPECT_EQ(result.at("after").at("loadout").as_array()[1].at("object")
                  .at("args").as_array().back().as_string(), "WEAPON_ID_UZIX2");
    ASSERT_EQ(result.at("after").at("children").as_array().size(), 2u);
    EXPECT_EQ(result.at("after").at("children").as_array()[0].as_string(), "151");
    EXPECT_EQ(result.at("after").at("children").as_array()[1].as_string(), "152");
}

TEST_F(McpDomainToolsTest, SupportsRpgEnemyWeaponLoadouts) {
    std::string error;
    const auto result = mcp::CallAiTool(
        *service_, "ai_set_weapon_loadout",
        mcp::JsonValue::Object{
            {"task_id", "170"},
            {"loadout", mcp::JsonValue::Array{
                mcp::JsonValue::Object{{"task_id", "171"}, {"weapon_id", "WEAPON_ID_UZIX2"}}}},
            {"dry_run", true},
        }, error);
    ASSERT_TRUE(error.empty()) << error;
    ASSERT_TRUE(result.at("after").at("loadout").is_array());
    EXPECT_EQ(result.at("after").at("loadout").as_array()[0].at("object")
                  .at("args").as_array().back().as_string(), "WEAPON_ID_UZIX2");
}

TEST_F(McpDomainToolsTest, BatchLoadoutPreflightsAllOperations) {
    std::string error;
    const auto result = mcp::CallAiTool(
        *service_, "ai_batch_set_loadout",
        mcp::JsonValue::Object{
            {"operations", mcp::JsonValue::Array{
                mcp::JsonValue::Object{
                    {"task_id", "150"},
                    {"loadout", mcp::JsonValue::Array{
                        mcp::JsonValue::Object{{"task_id", "151"}, {"weapon_id", "WEAPON_ID_UZI"}}}}},
                mcp::JsonValue::Object{
                    {"task_id", "170"},
                    {"loadout", mcp::JsonValue::Array{
                        mcp::JsonValue::Object{{"task_id", "171"}, {"weapon_id", "WEAPON_ID_UZIX2"}}}}}}},
            {"dry_run", true},
        }, error);
    ASSERT_TRUE(error.empty()) << error;
    ASSERT_TRUE(result.at("dry_run").as_bool());
    ASSERT_EQ(result.at("operations").as_array().size(), 2u);
    const auto object = service_->GetObject(1, "171", error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_EQ(object.at("args").as_array().back().as_string(), "WEAPON_ID_UZI");
}

TEST_F(McpDomainToolsTest, RejectsUnknownPropertiesForReservedMutationTools) {
    std::string error;
    const auto graph = mcp::CallGraphTool(
        *service_, "graph_node_update", mcp::JsonValue::Object{{"unexpected", true}}, error);
    EXPECT_TRUE(graph.is_null());
    EXPECT_EQ(error, "invalid_arguments");

    const auto asset = mcp::CallAssetTool(
        *service_, "asset_convert", mcp::JsonValue::Object{{"unexpected", true}}, error);
    EXPECT_TRUE(asset.is_null());
    EXPECT_EQ(error, "invalid_arguments");
}

TEST_F(McpDomainToolsTest, ExposesAiMissionGraphAndAssetReadOnlyOperations) {
    std::string error;
    const auto ai = mcp::CallAiTool(
        *service_, "ai_validate_script", mcp::JsonValue::Object{{"source", "Foo(1);"}}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_TRUE(ai.at("valid").as_bool());

    const auto mission = mcp::CallMissionTool(
        *service_, "mission_objective_list", mcp::JsonValue::Object{}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_EQ(mission.at("level").as_number(), 1);

    const auto graphs = mcp::CallGraphTool(
        *service_, "graph_list", mcp::JsonValue::Object{}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_TRUE(graphs.at("graphs").is_array());

    std::error_code file_error;
    fs::create_directories(root_ / "missions/location0/level1/graphs", file_error);
    ASSERT_FALSE(file_error) << file_error.message();
    std::ofstream(root_ / "missions/location0/level1/graphs/graph1.dat", std::ios::binary)
        << "graph";
    const auto assets = mcp::CallAssetTool(
        *service_, "asset_list", mcp::JsonValue::Object{{"format", "dat"}}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_TRUE(assets.at("assets").is_array());
    bool found_graph_dat = false;
    for (const auto& asset : assets.at("assets").as_array()) {
        if (asset.at("path").as_string().ends_with("/graphs/graph1.dat")) {
            found_graph_dat = true;
            EXPECT_EQ(asset.at("format").as_string(), "graph");
        }
    }
    EXPECT_TRUE(found_graph_dat);
}

TEST_F(McpDomainToolsTest, UsesCanonicalSchemaArgumentCounts) {
    std::string error;
    const auto object = service_->GetObject(1, "161", error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_TRUE(object.is_object());
    EXPECT_TRUE(object.at("position").as_array().empty());
}

TEST_F(McpDomainToolsTest, UsesListedAnonymousIdsAndRejectsUnknownMutationLayouts) {
    std::string error;
    const auto snapshot = service_->ListObjects(1, error);
    ASSERT_TRUE(error.empty()) << error;

    std::string anonymous_id;
    for (const auto& object : snapshot.at("objects").as_array()) {
        if (object.at("type").as_string() == "Building" &&
            object.at("id").as_string().starts_with("anon-")) {
            anonymous_id = object.at("id").as_string();
            break;
        }
    }
    ASSERT_FALSE(anonymous_id.empty());

    const auto anonymous_update = mcp::CallObjectTool(
        *service_, "object_set_model",
        mcp::JsonValue::Object{{"task_id", anonymous_id}, {"model_id", "304_01_1"}}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_FALSE(anonymous_update.is_null());
    const auto reread = service_->GetObject(1, anonymous_id, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_EQ(reread.at("id").as_string(), anonymous_id);

    const auto unknown_update = mcp::CallObjectTool(
        *service_, "object_set_transform",
        mcp::JsonValue::Object{{"task_id", "102"},
                               {"position", mcp::JsonValue::Array{11.0, 12.0, 13.0}}}, error);
    EXPECT_TRUE(unknown_update.is_null());
    EXPECT_EQ(error, "unsupported_operation");
}

TEST_F(McpDomainToolsTest, ReportsTypeSpecificObjectLayoutIndices) {
    std::string error;
    const auto schema = mcp::CallObjectTool(
        *service_, "object_get_schema", mcp::JsonValue::Object{{"type", "Door"}}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_EQ(schema.at("fields").at("position").at("parameter_indices").as_array()[0].as_number(), 3);
    EXPECT_EQ(schema.at("fields").at("rotation_radians").at("parameter_indices").as_array()[0].as_number(), 9);
    EXPECT_EQ(schema.at("fields").at("model_id").at("parameter_index").as_number(), 12);
}

TEST_F(McpDomainToolsTest, DirectSchemaLookupReloadsDeclaredSchemas) {
    TaskSchemaNS::ClearRegisteredSchemas();
    std::string error;
    const auto schema = mcp::CallObjectTool(
        *service_, "object_get_schema", mcp::JsonValue::Object{{"type", "CustomLayoutTask"}}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_EQ(schema.at("type").as_string(), "CustomLayoutTask");
    ASSERT_TRUE(schema.at("fields").contains("model_id"));
    EXPECT_EQ(schema.at("fields").at("model_id").at("parameter_index").as_number(), 4);
}

TEST_F(McpDomainToolsTest, DerivesTransformAndModelLayoutsFromSchemas) {
    std::string error;
    const auto camera = mcp::CallObjectTool(
        *service_, "object_get_schema", mcp::JsonValue::Object{{"type", "SCamera"}}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_EQ(camera.at("fields").at("rotation_radians").at("parameter_indices").as_array()[0].as_number(), 6);
    EXPECT_EQ(camera.at("fields").at("model_id").at("parameter_index").as_number(), 10);

    for (const std::string type : {"Fence", "Car", "Heli"}) {
        const auto schema = mcp::CallObjectTool(
            *service_, "object_get_schema", mcp::JsonValue::Object{{"type", type}}, error);
        ASSERT_TRUE(error.empty()) << error;
        EXPECT_FALSE(schema.at("fields").contains("rotation_radians")) << type;
    }

    const auto train = mcp::CallObjectTool(
        *service_, "object_get_schema", mcp::JsonValue::Object{{"type", "Train"}}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_FALSE(train.at("fields").contains("position"));
    EXPECT_FALSE(train.at("fields").contains("rotation_radians"));
    EXPECT_EQ(train.at("fields").at("model_id").at("parameter_index").as_number(), 6);
}

TEST_F(McpDomainToolsTest, RejectsIncompatibleTypeMigration) {
    std::string error;
    const auto result = mcp::CallObjectTool(
        *service_, "object_set_type",
        mcp::JsonValue::Object{{"task_id", "100"}, {"type", "Door"}}, error);
    EXPECT_TRUE(result.is_null());
    EXPECT_EQ(error, "unsupported_operation");

    const auto object = service_->GetObject(1, "100", error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_EQ(object.at("type").as_string(), "Building");
}

TEST_F(McpDomainToolsTest, EnforcesSchemaTypesForParameterMutation) {
    std::string error;
    const auto fractional_team = mcp::CallObjectTool(
        *service_, "object_set_parameter",
        mcp::JsonValue::Object{{"task_id", "100"}, {"parameter_index", 3}, {"value", "not-a-number"}}, error);
    EXPECT_TRUE(fractional_team.is_null());
    EXPECT_EQ(error, "unsupported_operation");

    const auto unknown_field = mcp::CallObjectTool(
        *service_, "object_set_parameter",
        mcp::JsonValue::Object{{"task_id", "100"}, {"parameter_index", 99}, {"value", 1}}, error);
    EXPECT_TRUE(unknown_field.is_null());
    EXPECT_EQ(error, "unsupported_operation");
}

TEST_F(McpDomainToolsTest, ResolvesAnonymousAndDuplicateAiIds) {
    std::string error;
    const auto duplicate = mcp::CallAiTool(
        *service_, "ai_update",
        mcp::JsonValue::Object{{"task_id", "101#1"},
                               {"fields", mcp::JsonValue::Object{{"team", 2}}}}, error);
    EXPECT_TRUE(duplicate.is_null());
    EXPECT_EQ(error, "ambiguous_task_id");

    std::string anonymous_id;
    const auto snapshot = service_->ListObjects(1, error);
    ASSERT_TRUE(error.empty()) << error;
    for (const auto& object : snapshot.at("objects").as_array()) {
        if (object.at("type").as_string() == "HumanSoldier" &&
            object.at("id").as_string().starts_with("anon-")) {
            anonymous_id = object.at("id").as_string();
            break;
        }
    }
    ASSERT_FALSE(anonymous_id.empty());
    const auto anonymous = mcp::CallAiTool(
        *service_, "ai_update",
        mcp::JsonValue::Object{{"task_id", anonymous_id},
                               {"fields", mcp::JsonValue::Object{{"team", 3}}}}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_FALSE(anonymous.is_null());
}

}  // namespace
