/******************************************************************************
 * @file    test_ladder_placement.cpp
 * @brief   Host-native tests for the ladder placement parity maths of GitHub
 *          issue #68 ("Ladder placement parity: activation volume, mount points,
 *          climb-line gizmos").
 *
 * Every expectation below is derived from the IGI-RE oracle, cited by file:line.
 * The helpers under test are pure (source/level/task_schema.cpp), so this file
 * needs no GL, no windows.h and no level archive.
 *
 * Oracle: /Users/haseeb-mir/Documents/Code/IGI-RE
 *   src/props/ladder.c:65-93     Ladder_Create 0x440280 (rung count)
 *   src/props/ladder.c:122-175   Ladder_Use 0x4404C0 (activation, both mounts)
 *   src/props/ladder.c:200-203   kReachMin/kReachMax DATA 0x533868/0x533440
 *   src/weapon/humanplayer.c:130-133,273-340
 *                                HumanPlayer_UpdateLadder 0x4113E0 (mount points)
 *   src/humanstate/humanstate.c:1167-1206  HumanPlayer_LadderUpdate (climb state)
 *   src/humanbase/human.c:1269-1300        Human_UpdateUse (the use query)
 *   src/game/game.c:896-928               UseQuery_Consider 0x416AD0
 *   src/file/vec.c:189-208                Vec3_Angle 0x4B2B30
 *   src/math/matrix.c:181-191             Matrix_GetEuler 0x4B3C50
 *   src/missile/missile.c:304-322         Model_GetPoint/Model_GetLocalPoint
 *   src/plane/gear.c:156-157              kQuarterPi INIT(0.7853982f)
 *****************************************************************************/
#include <gtest/gtest.h>

#include "level/task_schema.h"

#include <cmath>
#include <cstdint>
#include <cstring>

using namespace TaskSchemaNS;
using namespace TaskSchemaNS::Props;

namespace {

constexpr double kPi = 3.14159265358979323846;

// 1 world unit = 1/4096 m (IGI-RE src/props/wire.c:320-321 kWireScale), so a
// centimetre is 40.96 units. Every ladder constant is a round number of
// centimetres, which is how the retail values were authored.
constexpr double kUnitsPerCm = 40.96;

Basis IdentityBasis() {
    Basis b;
    b.x = Vec3(1.0, 0.0, 0.0);
    b.y = Vec3(0.0, 1.0, 0.0);
    b.z = Vec3(0.0, 0.0, 1.0);
    return b;
}

// A ladder with its foot at `foot_z`, its head `height` above it and magic point
// 1 a little higher still, all on the identity frame.
LadderPlacement MakeLadder(double foot_z, double height, double top_ref_extra = 0.0) {
    const Vec3 origin(0.0, 0.0, 0.0);
    const Vec3 foot(0.0, 0.0, foot_z);
    const Vec3 head(0.0, 0.0, foot_z + height);
    const Vec3 activation_top(0.0, 0.0, foot_z + height + top_ref_extra);
    return ComputeLadderPlacement(origin, activation_top, foot, head, IdentityBasis());
}

// Column 1 of the view matrix for a yaw of `degrees` in the editor's convention
// (x = sin, y = cos), which is what `view_axis_y` carries.
Vec3 ViewAxisForYaw(double degrees) {
    const double r = degrees * kPi / 180.0;
    return Vec3(std::sin(r), std::cos(r), 0.0);
}

}  // namespace

// ── Constants ────────────────────────────────────────────────────────────────

// IGI-RE src/props/ladder.c:200-203
//   DATA(533440) double kReachMax INIT(2867.2);
//   DATA(533860) double kReachMin INIT(-2867.2);
// applied to the player's local X in ladder.c:138. 2867.2 units = 70 cm.
TEST(LadderParityConstants, HalfWidthIsSeventyCentimetres) {
    EXPECT_DOUBLE_EQ(kLadderHalfWidthUnits, 2867.2);
    EXPECT_DOUBLE_EQ(kLadderHalfWidthUnits, 70.0 * kUnitsPerCm);
}

// The activation radius is not a stored constant: Human_UpdateUse writes
// `q.unk78 = 0` then `q.range = kUseRange` (IGI-RE src/humanbase/human.c:1287-1288,
// kUseRange = 6.0f at human.c:168) into a HumanUseQuery whose fields at +0x78 and
// +0x7C are an int and a float. Ladder_Use reads `q->range` through the
// UseQuery/ViewQuery view (src/props/propstypes.h:14, src/game/game.c:190) where
// range is the double at +0x78, so the two words are reinterpreted as
// 0x40C0000000000000 = 8192.0.
TEST(LadderParityConstants, ActivationRadiusIsTheReinterpretedUseRange) {
    EXPECT_DOUBLE_EQ(kLadderActivateRadiusUnits, 8192.0);
    EXPECT_DOUBLE_EQ(kLadderActivateRadiusUnits, 200.0 * kUnitsPerCm);

    const uint32_t low_word = 0;             // human.c:1287 `q.unk78 = 0`
    const uint32_t high_word = 0x40C00000u;  // 6.0f (human.c:168)
    const uint64_t bits = (static_cast<uint64_t>(high_word) << 32) | low_word;
    double reinterpreted = 0.0;
    std::memcpy(&reinterpreted, &bits, sizeof(reinterpreted));
    EXPECT_DOUBLE_EQ(reinterpreted, kLadderActivateRadiusUnits);
}

// IGI-RE src/props/ladder.c:55 declares kLadderAngle; src/plane/gear.c:156-157
// gives it ALIAS(kLadderAngle, kQuarterPi) with INIT(0.7853982f).
TEST(LadderParityConstants, FacingGateIsFortyFiveDegrees) {
    EXPECT_DOUBLE_EQ(kLadderFacingAngleRadians, 0.7853982);
    EXPECT_NEAR(kLadderFacingAngleRadians, kPi / 4.0, 1e-6);
    EXPECT_NEAR(std::cos(kLadderFacingAngleRadians), kLadderFacingCosine, 1e-6);

    // The retail value is a *float*, and it rounds above pi/4:
    // (float)0.7853982 == 0.78539818525314331 > 0.78539816339744828. The gate in
    // ladder.c:157 is `!(angle >= kLadderAngle)`, so a player looking at the
    // ladder from exactly 45 degrees still passes it.
    EXPECT_GT(static_cast<double>(0.7853982f), kPi / 4.0);
}

// IGI-RE src/weapon/humanplayer.c:130-133.
TEST(LadderParityConstants, MountOffsetsAreCentimetreValues) {
    EXPECT_DOUBLE_EQ(kLadderBottomStandoffUnits, 1515.52);
    EXPECT_DOUBLE_EQ(kLadderBottomStandoffUnits, 37.0 * kUnitsPerCm);
    EXPECT_DOUBLE_EQ(kLadderBottomLiftUnits, 3915.776);
    EXPECT_NEAR(kLadderBottomLiftUnits, 95.6 * kUnitsPerCm, 1e-9);
    EXPECT_DOUBLE_EQ(kLadderTopBackUnits, 3481.6);
    EXPECT_DOUBLE_EQ(kLadderTopBackUnits, 85.0 * kUnitsPerCm);
    EXPECT_DOUBLE_EQ(kLadderTopStandoffUnits, 1638.4);
    EXPECT_DOUBLE_EQ(kLadderTopStandoffUnits, 40.0 * kUnitsPerCm);
}

// IGI-RE src/props/ladder.c:52 + DATA(533860) kRungScale INIT(1/1228.8).
TEST(LadderParityConstants, RungSpacingIsThirtyCentimetres) {
    EXPECT_DOUBLE_EQ(kLadderRungSpacingUnits, 1228.8);
    EXPECT_DOUBLE_EQ(kLadderRungSpacingUnits, 30.0 * kUnitsPerCm);
    EXPECT_DOUBLE_EQ(1.0 / kLadderRungSpacingUnits, 0.0008138020833333334);
    EXPECT_EQ(kLadderTopStepMargin, 6);            // humanplayer.c:323
    EXPECT_DOUBLE_EQ(kLadderClimbTimeSeconds, 6.0); // ladder.c:59-63
}

// ── Mount points (HumanPlayer_UpdateLadder) ──────────────────────────────────

// IGI-RE src/weapon/humanplayer.c:308-314 — entering from the bottom starts at
// magic point 2 plus kLadderBack along the matrix's +Y column and kLadderUp
// along its +Z column.
TEST(LadderMounts, BottomMountUsesPointTwoPlusStandoffAndLift) {
    const LadderPlacement l = MakeLadder(7.03, 25053.53, 2611.2);
    ASSERT_TRUE(l.valid);
    EXPECT_DOUBLE_EQ(l.bottomMount.x, 0.0);
    EXPECT_DOUBLE_EQ(l.bottomMount.y, kLadderBottomStandoffUnits);
    EXPECT_DOUBLE_EQ(l.bottomMount.z, 7.03 + kLadderBottomLiftUnits);
}

// IGI-RE src/weapon/humanplayer.c:299-306 — entering from the top starts at
// magic point 3 plus kLadderTopBack along +Z minus kLadderTopUp along +Y, i.e.
// on the opposite side of the face from the bottom mount.
TEST(LadderMounts, TopMountUsesPointThreeBackAndMinusStandoff) {
    const LadderPlacement l = MakeLadder(7.03, 25053.53, 2611.2);
    ASSERT_TRUE(l.valid);
    EXPECT_DOUBLE_EQ(l.topMount.x, 0.0);
    EXPECT_DOUBLE_EQ(l.topMount.y, -kLadderTopStandoffUnits);
    EXPECT_DOUBLE_EQ(l.topMount.z, 7.03 + 25053.53 + kLadderTopBackUnits);
    // The two mounts sit on opposite sides of the ladder face.
    EXPECT_GT(l.bottomMount.y, 0.0);
    EXPECT_LT(l.topMount.y, 0.0);
}

// The offsets ride the ladder's own columns, so a ladder rotated 90° about X
// (its +Z column pointing along world +Y) puts both mounts off the world Z axis
// exactly as the game does.
TEST(LadderMounts, OffsetsFollowTheLadderColumnsNotTheWorldAxes) {
    Basis b;
    b.x = Vec3(1.0, 0.0, 0.0);
    b.y = Vec3(0.0, 0.0, 1.0);   // +Y column now points up in world Z
    b.z = Vec3(0.0, 1.0, 0.0);   // +Z column now points along world +Y
    const LadderPlacement l = ComputeLadderPlacement(
        Vec3(0.0, 0.0, 0.0),
        Vec3(0.0, 0.0, 0.0),
        Vec3(0.0, 0.0, 0.0),
        Vec3(0.0, 25053.53, 0.0),
        b);
    ASSERT_TRUE(l.valid);
    EXPECT_DOUBLE_EQ(l.bottomMount.x, 0.0);
    EXPECT_DOUBLE_EQ(l.bottomMount.y, kLadderBottomLiftUnits);   // along +Z column
    EXPECT_DOUBLE_EQ(l.bottomMount.z, kLadderBottomStandoffUnits); // along +Y column
    EXPECT_DOUBLE_EQ(l.topMount.y, 25053.53 + kLadderTopBackUnits);
    EXPECT_DOUBLE_EQ(l.topMount.z, -kLadderTopStandoffUnits);
}

// A degenerate orientation (no authored rotation) cannot place a mount.
TEST(LadderMounts, DegenerateBasisIsInvalid) {
    Basis b;   // all-zero columns
    const LadderPlacement l = ComputeLadderPlacement(
        Vec3(0, 0, 0), Vec3(0, 0, 1), Vec3(0, 0, 0), Vec3(0, 0, 1), b);
    EXPECT_FALSE(l.valid);
}

// ── Rung count and climb line (Ladder_Create) ────────────────────────────────

// IGI-RE src/props/ladder.c:86-91 — `l->rungs = (int)round_ll(h * kRungScale)`
// with h = |point2.z - point3.z|. round_ll rounds to nearest, so a height of
// 38.8 rungs is 39, not the 38 a truncating conversion would give.
TEST(LadderClimbLine, RungCountRoundsToNearest) {
    // 314_01_1: foot z = 7.03, head z = 47681.5352 -> 38.7975 rungs.
    const LadderPlacement l = MakeLadder(7.03, 47674.5052);
    ASSERT_TRUE(l.valid);
    EXPECT_EQ(l.rungCount, 39);
    EXPECT_EQ(l.topStep, 33);   // rungCount - 6, humanplayer.c:323

    // 402_02_1: foot z = 0.645, head z = 20057.0879 -> 16.32 rungs -> 16.
    const LadderPlacement l2 = MakeLadder(0.645, 20056.4429);
    ASSERT_TRUE(l2.valid);
    EXPECT_EQ(l2.rungCount, 16);
    EXPECT_EQ(l2.topStep, 10);
}

// The rung count uses the *local* Z difference, so a ladder whose local +Z is
// horizontal still counts rungs (its world Z span is zero).
TEST(LadderClimbLine, RungCountUsesLocalNotWorldHeight) {
    Basis b;
    b.x = Vec3(1.0, 0.0, 0.0);
    b.y = Vec3(0.0, 1.0, 0.0);
    b.z = Vec3(0.0, 1.0, 0.0);   // +Z column along world +Y
    const LadderPlacement l = ComputeLadderPlacement(
        Vec3(0, 0, 0), Vec3(0, 0, 0), Vec3(0, 0, 0), Vec3(0, 25053.53, 0), b);
    ASSERT_TRUE(l.valid);
    EXPECT_EQ(l.rungCount, 20);
    EXPECT_EQ(l.topStep, 14);
}

// IGI-RE src/weapon/humanplayer.c:320-322 — the mounted yaw is
// Matrix_GetEuler(m).z + pi, and Matrix_GetEuler reads az = atan2(m[3], m[0]).
TEST(LadderClimbLine, PlayerYawIsMatrixEulerZPlusPi) {
    const LadderPlacement l = MakeLadder(0.0, 1228.8);
    ASSERT_TRUE(l.valid);
    EXPECT_NEAR(l.playerYawRadians, kPi, 1e-12);   // atan2(0, 1) + pi

    // A ladder yawed 90° (local +X along world +Y): atan2(1, 0) + pi = 3pi/2.
    Basis b = IdentityBasis();
    b.x = Vec3(0.0, 1.0, 0.0);
    b.y = Vec3(-1.0, 0.0, 0.0);
    const LadderPlacement l2 = ComputeLadderPlacement(
        Vec3(0, 0, 0), Vec3(0, 0, 1), Vec3(0, 0, 0), Vec3(0, 0, 1), b);
    ASSERT_TRUE(l2.valid);
    EXPECT_NEAR(l2.playerYawRadians, 3.0 * kPi / 2.0, 1e-12);
}

// ── Activation (Ladder_Use) ──────────────────────────────────────────────────

// A player standing in front of the ladder, within both bands and inside the
// world-Z span, facing it: the bottom offer (ladder.c:138-162).
TEST(LadderActivation, BottomOfferInsideTheVolume) {
    const LadderPlacement l = MakeLadder(0.0, 25053.53, 2611.2);
    const LadderActivation a = LadderActivate(
        l, Vec3(0.0, 1000.0, 4096.0), Vec3(0.0, -1.0, 0.0));
    EXPECT_TRUE(a.bottom);
    EXPECT_FALSE(a.top);
}

// ladder.c:138 is a strict band: |localY| < 8192 and -2867.2 < localX < 2867.2.
TEST(LadderActivation, BandsAreStrict) {
    const LadderPlacement l = MakeLadder(0.0, 25053.53, 2611.2);
    const Vec3 facing(0.0, -1.0, 0.0);

    EXPECT_TRUE(LadderActivate(l, Vec3(0.0, 8191.9, 4096.0), facing).bottom);
    EXPECT_FALSE(LadderActivate(l, Vec3(0.0, 8192.0, 4096.0), facing).bottom);
    EXPECT_FALSE(LadderActivate(l, Vec3(0.0, -8192.0, 4096.0), facing).bottom);

    EXPECT_TRUE(LadderActivate(l, Vec3(2866.9, 1000.0, 4096.0), facing).bottom);
    EXPECT_FALSE(LadderActivate(l, Vec3(2867.2, 1000.0, 4096.0), facing).bottom);
    EXPECT_FALSE(LadderActivate(l, Vec3(-2867.2, 1000.0, 4096.0), facing).bottom);
    EXPECT_TRUE(LadderActivate(l, Vec3(-2866.9, 1000.0, 4096.0), facing).bottom);
}

// ladder.c:157 — `topz > q->pos.z && q->pos.z > l->obj.pos.z`, both strict.
TEST(LadderActivation, HeightSpanIsStrict) {
    const LadderPlacement l = MakeLadder(0.0, 25053.53, 2611.2);
    const Vec3 facing(0.0, -1.0, 0.0);
    const double top_z = l.activationTop.z;

    EXPECT_TRUE(LadderActivate(l, Vec3(0.0, 1000.0, 1.0), facing).bottom);
    EXPECT_FALSE(LadderActivate(l, Vec3(0.0, 1000.0, 0.0), facing).bottom);
    EXPECT_FALSE(LadderActivate(l, Vec3(0.0, 1000.0, top_z), facing).bottom);
    EXPECT_TRUE(LadderActivate(l, Vec3(0.0, 1000.0, top_z - 1.0), facing).bottom);
}

// ladder.c:139-154 compares the view's own Y column against the ladder's Y
// column, flipped to the player's side, and gates on the angle (Vec3_Angle
// returns the angle, src/file/vec.c:189-208).
TEST(LadderActivation, FacingGateAcceptsUpToFortyFiveDegrees) {
    const LadderPlacement l = MakeLadder(0.0, 25053.53, 2611.2);
    const Vec3 eye(0.0, 1000.0, 4096.0);   // in front, so n = -axisY

    EXPECT_TRUE(LadderActivate(l, eye, ViewAxisForYaw(180.0)).bottom);   // dead on
    EXPECT_TRUE(LadderActivate(l, eye, ViewAxisForYaw(180.0 - 44.0)).bottom);
    // Exactly 45° passes: kLadderAngle is the float 0.7853982f, which is a hair
    // above pi/4 (see LadderParityConstants.FacingGateIsFortyFiveDegrees).
    EXPECT_TRUE(LadderActivate(l, eye, ViewAxisForYaw(180.0 - 45.0)).bottom);
    EXPECT_FALSE(LadderActivate(l, eye, ViewAxisForYaw(180.0 - 46.0)).bottom);
    EXPECT_FALSE(LadderActivate(l, eye, ViewAxisForYaw(0.0)).bottom);    // facing away
}

// The ladder's Y column is flipped when the player is on its +Y side
// (ladder.c:149-153), so the same yaw works from either side.
TEST(LadderActivation, FacingUsesThePlayersOwnSide) {
    const LadderPlacement l = MakeLadder(0.0, 25053.53, 2611.2);
    EXPECT_TRUE(LadderActivate(l, Vec3(0.0, 1000.0, 4096.0),
                               ViewAxisForYaw(180.0)).bottom);
    EXPECT_TRUE(LadderActivate(l, Vec3(0.0, -1000.0, 4096.0),
                               ViewAxisForYaw(0.0)).bottom);
}

// ladder.c:164-173 — the top offer needs the player on the ladder's -Y side of
// magic point 1, within UseQuery_Consider's own radius, with the point ahead of
// the view (src/game/game.c:896-928).
TEST(LadderActivation, TopOfferNeedsTheReferencePointAheadAndInRange) {
    const LadderPlacement l = MakeLadder(0.0, 25053.53, 2611.2);
    const Vec3 top = l.activationTop;   // (0, 0, 27664.73)

    // On the reference's -Y side, just above it (so ladder.c:157's strict
    // `topz > q->pos.z` fails) and looking at it: the top offer alone.
    const Vec3 above(top.x, -1000.0, top.z + 100.0);
    const LadderActivation on = LadderActivate(l, above, ViewAxisForYaw(0.0));
    EXPECT_TRUE(on.top);
    EXPECT_FALSE(on.bottom);

    // Beyond the 8192 radius the reference point is out of range.
    EXPECT_FALSE(LadderActivate(l, Vec3(top.x, -8193.0, top.z + 100.0),
                                ViewAxisForYaw(0.0)).top);
    // Looking away from it.
    EXPECT_FALSE(LadderActivate(l, above, ViewAxisForYaw(180.0)).top);
    // On the +Y side of the reference the top branch does not apply.
    EXPECT_FALSE(LadderActivate(l, Vec3(top.x, 1000.0, top.z + 100.0),
                                ViewAxisForYaw(180.0)).top);
}

// ── Retail fixture ───────────────────────────────────────────────────────────

// The shipped 400_17_1.mef (a TASKTYPE_LADDER model — MAGICOBJ.QVM) carries four
// XTVM points; the handlers index them 0-based, so point 1 = index 1 is the
// activation reference, point 2 = index 2 the foot and point 3 = index 3 the
// head. Values as read by the editor's own MEF parser
// (MISSIONS/location0/LEVEL4/MODELS/models/400_17_1.mef).
TEST(LadderRetailFixture, FourHundredSeventeenOneMatchesTheOracleMaths) {
    const Vec3 activation_top(-9.1216, -85.8444, 27671.7578);
    const Vec3 foot(-6.5441, -85.8444, 7.0300);
    const Vec3 head(-9.0866, -85.8444, 25060.5566);
    const LadderPlacement l = ComputeLadderPlacement(
        Vec3(0.0, 0.0, 0.0), activation_top, foot, head, IdentityBasis());
    ASSERT_TRUE(l.valid);

    EXPECT_EQ(l.rungCount, 20);    // 25053.5266 / 1228.8 = 20.389
    EXPECT_EQ(l.topStep, 14);

    EXPECT_DOUBLE_EQ(l.bottomMount.x, foot.x);
    EXPECT_DOUBLE_EQ(l.bottomMount.y, foot.y + kLadderBottomStandoffUnits);
    EXPECT_DOUBLE_EQ(l.bottomMount.z, foot.z + kLadderBottomLiftUnits);

    EXPECT_DOUBLE_EQ(l.topMount.x, head.x);
    EXPECT_DOUBLE_EQ(l.topMount.y, head.y - kLadderTopStandoffUnits);
    EXPECT_DOUBLE_EQ(l.topMount.z, head.z + kLadderTopBackUnits);

    // The activation reference is above the head: that is what makes
    // ladder.c:157's `topz > q->pos.z` test cover the whole ladder.
    EXPECT_GT(activation_top.z, head.z);
    EXPECT_GT(l.topMount.z, l.bottomMount.z);
}
