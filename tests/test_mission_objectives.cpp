#include <gtest/gtest.h>

#include "../source/level_flow.h"
#include "../source/mission_expression.h"
#include "../source/mission_flow_loader.h"
#include "../source/mission_objective_loader.h"

#include <initializer_list>
#include <string>
#include <vector>

namespace {

igi::MissionObjectiveTaskSource MakeObjectiveTask(
    std::initializer_list<std::string> arguments) {
    igi::MissionObjectiveTaskSource source;
    source.task_type = "DefineComputerObjective";
    source.argument_tokens.assign(arguments.begin(), arguments.end());
    return source;
}

} // namespace

TEST(MissionObjectiveLoaderTest, ReadsAuthoredRowsAndPreservesExpressions) {
    const auto definitions = igi::LoadAuthoredMissionObjectiveDefinitions({
        MakeObjectiveTask({
            "104", "\"DefineComputerObjective\"", "\"\"", "\"\"",
            "\"M1_OBJ1\"", "1120", "\"EditVariable_105.nValue == 1\"", "\"\"",
            "\"M1_OBJ2\"", "1121", "\"Terminal_501.isHacked\"", "\"HumanPlayer_0.isDead\"",
            "\"\"", "-1", "\"\"", "\"\"",
        }),
    });

    ASSERT_EQ(definitions.size(), 1U);
    ASSERT_EQ(definitions[0].objectives.size(), 2U);
    EXPECT_EQ(definitions[0].valid_expression, "");
    EXPECT_EQ(definitions[0].objectives[0].text_resource, "M1_OBJ1");
    EXPECT_EQ(definitions[0].objectives[0].link_task_id, 1120);
    EXPECT_EQ(
        definitions[0].objectives[0].completion_expression,
        "EditVariable_105.nValue == 1");
    EXPECT_EQ(definitions[0].objectives[1].failure_expression, "HumanPlayer_0.isDead");
}

TEST(MissionObjectiveLoaderTest, IgnoresUnsupportedTaskTypesAndEmptyRows) {
    igi::MissionObjectiveTaskSource unsupported_task;
    unsupported_task.task_type = "StatusMessage";
    unsupported_task.argument_tokens = {"1", "\"StatusMessage\"", "\"text\""};

    const auto definitions = igi::LoadAuthoredMissionObjectiveDefinitions({
        unsupported_task,
        MakeObjectiveTask({
            "200", "\"DefineComputerObjective\"", "\"\"", "\"condition\"",
            "\"\"", "-1", "\"\"", "\"\"",
            "\"M2_OBJ1\"", "1200", "\"done\"", "\"failed\"",
        }),
    });

    ASSERT_EQ(definitions.size(), 1U);
    ASSERT_EQ(definitions[0].objectives.size(), 1U);
    EXPECT_EQ(definitions[0].valid_expression, "condition");
    EXPECT_EQ(definitions[0].objectives[0].text_resource, "M2_OBJ1");
}

TEST(LevelFlowTest, UsesAuthoredTextAndAdvancesAuthoredDefinition) {
    const std::vector<igi::AuthoredMissionObjectiveDefinition> first_definition = {
        {"M1_OBJ1", 1120, "complete", "failed"},
    };
    const std::vector<igi::AuthoredMissionObjectiveDefinition> second_definition = {
        {"M1_OBJ2", 1121, "complete-2", "failed-2"},
    };

    igi::AuthoredMissionObjectiveSet first_set;
    first_set.objectives = first_definition;
    igi::AuthoredMissionObjectiveSet second_set;
    second_set.objectives = second_definition;

    igi::LevelFlow flow;
    flow.InitializeMission(
        1,
        {first_set, second_set},
        [](const std::string& resource_key) {
            return resource_key == "M1_OBJ1"
                ? std::string("Steal the truck near the main entrance.")
                : std::string("Reach the extraction point.");
        });

    ASSERT_EQ(flow.GetObjectives().size(), 1U);
    EXPECT_EQ(flow.GetObjectiveDisplayText(), "Steal the truck near the main entrance.");
    EXPECT_TRUE(flow.CompleteFirstPendingPrimaryObjective());
    ASSERT_EQ(flow.GetObjectives().size(), 1U);
    EXPECT_EQ(flow.GetObjectiveDisplayText(), "Reach the extraction point.");
    EXPECT_EQ(flow.GetObjectives()[0].text_resource, "M1_OBJ2");
}

TEST(LevelFlowTest, ResolvesAuthoredObjectiveLinkPosition) {
    igi::AuthoredMissionObjectiveSet objective_set;
    objective_set.objectives.push_back({
        "M1_OBJ1",
        1120,
        "",
        "",
    });

    igi::LevelFlow flow;
    flow.InitializeMission(
        1,
        {objective_set},
        {},
        {},
        [](int32_t link_task_id, igi::MissionObjectiveLocation& location) {
            if (link_task_id != 1120) {
                return false;
            }
            location = {10.0, 20.0, 30.0};
            return true;
        });

    ASSERT_EQ(flow.GetObjectives().size(), 1U);
    EXPECT_TRUE(flow.GetObjectives()[0].has_location);
    EXPECT_EQ(flow.GetObjectives()[0].location.x, 10.0);
    EXPECT_EQ(flow.GetObjectives()[0].location.y, 20.0);
    EXPECT_EQ(flow.GetObjectives()[0].location.z, 30.0);
}

TEST(LevelFlowTest, EvaluatesPreservedCompletionExpressionAtFixedUpdateBoundary) {
    igi::AuthoredMissionObjectiveSet objective_set;
    objective_set.objectives.push_back({
        "M1_OBJ1",
        1120,
        "EditVariable_105.nValue == 1",
        "HumanPlayer_0.isDead",
    });

    igi::MissionExpressionState expression_state;
    expression_state.SetNumber("EditVariable_105.nValue", 1.0);

    igi::LevelFlow flow;
    flow.InitializeMission(1, {objective_set});
    flow.Update(
        true,
        false,
        [&expression_state](const std::string& expression) {
            bool result = false;
            return expression_state.TryEvaluate(expression, result) && result;
        });

    ASSERT_EQ(flow.GetObjectives().size(), 1U);
    EXPECT_EQ(flow.GetObjectives()[0].state, igi::ObjectiveState::Completed);
    // Nothing is pending, so the card shows the engine's composed objective line
    // (ComputerObjectives_Draw 0x469110, src/compmap/objectives.c:137-145). This
    // flow injects no resolver, so every key in that composition is unresolved
    // and renders the engine's "RM: <key>" marker (objectives.c:151) rather than
    // the raw key. The resolved form is pinned in
    // FinishedObjectiveSetShowsTheEnginesComposedStatusLine below.
    EXPECT_EQ(flow.GetObjectiveDisplayText(), "RM: OBJECTIVE 1: RM: COMPLETE");
}

// ===========================================================================
// The HUD objective card is the engine's text, never an editor string
// ===========================================================================

namespace {

// The engine's flat resource lookup, as the injected resolver sees it: the four
// constants ComputerObjectives_Draw composes from live in
// LANGUAGE:<lang>/objectives.res (OBJECTIVE / COMPLETE / INCOMPLETE / FAILED,
// src/compmap/objectives.c:137-143) and the objective's own text is the
// DefineComputerObjective `name` parameter resolved from the same archive
// (:148-149). MISSION_FAILED / MISSION_COMPLETE are NOT there -- they are
// StatusMessage text in LANGUAGE:<lang>/MESSAGES.RES.
std::string EngineTextResolver(const std::string& resource_key) {
    if (resource_key == "OBJECTIVE") return "Objective";
    if (resource_key == "COMPLETE") return "Complete";
    if (resource_key == "INCOMPLETE") return "Incomplete";
    if (resource_key == "FAILED") return "Failed!";
    if (resource_key == "M1_OBJ1") return "Steal the truck near the main entrance.";
    if (resource_key == "MISSION_FAILED") return "Mission Failed!";
    if (resource_key == "MISSION_COMPLETE") return "Mission Complete!";
    return "RM: " + resource_key;
}

} // namespace

TEST(LevelFlowTest, FinishedObjectiveSetShowsTheEnginesComposedStatusLine) {
    // The card used to fall back to the invented string "Reach the extraction
    // zone". The engine has no such string: it draws one line per objective,
    // composed as "<OBJECTIVE> <n>: " plus the state word (ComputerObjectives_Draw
    // 0x469110, src/compmap/objectives.c:137-145), so that composition is what the
    // card shows once the active set is finished.
    igi::AuthoredMissionObjectiveSet objective_set;
    objective_set.objectives.push_back({"M1_OBJ1", 1120, "done", ""});

    igi::LevelFlow flow;
    flow.InitializeMission(1, {objective_set}, EngineTextResolver);
    ASSERT_EQ(flow.GetObjectiveDisplayText(),
              "Steal the truck near the main entrance.");

    flow.Update(true, false, [](const std::string& expression) {
        return expression == "done";
    });

    ASSERT_EQ(flow.GetObjectives().size(), 1U);
    EXPECT_EQ(flow.GetObjectives()[0].state, igi::ObjectiveState::Completed);
    // Resource_Load("OBJECTIVE") + the 1-based slot + Resource_Load("COMPLETE").
    EXPECT_EQ(flow.GetObjectiveDisplayText(), "Objective 1: Complete");
}

TEST(LevelFlowTest, PendingObjectiveMissRendersTheEngineMissMarker) {
    // The objective's `text` parameter is a bare resource name and the engine
    // resolves it flat before drawing it (ComputerObjectives_Draw 0x469110,
    // src/compmap/objectives.c:148-149), rendering "RM: <key>" -- never the raw
    // key -- when no mounted archive defines it (:151). That holds on the pending
    // path too, both when the resolver misses and when none was injected at all.
    igi::AuthoredMissionObjectiveSet objective_set;
    objective_set.objectives.push_back({"M1_OBJ1", 1120, "", ""});

    igi::LevelFlow resolving_flow;
    resolving_flow.InitializeMission(
        1, {objective_set}, [](const std::string& key) {
            return "RM: " + key;   // the archive does not define M1_OBJ1
        });
    ASSERT_EQ(resolving_flow.GetObjectives().size(), 1U);
    EXPECT_EQ(resolving_flow.GetObjectives()[0].text_resource, "M1_OBJ1");
    EXPECT_EQ(resolving_flow.GetObjectiveDisplayText(), "RM: M1_OBJ1");

    igi::LevelFlow unresolved_flow;
    unresolved_flow.InitializeMission(1, {objective_set});
    ASSERT_EQ(unresolved_flow.GetObjectives().size(), 1U);
    EXPECT_EQ(unresolved_flow.GetObjectiveDisplayText(), "RM: M1_OBJ1");
}

TEST(LevelFlowTest, MissionFailureShowsTheEngineStatusMessageText) {
    // Retail sends a StatusMessage whose text parameter is the bare key
    // MISSION_FAILED and wires LevelFlow's failure expression to it
    // (MISSIONS/location0/level1/objects.qsc:1208, :1456). The engine resolves
    // that key flat from LANGUAGE:<lang>/MESSAGES.RES
    // (StatusMessageScreen_AddMessage 0x486650, src/mission/statusmessage.c:653-660;
    // StatusMessages_Load 0x4868A0, :671).
    igi::AuthoredMissionObjectiveSet objective_set;
    objective_set.objectives.push_back({"M1_OBJ1", 1120, "", ""});

    igi::LevelFlow flow;
    flow.InitializeMission(1, {objective_set}, EngineTextResolver);

    flow.Update(false, false);
    ASSERT_EQ(flow.GetStatus(), igi::MissionStatus::Failed);
    EXPECT_EQ(flow.GetObjectiveDisplayText(), "Mission Failed!");
}

TEST(LevelFlowTest, MissionCompletionShowsTheEngineStatusMessageText) {
    // LEVEL14's OBJECTS.QVM carries the mirrored pair: a StatusMessage named
    // "Mission Complete" with text "MISSION_COMPLETE" and one named
    // "Mission Failed" with text "MISSION_FAILED". Both are messages.res keys.
    igi::AuthoredMissionObjectiveSet objective_set;
    objective_set.objectives.push_back({"M1_OBJ1", 1120, "done", ""});

    igi::LevelFlow flow;
    flow.InitializeMission(1, {objective_set}, EngineTextResolver);
    flow.Update(true, true, [](const std::string& expression) {
        return expression == "done";
    });

    ASSERT_EQ(flow.GetStatus(), igi::MissionStatus::Success);
    EXPECT_EQ(flow.GetObjectiveDisplayText(), "Mission Complete!");
}

TEST(LevelFlowTest, MissionStatusTextFallsBackToTheEngineMissMarker) {
    // A key no mounted archive defines renders "RM: <key>" in the engine, never
    // the raw key and never an editor string (statusmessage.c:660,
    // objectives.c:151).
    igi::AuthoredMissionObjectiveSet objective_set;
    objective_set.objectives.push_back({"M1_OBJ1", 1120, "", ""});

    igi::LevelFlow flow;
    flow.InitializeMission(1, {objective_set}, [](const std::string& key) {
        return "RM: " + key;   // every key misses
    });

    flow.Update(false, false);
    ASSERT_EQ(flow.GetStatus(), igi::MissionStatus::Failed);
    EXPECT_EQ(flow.GetObjectiveDisplayText(), "RM: MISSION_FAILED");
}

TEST(LevelFlowTest, SelectsLastAuthoredDefinitionWithValidExpression) {
    igi::AuthoredMissionObjectiveSet inactive_definition;
    inactive_definition.valid_expression = "EditVariable_1.nValue == 0";
    inactive_definition.objectives.push_back({
        "M3_OBJ1", 100, "", ""});

    igi::AuthoredMissionObjectiveSet active_definition;
    active_definition.valid_expression = "EditVariable_1.nValue == 1";
    active_definition.objectives.push_back({
        "M3_OBJ2", 101, "", ""});

    igi::AuthoredMissionObjectiveSet later_active_definition;
    later_active_definition.valid_expression = "EditVariable_1.nValue == 1";
    later_active_definition.objectives.push_back({
        "M3_OBJ3", 102, "", ""});

    igi::MissionExpressionState expression_state;
    expression_state.SetNumber("EditVariable_1.nValue", 1.0);

    igi::LevelFlow flow;
    flow.InitializeMission(
        3,
        {inactive_definition, active_definition, later_active_definition});
    flow.Update(
        true,
        false,
        [&expression_state](const std::string& expression) {
            bool result = false;
            return expression_state.TryEvaluate(expression, result) && result;
        });

    ASSERT_EQ(flow.GetObjectives().size(), 1U);
    EXPECT_EQ(flow.GetObjectives()[0].text_resource, "M3_OBJ3");
}

TEST(MissionFlowLoaderTest, ReadsAuthoredCompletionAndFailureFields) {
    igi::MissionFlowTaskSource source;
    source.task_type = "LevelFlow";
    source.argument_tokens = {
        "10", "\"LevelFlow\"", "\"\"",
        "0", "0", "0", "0", "0", "0", "12",
        "CutScene_700.isFinished",
        "StatusMessage_4022.nTickSendt > 3*GAME_FREQUENCY",
        "FALSE", "1200",
    };

    const auto definitions = igi::LoadAuthoredMissionFlowDefinitions({source});

    ASSERT_EQ(definitions.size(), 1U);
    EXPECT_EQ(definitions[0].start_time_seconds, 12.0);
    EXPECT_EQ(definitions[0].complete_expression, "CutScene_700.isFinished");
    EXPECT_EQ(
        definitions[0].failure_expression,
        "StatusMessage_4022.nTickSendt > 3*GAME_FREQUENCY");
    EXPECT_FALSE(definitions[0].interface_timer_enabled);
    EXPECT_EQ(definitions[0].maximum_level_play_time_seconds, 1200.0);
}

TEST(LevelFlowTest, AuthoredFlowOwnsMissionResultOverExtractionFallback) {
    const std::vector<igi::AuthoredMissionObjectiveSet> objective_sets;
    const igi::AuthoredMissionFlowDefinition authored_flow = {
        0.0,
        "MissionComplete",
        "MissionFailed",
        false,
        0.0,
    };

    igi::MissionExpressionState expression_state;
    igi::LevelFlow flow;
    flow.InitializeMission(1, objective_sets, {}, authored_flow);
    flow.SetObjectiveState(1, igi::ObjectiveState::Completed);

    expression_state.SetBoolean("MissionComplete", false);
    flow.Update(
        true,
        true,
        [&expression_state](const std::string& expression) {
            bool result = false;
            return expression_state.TryEvaluate(expression, result) && result;
        });
    EXPECT_EQ(flow.GetStatus(), igi::MissionStatus::InProgress);

    expression_state.SetBoolean("MissionComplete", true);
    flow.Update(
        true,
        false,
        [&expression_state](const std::string& expression) {
            bool result = false;
            return expression_state.TryEvaluate(expression, result) && result;
        });
    EXPECT_EQ(flow.GetStatus(), igi::MissionStatus::Success);

    igi::LevelFlow failed_flow;
    failed_flow.InitializeMission(1, objective_sets, {}, authored_flow);
    expression_state.SetBoolean("MissionComplete", true);
    expression_state.SetBoolean("MissionFailed", true);
    failed_flow.Update(
        true,
        false,
        [&expression_state](const std::string& expression) {
            bool result = false;
            return expression_state.TryEvaluate(expression, result) && result;
        });
    EXPECT_EQ(failed_flow.GetStatus(), igi::MissionStatus::Failed);
}
