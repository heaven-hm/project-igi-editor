// test_alarm_light_rules.cpp - the retail "AlarmLight" task, pure-logic port
// (#43).
//
// The oracle for every number here is IGI-RE (read-only). Key citations:
//   fxobj/alarm.c:92-124        AlarmLight_Init 0x4397C0: the 8-parameter
//                               ParamTable and the three model slots
//   fxobj/alarm.c:126-151       AlarmLightReflector_Create 0x439A50: angle = 0
//   fxobj/alarm.c:154-193       AlarmLightReflector_Run 0x439B70: the turn
//   fxobj/alarm.c:197-205       AlarmLightReflector_Draw 0x439EC0
//   fxobj/alarm.c:208-244       AlarmLightReflector_DrawFlare 0x439F10: the
//                               facing test and the red triangle wave
//   elevator/alarmlight.c:16-42   AlarmLight_Create 0x43A0E0
//   elevator/alarmlight.c:44-69   AlarmLight_Run 0x43A270: the state machine
//   elevator/alarmlight.c:94-112  AlarmLight_Changed 0x43A450
//   elevator/alarmlight.c:122-131 AlarmLight_Destroy 0x43A5F0
//   elevator/elevatortypes.h:7-22 the struct layout
//   math/matrix.c:99-110          Matrix_SetEuler, i.e. RotZ * RotY * RotX
//   physobj/paramops.c:423-435    ParamReal32x9_Parse reads THREE Euler angles
//
// No retail level ships an AlarmLight in a decompiled objects.qsc, so the whole
// corpus half reads the shipping QVM bytecode: 16 instances across LEVEL6, 7, 8,
// 9 and 14, and the authored declaration itself.

#include "../source/level/qvm_parser.h"
#include "../source/level/task_schema.h"
#include "../source/runtime/alarm_light.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

fs::path GameRoot() {
    if (const char* env = std::getenv("IGI_GAME_PATH"); env != nullptr && *env != '\0')
        return fs::path(env);
    return fs::path("/Users/haseeb-mir/Downloads/IGI");
}

fs::path LevelDir(int level) {
    return GameRoot() / "MISSIONS" / "location0" / ("LEVEL" + std::to_string(level));
}

// ---------------------------------------------------------------------------
// QVM bytecode -> AlarmLight arg tokens
// ---------------------------------------------------------------------------

bool IsStringPush(QVMOpType type) {
    return type == QVMOpType::PUSHS || type == QVMOpType::PUSHSI ||
           type == QVMOpType::PUSHSIB || type == QVMOpType::PUSHSIW;
}

bool IsIntPush(QVMOpType type) {
    return type == QVMOpType::PUSHI || type == QVMOpType::PUSHII ||
           type == QVMOpType::PUSHIIB || type == QVMOpType::PUSHIIW ||
           type == QVMOpType::PUSHB || type == QVMOpType::PUSHW;
}

bool IsIdentifierPush(QVMOpType type) {
    return type == QVMOpType::PUSHII || type == QVMOpType::PUSHIIB ||
           type == QVMOpType::PUSHIIW;
}

std::string FormatFloat(float value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.9g", value);
    return buffer;
}

std::vector<std::string> ReadAlarmLightCall(const QVMFile& qvm, std::size_t type_index) {
    std::vector<std::string> tokens;
    tokens.push_back("-1");
    tokens.push_back("AlarmLight");

    const std::size_t kExpected =
        2 + 1 + static_cast<std::size_t>(igi::kAlarmLightTokenCount);

    for (std::size_t i = type_index + 1; i < qvm.instructions.size() && tokens.size() < kExpected; ++i) {
        const QVMInstruction& in = qvm.instructions[i];
        if (in.type == QVMOpType::BRK) continue;
        if (IsStringPush(in.type)) {
            tokens.push_back(in.operand < qvm.strings.size() ? qvm.strings[in.operand]
                                                             : std::string());
            continue;
        }
        if (IsIntPush(in.type)) {
            if (IsIdentifierPush(in.type) && in.operand < qvm.identifiers.size()) {
                tokens.push_back(qvm.identifiers[in.operand]);
            } else {
                tokens.push_back(std::to_string(static_cast<int>(in.operand)));
            }
            continue;
        }
        if (in.type == QVMOpType::PUSH0) { tokens.push_back("0"); continue; }
        if (in.type == QVMOpType::PUSH1) { tokens.push_back("1"); continue; }
        if (in.type == QVMOpType::PUSHF) { tokens.push_back(FormatFloat(in.operand_float)); continue; }
        if (in.type == QVMOpType::MINUS) {
            if (!tokens.empty()) tokens.back() = "-" + tokens.back();
            continue;
        }
        break;
    }
    return tokens;
}

std::vector<std::vector<std::string>> ExtractAlarmLightCalls(const QVMFile& qvm) {
    std::vector<std::vector<std::string>> out;
    for (std::size_t i = 0; i < qvm.instructions.size(); ++i) {
        const QVMInstruction& in = qvm.instructions[i];
        if (!IsStringPush(in.type)) continue;
        if (in.operand >= qvm.strings.size()) continue;
        if (qvm.strings[in.operand] != "AlarmLight") continue;

        std::size_t name = i + 1;
        while (name < qvm.instructions.size() && qvm.instructions[name].type == QVMOpType::BRK) ++name;
        if (name >= qvm.instructions.size() || !IsStringPush(qvm.instructions[name].type)) continue;

        std::size_t first = name + 1;
        while (first < qvm.instructions.size() && qvm.instructions[first].type == QVMOpType::BRK) ++first;
        if (first >= qvm.instructions.size() || IsStringPush(qvm.instructions[first].type)) continue;

        out.push_back(ReadAlarmLightCall(qvm, i));
    }
    return out;
}

// The alternating name/type strings a `Task_DeclareParameters("AlarmLight", ...)`
// call declares.
std::vector<std::string> DeclaredAlarmLightParameters(const QVMFile& qvm) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < qvm.instructions.size(); ++i) {
        const QVMInstruction& in = qvm.instructions[i];
        if (!IsIdentifierPush(in.type)) continue;
        if (in.operand >= qvm.identifiers.size()) continue;
        if (qvm.identifiers[in.operand] != "Task_DeclareParameters") continue;

        std::size_t j = i + 1;
        if (j < qvm.instructions.size() && qvm.instructions[j].type == QVMOpType::CALL) ++j;
        if (j < qvm.instructions.size() && qvm.instructions[j].type == QVMOpType::BRA) ++j;
        std::vector<std::string> pushed;
        for (; j < qvm.instructions.size() && qvm.instructions[j].type != QVMOpType::POP; ++j) {
            if (!IsStringPush(qvm.instructions[j].type)) continue;
            const std::size_t operand = qvm.instructions[j].operand;
            pushed.push_back(operand < qvm.strings.size() ? qvm.strings[operand] : std::string());
        }
        if (pushed.size() >= 3 && pushed[0] == "AlarmLight") {
            out.assign(pushed.begin() + 1, pushed.end());
            return out;
        }
    }
    return out;
}

// The LEVEL9 authored instance, which is the one used as the worked example.
igi::AlarmLightParams Level9Params() {
    igi::AlarmLightParams params;
    params.place[0] = 23470464.0;
    params.place[1] = -51103984.0;
    params.place[2] = 174511488.0;
    params.orientation[0] = 0.0f;
    params.orientation[1] = 0.0f;
    params.orientation[2] = 3.1415929794311523f;
    igi::AlarmLightEulerMatrix(params.orientation, params.matrix);
    params.on_expression = "AlarmControl_98.isAlarm";
    params.on_model = "344_03_1";
    params.off_model = "344_01_1";
    params.destroyed_model.clear();
    params.on_sound.clear();
    params.explode_sound.clear();
    return params;
}

}  // namespace

// ===========================================================================
// The authored parameter table
// ===========================================================================

TEST(AlarmLightRules, ParamTableShape) {
    // alarm.c:108 ParamTable_New(8), and alarm.c:109-115 register them: two
    // three-argument types (ObjectPos and Real32x9) and six one-argument ones,
    // for 12 authored tokens.
    const igi::AlarmLightParamDesc* table = igi::AlarmLightParamTable();
    ASSERT_NE(table, nullptr);

    int tokens = 0;
    int model_slots = 0;
    for (int i = 0; i < igi::kAlarmLightParamCount; ++i) {
        tokens += table[i].token_count;
        if (table[i].model_slot >= 0) ++model_slots;
    }
    EXPECT_EQ(tokens, igi::kAlarmLightTokenCount);
    EXPECT_EQ(tokens, 12);
    EXPECT_EQ(model_slots, 3);
    EXPECT_EQ(igi::kAlarmLightFirstParam, 3u);
}

TEST(AlarmLightRules, ParamTableNamesAreTheRetailDeclaration) {
    // The retail Task_DeclareParameters call, verbatim, including its typo.
    const igi::AlarmLightParamDesc* table = igi::AlarmLightParamTable();
    const char* kNames[igi::kAlarmLightParamCount] = {
        "Position", "Orientation", "On expression", "On model", "Off model",
        "Destroyde model", "On sound", "Explode sound",
    };
    const char* kTypes[igi::kAlarmLightParamCount] = {
        "ObjectPos", "Real32x9", "VarString", "String16", "String16",
        "String16", "String16", "String16",
    };
    for (int i = 0; i < igi::kAlarmLightParamCount; ++i) {
        EXPECT_STREQ(table[i].authored_name, kNames[i]) << "slot " << i;
        EXPECT_STREQ(table[i].type_name, kTypes[i]) << "slot " << i;
    }
}

TEST(AlarmLightRules, ModelSlotsAreTheThreeConsecutiveStructFields) {
    // alarm.c:112-113 registers `l.offModel + 0x10 * i` for i = 0..2, i.e. the
    // three 16-byte model fields at offsets 0x00, 0x10 and 0x20
    // (elevatortypes.h:11-13).
    const igi::AlarmLightParamDesc* table = igi::AlarmLightParamTable();
    EXPECT_EQ(table[3].model_slot, igi::kAlarmLightModelSlotA);
    EXPECT_EQ(table[4].model_slot, igi::kAlarmLightModelSlotB);
    EXPECT_EQ(table[5].model_slot, igi::kAlarmLightModelSlotC);
    EXPECT_EQ(table[2].model_slot, -1);  // the expression is not a model
    EXPECT_EQ(table[6].model_slot, -1);  // nor are the two sounds

    const igi::AlarmLightParams params = Level9Params();
    EXPECT_EQ(params.ModelForSlot(igi::kAlarmLightModelSlotA), "344_03_1");
    EXPECT_EQ(params.ModelForSlot(igi::kAlarmLightModelSlotB), "344_01_1");
    EXPECT_EQ(params.ModelForSlot(igi::kAlarmLightModelSlotC), "");
    EXPECT_EQ(params.ModelForSlot(99), "");
}

TEST(AlarmLightRules, OnModelSlotIsTheOffTransitionAndViceVersa) {
    // The authored "On model" is ParamTable slot 3, which writes struct offset
    // 0x00 (alarm.c:112-113 i=0); the authored "Off model" is slot 4, which
    // writes offset 0x10. AlarmLight_Run installs offset 0x00 on the ON -> OFF
    // transition and offset 0x10 on the OFF -> ON transition
    // (elevator/alarmlight.c:59-64). The port reproduces the positional
    // mapping, which means the value the level calls "On model" is the model
    // shown when the lamp turns OFF.
    EXPECT_EQ(igi::kAlarmLightModelSlotWhenTurningOn, igi::kAlarmLightModelSlotB);
    EXPECT_EQ(igi::kAlarmLightModelSlotWhenTurningOff, igi::kAlarmLightModelSlotA);
    EXPECT_EQ(igi::kAlarmLightModelSlotOnChange, igi::kAlarmLightModelSlotB);
    EXPECT_EQ(igi::kAlarmLightModelSlotWhenDestroyed, igi::kAlarmLightModelSlotC);

    const igi::AlarmLightParams params = Level9Params();
    // Turning the lamp ON installs the authored "Off model".
    EXPECT_EQ(params.ModelForSlot(igi::kAlarmLightModelSlotWhenTurningOn), params.off_model);
    // Turning it OFF installs the authored "On model".
    EXPECT_EQ(params.ModelForSlot(igi::kAlarmLightModelSlotWhenTurningOff), params.on_model);
}

// ===========================================================================
// Parsing the authored call
// ===========================================================================

TEST(AlarmLightRules, ParseReadsEveryAuthoredParameter) {
    const std::vector<std::string> tokens = {
        "-1", "AlarmLight", "",
        "23470464", "-51103984", "174511488",           // Position
        "0", "0", "3.1415929794311523",                  // Orientation
        "AlarmControl_98.isAlarm",                       // On expression
        "344_03_1",                                      // On model
        "344_01_1",                                      // Off model
        "",                                              // Destroyde model
        "",                                              // On sound
        "",                                              // Explode sound
    };
    ASSERT_EQ(tokens.size(), igi::kAlarmLightFirstParam + igi::kAlarmLightTokenCount);

    igi::AlarmLightParams params;
    ASSERT_TRUE(igi::ParseAlarmLightParams(tokens, &params));
    EXPECT_DOUBLE_EQ(params.place[0], 23470464.0);
    EXPECT_DOUBLE_EQ(params.place[1], -51103984.0);
    EXPECT_DOUBLE_EQ(params.place[2], 174511488.0);
    EXPECT_FLOAT_EQ(params.orientation[0], 0.0f);
    EXPECT_FLOAT_EQ(params.orientation[1], 0.0f);
    EXPECT_FLOAT_EQ(params.orientation[2], 3.1415929794311523f);
    EXPECT_EQ(params.on_expression, "AlarmControl_98.isAlarm");
    EXPECT_EQ(params.on_model, "344_03_1");
    EXPECT_EQ(params.off_model, "344_01_1");
    EXPECT_TRUE(params.destroyed_model.empty());
    EXPECT_TRUE(params.on_sound.empty());
    EXPECT_TRUE(params.explode_sound.empty());
}

TEST(AlarmLightRules, ParseRejectsATruncatedCall) {
    std::vector<std::string> tokens(igi::kAlarmLightFirstParam + igi::kAlarmLightTokenCount - 1, "");
    tokens[0] = "-1";
    tokens[1] = "AlarmLight";
    igi::AlarmLightParams params;
    EXPECT_FALSE(igi::ParseAlarmLightParams(tokens, &params));
    EXPECT_TRUE(params.on_model.empty());
}

TEST(AlarmLightRules, ResolveFiltersToAlarmLightTasksOnly) {
    std::vector<igi::AlarmLightTaskObject> objects(2);
    objects[0].type = "Smoke";
    objects[0].arg_tokens = {"-1", "Smoke", ""};
    objects[1].type = "AlarmLight";
    objects[1].arg_tokens.assign(igi::kAlarmLightFirstParam + igi::kAlarmLightTokenCount, "");
    objects[1].arg_tokens[0] = "-1";
    objects[1].arg_tokens[1] = "AlarmLight";
    const std::vector<igi::AlarmLightParams> resolved = igi::ResolveAlarmLightTasks(objects);
    ASSERT_EQ(resolved.size(), 1u);
}

// ===========================================================================
// The state machine (AlarmLight_Run 0x43A270)
// ===========================================================================

TEST(AlarmLightRules, StartsOffAndUndestroyed) {
    // elevator/alarmlight.c:35-36.
    const igi::AlarmLightState state;
    EXPECT_FALSE(state.is_on);
    EXPECT_FALSE(state.is_destroyed);
    EXPECT_FALSE(state.sound_playing);
}

TEST(AlarmLightRules, ConditionDrivesTheLampAndTheModelSwap) {
    igi::AlarmLightState state;

    // Off -> on installs engine slot B.
    igi::AlarmLightTick tick = igi::AlarmLightRun(state, 1.0, true);
    EXPECT_TRUE(tick.ran_children);
    EXPECT_TRUE(tick.is_on);
    EXPECT_TRUE(state.is_on);
    EXPECT_EQ(tick.model_slot, igi::kAlarmLightModelSlotWhenTurningOn);

    // Staying on installs nothing.
    tick = igi::AlarmLightRun(state, 1.0, true);
    EXPECT_TRUE(tick.is_on);
    EXPECT_EQ(tick.model_slot, -1);

    // On -> off installs engine slot A.
    tick = igi::AlarmLightRun(state, 0.0, true);
    EXPECT_FALSE(tick.is_on);
    EXPECT_FALSE(state.is_on);
    EXPECT_EQ(tick.model_slot, igi::kAlarmLightModelSlotWhenTurningOff);

    // Staying off installs nothing.
    tick = igi::AlarmLightRun(state, 0.0, true);
    EXPECT_EQ(tick.model_slot, -1);
}

TEST(AlarmLightRules, NoExpressionMeansTheLampNeverChanges) {
    // elevator/alarmlight.c:57-58 only assigns when VarString_HasExpr holds.
    igi::AlarmLightState state;
    igi::AlarmLightTick tick = igi::AlarmLightRun(state, 1.0, false);
    EXPECT_FALSE(tick.is_on);
    EXPECT_EQ(tick.model_slot, -1);

    state.is_on = true;
    tick = igi::AlarmLightRun(state, 0.0, false);
    EXPECT_TRUE(tick.is_on);
    EXPECT_EQ(tick.model_slot, -1);
}

TEST(AlarmLightRules, ConditionIsRoundedThenTruncatedToChar) {
    // `al->isOn = (char)round_ll(VarString_EvalReal(...))`
    // (elevator/alarmlight.c:58). The rounding is fistpll (round-half-to-even)
    // and the char cast then truncates to eight bits, so 256 switches the lamp
    // back off and 255 stays on.
    struct Case { double value; bool expected; };
    const Case kCases[] = {
        {0.0, false},
        {0.4, false},
        {0.5, false},     // ties to even
        {0.6, true},
        {1.0, true},
        {-1.0, true},     // a negative char is still non-zero
        {255.0, true},    // (char)255 == -1
        {256.0, false},   // (char)256 == 0
        {257.0, true},    // (char)257 == 1
    };
    for (const Case& test : kCases) {
        igi::AlarmLightState state;
        const igi::AlarmLightTick tick = igi::AlarmLightRun(state, test.value, true);
        EXPECT_EQ(tick.is_on, test.expected) << "value " << test.value;
    }
}

TEST(AlarmLightRules, SoundLagsTheLampByOneTick) {
    // elevator/alarmlight.c:46-54 decides from the state at ENTRY, so the sound
    // starts one tick after the lamp comes on and stops one tick after it goes
    // off. The model swap, by contrast, happens on the same tick.
    igi::AlarmLightState state;

    igi::AlarmLightTick tick = igi::AlarmLightRun(state, 1.0, true);
    EXPECT_TRUE(tick.is_on);
    EXPECT_FALSE(tick.sound_wanted);   // still using the entry state
    EXPECT_FALSE(tick.sound_changed);

    tick = igi::AlarmLightRun(state, 1.0, true);
    EXPECT_TRUE(tick.sound_wanted);
    EXPECT_TRUE(tick.sound_changed);

    tick = igi::AlarmLightRun(state, 1.0, true);
    EXPECT_TRUE(tick.sound_wanted);
    EXPECT_FALSE(tick.sound_changed);  // already playing

    tick = igi::AlarmLightRun(state, 0.0, true);
    EXPECT_FALSE(tick.is_on);
    EXPECT_TRUE(tick.sound_wanted);    // one tick of lag again
    tick = igi::AlarmLightRun(state, 0.0, true);
    EXPECT_FALSE(tick.sound_wanted);
    EXPECT_TRUE(tick.sound_changed);
}

TEST(AlarmLightRules, DestroyedStopsEverything) {
    // elevator/alarmlight.c:55-56 returns before the expression, the model swap
    // and CALL_CHILDREN; the sound branch above it still runs and tears the
    // sound down because `wasOn && !isDestroyed` is now false.
    igi::AlarmLightState state;
    igi::AlarmLightRun(state, 1.0, true);   // on
    igi::AlarmLightRun(state, 1.0, true);   // sound now playing
    ASSERT_TRUE(state.sound_playing);

    state.is_destroyed = true;
    const igi::AlarmLightTick tick = igi::AlarmLightRun(state, 0.0, true);
    EXPECT_FALSE(tick.ran_children);
    EXPECT_TRUE(tick.is_on);                 // the lamp state is frozen
    EXPECT_EQ(tick.model_slot, -1);          // no swap: Destroy did the swap
    EXPECT_FALSE(tick.sound_wanted);
    EXPECT_TRUE(tick.sound_changed);
    EXPECT_FALSE(state.sound_playing);
}

TEST(AlarmLightRules, DestroyInstallsTheThirdModelSlot) {
    // AlarmLight_Destroy 0x43A5F0 (elevator/alarmlight.c:122-131) sets
    // isDestroyed and installs deadModel, which is the authored
    // "Destroyde model" in engine slot C.
    const igi::AlarmLightParams params = Level9Params();
    EXPECT_EQ(igi::kAlarmLightModelSlotWhenDestroyed, igi::kAlarmLightModelSlotC);
    EXPECT_EQ(params.ModelForSlot(igi::kAlarmLightModelSlotWhenDestroyed), params.destroyed_model);

    const igi::AlarmLightExplosion& explosion = igi::AlarmLightDestroyExplosion();
    EXPECT_EQ(explosion.count, 1);
    EXPECT_EQ(explosion.delay, 0);
    EXPECT_FLOAT_EQ(explosion.unk50, 0.05f);
    EXPECT_FLOAT_EQ(explosion.radius, 4096.0f);
    EXPECT_FLOAT_EQ(explosion.burn_size, 2048.0f);
    EXPECT_EQ(explosion.unk68, 5);
    EXPECT_EQ(explosion.unk6c, 2);
    EXPECT_FALSE(explosion.has_burn);
    EXPECT_STREQ(explosion.sprite, "1009_01_1");
    EXPECT_TRUE(explosion.sound_is_authored_explode_sound);
    EXPECT_EQ(explosion.noise_type, 0x15);
    EXPECT_EQ(explosion.noise_unk8, 2);
    EXPECT_FLOAT_EQ(explosion.noise_radius, 20480.0f);
    EXPECT_EQ(explosion.unk84, 0);
}

TEST(AlarmLightRules, RoundLLIsFistpll) {
    // animcam.h:67-73.
    EXPECT_EQ(igi::AlarmLightRoundLL(0.5L), 0);
    EXPECT_EQ(igi::AlarmLightRoundLL(1.5L), 2);
    EXPECT_EQ(igi::AlarmLightRoundLL(2.5L), 2);
    EXPECT_EQ(igi::AlarmLightRoundLL(-1.5L), -2);
}

// ===========================================================================
// The reflector: the visible "blink"
// ===========================================================================

TEST(AlarmLightRules, ReflectorTurnsOnlyWhileTheLampIsOn) {
    // alarm.c:158-166: the whole body is inside
    // `if (l->isOn && !l->isDestroyed)`, and the increment is `angle + kTurn`.
    EXPECT_FLOAT_EQ(igi::kAlarmLightTurn, 0.10471975803375244f);
    EXPECT_FLOAT_EQ(igi::AlarmLightReflectorAngle(0.0f, true, false), igi::kAlarmLightTurn);
    EXPECT_FLOAT_EQ(igi::AlarmLightReflectorAngle(0.5f, true, false), 0.5f + igi::kAlarmLightTurn);
    EXPECT_FLOAT_EQ(igi::AlarmLightReflectorAngle(0.5f, false, false), 0.5f);
    EXPECT_FLOAT_EQ(igi::AlarmLightReflectorAngle(0.5f, true, true), 0.5f);
    EXPECT_FLOAT_EQ(igi::AlarmLightReflectorAngle(0.5f, false, true), 0.5f);
}

TEST(AlarmLightRules, FlarePeriodIsThirtyTicks) {
    // kTurn is pi/30 to float precision (alarm.c:71), so the ratio is
    // 29.999999165173982 and the flash is one pulse per second at 30 Hz.
    EXPECT_NEAR(igi::AlarmLightFlarePeriodTicks(), 29.999999165173982, 1e-9);
    EXPECT_NEAR(igi::AlarmLightFlarePeriodTicks(), 30.0, 1e-3);
}

TEST(AlarmLightRules, FlareRedIsATriangleWaveFromZeroTo128) {
    // alarm.c:235-242: red = 2 * |fmod(angle, pi)/pi - 0.5| * 128.
    EXPECT_FLOAT_EQ(igi::AlarmLightFlareRed(0.0f), 128.0f);
    EXPECT_FLOAT_EQ(igi::kAlarmLightFlareScale, 128.0f);
    EXPECT_FLOAT_EQ(igi::kAlarmLightFlareGreen, 0.0f);
    EXPECT_FLOAT_EQ(igi::kAlarmLightFlareBlue, 0.0f);

    // The turning sequence, accumulated one float addition per tick exactly as
    // the engine does it.
    EXPECT_FLOAT_EQ(igi::AlarmLightFlareRedAfterTicks(0), 128.0f);
    EXPECT_NEAR(igi::AlarmLightFlareRedAfterTicks(1), 119.46666717529297, 1e-4);
    EXPECT_NEAR(igi::AlarmLightFlareRedAfterTicks(7), 68.26667022705078, 1e-3);
    EXPECT_NEAR(igi::AlarmLightFlareRedAfterTicks(14), 8.533334732055664, 1e-3);
    EXPECT_LT(igi::AlarmLightFlareRedAfterTicks(15), 1e-4);   // the trough
    EXPECT_NEAR(igi::AlarmLightFlareRedAfterTicks(29), 119.4665756225586, 1e-3);
    EXPECT_NEAR(igi::AlarmLightFlareRedAfterTicks(30), 128.0, 1e-3);  // back to peak
    EXPECT_NEAR(igi::AlarmLightFlareRedAfterTicks(45), 0.0, 1e-3);
    EXPECT_NEAR(igi::AlarmLightFlareRedAfterTicks(60), 128.0, 1e-3);

    // A NaN angle stays NaN: the engine's `k < 0 || k != k` negates a NaN and
    // negation leaves it NaN.
    EXPECT_TRUE(std::isnan(igi::AlarmLightFlareRed(
        std::numeric_limits<float>::quiet_NaN())));
}

TEST(AlarmLightRules, FlareFacingUsesTheAuthoredOrientationRowOne) {
    // alarm.c:220-227: `dz * m7 + dy * m4 + dx * m1 > 0` against the light's
    // placeMatrix, i.e. its authored "Orientation", not the turned reflector.
    const float identity[9] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    const double light[3] = {0.0, 0.0, 0.0};

    const double above[3] = {0.0, 1.0, 0.0};   // +Y, the identity's row 1
    const double below[3] = {0.0, -1.0, 0.0};
    const double aside[3] = {5.0, 0.0, 0.0};
    EXPECT_TRUE(igi::AlarmLightFlareFacesCamera(light, identity, above));
    EXPECT_FALSE(igi::AlarmLightFlareFacesCamera(light, identity, below));
    EXPECT_FALSE(igi::AlarmLightFlareFacesCamera(light, identity, aside));  // exactly zero

    // The LEVEL9 authored orientation is z = pi, which flips the axis to -Y.
    const igi::AlarmLightParams params = Level9Params();
    EXPECT_NEAR(params.matrix[1], 0.0f, 1e-6);
    EXPECT_NEAR(params.matrix[4], -1.0f, 1e-6);
    EXPECT_NEAR(params.matrix[7], 0.0f, 1e-6);

    const double offset_up[3] = {params.place[0], params.place[1] + 4096.0, params.place[2]};
    const double offset_down[3] = {params.place[0], params.place[1] - 4096.0, params.place[2]};
    EXPECT_FALSE(igi::AlarmLightFlareFacesCamera(params.place, params.matrix, offset_up));
    EXPECT_TRUE(igi::AlarmLightFlareFacesCamera(params.place, params.matrix, offset_down));
}

// ===========================================================================
// Matrix_SetEuler
// ===========================================================================

TEST(AlarmLightRules, EulerMatrixIsRotZTimesRotYTimesRotX) {
    // matrix.c:99-109 with HOOK at :110, and Mul's own summation order
    // (matrix.c:10-23).
    float matrix[9] = {0.0f};
    const float identity[3] = {0.0f, 0.0f, 0.0f};
    igi::AlarmLightEulerMatrix(identity, matrix);
    const float kExpectedIdentity[9] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    for (int i = 0; i < 9; ++i) EXPECT_FLOAT_EQ(matrix[i], kExpectedIdentity[i]) << i;

    // Rotating about Z by pi/2 maps +X onto +Y.
    const float rot_z[3] = {0.0f, 0.0f, 1.5707963267948966f};
    igi::AlarmLightEulerMatrix(rot_z, matrix);
    EXPECT_NEAR(matrix[0], 0.0f, 1e-6);
    EXPECT_NEAR(matrix[1], -1.0f, 1e-6);
    EXPECT_NEAR(matrix[3], 1.0f, 1e-6);
    EXPECT_NEAR(matrix[4], 0.0f, 1e-6);
    EXPECT_NEAR(matrix[8], 1.0f, 1e-6);

    // Rotating about X by pi/2 maps +Y onto +Z.
    const float rot_x[3] = {1.5707963267948966f, 0.0f, 0.0f};
    igi::AlarmLightEulerMatrix(rot_x, matrix);
    EXPECT_NEAR(matrix[0], 1.0f, 1e-6);
    EXPECT_NEAR(matrix[4], 0.0f, 1e-6);
    EXPECT_NEAR(matrix[5], -1.0f, 1e-6);
    EXPECT_NEAR(matrix[7], 1.0f, 1e-6);
    EXPECT_NEAR(matrix[8], 0.0f, 1e-6);
}

TEST(AlarmLightRules, EulerMatrixIsOrthonormalForTheRetailOrientation) {
    const igi::AlarmLightParams params = Level9Params();
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            double dot = 0.0;
            for (int k = 0; k < 3; ++k) {
                dot += static_cast<double>(params.matrix[row * 3 + k]) *
                       static_cast<double>(params.matrix[col * 3 + k]);
            }
            EXPECT_NEAR(dot, row == col ? 1.0 : 0.0, 1e-5) << row << "," << col;
        }
    }
}

// ===========================================================================
// The retail corpus
// ===========================================================================

TEST(AlarmLightCorpus, EveryRetailAlarmLightParses) {
    if (!fs::is_regular_file(LevelDir(9) / "OBJECTS.QVM")) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string()
                     << " (set IGI_GAME_PATH to a retail install to run this test)";
    }

    // No retail level decompiles an AlarmLight into objects.qsc, so these counts
    // come from the shipping QVM bytecode.
    const int kExpected[15] = {0, 0, 0, 0, 0, 0, 3, 2, 7, 2, 0, 0, 0, 0, 2};
    int total = 0;

    for (int level = 1; level <= 14; ++level) {
        const fs::path path = LevelDir(level) / "OBJECTS.QVM";
        if (!fs::is_regular_file(path)) continue;

        const QVMFile qvm = QVM_Parse(path.string());
        const std::vector<std::vector<std::string>> calls = ExtractAlarmLightCalls(qvm);
        EXPECT_EQ(static_cast<int>(calls.size()), kExpected[level]) << "LEVEL" << level;

        for (const std::vector<std::string>& tokens : calls) {
            ASSERT_EQ(tokens.size(), igi::kAlarmLightFirstParam + igi::kAlarmLightTokenCount)
                << "LEVEL" << level << ": an AlarmLight call did not yield 12 parameter tokens";
            igi::AlarmLightParams params;
            ASSERT_TRUE(igi::ParseAlarmLightParams(tokens, &params)) << "LEVEL" << level;
            ++total;

            // Every retail instance authors an expression and two models; the
            // destroy model and both sounds are usually empty.
            EXPECT_FALSE(params.on_expression.empty()) << "LEVEL" << level;
            EXPECT_FALSE(params.on_model.empty()) << "LEVEL" << level;
            EXPECT_FALSE(params.off_model.empty()) << "LEVEL" << level;
        }
    }
    EXPECT_EQ(total, 16);
}

TEST(AlarmLightCorpus, Level9ValuesAreExactlyTheAuthoredOnes) {
    const fs::path path = LevelDir(9) / "OBJECTS.QVM";
    if (!fs::is_regular_file(path)) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string();
    }
    const QVMFile qvm = QVM_Parse(path.string());
    const std::vector<std::vector<std::string>> calls = ExtractAlarmLightCalls(qvm);
    ASSERT_EQ(calls.size(), 2u);

    std::vector<igi::AlarmLightParams> params;
    for (const std::vector<std::string>& tokens : calls) {
        igi::AlarmLightParams parsed;
        ASSERT_TRUE(igi::ParseAlarmLightParams(tokens, &parsed));
        params.push_back(parsed);
    }

    EXPECT_DOUBLE_EQ(params[0].place[0], 23470464.0);
    EXPECT_DOUBLE_EQ(params[0].place[1], -51103984.0);
    EXPECT_DOUBLE_EQ(params[0].place[2], 174511488.0);
    EXPECT_FLOAT_EQ(params[0].orientation[0], 0.0f);
    EXPECT_FLOAT_EQ(params[0].orientation[1], 0.0f);
    EXPECT_FLOAT_EQ(params[0].orientation[2], 3.1415929794311523f);
    EXPECT_EQ(params[0].on_expression, "AlarmControl_98.isAlarm");
    EXPECT_EQ(params[0].on_model, "344_03_1");
    EXPECT_EQ(params[0].off_model, "344_01_1");
    EXPECT_TRUE(params[0].destroyed_model.empty());
    EXPECT_TRUE(params[0].on_sound.empty());
    EXPECT_TRUE(params[0].explode_sound.empty());

    // The second LEVEL9 instance is the same but for its position.
    EXPECT_DOUBLE_EQ(params[1].place[0], 23515142.0);
    EXPECT_DOUBLE_EQ(params[1].place[1], -50666360.0);
    EXPECT_DOUBLE_EQ(params[1].place[2], 174495072.0);
    EXPECT_EQ(params[1].on_model, params[0].on_model);
    EXPECT_EQ(params[1].off_model, params[0].off_model);

    // The real-data consequence of the slot mapping: the lamp is authored to
    // show '344_03_1' when on, and that is exactly the model the runtime
    // installs when it turns OFF.
    const igi::AlarmLightState off;
    igi::AlarmLightState state = off;
    const igi::AlarmLightTick on_tick = igi::AlarmLightRun(state, 1.0, true);
    EXPECT_EQ(params[0].ModelForSlot(on_tick.model_slot), "344_01_1");  // authored "Off model"
    const igi::AlarmLightTick off_tick = igi::AlarmLightRun(state, 0.0, true);
    EXPECT_EQ(params[0].ModelForSlot(off_tick.model_slot), "344_03_1");  // authored "On model"
}

TEST(AlarmLightCorpus, Level7And14AuthorTheExplodeSound) {
    for (int level : {7, 14}) {
        const fs::path path = LevelDir(level) / "OBJECTS.QVM";
        if (!fs::is_regular_file(path)) {
            GTEST_SKIP() << "retail IGI not found at " << GameRoot().string();
        }
        const QVMFile qvm = QVM_Parse(path.string());
        const std::vector<std::vector<std::string>> calls = ExtractAlarmLightCalls(qvm);
        ASSERT_FALSE(calls.empty()) << "LEVEL" << level;

        for (const std::vector<std::string>& tokens : calls) {
            igi::AlarmLightParams params;
            ASSERT_TRUE(igi::ParseAlarmLightParams(tokens, &params));
            EXPECT_EQ(params.explode_sound, "explo_01_s") << "LEVEL" << level;
            EXPECT_EQ(params.on_expression, level == 7 ? "AlarmControl_97.isAlarm"
                                                       : "!ConditionalContainer_2222.isRun");
        }
    }
}

TEST(AlarmLightCorpus, Level8AuthorsTwoInstancesWhoseModelsMatch) {
    // LEVEL8 has a pair authored with the same model in both slots, so the
    // on/off inversion is invisible there -- and a pair driven by a different
    // control object.
    const fs::path path = LevelDir(8) / "OBJECTS.QVM";
    if (!fs::is_regular_file(path)) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string();
    }
    const QVMFile qvm = QVM_Parse(path.string());
    const std::vector<std::vector<std::string>> calls = ExtractAlarmLightCalls(qvm);
    ASSERT_EQ(calls.size(), 7u);

    int identical_pairs = 0;
    int different_controls = 0;
    for (const std::vector<std::string>& tokens : calls) {
        igi::AlarmLightParams params;
        ASSERT_TRUE(igi::ParseAlarmLightParams(tokens, &params));
        if (params.on_model == params.off_model) ++identical_pairs;
        if (params.on_expression == "AlarmControl_1097.isAlarm") ++different_controls;
    }
    EXPECT_EQ(identical_pairs, 2);
    EXPECT_EQ(different_controls, 2);
}

TEST(AlarmLightCorpus, DeclaredParametersMatchTheParameterTable) {
    // The authored declaration is the argument block of the
    // Task_DeclareParameters call in the QVM bytecode, so the port's table is
    // checked against the shipping data rather than a hand-copied list.
    const fs::path path = LevelDir(9) / "OBJECTS.QVM";
    if (!fs::is_regular_file(path)) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string();
    }
    const QVMFile qvm = QVM_Parse(path.string());
    const std::vector<std::string> declared = DeclaredAlarmLightParameters(qvm);
    ASSERT_EQ(declared.size(), static_cast<std::size_t>(igi::kAlarmLightParamCount) * 2u);

    const igi::AlarmLightParamDesc* table = igi::AlarmLightParamTable();
    for (int i = 0; i < igi::kAlarmLightParamCount; ++i) {
        EXPECT_EQ(declared[static_cast<std::size_t>(i) * 2], table[i].authored_name) << "slot " << i;
        EXPECT_EQ(declared[static_cast<std::size_t>(i) * 2 + 1], table[i].type_name) << "slot " << i;
    }
    // The retail typo is part of the data, not of this port.
    EXPECT_EQ(declared[5 * 2], "Destroyde model");
}

// ===========================================================================
// The inspector schema (GitHub issue #43)
// ===========================================================================
//
// The editor's inspector rows for "AlarmLight" are DERIVED from
// AlarmLightParamTable() (source/level/task_schema.cpp). The oracle for the
// rows is AlarmLight_Init's ParamTable (HOOK 0x4397C0, src/fxobj/alarm.c:108-115)
// and the retail Task_DeclareParameters("AlarmLight", ...) line; the offsets are
// the running token sum from arg 3, in the LevelObject::argTokens layout.

TEST(AlarmLightSchema, EveryAuthoredParameterHasAnInspectorRowInParamTableOrder) {
    const TaskSchemaNS::TaskSchema* schema =
        TaskSchemaNS::GetBuiltinSchema("AlarmLight");
    ASSERT_NE(schema, nullptr) << "AlarmLight must have a built-in schema";
    ASSERT_EQ(schema->size(), static_cast<std::size_t>(igi::kAlarmLightParamCount));

    const igi::AlarmLightParamDesc* table = igi::AlarmLightParamTable();
    for (int i = 0; i < igi::kAlarmLightParamCount; ++i) {
        const TaskSchemaNS::FieldDef& f = (*schema)[static_cast<std::size_t>(i)];
        EXPECT_EQ(f.name, table[i].authored_name) << "slot " << i;
        EXPECT_EQ(f.typeName, table[i].type_name) << "slot " << i;
        EXPECT_EQ(f.argCount, table[i].token_count) << "slot " << i;
    }
    // The retail typo is data, and the inspector shows the level file's word.
    EXPECT_EQ((*schema)[5].name, "Destroyde model");
}

TEST(AlarmLightSchema, OffsetsAreTheRunningTokenSumFromArgThree) {
    const TaskSchemaNS::TaskSchema* schema =
        TaskSchemaNS::GetBuiltinSchema("AlarmLight");
    ASSERT_NE(schema, nullptr);
    const igi::AlarmLightParamDesc* table = igi::AlarmLightParamTable();

    int expected = 3;
    for (int i = 0; i < igi::kAlarmLightParamCount; ++i) {
        EXPECT_EQ((*schema)[static_cast<std::size_t>(i)].argOffset, expected) << "slot " << i;
        expected += table[i].token_count;
    }
    // 12 tokens: 3 for ObjectPos, 3 for the Real32x9 Euler triple
    // (paramops.c:423-435), then six single-token scalars.
    EXPECT_EQ(expected, 3 + igi::kAlarmLightTokenCount);
    EXPECT_EQ(expected, 15);
}

TEST(AlarmLightSchema, TheModelSlotsAreAtTheirRealArgumentsNotAtArgNine) {
    // The row this schema replaced said "Model, String16, @9".  Arg 9 is the
    // "On expression" condition (alarm.c:111), so the inspector labelled the
    // activation expression a model and the lamp's real model slots (args 10,
    // 11, 12 -- the three String16 fields alarm.c:113 registers as
    // `l.offModel + 0x10 * i`) were invisible.
    const TaskSchemaNS::TaskSchema* schema =
        TaskSchemaNS::GetBuiltinSchema("AlarmLight");
    ASSERT_NE(schema, nullptr);
    EXPECT_EQ((*schema)[2].name, "On expression");
    EXPECT_EQ((*schema)[2].argOffset, 9);
    EXPECT_EQ((*schema)[2].typeName, "VarString");
    EXPECT_EQ((*schema)[3].name, "On model");
    EXPECT_EQ((*schema)[3].argOffset, 10);
    EXPECT_EQ((*schema)[4].name, "Off model");
    EXPECT_EQ((*schema)[4].argOffset, 11);
    EXPECT_EQ((*schema)[5].argOffset, 12);
}
