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
    EXPECT_EQ(flow.GetObjectiveDisplayText(), "Reach the extraction zone");
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
