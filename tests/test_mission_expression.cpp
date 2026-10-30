#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include "../source/game_clock.h"
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

// Issue #54: the authored GAME_FREQUENCY and the simulation clock's tick rate
// are one number, not two literals that happen to match.
//
// Oracle: Game_Init registers Script_RegisterConstant("GAME_FREQUENCY", 30)
// (src/game/game.c:262), and the engine counts every frame-duration at that
// rate -- #define FPS 30.0f (src/humanstate/humanstate.c:91) feeding
// #define FRAMES(seconds) x87_fistp((seconds) * FPS)
// (src/humanstate/humanstate.c:520), which times the picklock/search/climb
// actions (humanstate.c:641,726,789,1010,1117,1308).
//
// Before this test the evaluator held its own 30.0 (mission_expression.cpp) and
// the clock held TICK_RATE_HZ = 30 (game_clock.h): changing either one alone
// left the other behind and no test failed. This one does.
TEST(MissionExpressionTest, GameFrequencyConstantIsTheSimulationTickRate) {
    igi::MissionExpressionState state;

    double frequency = 0.0;
    ASSERT_TRUE(state.TryEvaluateNumber("GAME_FREQUENCY", frequency));
    // The value retail registers ...
    EXPECT_DOUBLE_EQ(frequency, 30.0);
    // ... is the rate the simulation clock ticks at.
    EXPECT_DOUBLE_EQ(frequency, static_cast<double>(igi::GameClock::TICK_RATE_HZ));
}

// The same link, behaviourally: the shipped authored shape is
// "<frame count> > <seconds>*GAME_FREQUENCY" (e.g. "LevelTimer_95.nTick >
// 60*GAME_FREQUENCY" in the retail level timers). A two-second threshold must
// therefore flip on the first tick whose count exceeds 2*TICK_RATE_HZ, so the
// tick index at which it flips is fixed by whichever of the two numbers moves.
TEST(MissionExpressionTest, AuthoredTwoSecondThresholdFiresOnTheClockTickBoundary) {
    igi::MissionExpressionState state;
    igi::GameClock clock;
    clock.Reset(0);
    clock.Update(0);

    const int64_t limit = 4 * static_cast<int64_t>(igi::GameClock::TICK_RATE_HZ);
    int64_t first_true_tick = -1;
    for (int64_t tick = 0; tick < limit && first_true_tick < 0; ++tick) {
        clock.Update(clock.GetDueMilliseconds() + 1);
        ASSERT_TRUE(clock.IsTickDue());
        clock.CompleteTick();
        clock.CompleteRender();

        state.SetNumber(
            "LevelTimer_95.nTick",
            static_cast<double>(clock.GetTickCount()));
        bool result = false;
        ASSERT_TRUE(state.TryEvaluate(
            "LevelTimer_95.nTick > 2*GAME_FREQUENCY",
            result));
        if (result) {
            first_true_tick = static_cast<int64_t>(clock.GetTickCount());
        }
    }

    EXPECT_EQ(
        first_true_tick,
        2 * static_cast<int64_t>(igi::GameClock::TICK_RATE_HZ) + 1);
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

// Issue #56: the shipped authored conditions that CALL a script function.
//
// Oracle for the one call the corpus makes with a fully specified, input-only
// result: LevelFlow_GetBreakCutSceneKey HOOK 0x488E00
// (src/levelflow/levelflow.c:73-82) returns `gKeys[1] & 0x80` -- 128 while
// Escape is held, 0 while it is not. gKeys is the 256-byte DirectInput keyboard
// state (src/win/winvars.h:13) filled by Keyboard_Update HOOK 0x490230
// (src/win/input.c:296-315); index 1 is DIK_ESCAPE, which the engine itself
// spells `#define KEY_ESCAPE 1` (src/widget/controlsmenu.c:64) and
// `#define KEY_ESCAPE 0x01` (src/game/introscreen.c:81), both masked with 0x80.
//
// Before this the grammar had no call production at all, so the string below
// failed to parse and every consumer read that as "condition not met": the
// authored container was inert and silent.
TEST(MissionExpressionTest, ParsesTheShippedBreakCutSceneCondition) {
    igi::MissionExpressionState state;
    state.SetNumber("EditVariable_4004.nValue", 0.0);

    // Byte for byte out of the shipped MISSIONS/location0/level1/OBJECTS.QVM.
    const std::string shipped =
        "EditVariable_4004.nValue == 0 && LevelFlow_GetBreakCutSceneKey()";

    bool result = true;
    ASSERT_TRUE(state.TryEvaluate(shipped, result))
        << "the shipped condition must parse";
    EXPECT_FALSE(result); // Escape is not held.

    state.SetBreakCutSceneKeyHeld(true);
    ASSERT_TRUE(state.TryEvaluate(shipped, result));
    EXPECT_TRUE(result); // Escape is held.

    // The EditVariable half still gates it.
    state.SetNumber("EditVariable_4004.nValue", 1.0);
    ASSERT_TRUE(state.TryEvaluate(shipped, result));
    EXPECT_FALSE(result);

    EXPECT_TRUE(state.GetUnparsedExpressions().empty());
}

TEST(MissionExpressionTest, BreakCutSceneKeyResolvesToTheMaskedDirectInputByte) {
    igi::MissionExpressionState state;

    double value = -1.0;
    ASSERT_TRUE(state.TryEvaluateNumber("LevelFlow_GetBreakCutSceneKey()", value));
    EXPECT_DOUBLE_EQ(value, 0.0);

    state.SetBreakCutSceneKeyHeld(true);
    ASSERT_TRUE(state.TryEvaluateNumber("LevelFlow_GetBreakCutSceneKey()", value));
    // `gKeys[1] & 0x80`, not the 1 the engine's own comment claims.
    EXPECT_DOUBLE_EQ(value, 128.0);
}

TEST(MissionExpressionTest, AcceptsOnlyTheZeroArgumentCallForm) {
    igi::MissionExpressionState state;
    bool result = true;

    // Retail registers the function with argc 0
    // (Script_RegisterFunction(..., NULL, 0), src/levelflow/levelflow.c:66), so
    // an argument list is not a call the script compiler accepts, and a bare
    // name is a variable lookup that fails.
    EXPECT_FALSE(state.TryEvaluate("LevelFlow_GetBreakCutSceneKey(1)", result));
    EXPECT_FALSE(state.TryEvaluate("LevelFlow_GetBreakCutSceneKey", result));
}

TEST(MissionExpressionTest, ReportsEachUnparsedAuthoredConditionOnce) {
    igi::MissionExpressionState state;
    std::vector<std::pair<LogLevel, std::string>> reported;
    state.SetLogSink([&reported](LogLevel level, const std::string& message) {
        reported.emplace_back(level, message);
    });

    bool result = true;
    const std::string unparsed =
        "MenuManager_SetEnabled(FALSE) && Game_DisableMusic()";

    // Semantics unchanged: an unparseable expression still fails the call, so
    // every consumer keeps reading it as "condition not met".
    EXPECT_FALSE(state.TryEvaluate(unparsed, result));
    EXPECT_FALSE(state.TryEvaluate(unparsed, result));
    EXPECT_FALSE(state.TryEvaluate(unparsed, result));

    // ... but it is no longer silent, and it is reported once per distinct
    // string rather than once per simulation tick.
    ASSERT_EQ(reported.size(), 1u);
    EXPECT_EQ(reported[0].first, LogLevel::WARNING);
    EXPECT_NE(reported[0].second.find(unparsed), std::string::npos);
    ASSERT_EQ(state.GetUnparsedExpressions().size(), 1u);
    EXPECT_EQ(state.GetUnparsedExpressions()[0], unparsed);

    // A different expression is a different report ...
    EXPECT_FALSE(state.TryEvaluate("Game_EnableMusic()", result));
    EXPECT_EQ(reported.size(), 2u);
    EXPECT_EQ(state.GetUnparsedExpressions().size(), 2u);

    // ... and a new mission starts a new report.
    state.Clear();
    EXPECT_TRUE(state.GetUnparsedExpressions().empty());
    EXPECT_FALSE(state.TryEvaluate(unparsed, result));
    EXPECT_EQ(reported.size(), 3u);
}

// The limitation, written down rather than silent: these are the other script
// calls the shipped levels use, measured over
// MISSIONS/location0/LEVEL*/OBJECTS.QVM (3 + 2 + 1 + 1 call sites per level,
// fewer in LEVEL14; see mission_expression.h for the table).
//
// They stay unparsed because each mutates menu, music or cutscene state, so
// implementing one is a design decision rather than a mechanical port. If one
// is ever implemented, this test has to be updated deliberately -- which is the
// point of pinning it here.
TEST(MissionExpressionTest, KeepsTheOtherShippedCallFormsUnparsed) {
    igi::MissionExpressionState state;
    const std::vector<std::string> shipped_forms = {
        "MenuManager_SetEnabled(FALSE) && Game_DisableMusic()",
        "MenuManager_SetEnabled(TRUE) && Game_EnableMusic()",
        "MenuManager_SetEnabled(FALSE) && Game_DisableMusic() && "
        "Game_CutsceneDelete()",
    };

    for (const std::string& form : shipped_forms) {
        bool result = true;
        EXPECT_FALSE(state.TryEvaluate(form, result)) << form;
    }
    EXPECT_EQ(state.GetUnparsedExpressions().size(), shipped_forms.size());
}
