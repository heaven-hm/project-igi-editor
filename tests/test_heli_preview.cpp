// test_heli_preview.cpp - the placed-helicopter FLIGHT PREVIEW (#60): the engine's
// own per-tick sequence, pinned tick by tick.
//
// The oracle for every number here is IGI-RE (read-only). Key citations:
//   heli/heli.c:358-405   Heli_Control 0x431B70: the axes assignment, the -1.0f
//                         throttle sentinel, the tick-0 reset, the `tick > 0`
//                         Heli_Move gate
//   heli/heli.c:412-520   Heli_Move 0x431E30: the speed ramp, the smoothed turn
//                         forces, the nose pitch, the rotor sound parameters
//   vehbase/vehicle.c:189-197  Vehicle_ResetControls 0x4F4600
//   vehbase/animtask.c:403-475 AnimTask_Seek 0x4F3EF0 / AnimTask_SendControl
//                         0x4F4060: who sends the control message, with which tick
//   graphtask/physobj.c:404-447 PhysicsObj_Collide 0x4FC0E0: the position step
//   graphtask/physobj.c:47      kSlop = 40.96
//   animtrig/body.c:325-339     Body_Update 0x4ECDB0
//   file/vec.c:140-181          Vec3_FastLength 0x4B2A10
//
// The rule itself is `source/runtime/heli_rules.{h,cpp}`; this suite pins the
// link between it and the authored parameters the editor already parses.
//
// The corpus half reads the retail OBJECTS.QVM / HELI.QVM with the editor's own
// parsers and checks that a real placed helicopter produces the flight the
// authored values imply.

#include "../source/level/qvm_decompiler.h"
#include "../source/level/qvm_parser.h"
#include "../source/renderer/atta_children.h"
#include "../source/runtime/heli_preview.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

fs::path GameRoot() {
    if (const char* env = std::getenv("IGI_GAME_PATH"); env != nullptr && *env != '\0')
        return fs::path(env);
    return fs::path("/Users/haseeb-mir/Downloads/IGI");
}

// ---------------------------------------------------------------------------
// Authored tokens
// ---------------------------------------------------------------------------

std::string FormatFloat(float value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.9g", value);
    return buffer;
}

// The editor's argTokens layout: [0] = task id, [1] = type, [2] = name,
// [3] = parameter 0 (source/level/level_objects.h:116). `base` is the 19-token
// parameter list in the engine's ParamTable_Add order (heli.c:186-200).
std::vector<std::string> HeliTokens(const std::vector<std::string>& base) {
    std::vector<std::string> tokens;
    tokens.push_back("-1");
    tokens.push_back("Heli");
    tokens.push_back("heli_1");
    for (const std::string& value : base) tokens.push_back(value);
    return tokens;
}

// A "Heli" instance with the given authored fields and everything else at the
// engine's own defaults: orientation 0,0,0, model 709_01_1, no collision, no
// Force Z, three empty VarStrings and the three ViewCone values the retail
// LEVEL2 helicopter authors.
std::vector<std::string> HeliTokensFor(double x, double y, double z, float ax, float ay, float az,
                                       float original_thrust, float vx, float vy, float vz) {
    std::vector<std::string> base;
    base.push_back(FormatFloat(static_cast<float>(x)));
    base.push_back(FormatFloat(static_cast<float>(y)));
    base.push_back(FormatFloat(static_cast<float>(z)));
    base.push_back(FormatFloat(ax));
    base.push_back(FormatFloat(ay));
    base.push_back(FormatFloat(az));
    base.push_back(FormatFloat(original_thrust));
    base.push_back(FormatFloat(vx));
    base.push_back(FormatFloat(vy));
    base.push_back(FormatFloat(vz));
    base.push_back("709_01_1");
    base.push_back("FALSE");
    base.push_back("FALSE");
    base.push_back("");
    base.push_back("");
    base.push_back("");
    base.push_back("180.0");
    base.push_back("360.0");
    base.push_back("60.0");
    return HeliTokens(base);
}

// The BELL vehicle config, as the shipped HELI.QVM authors it (asserted by
// `HeliRulesCorpus.BellPhysicsDefinitionsMatchTheShippedHeliQvm`).
igi::HeliVehicleConfig BellConfig() {
    igi::HeliVehicleConfig cfg{};
    cfg.start_speed_scale = 200.0f;
    for (int i = 0; i < 3; ++i) {
        cfg.axis[i].rate = 50.0f;
        cfg.axis[i].smooth = 0.4f;
    }
    cfg.thrust = 3000.0f;
    cfg.accel = 0.027000000700354576f;
    cfg.accel_slow = 0.0030000000260770321f;
    cfg.part_count = 3;
    cfg.valid = true;
    return cfg;
}

void Identity(float m[9]) {
    for (int i = 0; i < 9; ++i) m[i] = (i % 4 == 0) ? 1.0f : 0.0f;
}

// The control message `Heli_ReadKeys` sends with no keys held (heli.c:687-719):
// all three turn axes 0 and the -1.0f throttle sentinel in axes[3].
igi::HeliPreviewInput NoInput() { return igi::HeliPreviewInput{}; }

// The same message with a real throttle request.
igi::HeliPreviewInput Throttle(float value) {
    igi::HeliPreviewInput in;
    in.throttle = value;
    return in;
}

// The same message with one turn axis held.
igi::HeliPreviewInput Steer(int axis, float value) {
    igi::HeliPreviewInput in;
    in.steer[axis] = value;
    return in;
}

// A fresh preview state for a helicopter at the origin, nose +X, flying at
// `original_thrust` along +X.
igi::HeliPreviewState PreviewAt(float original_thrust, float vx, float vy, float vz) {
    return igi::BuildHeliPreviewState(
        HeliTokensFor(0.0, 0.0, 0.0, 0.0f, 0.0f, 0.0f, original_thrust, vx, vy, vz), BellConfig());
}

// ---------------------------------------------------------------------------
// Retail corpus
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

// Walks one authored Heli call and returns its arguments in the editor's
// argTokens layout. Identical to the walk in tests/test_heli_rules.cpp: the
// preview consumes the same list, so the corpus test builds it the same way.
std::vector<std::string> ReadHeliCall(const QVMFile& qvm, std::size_t type_index) {
    std::vector<std::string> tokens;
    tokens.push_back("-1");
    tokens.push_back("Heli");

    const std::size_t kExpected = 2 + 1 + static_cast<std::size_t>(igi::kHeliTokenCount);

    for (std::size_t i = type_index + 1;
         i < qvm.instructions.size() && tokens.size() < kExpected; ++i) {
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

std::vector<std::vector<std::string>> ExtractHeliCalls(const QVMFile& qvm) {
    std::vector<std::vector<std::string>> out;
    for (std::size_t i = 0; i < qvm.instructions.size(); ++i) {
        const QVMInstruction& in = qvm.instructions[i];
        if (!IsStringPush(in.type)) continue;
        if (in.operand >= qvm.strings.size()) continue;
        if (qvm.strings[in.operand] != "Heli") continue;

        std::size_t name = i + 1;
        while (name < qvm.instructions.size() && qvm.instructions[name].type == QVMOpType::BRK) ++name;
        if (name >= qvm.instructions.size() || !IsStringPush(qvm.instructions[name].type)) continue;

        std::size_t first = name + 1;
        while (first < qvm.instructions.size() && qvm.instructions[first].type == QVMOpType::BRK) ++first;
        if (first >= qvm.instructions.size() || IsStringPush(qvm.instructions[first].type)) continue;

        out.push_back(ReadHeliCall(qvm, i));
    }
    return out;
}

// The level-2 helicopter the shipped LEVEL2/OBJECTS.QVM places. LEVEL2 is the
// only retail level with a placed Heli (docs/igi-re-parity/HELICOPTER.md).
std::vector<std::string> Level2HeliTokens() {
    const fs::path path = GameRoot() / "MISSIONS" / "location0" / "LEVEL2" / "OBJECTS.QVM";
    if (!fs::is_regular_file(path)) return {};
    const QVMFile qvm = QVM_Parse(path.string());
    if (!qvm.valid) return {};
    const std::vector<std::vector<std::string>> calls = ExtractHeliCalls(qvm);
    if (calls.empty()) return {};
    return calls.front();
}

std::string DecompiledHeliQvm(const char* model_dir) {
    const fs::path path = GameRoot() / "physicsobj" / "HELIS" / model_dir / "HELI.QVM";
    if (!fs::is_regular_file(path)) return {};
    const QVMFile qvm = QVM_Parse(path.string());
    if (!qvm.valid) return {};
    return QVM_DecompileToString(qvm);
}

}  // namespace

// ===========================================================================
// The authored -> preview-state step
// ===========================================================================

TEST(HeliPreview, BuildRefusesATruncatedArgumentList) {
    // 18 parameters, one short of the 19 the engine's 13-row table consumes
    // (heli.c:186-200). A preview built from it would look plausible and be
    // wrong, so `valid` is false and nothing is filled in.
    std::vector<std::string> tokens = HeliTokensFor(1.0, 2.0, 3.0, 0.0f, 0.0f, 0.0f, 0.75f, 1.0f,
                                                    0.0f, 0.0f);
    tokens.pop_back();
    const igi::HeliPreviewState state = igi::BuildHeliPreviewState(tokens, BellConfig());
    EXPECT_FALSE(state.valid);
}

TEST(HeliPreview, BuildTakesTheAuthoredFieldsVerbatim) {
    const igi::HeliPreviewState state = igi::BuildHeliPreviewState(
        HeliTokensFor(126942784.0, -38370560.0, 175304928.0, 0.0f, 0.0f, 3.8061590194702148f,
                      0.75f, 0.0f, 0.0f, 0.0f),
        BellConfig());

    ASSERT_TRUE(state.valid);
    EXPECT_EQ(state.tick, -1);  // no tick has run
    EXPECT_DOUBLE_EQ(state.start_pos[0], 126942784.0);
    EXPECT_DOUBLE_EQ(state.start_pos[1], -38370560.0);
    EXPECT_DOUBLE_EQ(state.start_pos[2], 175304928.0);
    EXPECT_DOUBLE_EQ(state.pos[0], 126942784.0);
    // "Original Thrust" is the START SPEED, not a thrust (heli.c:190, :379-380).
    EXPECT_FLOAT_EQ(state.start_speed, 0.75f);
    EXPECT_FLOAT_EQ(state.start_vel[0], 0.0f);
    EXPECT_FLOAT_EQ(state.start_vel[2], 0.0f);

    // "Orientation" is Real32x9 but the engine reads THREE angles and stores
    // Matrix_SetEuler of them (paramops.c:423-435, matrix.c:99-110). The
    // preview stores that product, so it must equal HeliSetEuler of the three
    // authored angles.
    float expected[9];
    igi::HeliSetEuler(expected, 0.0f, 0.0f, 3.8061590194702148f);
    for (int i = 0; i < 9; ++i) {
        EXPECT_FLOAT_EQ(state.start_matrix[i], expected[i]) << "m " << i;
    }
    // A pure Z rotation keeps the Z row and column intact.
    EXPECT_NEAR(state.start_matrix[8], 1.0f, 1e-6);
}

// ===========================================================================
// The control sentinel (heli.c:360-364)
// ===========================================================================

TEST(HeliPreview, ThrottleSentinelIsTheRawBitsOfMinusOne) {
    // `*(uint32_t *)&arg->axes[3] != 0xBF800000` (heli.c:360). The test is on
    // the raw bits, so only the exact pattern -1.0f is the sentinel: a NaN
    // throttle is a request, and so is 0.
    float axes[igi::kHeliControlCount] = {0.0f, 0.0f, 0.0f, igi::kHeliThrustNone, 0.0f};
    EXPECT_FALSE(igi::HeliControlThrustRequested(axes));

    axes[3] = 0.0f;
    EXPECT_TRUE(igi::HeliControlThrustRequested(axes));
    axes[3] = 1.0f;
    EXPECT_TRUE(igi::HeliControlThrustRequested(axes));
    axes[3] = -1.0000001f;  // one bit away from the sentinel
    EXPECT_TRUE(igi::HeliControlThrustRequested(axes));
    axes[3] = std::nanf("");
    EXPECT_TRUE(igi::HeliControlThrustRequested(axes));
}

TEST(HeliPreview, NoThrottleRequestLeavesTheTargetSpeedAlone) {
    // `kHeliThrustNone` is the value `Heli_ReadKeys` parks in axes[3] when the
    // throttle keys are idle (heli.c:696), and `Heli_Control` then skips the
    // target_speed assignment (heli.c:360-361). The preview's own default
    // throttle is exactly that, so a previewed helicopter holds the speed the
    // last tick left it with.
    igi::HeliPreviewState state = PreviewAt(0.75f, 1.0f, 0.0f, 0.0f);
    ASSERT_TRUE(state.valid);
    igi::HeliPreviewTick(&state, 0, NoInput());
    ASSERT_FLOAT_EQ(state.move.target_speed, 0.75f);

    igi::HeliPreviewTick(&state, 1, NoInput());
    EXPECT_FLOAT_EQ(state.move.target_speed, 0.75f);
    // speed == target, so the ramp is a no-op (heli.c:422-430).
    EXPECT_FLOAT_EQ(state.move.speed, 0.75f);

    // A real request DOES move the target (heli.c:361).
    igi::HeliPreviewTick(&state, 2, Throttle(1.2f));
    EXPECT_FLOAT_EQ(state.move.target_speed, 1.2f);
}

// ===========================================================================
// The tick-0 reset (heli.c:365-400)
// ===========================================================================

TEST(HeliPreview, TickZeroResetsToTheAuthoredPoseAndSpeed) {
    igi::HeliPreviewState state = PreviewAt(0.75f, 3.0f, 4.0f, 0.0f);
    ASSERT_TRUE(state.valid);

    // Something has to have run first for the reset to be observable.
    igi::HeliPreviewTick(&state, 0, NoInput());
    ASSERT_FLOAT_EQ(state.move.speed, 0.75f);

    const bool moved = igi::HeliPreviewTick(&state, 0, Throttle(1.2f));
    EXPECT_FALSE(moved) << "tick 0 is the reset, not a Heli_Move (heli.c:401)";

    // heli.c:379-380 -- BOTH speed fields come back from "Original Thrust".
    EXPECT_FLOAT_EQ(state.move.speed, 0.75f);
    EXPECT_FLOAT_EQ(state.move.target_speed, 0.75f);
    // heli.c:381-388 -- turn and steer are zeroed.
    for (int i = 0; i < 3; ++i) {
        EXPECT_FLOAT_EQ(state.move.turn[i], 0.0f) << "turn " << i;
        EXPECT_FLOAT_EQ(state.move.steer[i], 0.0f) << "steer " << i;
    }
    // heli.c:376 -- the matrix is the authored one again.
    float authored[9];
    Identity(authored);
    igi::HeliSetEuler(authored, 0.0f, 0.0f, 0.0f);
    for (int i = 0; i < 9; ++i) EXPECT_FLOAT_EQ(state.move.matrix[i], authored[i]) << "m " << i;
    EXPECT_EQ(state.move.matrix_count, 0);
    // heli.c:370 -- DynCube_Move puts it back at the authored start position.
    EXPECT_DOUBLE_EQ(state.pos[0], 0.0);
    EXPECT_DOUBLE_EQ(state.pos[1], 0.0);
    EXPECT_DOUBLE_EQ(state.pos[2], 0.0);

    // heli.c:390-393 -- the body velocity is the authored "Speed" turned by the
    // authored "Orientation" matrix. Identity here, so it is the triplet itself.
    EXPECT_FLOAT_EQ(state.vel[0], 3.0f);
    EXPECT_FLOAT_EQ(state.vel[1], 4.0f);
    EXPECT_FLOAT_EQ(state.vel[2], 0.0f);
    // heli.c:394 Body_Update -> body.speed is Vec3_FastLength of that.
    EXPECT_FLOAT_EQ(state.move.body_speed, igi::FastLength3(state.vel));
}

TEST(HeliPreview, TickZeroIgnoresTheThrottleItWasHanded) {
    // The axes assignment at heli.c:360-364 runs BEFORE the reset, and the reset
    // then overwrites all five slots (heli.c:371-375). A tick-0 message carrying
    // any throttle therefore has no effect on the target speed.
    igi::HeliPreviewState requested = PreviewAt(0.75f, 1.0f, 0.0f, 0.0f);
    igi::HeliPreviewState untouched = PreviewAt(0.75f, 1.0f, 0.0f, 0.0f);
    ASSERT_TRUE(requested.valid);
    ASSERT_TRUE(untouched.valid);

    igi::HeliPreviewTick(&requested, 0, Throttle(0.0f));
    igi::HeliPreviewTick(&untouched, 0, NoInput());
    EXPECT_FLOAT_EQ(requested.move.target_speed, untouched.move.target_speed);
    EXPECT_FLOAT_EQ(requested.move.target_speed, 0.75f);
}

TEST(HeliPreview, TickZeroTurnsTheAuthoredSpeedByTheAuthoredOrientation) {
    // A 90-degree Z rotation turns +X into +Y (matrix.c:148-164 sets
    // m[0] = cos, m[1] = sin, m[3] = -sin, m[4] = cos).
    igi::HeliPreviewState state = igi::BuildHeliPreviewState(
        HeliTokensFor(0.0, 0.0, 0.0, 0.0f, 0.0f, std::acos(0.0f), 1.0f, 5.0f, 0.0f, 0.0f),
        BellConfig());
    ASSERT_TRUE(state.valid);
    igi::HeliPreviewTick(&state, 0, NoInput());

    // The engine rotates a vector as `v_j = sum_i v_i * m[3i+j]` (heli.c:390-392),
    // so `v.y` is `startVel.x * m[3]` -- the first ROW's second entry. The first
    // row is what Matrix_SetEuler leaves there: it is built as Rz * Ry * Rx with
    // the engine's `Mul` (matrix.c:99-110), whose product has the same row-vector
    // convention as Matrix_SetRotZ's own `m[1] = -sin, m[3] = sin`
    // (matrix.c:148-164). A quarter turn about +Z therefore sends the authored +X
    // speed to +Y. This assertion pins the row/column convention that the draw
    // path's GLM conversion also depends on.
    EXPECT_NEAR(state.vel[0], 0.0f, 1e-5);
    EXPECT_NEAR(state.vel[1], 5.0f, 1e-5);
    EXPECT_NEAR(state.vel[2], 0.0f, 1e-6);
}

// ===========================================================================
// The speed ramp (heli.c:416-433)
// ===========================================================================

TEST(HeliPreview, TheRampStepsTowardsTheTargetAtTheConfigsRate) {
    // accel_slow while the speed is below 0.8 (heli.c:416), accel once it is
    // there. The BELL config's rates are 0.003 and 0.027.
    const igi::HeliVehicleConfig cfg = BellConfig();

    igi::HeliPreviewState slow = PreviewAt(0.75f, 0.0f, 0.0f, 0.0f);
    ASSERT_TRUE(slow.valid);
    igi::HeliPreviewTick(&slow, 0, NoInput());
    igi::HeliPreviewTick(&slow, 1, Throttle(0.0f));  // a request: target 0
    EXPECT_NEAR(slow.move.speed, 0.75f - cfg.accel_slow, 1e-6);

    igi::HeliPreviewState fast = PreviewAt(1.2f, 0.0f, 0.0f, 0.0f);
    ASSERT_TRUE(fast.valid);
    igi::HeliPreviewTick(&fast, 0, NoInput());
    igi::HeliPreviewTick(&fast, 1, Throttle(1.0f));
    EXPECT_NEAR(fast.move.speed, 1.2f - cfg.accel, 1e-6);
}

TEST(HeliPreview, TheRampClampsAtTheTargetAndSnapsAStandstillToZero) {
    // heli.c:422-430: overshoot is clamped to the target. heli.c:431: below
    // 0.001 with a zero target, the speed collapses to exactly 0.
    igi::HeliPreviewState state = PreviewAt(0.75f, 0.0f, 0.0f, 0.0f);
    ASSERT_TRUE(state.valid);
    igi::HeliPreviewTick(&state, 0, NoInput());

    // Request a target just above the current speed; one ramp step would
    // overshoot it, so the clamp is what decides the result.
    igi::HeliPreviewTick(&state, 1, Throttle(0.7505f));
    EXPECT_FLOAT_EQ(state.move.speed, 0.7505f);

    // Now ask for 0 and let it run down past the 0.001 collapse.
    for (int tick = 2; tick < 600; ++tick) igi::HeliPreviewTick(&state, tick, Throttle(0.0f));
    EXPECT_FLOAT_EQ(state.move.speed, 0.0f);
}

TEST(HeliPreview, RunToTickIsTheSameAsTickByTick) {
    igi::HeliPreviewState stepped = PreviewAt(1.1f, 2.0f, 0.0f, 0.0f);
    igi::HeliPreviewState seeked = PreviewAt(1.1f, 2.0f, 0.0f, 0.0f);
    ASSERT_TRUE(stepped.valid);
    ASSERT_TRUE(seeked.valid);

    igi::HeliPreviewTick(&stepped, 0, NoInput());
    for (int tick = 1; tick <= 40; ++tick) igi::HeliPreviewTick(&stepped, tick, NoInput());
    igi::HeliPreviewRunToTick(&seeked, 40, NoInput());

    EXPECT_EQ(stepped.tick, seeked.tick);
    EXPECT_FLOAT_EQ(stepped.move.speed, seeked.move.speed);
    for (int i = 0; i < 9; ++i) EXPECT_FLOAT_EQ(stepped.move.matrix[i], seeked.move.matrix[i]);
    for (int i = 0; i < 3; ++i) EXPECT_DOUBLE_EQ(stepped.pos[i], seeked.pos[i]);

    // Seeking backwards is a no-op, which is what makes the cache in the draw
    // path safe.
    igi::HeliPreviewRunToTick(&seeked, 10, NoInput());
    EXPECT_EQ(seeked.tick, 40);
}

// ===========================================================================
// The three-axis turn (heli.c:434-456)
// ===========================================================================

TEST(HeliPreview, TheTurnIsSmoothedBeforeItIsUsed) {
    // heli.c:434-439: `t0 = (1 - smooth) * steer[0] + smooth * turn[0]`, with the
    // BELL config's smooth = 0.4. With turn starting at 0, one tick of steer 1
    // gives 0.6, and the second tick gives 0.4*0.6 + 0.6 = 0.84.
    //
    // The body must be flying faster than 2 units/tick or the turn-FORCE gate
    // (heli.c:440-441) would skip the block -- but the smoothing at :434-439 is
    // outside that gate, which is exactly what this test is about.
    igi::HeliPreviewState state = PreviewAt(1.0f, 10.0f, 0.0f, 0.0f);
    ASSERT_TRUE(state.valid);
    igi::HeliPreviewTick(&state, 0, NoInput());

    // `steer` is written by the axes assignment on every tick that is not the
    // reset (heli.c:362-364), so the input is what sets it -- writing the state
    // directly would be overwritten before Heli_Move reads it.
    igi::HeliPreviewTick(&state, 1, Steer(0, 1.0f));
    EXPECT_NEAR(state.move.turn[0], 0.6f, 1e-6);
    igi::HeliPreviewTick(&state, 2, Steer(0, 1.0f));
    EXPECT_NEAR(state.move.turn[0], 0.84f, 1e-6);
    // The other two axes never saw input.
    EXPECT_FLOAT_EQ(state.move.turn[1], 0.0f);
    EXPECT_FLOAT_EQ(state.move.turn[2], 0.0f);
}

TEST(HeliPreview, TheTurnAddsTheEnginesThreeForces) {
    // heli.c:443-455. With t0 = t1 = t2 = 0.6, mass = 3000, rate = 50:
    //   f1[1] = a0 * mass * kYawForce       = 30 * 3000 * -0.8  = -72000
    //   f2[0] = a1 * mass                   = 30 * 3000        =  90000
    //   f3[0] = s * a2 * mass * kRollForce  = 1 * 30 * 3000 * 0.08 = 7200
    // The fixed points are the engine's own (heli.c:445-447): f1/f2 sit at
    // z = 819.2 and 409.6, f3 at y = 12288.
    igi::HeliPreviewState state = PreviewAt(1.0f, 10.0f, 0.0f, 0.0f);
    ASSERT_TRUE(state.valid);
    igi::HeliPreviewTick(&state, 0, NoInput());
    state.move.mass = 3000.0f;
    // One tick with all three axes held: `steer` is set from the input, and the
    // smoothing turns each 1.0 into 0.6 (smooth = 0.4).
    igi::HeliPreviewInput held;
    held.steer[0] = held.steer[1] = held.steer[2] = 1.0f;
    igi::HeliPreviewTick(&state, 1, held);
    ASSERT_NEAR(state.move.turn[0], 0.6f, 1e-6);
    ASSERT_NEAR(state.move.turn[1], 0.6f, 1e-6);
    ASSERT_NEAR(state.move.turn[2], 0.6f, 1e-6);

    // The NEXT tick's Heli_Move is what turns that smoothed state into forces.
    // Holding the same input keeps the turn values at 0.84, so ask for the forces
    // from a fresh step on a copy whose turn values are the 0.6 this tick made.
    igi::HeliMoveState forces_state = state.move;
    forces_state.turn[0] = forces_state.turn[1] = forces_state.turn[2] = 0.6f;
    forces_state.steer[0] = forces_state.steer[1] = forces_state.steer[2] = 0.6f;
    forces_state.body_speed = 10.0f;

    igi::HeliMoveResult result;
    igi::HeliMoveStep(&forces_state, BellConfig(), &result);

    ASSERT_EQ(result.force_count, 3);
    // The engine's float arithmetic rounds each product to float; 72000 and
    // 90000 are not exactly representable in the last bits at this magnitude, so
    // the tolerance is a float ulp (0.0078125 at 72000).
    EXPECT_NEAR(result.forces[0].force[1], -72000.0f, 0.0078125f);
    EXPECT_FLOAT_EQ(result.forces[0].point[2], 819.2f);
    EXPECT_NEAR(result.forces[1].force[0], 90000.0f, 0.0078125f);
    EXPECT_FLOAT_EQ(result.forces[1].point[2], 409.6f);
    EXPECT_NEAR(result.forces[2].force[0], 7200.0f, 0.0078125f);
    EXPECT_FLOAT_EQ(result.forces[2].point[1], 12288.0f);
}

TEST(HeliPreview, ATouchingHelicopterBelowTwoUnitsTurnsNot) {
    // heli.c:440-441: the turn-force gate is `!touching || body.speed > 2`.
    igi::HeliPreviewState state = PreviewAt(1.0f, 10.0f, 0.0f, 0.0f);
    ASSERT_TRUE(state.valid);
    igi::HeliPreviewTick(&state, 0, NoInput());
    igi::HeliPreviewTick(&state, 1, Steer(0, 1.0f));
    state.move.touching = 1;
    state.move.body_speed = 2.0f;  // not > 2
    const float smoothed_turn = state.move.turn[0];

    igi::HeliMoveResult result;
    igi::HeliMoveStep(&state.move, BellConfig(), &result);
    EXPECT_EQ(result.force_count, 0);
    EXPECT_FALSE(result.turn_forces_added);
    // The smoothing still ran -- only the forces are gated. One more tick of the
    // same input smooths 0.6 towards 1.0: 0.4*0.6 + 0.6 = 0.84.
    EXPECT_NEAR(result.turn[0], 0.4f * smoothed_turn + 0.6f, 1e-6);
    EXPECT_NEAR(result.turn[0], 0.84f, 1e-6);
}

// ===========================================================================
// The nose pitch (heli.c:457-485)
// ===========================================================================

TEST(HeliPreview, TheNosePitchIsABodyFramePostMultiply) {
    // heli.c:457-484. `nm = m * r`, so the pitch is about the helicopter's OWN
    // X axis; `r * m` would be a world-axis rotation and is a different matrix.
    //
    // The pitch angle is heli.c:463-472. The heading basis is `(m[0], m[3], m[6])`
    // normalised (`cx`, `cy`), and
    //     pitch = cy * m[5] + cx * m[2]
    // then `* kPitchDown` (-0.7) when m[8] > 0, else `* kSevenTenths` (0.7).
    float identity[9];
    Identity(identity);
    // cx = 1, cy = 0, m[2] = m[5] = 0 -> pitch = 0, and 0 * anything is 0.
    EXPECT_NEAR(static_cast<double>(igi::HeliNosePitch(identity, 0.0f, nullptr)), 0.0, 1e-12);

    // A level basis whose local X leans +Z: m[2] = 0.5, m[8] = 1 (still > 0).
    float lean_z[9];
    Identity(lean_z);
    lean_z[2] = 0.5f;
    EXPECT_NEAR(static_cast<double>(igi::HeliNosePitch(lean_z, 0.0f, nullptr)),
                0.5 * igi::kHeliPitchDown, 1e-9);

    // A basis whose local Z points DOWN (m[8] = -1) takes the kSevenTenths arm
    // even though m[2] is the same +0.5.
    float nose_down_basis[9];
    Identity(nose_down_basis);
    nose_down_basis[2] = 0.5f;
    nose_down_basis[8] = -1.0f;
    EXPECT_NEAR(static_cast<double>(igi::HeliNosePitch(nose_down_basis, 0.0f, nullptr)),
                0.5 * igi::kHeliSevenTenths, 1e-9);

    // The composition is the engine's inline product, which for an identity `m`
    // is the rotation matrix itself.
    const long double nose_pitch = igi::HeliNosePitch(lean_z, 0.0f, nullptr);
    float r[9];
    igi::HeliSetRotX(r, static_cast<float>(nose_pitch * BellConfig().axis[2].rate *
                                           igi::kHeliPitchScale));
    float expected[9];
    igi::HeliMatrixMul(expected, identity, r, 1, 1, 1);

    float got[9];
    int got_count = 0;
    igi::HeliComposeNoseMatrix(identity, 0, BellConfig(), nose_pitch, got, &got_count);
    for (int i = 0; i < 9; ++i) EXPECT_NEAR(got[i], expected[i], 1e-6) << "m " << i;
    EXPECT_EQ(got_count, 1);  // m.count + r.count + 1 (heli.c:483)
    // The rotation really happened: the local Y axis (m[1], m[4], m[7]) moved in Z.
    EXPECT_GT(std::fabs(got[7]), 1e-6);
}

TEST(HeliPreview, TheNosePitchOnlyAppliesAirborneAndMoving) {
    // heli.c:457: `!touching && body.speed > 2`.
    igi::HeliPreviewState state = PreviewAt(1.0f, 10.0f, 0.0f, 0.0f);
    ASSERT_TRUE(state.valid);
    igi::HeliPreviewTick(&state, 0, NoInput());
    state.move.touching = 1;   // on the ground
    state.move.body_speed = 50.0f;

    float before[9];
    for (int i = 0; i < 9; ++i) before[i] = state.move.matrix[i];

    igi::HeliMoveResult result;
    igi::HeliMoveStep(&state.move, BellConfig(), &result);
    EXPECT_FALSE(result.nose_pitch_applied);
    for (int i = 0; i < 9; ++i) EXPECT_FLOAT_EQ(result.matrix[i], before[i]) << "m " << i;

    // Airborne and fast enough: it applies.
    state.move.touching = 0;
    igi::HeliMoveStep(&state.move, BellConfig(), &result);
    EXPECT_TRUE(result.nose_pitch_applied);
    EXPECT_EQ(result.matrix_count, 1);
}

TEST(HeliPreview, TheDegenerateHeadingBasisUsesTheStaleSlot) {
    // heli.c:463-476: when `m[0]` and `m[3]` are both zero the engine reads the
    // stack slot that held f2[0]. The port carries it as `stale`.
    float degenerate[9];
    Identity(degenerate);
    degenerate[0] = 0.0f;
    degenerate[3] = 0.0f;

    bool used_stale = false;
    const long double pitch = igi::HeliNosePitch(degenerate, 1234.5f, &used_stale);
    EXPECT_TRUE(used_stale);
    EXPECT_NEAR(static_cast<double>(pitch), 1234.5, 1e-4);
}

TEST(HeliPreview, ThePitchRotatesTheBasisAndLeavesEveryAxisUnitLength) {
    // The pitch is a rotation of the basis about its own X, so the local X axis
    // is fixed and the local Y and Z axes swing in the YZ plane. This is the
    // preview-visible consequence of the rule: the helicopter's nose does not
    // move, its belly does.
    igi::HeliPreviewState state = PreviewAt(1.0f, 10.0f, 0.0f, 0.0f);
    ASSERT_TRUE(state.valid);
    igi::HeliPreviewTick(&state, 0, NoInput());
    state.move.body_speed = 50.0f;
    state.move.touching = 0;

    // A basis with a non-zero nose pitch. It must be a real rotation for the
    // length-preservation assertion below to mean anything, and a pure X
    // rotation is NOT one: it leaves the heading (m[0], m[3], m[6]) at (1,0,0),
    // so `pitch = cx * m[2]` is 0 and nothing is composed. A Y rotation tilts the
    // heading up, which is what the engine's formula reacts to.
    igi::HeliSetEuler(state.move.matrix, 0.0f, 0.3f, 0.0f);

    const double before_x[3] = {state.move.matrix[0], state.move.matrix[3], state.move.matrix[6]};
    const double before_y[3] = {state.move.matrix[1], state.move.matrix[4], state.move.matrix[7]};

    igi::HeliMoveResult result;
    igi::HeliMoveStep(&state.move, BellConfig(), &result);
    ASSERT_TRUE(result.nose_pitch_applied);

    const double after_x[3] = {result.matrix[0], result.matrix[3], result.matrix[6]};
    const double after_y[3] = {result.matrix[1], result.matrix[4], result.matrix[7]};

    // The local X axis is the rotation axis: unchanged.
    for (int i = 0; i < 3; ++i) EXPECT_NEAR(after_x[i], before_x[i], 1e-6) << "x " << i;
    // The local Y axis moved, in Z.
    EXPECT_GT(std::fabs(after_y[2] - before_y[2]), 1e-6);

    // Both axes keep unit length: the product is a rotation, not a scale.
    const double len_y_before = std::sqrt(before_y[0] * before_y[0] + before_y[1] * before_y[1] +
                                          before_y[2] * before_y[2]);
    const double len_y_after = std::sqrt(after_y[0] * after_y[0] + after_y[1] * after_y[1] +
                                         after_y[2] * after_y[2]);
    EXPECT_NEAR(len_y_after, len_y_before, 1e-6);
}

// ===========================================================================
// The rotor sound parameters (heli.c:508-515)
// ===========================================================================

TEST(HeliPreview, TheRotorSoundFollowsTheSpeed) {
    igi::HeliPreviewState state = PreviewAt(1.0f, 1.0f, 0.0f, 0.0f);
    ASSERT_TRUE(state.valid);
    igi::HeliPreviewTick(&state, 0, NoInput());

    igi::HeliMoveResult result;
    igi::HeliMoveStep(&state.move, BellConfig(), &result);
    EXPECT_TRUE(result.sound_updated);
    EXPECT_FLOAT_EQ(result.sound_params[0], 2.0f);  // SoundParams_New(2)
    EXPECT_FLOAT_EQ(result.sound_params[1], result.speed);
    EXPECT_FLOAT_EQ(result.sound_params[2], 1.0f);  // speed > 0.01

    // A standstill is "not fast" (heli.c:512).
    state.move.speed = 0.0f;
    state.move.target_speed = 0.0f;
    igi::HeliMoveStep(&state.move, BellConfig(), &result);
    EXPECT_FLOAT_EQ(result.sound_params[2], 0.0f);
}

// ===========================================================================
// The position step (PhysicsObj_Collide, physobj.c:422-426)
// ===========================================================================

TEST(HeliPreview, PositionAdvancesByTheVelocityOnlyAboveTheSlopThreshold) {
    // physobj.c:47: kSlop = 40.96. Below it the body is "not moving" and the
    // position does not advance at all -- it does not advance more slowly.
    double pos[3] = {100.0, 200.0, 300.0};

    const float below[3] = {40.0f, 0.0f, 0.0f};
    igi::HeliPreviewAdvancePosition(pos, below);
    EXPECT_DOUBLE_EQ(pos[0], 100.0);

    const float above[3] = {100.0f, 0.0f, 0.0f};
    igi::HeliPreviewAdvancePosition(pos, above);
    EXPECT_DOUBLE_EQ(pos[0], 200.0);
    EXPECT_DOUBLE_EQ(pos[1], 200.0);
    EXPECT_DOUBLE_EQ(pos[2], 300.0);

    // The gate is on the FAST length, not on any one component: a diagonal
    // vector whose components are all below 40.96 still passes it.
    const float diagonal[3] = {30.0f, 30.0f, 30.0f};
    EXPECT_GT(igi::FastLength3(diagonal), 40.96f);
    igi::HeliPreviewAdvancePosition(pos, diagonal);
    EXPECT_DOUBLE_EQ(pos[0], 230.0);
    EXPECT_DOUBLE_EQ(pos[1], 230.0);
    EXPECT_DOUBLE_EQ(pos[2], 330.0);
}

TEST(HeliPreview, TheFlownPositionIsTheAuthoredStartPlusWholeTicksOfVelocity) {
    // End to end: tick 0 puts the helicopter at the authored position with the
    // authored velocity, and each later tick adds exactly one velocity step
    // (damp = 1.0 for a helicopter, heli.c:500).
    igi::HeliPreviewState state = igi::BuildHeliPreviewState(
        HeliTokensFor(1000.0, 2000.0, 3000.0, 0.0f, 0.0f, 0.0f, 1.0f, 100.0f, 0.0f, 0.0f),
        BellConfig());
    ASSERT_TRUE(state.valid);
    igi::HeliPreviewTick(&state, 0, NoInput());
    EXPECT_DOUBLE_EQ(state.pos[0], 1000.0);

    igi::HeliPreviewRunToTick(&state, 5, NoInput());
    EXPECT_DOUBLE_EQ(state.pos[0], 1000.0 + 5.0 * 100.0);
    EXPECT_DOUBLE_EQ(state.pos[1], 2000.0);
    EXPECT_DOUBLE_EQ(state.pos[2], 3000.0);
    // speed == target, so the velocity never changed.
    EXPECT_FLOAT_EQ(state.vel[0], 100.0f);
}

// ===========================================================================
// The preview clock
// ===========================================================================

TEST(HeliPreview, TheClockRunsAtTheEnginesThirtyHertz) {
    // kFps = 30 (src/math/mathvars.h:8), the rate `AnimTask_Run` steps the
    // control channels at (animtask.c:292-298).
    EXPECT_FLOAT_EQ(igi::kHeliPreviewTicksPerSecond, 30.0f);
    EXPECT_EQ(igi::HeliPreviewTickFromSeconds(0.0f), 0);
    EXPECT_EQ(igi::HeliPreviewTickFromSeconds(1.0f), 30);
    EXPECT_EQ(igi::HeliPreviewTickFromSeconds(2.5f), 75);
    EXPECT_EQ(igi::HeliPreviewTickFromSeconds(-1.0f), 0);
}

// ===========================================================================
// The engine Matrix -> GLM conversion the draw path uses
// ===========================================================================

TEST(HeliPreview, TheEngineMatrixMapsToTheGlmBasisWithoutTransposing) {
    // The engine rotates a vector as `v_j = sum_i v_i * m[3i+j]` (heli.c:390-392,
    // body.c:218-220, and Matrix_SetRotX/SetRotZ themselves), i.e. it treats the
    // 3x3 as ROW-major and the image of the local X axis is the first ROW
    // (m[0], m[1], m[2]). `Direct3D_MakeMatrix` transposes that into D3D's
    // row-vector convention (`_11 = rot[0]`, `_12 = rot[3]`, `_13 = rot[6]`,
    // src/d3d/direct3d.c:1211-1237), which is the same product.
    //
    // GLM fills COLUMNS, so passing m[0..8] in order stores that same array as
    // the columns of the GLM matrix, and `glm::mat3 * v` then reproduces
    // `v_j = sum_i v_i * m[3i+j]` exactly. The conversion is therefore the
    // identity on the array, and the image of the local X axis is the first row.
    //
    // (The angle is built from acos(0) rather than a decimal literal: the double
    // pi/2 rounds UP to the nearest float, which lands just past the pole and
    // makes cos/sin read as a tiny negative and -1, i.e. the wrong quadrant.)
    float rotZ[9];
    igi::HeliSetRotZ(rotZ, std::acos(0.0f));
    EXPECT_NEAR(rotZ[0], 0.0f, 1e-6);
    EXPECT_NEAR(rotZ[1], -1.0f, 1e-6);

    const glm::mat3 glm_rot = igi::IgiEngineMatrixToGlm(rotZ);
    const glm::vec3 engine_x(rotZ[0], rotZ[1], rotZ[2]);   // the matrix's first ROW
    const glm::vec3 mapped_x = glm_rot * glm::vec3(1.0f, 0.0f, 0.0f);
    EXPECT_NEAR(mapped_x.x, engine_x.x, 1e-6);
    EXPECT_NEAR(mapped_x.y, engine_x.y, 1e-6);
    EXPECT_NEAR(mapped_x.z, engine_x.z, 1e-6);
    EXPECT_NEAR(mapped_x.y, -1.0f, 1e-6);

    // The identity maps to the identity.
    float identity[9];
    Identity(identity);
    const glm::mat3 glm_identity = igi::IgiEngineMatrixToGlm(identity);
    const glm::vec3 id_x = glm_identity * glm::vec3(1.0f, 0.0f, 0.0f);
    EXPECT_NEAR(id_x.x, 1.0f, 1e-6);
    EXPECT_NEAR(id_x.y, 0.0f, 1e-6);

    // A post-multiply about the object's own X stays a local-X rotation: after
    // the Z turn, the object's local X is the world -Y, and a local-X spin leaves
    // it there.
    const glm::mat3 local_x =
        glm::mat3(glm::rotate(glm::mat4(1.0f), 0.5f, glm::vec3(1.0f, 0.0f, 0.0f)));
    const glm::mat3 spun = glm_rot * local_x;
    const glm::vec3 spun_x = spun * glm::vec3(1.0f, 0.0f, 0.0f);
    EXPECT_NEAR(spun_x.y, -1.0f, 1e-6) << "the local X axis is what a local-X spin leaves alone";
}

// ===========================================================================
// The retail corpus
// ===========================================================================

TEST(HeliPreviewCorpus, TheLevelTwoHelicopterFliesAtItsAuthoredValues) {
    const std::vector<std::string> tokens = Level2HeliTokens();
    if (tokens.empty()) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string();
    }

    // The shipped LEVEL2/OBJECTS.QVM places one helicopter: Task_New(3344,
    // "Heli", "", 126942784, -38370560, 175304928, 0, 0, 3.8061590194702148,
    // 0.75, 0, 0, 0, "709_01_1", TRUE, FALSE, "", "", "", 180, 360, 60)
    // (LEVEL2/objects.qsc:1100-1101).
    const igi::HeliPreviewState state = igi::BuildHeliPreviewState(tokens, BellConfig());
    ASSERT_TRUE(state.valid) << "the shipped Heli call did not parse";
    EXPECT_DOUBLE_EQ(state.start_pos[0], 126942784.0);
    EXPECT_DOUBLE_EQ(state.start_pos[1], -38370560.0);
    EXPECT_DOUBLE_EQ(state.start_pos[2], 175304928.0);
    EXPECT_FLOAT_EQ(state.start_speed, 0.75f);
    EXPECT_FLOAT_EQ(state.start_vel[0], 0.0f);
    EXPECT_FLOAT_EQ(state.start_vel[1], 0.0f);
    EXPECT_FLOAT_EQ(state.start_vel[2], 0.0f);

    // The authored "Speed" is the zero triplet, so the helicopter's body
    // velocity is zero, its body speed is below kSlop and the preview holds the
    // authored position -- the honest result for this placement, not a
    // fabricated flight.
    igi::HeliPreviewState flown = state;
    igi::HeliPreviewRunToTick(&flown, 30, NoInput());
    EXPECT_FLOAT_EQ(flown.vel[0], 0.0f);
    EXPECT_FLOAT_EQ(flown.move.body_speed, 0.0f);
    EXPECT_DOUBLE_EQ(flown.pos[0], 126942784.0);
    EXPECT_DOUBLE_EQ(flown.pos[1], -38370560.0);
    EXPECT_DOUBLE_EQ(flown.pos[2], 175304928.0);
    // speed == target == "Original Thrust", so the ramp is a no-op and the
    // speed is the authored 0.75 one second in.
    EXPECT_FLOAT_EQ(flown.move.speed, 0.75f);
}

TEST(HeliPreviewCorpus, TheBellPhysicsQvmResolvesToTheConfigThePreviewUses) {
    const std::string text = DecompiledHeliQvm("BELL");
    if (text.empty()) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string();
    }

    igi::HeliVehicleConfig cfg{};
    ASSERT_TRUE(igi::ResolveHeliVehicleConfig(text, "709_01_1", &cfg));
    EXPECT_TRUE(cfg.valid);
    EXPECT_FLOAT_EQ(cfg.start_speed_scale, 200.0f);
    for (int i = 0; i < 3; ++i) {
        EXPECT_FLOAT_EQ(cfg.axis[i].rate, 50.0f) << "axis " << i;
        EXPECT_FLOAT_EQ(cfg.axis[i].smooth, 0.4f) << "axis " << i;
    }
    EXPECT_FLOAT_EQ(cfg.thrust, 3000.0f);
    EXPECT_FLOAT_EQ(cfg.accel, 0.027000000700354576f);
    EXPECT_FLOAT_EQ(cfg.accel_slow, 0.0030000000260770321f);
    ASSERT_EQ(cfg.part_count, 3);

    // A model the file does not define is a refusal, not a fallback to the
    // first helicopter in the file.
    igi::HeliVehicleConfig other{};
    EXPECT_FALSE(igi::ResolveHeliVehicleConfig(text, "no_such_model", &other));
    // An empty model asks for the first TASKTYPE_HELI definition, which is what
    // the renderer's resolver does.
    EXPECT_TRUE(igi::ResolveHeliVehicleConfig(text, "", &other));
    EXPECT_FLOAT_EQ(other.accel, cfg.accel);
}

TEST(HeliPreviewCorpus, TheMilPhysicsQvmHasItsOwnConfig) {
    const std::string text = DecompiledHeliQvm("MIL");
    if (text.empty()) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string();
    }

    // The MIL airframe is the second shipped helicopter: model 700_01_1, three
    // aero parts, and the only one of the two that populates both HeliConfig
    // lists (tests/test_heli_rules.cpp pins those). Its own config is what
    // `Heli_Move` reads through `phys->data`, so the resolver must reach it by
    // model name and not fall back to the BELL file's numbers.
    igi::HeliVehicleConfig cfg{};
    ASSERT_TRUE(igi::ResolveHeliVehicleConfig(text, "700_01_1", &cfg));
    EXPECT_TRUE(cfg.valid);
    EXPECT_FLOAT_EQ(cfg.start_speed_scale, 200.0f);
    ASSERT_EQ(cfg.part_count, 3);
    // `HeliAccelRate` reads both rates (heli.c:416), so a config with zero rates
    // would silently freeze the ramp. The MIL file authors its own; they are
    // pinned by value in tests/test_heli_rules.cpp's MIL corpus test.
    EXPECT_GT(cfg.accel, 0.0f);
    EXPECT_GT(cfg.accel_slow, 0.0f);
    EXPECT_GT(cfg.axis[2].rate, 0.0f);
}

TEST(HeliPreviewCorpus, TheRendererResolversWholeChainRunsOnTheShippedQvm) {
    // The renderer's `Renderer_Objects::HeliPreviewConfig` cannot be linked in the
    // host lane (it lives in the Windows-only renderer), so this test runs the
    // SAME chain it does, in the same order, over the same file:
    //
    //     QVM_Parse -> QVM_DecompileToString -> igi::ResolveHeliVehicleConfig
    //
    // which is what makes the shipped physics definition reach the preview. If
    // any link in that chain breaks, the preview silently runs on zero rates and
    // the helicopter stops pitching; this is the test that would catch it.
    const fs::path path = GameRoot() / "physicsobj" / "HELIS" / "BELL" / "HELI.QVM";
    if (!fs::is_regular_file(path)) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string();
    }

    const QVMFile qvm = QVM_Parse(path.string());
    ASSERT_TRUE(qvm.valid) << qvm.error;
    const std::string text = QVM_DecompileToString(qvm);
    ASSERT_FALSE(text.empty());

    igi::HeliVehicleConfig cfg{};
    ASSERT_TRUE(igi::ResolveHeliVehicleConfig(text, "", &cfg));
    ASSERT_TRUE(cfg.valid);

    // And the resolved config is the one the preview flies with: a helicopter at
    // a standstill under a zero-throttle request ramps down at `accel_slow`,
    // which is 0.003 for this file. A zero-rate config would leave the speed
    // untouched and this assertion would fail.
    const std::vector<std::string> tokens = Level2HeliTokens();
    if (tokens.empty()) GTEST_SKIP() << "retail LEVEL2 not found";
    igi::HeliPreviewState state = igi::BuildHeliPreviewState(tokens, cfg);
    ASSERT_TRUE(state.valid);
    igi::HeliPreviewTick(&state, 0, igi::HeliPreviewInput{});
    const float start_speed = state.move.speed;
    igi::HeliPreviewTick(&state, 1, Throttle(0.0f));
    EXPECT_NEAR(state.move.speed, start_speed - cfg.accel_slow, 1e-6);
}
