#include <gtest/gtest.h>

#include "../source/mission_expression.h"

TEST(MissionExpressionTest, EvaluatesVanillaObjectiveComparisonsAndConstants) {
    igi::MissionExpressionState state;
    state.SetNumber("EditVariable_105.nValue", 1.0);
    state.SetNumber("LevelTimer_95.nTick", 61.0 * 30.0);

    bool result = false;
    EXPECT_TRUE(state.TryEvaluate("EditVariable_105.nValue == 1", result));
    EXPECT_TRUE(result);
    EXPECT_TRUE(state.TryEvaluate("LevelTimer_95.nTick > 60*GAME_FREQUENCY", result));
    EXPECT_TRUE(result);
}

TEST(MissionExpressionTest, EvaluatesBooleanPrecedenceAndParentheses) {
    igi::MissionExpressionState state;
    state.SetBoolean("AlarmControl_98.isAlarm", false);
    state.SetBoolean("SCameraControl_91.isDetection", true);
    state.SetBoolean("Switch_94.isLastPressed", false);

    bool result = false;
    EXPECT_TRUE(state.TryEvaluate(
        "!AlarmControl_98.isAlarm && "
        "(SCameraControl_91.isDetection || Switch_94.isLastPressed)",
        result));
    EXPECT_TRUE(result);
}

TEST(MissionExpressionTest, RejectsUnknownVariablesAndMalformedExpressions) {
    igi::MissionExpressionState state;
    bool result = true;

    EXPECT_FALSE(state.TryEvaluate("Terminal_501.isHacked", result));
    EXPECT_FALSE(state.TryEvaluate("(1 == 1", result));
}

TEST(MissionExpressionTest, SupportsBooleanStateAsNumericComparisons) {
    igi::MissionExpressionState state;
    state.SetBoolean("Door_402.isOpen", true);

    bool result = false;
    EXPECT_TRUE(state.TryEvaluate("Door_402.isOpen == 1", result));
    EXPECT_TRUE(result);
    EXPECT_TRUE(state.TryEvaluate("Door_402.isOpen != 0", result));
    EXPECT_TRUE(result);
}

TEST(MissionExpressionTest, EvaluatesNumericValuesForEditVariables) {
    igi::MissionExpressionState state;
    state.SetNumber("EditVariable_105.nValue", 2.0);

    double result = 0.0;
    EXPECT_TRUE(state.TryEvaluateNumber(
        "EditVariable_105.nValue + 2 * 3",
        result));
    EXPECT_DOUBLE_EQ(result, 8.0);
}
