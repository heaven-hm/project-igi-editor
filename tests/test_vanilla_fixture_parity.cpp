#include <gtest/gtest.h>

#include "../source/ai_script_host.h"
#include "../source/level/qvm_decompiler.h"
#include "../source/level/qvm_ai_bindings.h"
#include "../source/level/qvm_parser.h"
#include "../source/level/qsc_lexer.h"
#include "../source/level/qsc_parser.h"
#include "../source/mission_flow_loader.h"
#include "../source/mission_objective_loader.h"
#include "../source/renderer/graph_writer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return value;
}

std::filesystem::path FindChildCaseInsensitive(
    const std::filesystem::path& parent,
    const std::string& expected_name) {
    std::error_code error;
    const std::filesystem::path exact_path = parent / expected_name;
    if (std::filesystem::is_regular_file(exact_path, error) ||
        std::filesystem::is_directory(exact_path, error)) {
        return exact_path;
    }

    error.clear();
    if (!std::filesystem::is_directory(parent, error)) {
        return {};
    }

    const std::string expected_lower = ToLower(expected_name);
    for (const auto& entry : std::filesystem::directory_iterator(parent, error)) {
        if (error) {
            return {};
        }
        if (ToLower(entry.path().filename().string()) == expected_lower) {
            return entry.path();
        }
    }
    return {};
}

std::filesystem::path VanillaRoot() {
    const char* configured_root = std::getenv("IGI_VANILLA_ROOT");
    if (configured_root == nullptr || configured_root[0] == '\0') {
        return {};
    }
    return std::filesystem::path(configured_root);
}

std::filesystem::path VanillaLevelOneDirectory() {
    const std::filesystem::path root = VanillaRoot();
    const std::filesystem::path missions = FindChildCaseInsensitive(root, "missions");
    const std::filesystem::path location = FindChildCaseInsensitive(missions, "location0");
    return FindChildCaseInsensitive(location, "level1");
}

std::filesystem::path VanillaFile(const std::string& relative_name) {
    std::filesystem::path current = VanillaLevelOneDirectory();
    const std::filesystem::path relative_path(relative_name);
    for (const auto& component : relative_path) {
        current = FindChildCaseInsensitive(current, component.string());
        if (current.empty()) {
            return {};
        }
    }
    return current;
}

bool ContainsText(const QVMFile& qvm, const std::string& expected_text) {
    const auto contains = [&expected_text](const std::vector<std::string>& values) {
        return std::any_of(values.begin(), values.end(),
                           [&expected_text](const std::string& value) {
                               return value.find(expected_text) != std::string::npos;
                           });
    };

    if (contains(qvm.identifiers) || contains(qvm.strings)) {
        return true;
    }
    return std::any_of(qvm.instructions.begin(), qvm.instructions.end(),
                       [&expected_text](const QVMInstruction& instruction) {
                           return instruction.inline_text.find(expected_text) !=
                                  std::string::npos;
                       });
}

std::string QscToken(const qsc::Node& node) {
    switch (node.kind) {
    case qsc::NodeKind::IntLit:
        return std::to_string(node.i_val);
    case qsc::NodeKind::FloatLit:
        return std::to_string(node.f_val);
    case qsc::NodeKind::BoolLit:
        return node.b_val ? "TRUE" : "FALSE";
    case qsc::NodeKind::StringLit: {
        std::string escaped;
        escaped.reserve(node.s_val.size() + 2);
        for (const char character : node.s_val) {
            if (character == '\\' || character == '"') {
                escaped.push_back('\\');
            }
            escaped.push_back(character);
        }
        return '"' + escaped + '"';
    }
    case qsc::NodeKind::IdentLit:
        return node.s_val;
    default:
        return {};
    }
}

void CollectAuthoredTaskSources(
    const qsc::Node& node,
    std::vector<igi::MissionObjectiveTaskSource>& objective_sources,
    std::vector<igi::MissionFlowTaskSource>& flow_sources) {
    if (node.kind == qsc::NodeKind::Call &&
        node.s_val == "Task_New" &&
        node.children.size() >= 2 &&
        node.children[1]->kind == qsc::NodeKind::StringLit) {
        const std::string task_type = node.children[1]->s_val;
        std::vector<std::string> argument_tokens;
        argument_tokens.reserve(node.children.size());
        for (const std::unique_ptr<qsc::Node>& argument : node.children) {
            // Nested Task_New calls are child tasks, not scalar arguments. This
            // mirrors LevelObjects::LoadRecursive's lossless task projection.
            if (argument->kind == qsc::NodeKind::Call) {
                continue;
            }
            argument_tokens.push_back(QscToken(*argument));
        }

        if (task_type == "DefineComputerObjective") {
            objective_sources.push_back({task_type, std::move(argument_tokens)});
        } else if (task_type == "LevelFlow") {
            flow_sources.push_back({task_type, std::move(argument_tokens)});
        }
    }

    for (const std::unique_ptr<qsc::Node>& child : node.children) {
        CollectAuthoredTaskSources(*child, objective_sources, flow_sources);
    }
}

} // namespace

TEST(VanillaFixtureParityTest, LevelOneObjectsExposeAuthoredGameplayPrimitives) {
    const std::filesystem::path objects_path = VanillaFile("objects.qvm");
    if (VanillaRoot().empty()) {
        GTEST_SKIP() << "Set IGI_VANILLA_ROOT to the vanilla Project IGI install "
                        "to run this fixture parity test";
    }
    ASSERT_FALSE(objects_path.empty()) << "Vanilla objects.qvm is missing below "
                                          "IGI_VANILLA_ROOT";

    const QVMFile objects = QVM_Parse(objects_path.string());
    ASSERT_TRUE(objects.valid) << objects.error;
    EXPECT_EQ(objects.header.ver_major, 8U);
    EXPECT_EQ(objects.header.ver_minor, 5U);

    // These definitions are the authored bridge between the QVM and the
    // native runtime: player spawn, patrol, mission flow, and objectives.
    for (const std::string& primitive : {
             "HumanPlayer", "PatrolPath", "PatrolPathCommand", "LevelFlow",
             "DefineComputerObjective", "AreaActivate", "EditVariable"}) {
        EXPECT_TRUE(ContainsText(objects, primitive)) << primitive;
    }

    const std::string decompiled = QVM_DecompileToString(objects);
    EXPECT_NE(decompiled.find("HumanPlayer_0"), std::string::npos);
    EXPECT_NE(decompiled.find("LevelFlow"), std::string::npos);
}

TEST(VanillaFixtureParityTest, LevelOnePreservesAuthoredMissionResultExpressions) {
    const std::filesystem::path objects_path = VanillaFile("objects.qvm");
    if (VanillaRoot().empty()) {
        GTEST_SKIP() << "Set IGI_VANILLA_ROOT to the vanilla Project IGI install "
                        "to run this fixture parity test";
    }
    ASSERT_FALSE(objects_path.empty()) << "Vanilla objects.qvm is missing below "
                                          "IGI_VANILLA_ROOT";

    const QVMFile objects = QVM_Parse(objects_path.string());
    ASSERT_TRUE(objects.valid) << objects.error;

    // Level 1 completes through the authored cutscene and fails through the
    // authored status-message expression; it does not use an extraction zone.
    EXPECT_TRUE(ContainsText(objects, "CutScene_1204.isFinished"));
    EXPECT_TRUE(ContainsText(objects, "StatusMessage_1391.isSendt"));
    EXPECT_TRUE(ContainsText(
        objects,
        "HumanPlayer_0.isDead ||\nCar_1099.isExploded ||\nHumanSoldier_4023.isDead"));
    EXPECT_TRUE(ContainsText(objects, "MISSION_FAILED"));
}

TEST(VanillaFixtureParityTest, LevelOneDecompiledTasksDriveMissionLoaders) {
    const std::filesystem::path objects_path = VanillaFile("objects.qvm");
    if (VanillaRoot().empty()) {
        GTEST_SKIP() << "Set IGI_VANILLA_ROOT to the vanilla Project IGI install "
                        "to run this fixture parity test";
    }
    ASSERT_FALSE(objects_path.empty()) << "Vanilla objects.qvm is missing below "
                                          "IGI_VANILLA_ROOT";

    const QVMFile objects = QVM_Parse(objects_path.string());
    ASSERT_TRUE(objects.valid) << objects.error;
    const std::string decompiled_source = QVM_DecompileToString(objects);
    ASSERT_FALSE(decompiled_source.empty());

    const qsc::LexResult lexed_source = qsc::Lex(decompiled_source);
    ASSERT_TRUE(lexed_source.ok) << lexed_source.error;
    const qsc::ParseResult parsed_source = qsc::Parse(lexed_source.tokens);
    ASSERT_TRUE(parsed_source.ok) << parsed_source.error;
    ASSERT_NE(parsed_source.program, nullptr);

    std::vector<igi::MissionObjectiveTaskSource> objective_sources;
    std::vector<igi::MissionFlowTaskSource> flow_sources;
    CollectAuthoredTaskSources(
        *parsed_source.program,
        objective_sources,
        flow_sources);

    const std::vector<igi::AuthoredMissionObjectiveSet> objective_sets =
        igi::LoadAuthoredMissionObjectiveDefinitions(objective_sources);
    const std::vector<igi::AuthoredMissionFlowDefinition> flow_definitions =
        igi::LoadAuthoredMissionFlowDefinitions(flow_sources);

    ASSERT_FALSE(objective_sets.empty());
    ASSERT_FALSE(objective_sets.back().objectives.empty());
    ASSERT_FALSE(flow_definitions.empty());
    EXPECT_TRUE(flow_definitions.back().complete_expression.find(
        "CutScene_1204.isFinished") != std::string::npos);
    EXPECT_TRUE(flow_definitions.back().failure_expression.find(
        "StatusMessage_1391.isSendt") != std::string::npos);
    EXPECT_NE(decompiled_source.find(
        "Task_New(2405, \"PatrolPath\""), std::string::npos);
    EXPECT_NE(decompiled_source.find(
        "Task_New(-1, \"PatrolPathCommand\", \"Walks to node id 106\", 2, 106)"),
              std::string::npos);
}

TEST(VanillaFixtureParityTest, LevelOneGraphProvidesNavigableAuthoredData) {
    const std::filesystem::path graph_path = VanillaFile("graphs/graph1.dat");
    if (VanillaRoot().empty()) {
        GTEST_SKIP() << "Set IGI_VANILLA_ROOT to the vanilla Project IGI install "
                        "to run this fixture parity test";
    }
    ASSERT_FALSE(graph_path.empty()) << "Vanilla graph1.dat is missing below "
                                         "IGI_VANILLA_ROOT";

    const GraphFile graph = GRAPH_Parse(graph_path.string());
    ASSERT_TRUE(graph.valid) << graph.error;
    ASSERT_GT(graph.max_nodes, 0);
    ASSERT_FALSE(graph.nodes.empty());
    EXPECT_EQ(graph.route_table.size(),
              static_cast<size_t>(graph.max_nodes) * graph.max_nodes);

    for (const GraphNode& node : graph.nodes) {
        EXPECT_GE(node.id, 0);
        EXPECT_TRUE(std::isfinite(node.x));
        EXPECT_TRUE(std::isfinite(node.y));
        EXPECT_TRUE(std::isfinite(node.z));
    }
}

TEST(VanillaFixtureParityTest, RetailAiScriptDrivesPatrolAndAlarmBindings) {
    const std::filesystem::path ai_script_path = VanillaFile("ai/2205.qvm");
    if (VanillaRoot().empty()) {
        GTEST_SKIP() << "Set IGI_VANILLA_ROOT to the vanilla Project IGI install "
                        "to run this fixture parity test";
    }
    ASSERT_FALSE(ai_script_path.empty()) << "Vanilla AI 2205 QVM is missing below "
                                            "IGI_VANILLA_ROOT";

    const QVMFile ai_script = QVM_Parse(ai_script_path.string());
    ASSERT_TRUE(ai_script.valid) << ai_script.error;
    EXPECT_EQ(igi::FindFirstCallIntegerArgument(ai_script, "AIAction_Patrol"), 2405);

    igi::QvmNativeRegistry registry;
    igi::AiScriptHost script_host(registry);
    igi::QvmProgram program;
    ASSERT_TRUE(script_host.LoadProgram(ai_script, program))
        << script_host.GetLastError();

    igi::AiGuardEntity guard;
    guard.patrol_routes[2405] = {
        igi::AiPatrolCommand{igi::AiPatrolCommandKind::Delay, 3},
    };
    ASSERT_TRUE(script_host.Run(program, guard, 4))
        << script_host.GetLastError();
    EXPECT_EQ(guard.script_patrol_path_id, 2405);
    EXPECT_EQ(guard.active_patrol_path_id, 2405);
    ASSERT_EQ(guard.patrol_commands.size(), 1U);
    EXPECT_EQ(guard.patrol_commands[0].kind, igi::AiPatrolCommandKind::Delay);

    guard.patrol_started = true;
    guard.command_index = 0;
    ASSERT_TRUE(script_host.Run(program, guard, 4))
        << script_host.GetLastError();
    EXPECT_EQ(guard.command_index, 0);

    ASSERT_TRUE(script_host.Run(program, guard, 0))
        << script_host.GetLastError();
    EXPECT_EQ(guard.script_alarm_control_id, 98);
}

TEST(VanillaFixtureParityTest, CommonPatrolScriptExposesAnimationArguments) {
    if (VanillaRoot().empty()) {
        GTEST_SKIP() << "Set IGI_VANILLA_ROOT to the vanilla Project IGI install "
                        "to run this fixture parity test";
    }

    const std::filesystem::path common_path =
        FindChildCaseInsensitive(VanillaRoot(), "common");
    const std::filesystem::path ai_path =
        FindChildCaseInsensitive(common_path, "ai");
    const std::filesystem::path patrol_path =
        FindChildCaseInsensitive(ai_path, "patrol.qvm");
    ASSERT_FALSE(patrol_path.empty()) << "Vanilla COMMON/AI/PATROL.QVM is missing";

    const QVMFile patrol_script = QVM_Parse(patrol_path.string());
    ASSERT_TRUE(patrol_script.valid) << patrol_script.error;
    const std::vector<int> animation_ids = igi::FindCallIntegerArguments(
        patrol_script,
        "AIAction_PlayAnimation",
        0);
    EXPECT_NE(std::find(animation_ids.begin(), animation_ids.end(), 39),
              animation_ids.end());
    EXPECT_NE(std::find(animation_ids.begin(), animation_ids.end(), 36),
              animation_ids.end());
}

TEST(VanillaFixtureParityTest, CommonPatrolScriptExecutesAnimationEventBinding) {
    if (VanillaRoot().empty()) {
        GTEST_SKIP() << "Set IGI_VANILLA_ROOT to the vanilla Project IGI install "
                        "to run this fixture parity test";
    }

    const std::filesystem::path common_path =
        FindChildCaseInsensitive(VanillaRoot(), "common");
    const std::filesystem::path ai_path =
        FindChildCaseInsensitive(common_path, "ai");
    const std::filesystem::path patrol_path =
        FindChildCaseInsensitive(ai_path, "patrol.qvm");
    ASSERT_FALSE(patrol_path.empty()) << "Vanilla COMMON/AI/PATROL.QVM is missing";

    const QVMFile patrol_script = QVM_Parse(patrol_path.string());
    ASSERT_TRUE(patrol_script.valid) << patrol_script.error;

    igi::QvmNativeRegistry registry;
    igi::AiScriptHost script_host(registry);
    igi::QvmProgram program;
    ASSERT_TRUE(script_host.LoadProgram(patrol_script, program))
        << script_host.GetLastError();

    igi::AiGuardEntity guard;
    guard.requested_animation = 39;
    ASSERT_TRUE(script_host.Run(program, guard, 3))
        << script_host.GetLastError();

    // Retail AIEVENT_ANIMATION stores the animation selected by the previous
    // action in slot zero, reads that slot back, then dispatches the dynamic
    // AIAction_PlayAnimation call. Verify the complete native binding chain.
    ASSERT_EQ(guard.script_integer_values.at(0), 39);
    EXPECT_EQ(guard.requested_animation, 39);
    EXPECT_EQ(guard.animation_request_serial, 1U);
}
