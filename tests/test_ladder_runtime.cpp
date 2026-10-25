/******************************************************************************
 * @file    test_ladder_runtime.cpp
 * @brief   Host-native tests for the ladder RUNTIME parity rules of GitHub
 *          issue #68 (task-12): rung count, use-query selection, facing test.
 *
 * The subject is source/player_ladder.cpp, the play-mode ladder state the
 * runtime drives from source/runtime/runtime_world.cpp. Everything under test is
 * pure (glm only), so this file needs no GL, no windows.h and no level archive.
 *
 * Oracle: /Users/haseeb-mir/Documents/Code/IGI-RE
 *   src/props/ladder.c:65-93     Ladder_Create 0x440280 (rung count, round_ll)
 *   src/props/ladder.c:122-175   Ladder_Use 0x4404C0 (both offers)
 *   src/props/ladder.c:200-203   kReachMin/kReachMax DATA 0x533868/0x533440
 *   src/props/props.h:50-57      round_ll = x87 fistpll (current rounding mode)
 *   src/plane/gear.c:156-157     kLadderAngle = kQuarterPi = 0.7853982f
 *   src/game/game.c:896-928      UseQuery_Consider 0x416AD0
 *   src/game/game.h:182-192      ViewQuery: bestPitch 0x70, bestYaw 0x74
 *   src/props/propstypes.h:6-16  UseQuery: the same two fields
 *   src/game/misc.c:42-72        Vec3d_PitchAngle/Vec3d_YawAngle 0x4148F0/0x414950
 *   src/file/vec.c:183-208       IsZero / Vec3_Angle 0x4B2B30
 *   src/humanbase/human.c:1269-1300 Human_UpdateUse (the scan + kQuarterPi bounds)
 *   src/humanplayer.c:130-133,273-340 (mount points, exercised in task-9)
 *****************************************************************************/
#include <gtest/gtest.h>

#include "player_ladder.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <vector>

using igi::LadderPlacement;
using igi::LadderUseQuery;
using igi::ViewFrame;

namespace {

constexpr double kPi = 3.14159265358979323846;

// IGI-RE composes orientations as Rz*Ry*Rx (src/math/matrix.c:99-110
// Matrix_SetEuler 0x4B38E0); glm post-multiplies each rotate() call, so this
// order matches.
glm::mat3 RotationBasis(float x_degrees, float y_degrees, float z_degrees) {
    glm::mat4 basis(1.0f);
    basis = glm::rotate(basis, glm::radians(z_degrees), glm::vec3(0.0f, 0.0f, 1.0f));
    basis = glm::rotate(basis, glm::radians(y_degrees), glm::vec3(0.0f, 1.0f, 0.0f));
    basis = glm::rotate(basis, glm::radians(x_degrees), glm::vec3(1.0f, 0.0f, 0.0f));
    return glm::mat3(basis);
}

// A ladder with the identity basis: +X across, +Y the face normal (the side the
// player mounts from, see TopStandoffUnits) and +Z the climb axis.
LadderPlacement MakeLadder(const glm::vec3& origin, const glm::vec3& activation_top) {
    return LadderPlacement(
        origin,
        activation_top,
        origin,
        origin + glm::vec3(0.0f, 0.0f, 20000.0f),
        glm::mat3(1.0f));
}

// A view frame whose look direction is `forward` (column 1) and whose columns are
// right-handed: right x forward = up. Builds the same frame the runtime builds in
// source/runtime/runtime_world.cpp (BuildPlayerViewFrame).
ViewFrame MakeView(const glm::vec3& forward) {
    ViewFrame view;
    view.forward = glm::normalize(forward);
    view.right = glm::normalize(glm::cross(view.forward, glm::vec3(0.0f, 0.0f, 1.0f)));
    view.up = glm::cross(view.right, view.forward);
    return view;
}

ViewFrame MakeView(float yaw_degrees) {
    const float yaw_radians = glm::radians(yaw_degrees);
    return MakeView(glm::vec3(
        std::sin(yaw_radians),
        std::cos(yaw_radians),
        0.0f));
}

float DistanceTo(const glm::vec3& a, const glm::vec3& b) {
    return glm::length(b - a);
}

} // namespace

// ---------------------------------------------------------------------------
// task-12 defect 1: the rung count rounded the wrong way and measured the world
// Z difference instead of the ladder's local Z difference.
// ---------------------------------------------------------------------------

TEST(LadderRungCount, RoundsInsteadOfTruncating) {
    // Retail 314_01_1.mef (measured in task-9): foot z 7.0300, head z 47681.5352.
    // IGI-RE src/props/ladder.c:86-91 computes
    //   h = |p2.z - p3.z| = 47674.5052; l->rungs = (int)round_ll(h * kRungScale)
    // with kRungScale = 1/1228.8 (src/props/ladder.c:202), so 38.7975 -> 39.
    const LadderPlacement retail(
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 50000.0f),
        glm::vec3(0.0f, 0.0f, 7.0300f),
        glm::vec3(0.0f, 0.0f, 47681.5352f),
        glm::mat3(1.0f));
    EXPECT_EQ(retail.GetClimbLine().GetStepCount(), 39);
    EXPECT_EQ(retail.GetClimbLine().GetTopStep(), 33);
}

TEST(LadderRungCount, TruncationWouldHaveProducedTheOldEditorValue) {
    // 47674.5052 / 1228.8 = 38.7975: truncating gives 38 (the value the Lead
    // measured in the editor before this fix), rounding gives 39
    // (src/props/ladder.c:91). Regression guard for task-12 defect 1.
    const float height = 47681.5352f - 7.0300f;
    EXPECT_EQ(static_cast<int>(height / 1228.8f), 38);
    EXPECT_EQ(static_cast<int>(std::nearbyint(height / 1228.8f)), 39);

    const LadderPlacement retail(
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 50000.0f),
        glm::vec3(0.0f, 0.0f, 7.0300f),
        glm::vec3(0.0f, 0.0f, 47681.5352f),
        glm::mat3(1.0f));
    EXPECT_EQ(retail.GetClimbLine().GetStepCount(), 39);
}

TEST(LadderRungCount, MeasuresTheLaddersLocalZAxisNotWorldZ) {
    // IGI-RE measures the distance between two MODEL-LOCAL points
    // (src/props/ladder.c:86-90 Model_GetLocalPoint), so a ladder rotated onto its
    // side still counts rungs along its own Z axis. Rotating +90 degrees about X
    // maps the local +Z column onto world -Y.
    const glm::mat3 side = RotationBasis(90.0f, 0.0f, 0.0f);
    EXPECT_NEAR(side[2].x, 0.0f, 1e-5f);
    EXPECT_NEAR(side[2].y, -1.0f, 1e-5f);
    EXPECT_NEAR(side[2].z, 0.0f, 1e-5f);

    const LadderPlacement ladder(
        glm::vec3(0.0f),
        glm::vec3(0.0f, -50000.0f, 0.0f),
        glm::vec3(0.0f),
        glm::vec3(0.0f, -30000.0f, 0.0f),
        side);
    // 30000 / 1228.8 = 24.414 -> 24 (src/props/ladder.c:91).
    EXPECT_EQ(ladder.GetClimbLine().GetStepCount(), 24);
    EXPECT_EQ(ladder.GetClimbLine().GetTopStep(), 18);
    // The world-Z difference is zero, so the pre-fix world-space rule collapsed
    // this ladder to a single rung.
    EXPECT_FLOAT_EQ(ladder.GetTop().z - ladder.GetBottom().z, 0.0f);
    EXPECT_EQ(std::max(1, static_cast<int>(0.0f / 1228.8f)), 1);
}

TEST(LadderRungCount, KeepsAtLeastOneRung) {
    // A degenerate climb line cannot produce a zero-rung ladder: the editor keeps
    // the floor at one step (PositionAtStep divides by the count).
    const LadderPlacement flat(
        glm::vec3(0.0f),
        glm::vec3(0.0f, 0.0f, 100.0f),
        glm::vec3(0.0f),
        glm::vec3(0.0f, 0.0f, 100.0f),
        glm::mat3(1.0f));
    EXPECT_EQ(flat.GetClimbLine().GetStepCount(), 1);
    EXPECT_EQ(flat.GetClimbLine().GetTopStep(), 0);
}

// ---------------------------------------------------------------------------
// task-12 defect 3: the facing test used a 2-D yaw vector. IGI-RE compares the
// view matrix's column 1 with the ladder's column 1 (src/props/ladder.c:143-157).
// ---------------------------------------------------------------------------

TEST(LadderUseQuery, BottomOfferInsideTheActivationBand) {
    const LadderPlacement ladder = MakeLadder(
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 30000.0f));
    // The player stands on the +Y side of the ladder looking at it, inside the
    // world-Z span between the object origin and magic point 1
    // (src/props/ladder.c:155-157).
    const glm::vec3 eye(0.0f, -1000.0f, 1000.0f);

    LadderUseQuery query;
    query.Consider(ladder, eye, MakeView(glm::vec3(0.0f, 1.0f, 0.0f)), 0);
    ASSERT_TRUE(query.HasBest());
    EXPECT_EQ(query.GetBestIndex(), 0);
    EXPECT_FALSE(query.IsBestAtTop());
}

TEST(LadderUseQuery, BottomOfferRejectsExactlyAtTheHalfWidth) {
    // src/props/ladder.c:138 gates with `lx > kReachMin && !(lx >= kReachMax)`,
    // i.e. strictly inside +/-2867.2 world units (DATA 0x533868/0x533440).
    const LadderPlacement ladder = MakeLadder(
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 30000.0f));
    const ViewFrame view = MakeView(glm::vec3(0.0f, 1.0f, 0.0f));

    LadderUseQuery just_inside;
    just_inside.Consider(ladder, glm::vec3(2867.1f, -1000.0f, 1000.0f), view, 0);
    EXPECT_TRUE(just_inside.HasBest());

    LadderUseQuery exactly_at;
    exactly_at.Consider(ladder, glm::vec3(2867.2f, -1000.0f, 1000.0f), view, 0);
    EXPECT_FALSE(exactly_at.HasBest());

    LadderUseQuery other_side;
    other_side.Consider(ladder, glm::vec3(-2867.2f, -1000.0f, 1000.0f), view, 0);
    EXPECT_FALSE(other_side.HasBest());
}

TEST(LadderUseQuery, BottomOfferRejectsAtTheActivateRadius) {
    // src/props/ladder.c:136-138 compares |ly| against q->range, which
    // Human_UpdateUse fills with 8192.0 (the double at src/game/game.c:185-191's
    // 0x78 slot; see docs/igi-re-parity/LADDER_PLACEMENT.md section 2.1).
    const LadderPlacement ladder = MakeLadder(
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 30000.0f));
    const ViewFrame view = MakeView(glm::vec3(0.0f, 1.0f, 0.0f));

    LadderUseQuery inside;
    inside.Consider(ladder, glm::vec3(0.0f, -8191.0f, 1000.0f), view, 0);
    EXPECT_TRUE(inside.HasBest());

    LadderUseQuery at_radius;
    at_radius.Consider(ladder, glm::vec3(0.0f, -8192.0f, 1000.0f), view, 0);
    EXPECT_FALSE(at_radius.HasBest());
}

TEST(LadderUseQuery, BottomOfferRejectsOutsideTheWorldZSpan) {
    // src/props/ladder.c:157 requires `topz > q->pos.z > l->obj.pos.z`, strict on
    // both ends, so the eye must be strictly between the object origin and magic
    // point 1 for the bottom offer.
    const LadderPlacement ladder = MakeLadder(
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 30000.0f));
    const ViewFrame view = MakeView(glm::vec3(0.0f, 1.0f, 0.0f));

    // Exactly at magic point 1: no bottom offer, but the eye is already on the
    // reference point's -Y side, so the top offer (src/props/ladder.c:164-173)
    // takes over.
    LadderUseQuery at_reference;
    at_reference.Consider(ladder, glm::vec3(0.0f, -1000.0f, 30000.0f), view, 0);
    ASSERT_TRUE(at_reference.HasBest());
    EXPECT_TRUE(at_reference.IsBestAtTop());

    // Exactly at the object origin: no bottom offer, and the reference point is
    // 30017 units away, past the 8192 query range (src/game/game.c:906).
    LadderUseQuery at_origin;
    at_origin.Consider(ladder, glm::vec3(0.0f, -1000.0f, 0.0f), view, 0);
    EXPECT_FALSE(at_origin.HasBest());
}

TEST(LadderUseQuery, FacingGateRejectsJustOverFortyFiveDegrees) {
    // src/props/ladder.c:154-157: `Vec3_Angle(u, n) < kLadderAngle` with u the
    // view's column 1 and n the ladder's column 1, flipped to the player's side.
    const LadderPlacement ladder = MakeLadder(
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 30000.0f));
    const glm::vec3 eye(0.0f, -1000.0f, 1000.0f);

    LadderUseQuery under;
    under.Consider(ladder, eye, MakeView(44.9f), 0);
    EXPECT_TRUE(under.HasBest());

    LadderUseQuery over;
    over.Consider(ladder, eye, MakeView(45.1f), 0);
    EXPECT_FALSE(over.HasBest());
}

TEST(LadderUseQuery, FacingThresholdIsAHairAboveFortyFiveDegrees) {
    // kLadderAngle is ALIAS(kLadderAngle, kQuarterPi) INIT(0.7853982f)
    // (src/plane/gear.c:156-157), and that float is strictly larger than pi/4, so
    // a construction whose angle lands on exactly pi/4 stays inside the gate.
    // task-9 recorded the same finding for the task_schema copy of this constant.
    EXPECT_FLOAT_EQ(LadderPlacement::FacingAngleRadians, 0.7853982f);
    EXPECT_GT(
        static_cast<double>(LadderPlacement::FacingAngleRadians),
        kPi / 4.0);
}

TEST(LadderUseQuery, BottomOfferUsesTheViewFrameNotAFlatYawVector) {
    // The pre-fix test built its forward vector as (sin yaw, cos yaw), but this
    // runtime's look direction is (-sin yaw * cos pitch, cos yaw * cos pitch,
    // sin pitch) (source/runtime/runtime_world.cpp:2420 and :2496-2499, and
    // source/player_controller.cpp:130-131 for the movement basis). A pitch of
    // 45 degrees therefore shortens the horizontal component; with the old 2-D
    // vector the gate could not see pitch at all.
    const LadderPlacement ladder = MakeLadder(
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 30000.0f));
    const glm::vec3 eye(0.0f, -1000.0f, 1000.0f);

    // Looking at the ladder while pitched up 40 degrees: the horizontal
    // component still points at the ladder, and the 3-D angle to the ladder's
    // normal is 40 degrees, so the offer stands.
    const float pitch = glm::radians(40.0f);
    const ViewFrame pitched = MakeView(glm::vec3(
        0.0f,
        std::cos(pitch),
        std::sin(pitch)));
    LadderUseQuery query;
    query.Consider(ladder, eye, pitched, 0);
    EXPECT_TRUE(query.HasBest());

    // Looking away in yaw is rejected regardless of pitch.
    const ViewFrame away = MakeView(glm::vec3(
        std::sin(glm::radians(60.0f)),
        std::cos(glm::radians(60.0f)),
        0.0f));
    LadderUseQuery away_query;
    away_query.Consider(ladder, eye, away, 0);
    EXPECT_FALSE(away_query.HasBest());
}

// ---------------------------------------------------------------------------
// task-12 defect 2: selection was "nearest origin". IGI-RE lets every ladder
// update one shared UseQuery during the task scan (src/humanbase/human.c:1291-1298)
// and UseQuery_Consider keeps the item closest to straight ahead
// (src/game/game.c:182,897-928).
// ---------------------------------------------------------------------------

TEST(LadderUseQuery, TopOfferAcceptsInsideTheQueryCone) {
    const LadderPlacement ladder = MakeLadder(
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 30000.0f));
    // Above the reference point and on its -Y side, so only the top offer
    // applies (src/props/ladder.c:164-173).
    const glm::vec3 eye(0.0f, -1000.0f, 30500.0f);

    LadderUseQuery query;
    query.Consider(ladder, eye, MakeView(glm::vec3(0.0f, 1.0f, 0.0f)), 0);
    ASSERT_TRUE(query.HasBest());
    EXPECT_EQ(query.GetBestIndex(), 0);
    EXPECT_TRUE(query.IsBestAtTop());
    // 26.6 degrees of pitch, 0 of yaw: inside the kQuarterPi bounds
    // (src/humanbase/human.c:1286).
    EXPECT_NEAR(query.GetBestPitch(), 0.4636f, 1e-3f);
    EXPECT_NEAR(query.GetBestYaw(), 0.0f, 1e-6f);
}

TEST(LadderUseQuery, TopOfferNeedsTheEyeOnTheFarSideOfTheReferencePoint) {
    // src/props/ladder.c:172 requires `dot(eye - reference, column 1) < 0`. The
    // player reaches that side by climbing: the top mount is offset along
    // -column 1 (TopStandoffUnits, src/weapon/humanplayer.c:130-133,273-340).
    const LadderPlacement ladder = MakeLadder(
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 30000.0f));
    const glm::vec3 eye(0.0f, 1000.0f, 30500.0f);

    LadderUseQuery query;
    query.Consider(ladder, eye, MakeView(glm::vec3(0.0f, -1.0f, 0.0f)), 0);
    EXPECT_FALSE(query.HasBest());
}

TEST(LadderUseQuery, TopOfferRejectsAtTheActivateRadius) {
    // src/game/game.c:906: `if (Vec3d_Length(&d) >= q->range) return 0;`.
    const LadderPlacement ladder = MakeLadder(
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 30000.0f));
    const ViewFrame view = MakeView(glm::vec3(0.0f, 1.0f, 0.0f));

    LadderUseQuery inside;
    inside.Consider(ladder, glm::vec3(0.0f, -8191.0f, 30000.0f), view, 0);
    EXPECT_TRUE(inside.HasBest());

    LadderUseQuery at_radius;
    at_radius.Consider(ladder, glm::vec3(0.0f, -8192.0f, 30000.0f), view, 0);
    EXPECT_FALSE(at_radius.HasBest());
}

TEST(LadderUseQuery, NearestOriginLosesToTheBetterAlignedTopOffer) {
    // Two top offers. Ladder 0 is nearer by origin distance but 31 degrees off
    // the view axis on both axes; ladder 1 is further away and dead ahead.
    // IGI-RE's rule (src/game/game.c:914-926) keeps the one closest to straight
    // ahead; the pre-fix editor rule picked the nearer origin.
    const glm::vec3 eye(0.0f, 0.0f, 30000.0f);
    const ViewFrame view = MakeView(glm::vec3(0.0f, 1.0f, 0.0f));

    // reference = eye + (1500, 2500, -1500); origin is 3000 below the reference.
    const LadderPlacement near_but_off_axis = MakeLadder(
        glm::vec3(1500.0f, 2500.0f, 25500.0f),
        glm::vec3(1500.0f, 2500.0f, 28500.0f));
    // reference = eye + (0, 7000, 0).
    const LadderPlacement far_but_aligned = MakeLadder(
        glm::vec3(0.0f, 7000.0f, 27000.0f),
        glm::vec3(0.0f, 7000.0f, 30000.0f));

    const float near_distance = DistanceTo(eye, near_but_off_axis.GetOrigin());
    const float far_distance = DistanceTo(eye, far_but_aligned.GetOrigin());
    EXPECT_LT(near_distance, far_distance);
    EXPECT_NEAR(near_distance, 5361.9f, 0.5f);
    EXPECT_NEAR(far_distance, 7615.8f, 0.5f);

    std::vector<LadderPlacement> ladders;
    ladders.push_back(near_but_off_axis);
    ladders.push_back(far_but_aligned);

    LadderUseQuery query;
    for (size_t index = 0; index < ladders.size(); ++index) {
        query.Consider(ladders[index], eye, view, static_cast<int>(index));
    }
    ASSERT_TRUE(query.HasBest());
    EXPECT_EQ(query.GetBestIndex(), 1);
    EXPECT_TRUE(query.IsBestAtTop());
    // 2500 / sqrt(2500^2 + 1500^2) = 0.8575 -> 0.5413 rad on both axes.
    EXPECT_NEAR(query.GetBestPitch(), 0.0f, 1e-6f);
    EXPECT_NEAR(query.GetBestYaw(), 0.0f, 1e-6f);

    // The winner does not depend on the scan order: the dominance rule rejects
    // anything that is not strictly better on BOTH axes (game.c:922).
    LadderUseQuery reversed;
    reversed.Consider(far_but_aligned, eye, view, 0);
    reversed.Consider(near_but_off_axis, eye, view, 1);
    ASSERT_TRUE(reversed.HasBest());
    EXPECT_EQ(reversed.GetBestIndex(), 0);
}

TEST(LadderUseQuery, BottomOfferZeroesTheAngleBoundsAndLocksOutALaterTopOffer) {
    // src/props/ladder.c:158-161 sets best_pitch = best_yaw = 0 when the bottom
    // offer succeeds. Those are the very fields UseQuery_Consider compares against
    // (src/game/game.c:922-925; the same 0x70/0x74 pair in both structs, see
    // src/game/game.h:188-189 and src/props/propstypes.h:12-13), so every later
    // top offer is rejected no matter how well aligned it is.
    const glm::vec3 eye(0.0f, 0.0f, 1000.0f);
    const ViewFrame view = MakeView(glm::vec3(0.0f, 1.0f, 0.0f));

    const LadderPlacement bottom_offer = MakeLadder(
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 30000.0f));
    const LadderPlacement perfect_top_offer = MakeLadder(
        glm::vec3(0.0f, 3000.0f, -2000.0f),
        glm::vec3(0.0f, 3000.0f, 1000.0f));

    // Control: on its own, the perfect top offer wins.
    LadderUseQuery control;
    control.Consider(perfect_top_offer, eye, view, 0);
    ASSERT_TRUE(control.HasBest());
    EXPECT_TRUE(control.IsBestAtTop());

    LadderUseQuery query;
    query.Consider(bottom_offer, eye, view, 0);
    query.Consider(perfect_top_offer, eye, view, 1);
    ASSERT_TRUE(query.HasBest());
    EXPECT_EQ(query.GetBestIndex(), 0);
    EXPECT_FALSE(query.IsBestAtTop());
}

TEST(LadderUseQuery, NoOfferLeavesTheQueryEmpty) {
    // A ladder whose reference point is out of reach: src/game/game.c:906 rejects
    // it, and the bottom band is 20000 units away (>= 8192).
    const LadderPlacement ladder = MakeLadder(
        glm::vec3(0.0f, 20000.0f, 0.0f),
        glm::vec3(0.0f, 20000.0f, 30000.0f));
    LadderUseQuery query;
    query.Consider(ladder, glm::vec3(0.0f, 0.0f, 1000.0f),
                   MakeView(glm::vec3(0.0f, 1.0f, 0.0f)), 0);
    EXPECT_FALSE(query.HasBest());
    EXPECT_EQ(query.GetBestIndex(), -1);

    // Reset restores the player's kQuarterPi bounds (src/humanbase/human.c:1286).
    LadderUseQuery reset;
    reset.Reset();
    EXPECT_FLOAT_EQ(reset.GetBestPitch(), LadderUseQuery::InitialBestAngleRadians);
    EXPECT_FLOAT_EQ(reset.GetBestYaw(), LadderUseQuery::InitialBestAngleRadians);
}
