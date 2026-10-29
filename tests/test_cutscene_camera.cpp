/******************************************************************************
 * @file    test_cutscene_camera.cpp
 * @brief   Host-native tests for the authored CutScene camera PROGRAMME
 *          (GitHub issue #80): a CutScene task plus its EditCamera key chain,
 *          resolved into the engine's own timeline and evaluated at a tick.
 *
 * The subject is source/runtime/cutscene_camera.{h,cpp}, which is pure: it
 * calls source/runtime/cutscene_rules.{h,cpp} and needs no GL, no windows.h,
 * no level archive and no retail data.
 *
 * What these tests are FOR. Before this module the gameplay camera blended the
 * authored shots with `glm::mix` on position and on the forward/up columns.
 * The engine does not do that: `CutScene_Interpolate` evaluates a cubic Hermite
 * through a four-key window. The last test in this file is the one that would
 * fail if the linear path ever came back.
 *
 * Oracle: /Users/haseeb-mir/Documents/Code/IGI-RE
 *   src/cutscene/cutscene.c:163-180   CutScene_CollectKeys 0x4F4880
 *   src/cutscene/cutscene.c:182-190   CutScene_GetKey 0x4F4910 (both-end clamp)
 *   src/cutscene/cutscene.c:193-211   CutScene_UnwrapAngles 0x4F4940
 *   src/cutscene/cutscene.c:417-462   CutScene_Update 0x4F5400 (window, finished)
 *   src/cutscene/cutscene.c:465-471   CutScene_Spline 0x4F6290 (the SPLINE macro)
 *   src/cutscene/cutscene.c:631-651   CutScene_Spline 0x4F6290 (the body)
 *   src/cutscene/cutscene.c:427       t = c->tick / gCutSceneTicksPerSec
 *   src/editcam/editcamera.c:88-107   EditCamera_Init 0x507F10 (the 16-row table)
 *   src/editcam/editcamera.c:112-132  EditCamera_Create 0x508290 (the defaults)
 *   src/terrdraw/flare.c:44-61        Hermite_Init 0x518130, Hermite_Eval 0x518190
 *   src/game/game.c:465               CutScene_SetTicksPerSecond(30)
 *****************************************************************************/
#include <gtest/gtest.h>

#include "runtime/cutscene_camera.h"
#include "runtime/cutscene_rules.h"

#include <cmath>
#include <string>
#include <vector>

namespace {

using igi::AuthoredCutSceneProgramme;
using igi::BuildAuthoredCutSceneProgramme;
using igi::CutSceneCameraState;
using igi::CutSceneKey;
using igi::CutSceneKeyWindow;
using igi::CutSceneSpline;
using igi::EvaluateAuthoredCutSceneAtTick;
using igi::SelectCutSceneKeys;

// One authored CutScene call, in the `LevelObject::argTokens` layout the loader
// and `ParseCutSceneScreen` expect: [0] = task id, [1] = type, [2] = name,
// [3] = parameter 0. The 21 tokens are the 14-row CutScene table's argument
// count (cutscene.c:227-263).
std::vector<std::string> CutSceneTokens(const std::string& task_id, float time_scale,
                                       float viewport_height_factor) {
    return {
        task_id, "CutScene", "",
        "0", "0", "0",                 // Position
        "0", "0", "0",                 // Orientation
        "",                            // Run
        "",                            // Reset
        "",                            // Time delta (seconds)
        "0",                           // Start time (seconds)
        "FALSE",                       // Initial run
        std::to_string(time_scale),    // Time scale
        std::to_string(viewport_height_factor),  // Viewport height factor
        "0", "0",                      // fade in / fade out
        "0",                           // Time of day
        "", "",                        // Start / Stop expression
    };
}

// One authored EditCamera call, same layout. The 23 tokens are the 16-row
// EditCamera table's argument count (editcamera.c:88-107); `smooth` is the
// key's "Smooth to next" flag, which CutScene_Update reads when it collapses
// the k[4] window (cutscene.c:455-460).
std::vector<std::string> EditCameraTokens(double x, double y, double z, double alpha, double beta,
                                         double gamma, double fov, double duration, bool smooth) {
    return {
        "-1", "EditCamera", "Shot",
        std::to_string(x), std::to_string(y), std::to_string(z),   // Position
        std::to_string(alpha), std::to_string(beta), std::to_string(gamma),  // Alpha/Beta/Gamma
        std::to_string(fov),                                       // FOV
        std::to_string(duration),                                  // Duration
        "-1", "TRUE",                                              // Link task ID + follow
        "-1", "TRUE",                                              // Target task ID + follow
        smooth ? "TRUE" : "FALSE",                                 // Smooth to next
        "-1",                                                      // Time of day (inherit)
        "CAMERAFILTER_TYPE_NONE",                                  // FILTER
        "0.5",                                                     // Noise
        "0", "0", "0",                                             // Filter color
        "0",                                                       // Camera shake
    };
}

// A key whose authored pose is `index * 100` along +X, so a wrong key selection
// shows up as a whole 100-unit step rather than a rounding difference.
std::vector<std::string> KeyAt(double index, double duration, bool smooth) {
    return EditCameraTokens(index * 100.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, duration, smooth);
}

double PositionX(const CutSceneCameraState& state) {
    return state.position[0];
}

}  // namespace

// ---------------------------------------------------------------------------
// CutScene_CollectKeys 0x4F4880 -- the timeline
// ---------------------------------------------------------------------------

TEST(CutSceneCameraTest, AccumulatesEachKeysStartFromThePreviousDurations) {
    // cutscene.c:163-180: `keys[*count].time = *time; *time = scale * duration + *time;`
    const AuthoredCutSceneProgramme programme = BuildAuthoredCutSceneProgramme(
        CutSceneTokens("1204", 1.0f, 1.0f),
        {KeyAt(0, 1.0, true), KeyAt(1, 2.0, true), KeyAt(2, 3.0, true)});

    ASSERT_TRUE(programme.valid);
    ASSERT_EQ(programme.keys.size(), 3U);
    EXPECT_FLOAT_EQ(programme.keys[0].start_time, 0.0f);
    EXPECT_FLOAT_EQ(programme.keys[1].start_time, 1.0f);
    EXPECT_FLOAT_EQ(programme.keys[2].start_time, 3.0f);
    // `end` is the walk's final `*time`, which includes the LAST key's own
    // duration (cutscene.c:442 compares `t > end` against exactly this).
    EXPECT_FLOAT_EQ(programme.end, 6.0f);
}

TEST(CutSceneCameraTest, TimeScaleMultipliesEveryDuration) {
    const AuthoredCutSceneProgramme programme = BuildAuthoredCutSceneProgramme(
        CutSceneTokens("1204", 0.5f, 1.0f),
        {KeyAt(0, 1.0, true), KeyAt(1, 2.0, true), KeyAt(2, 3.0, true)});

    ASSERT_TRUE(programme.valid);
    ASSERT_EQ(programme.keys.size(), 3U);
    EXPECT_FLOAT_EQ(programme.keys[1].start_time, 0.5f);
    EXPECT_FLOAT_EQ(programme.keys[2].start_time, 1.5f);
    EXPECT_FLOAT_EQ(programme.end, 3.0f);
}

TEST(CutSceneCameraTest, PreservesTheAuthoredKeyParameters) {
    std::vector<std::string> key = EditCameraTokens(11.0, 22.0, 33.0, 0.25, -0.5, 1.75, 0.9, 2.5, false);
    const AuthoredCutSceneProgramme programme = BuildAuthoredCutSceneProgramme(
        CutSceneTokens("1204", 1.0f, 0.75f), {key});

    ASSERT_TRUE(programme.valid);
    ASSERT_EQ(programme.keys.size(), 1U);
    const igi::CutSceneKeyParams& params = programme.keys[0].params;
    EXPECT_DOUBLE_EQ(params.position[0], 11.0);
    EXPECT_DOUBLE_EQ(params.position[1], 22.0);
    EXPECT_DOUBLE_EQ(params.position[2], 33.0);
    EXPECT_FLOAT_EQ(params.angles[0], 0.25f);
    EXPECT_FLOAT_EQ(params.angles[1], -0.5f);
    EXPECT_FLOAT_EQ(params.angles[2], 1.75f);
    EXPECT_FLOAT_EQ(params.fov, 0.9f);
    EXPECT_FLOAT_EQ(params.duration, 2.5f);
    EXPECT_FALSE(params.smooth);
    EXPECT_EQ(params.filter_name, "CAMERAFILTER_TYPE_NONE");
    // The header's own "Viewport height factor", not a caller default.
    EXPECT_FLOAT_EQ(programme.screen.screen_size, 0.75f);
}

// ---------------------------------------------------------------------------
// Refusals. A half-read programme would preview a plausible wrong pose, which
// is the failure mode CUTSCENES.md section 11 records a policy against.
// ---------------------------------------------------------------------------

TEST(CutSceneCameraTest, ATruncatedCutSceneHeaderIsRefused) {
    std::vector<std::string> header = CutSceneTokens("1204", 1.0f, 1.0f);
    header.resize(20);  // kCutSceneScreenTokenCount is 21
    const AuthoredCutSceneProgramme programme =
        BuildAuthoredCutSceneProgramme(header, {KeyAt(0, 1.0, true)});

    EXPECT_FALSE(programme.valid);
    EXPECT_TRUE(programme.keys.empty());
}

TEST(CutSceneCameraTest, ACutSceneWithNoEditCameraChildIsNotAProgramme) {
    // CutScene_Update returns before CollectKeys when the task has no
    // EditCamera child at all (cutscene.c:429-430).
    const AuthoredCutSceneProgramme programme =
        BuildAuthoredCutSceneProgramme(CutSceneTokens("1204", 1.0f, 1.0f), {});

    EXPECT_FALSE(programme.valid);
    EXPECT_TRUE(programme.keys.empty());
}

TEST(CutSceneCameraTest, ATruncatedEditCameraKeyRefusesTheWholeProgramme) {
    std::vector<std::string> short_key = EditCameraTokens(1.0, 2.0, 3.0, 0.0, 0.0, 0.0, 1.0, 1.0, true);
    short_key.resize(22);  // kCutSceneKeyTokenCount is 23

    const AuthoredCutSceneProgramme programme = BuildAuthoredCutSceneProgramme(
        CutSceneTokens("1204", 1.0f, 1.0f), {KeyAt(0, 1.0, true), short_key});

    // Not "one good key": a silently halved chain is refused outright.
    EXPECT_FALSE(programme.valid);
    EXPECT_TRUE(programme.keys.empty());
}

TEST(CutSceneCameraTest, AnInvalidProgrammeIsNeverEvaluated) {
    const AuthoredCutSceneProgramme empty;
    CutSceneCameraState state;
    state.position[0] = 1234.0;  // must survive untouched
    EXPECT_FALSE(EvaluateAuthoredCutSceneAtTick(empty, 0, &state));
    EXPECT_DOUBLE_EQ(state.position[0], 1234.0);
}

// ---------------------------------------------------------------------------
// CutScene_Update 0x4F5400 / CutScene_Interpolate 0x4F55D0 -- the pose
// ---------------------------------------------------------------------------

TEST(CutSceneCameraTest, EveryKeyHoldsItsAuthoredPoseAtItsOwnStartTime) {
    // The engine property CUTSCENES.md section 10 proves on the retail corpus:
    // at a key's own start time the k[4] window has u == 0, so Hermite_Eval
    // returns p1 -- that key's own authored value (cutscene.c:631-651).
    const AuthoredCutSceneProgramme programme = BuildAuthoredCutSceneProgramme(
        CutSceneTokens("1204", 1.0f, 1.0f),
        {KeyAt(0, 1.0, true), KeyAt(1, 2.0, true), KeyAt(2, 3.0, true)});
    ASSERT_TRUE(programme.valid);

    for (int index = 0; index < 3; ++index) {
        const int tick = igi::CutSceneTickFromTime(programme.keys[index].start_time);
        CutSceneCameraState state;
        ASSERT_TRUE(EvaluateAuthoredCutSceneAtTick(programme, tick, &state))
            << "key " << index << " at tick " << tick;
        EXPECT_DOUBLE_EQ(PositionX(state), index * 100.0)
            << "key " << index << " at tick " << tick;
        EXPECT_FLOAT_EQ(state.fov, 1.0f);
        EXPECT_EQ(state.active_key, index);
        EXPECT_FALSE(state.finished);
    }
}

TEST(CutSceneCameraTest, ThePoseIsTheEngineSplineNotALinearMix) {
    // Two keys 100 units apart, each one second long, both smooth. At t = 0.5
    // the window is [k0, k0, k1, k1] with times [0, 0, 1, 1], so
    //   half = (p3 - p1) * 0.5 = 50,  m1 = (t1 - t0) * 2 / (t2 - t0) * ((p2 - p0) * 0.5) = 0
    //   m2 = (t2 - t1) * 2 / (t3 - t1) * half = 100,  u = 0.5
    //   h(u) = u * m0 + (u^3 * h2 + u^2 * h3) + p0
    //        = 0 + (0.125 * -100 + 0.25 * 200) + 0 = 37.5
    // A LINEAR mix would report 50. This is the assertion that fails if the
    // removed `glm::mix` path ever comes back.
    const AuthoredCutSceneProgramme programme = BuildAuthoredCutSceneProgramme(
        CutSceneTokens("1204", 1.0f, 1.0f),
        {KeyAt(0, 1.0, true), KeyAt(1, 1.0, true)});
    ASSERT_TRUE(programme.valid);

    CutSceneCameraState state;
    ASSERT_TRUE(EvaluateAuthoredCutSceneAtTick(programme, 15, &state));  // t = 0.5
    EXPECT_NEAR(PositionX(state), 37.5, 1e-9);
    EXPECT_NE(PositionX(state), 50.0);
}

TEST(CutSceneCameraTest, TheValueIsCutSceneSplineOverTheWindowCutSceneUpdateSelects) {
    // The same thing stated against the engine's own primitives: whatever the
    // pose is, it must equal CutScene_Spline over the k[4] window
    // SelectCutSceneKeys returns. This does not restate my arithmetic.
    const AuthoredCutSceneProgramme programme = BuildAuthoredCutSceneProgramme(
        CutSceneTokens("1204", 1.0f, 1.0f),
        {KeyAt(0, 1.0, true), KeyAt(1, 2.0, true), KeyAt(2, 3.0, true)});
    ASSERT_TRUE(programme.valid);

    for (const int tick : {7, 29, 45, 61, 88, 120}) {
        const float t = igi::CutSceneTimeFromTick(tick);
        const CutSceneKeyWindow window = SelectCutSceneKeys(programme.keys, programme.end, t);
        const CutSceneKey& k0 = programme.keys[static_cast<size_t>(window.indices[0])];
        const CutSceneKey& k1 = programme.keys[static_cast<size_t>(window.indices[1])];
        const CutSceneKey& k2 = programme.keys[static_cast<size_t>(window.indices[2])];
        const CutSceneKey& k3 = programme.keys[static_cast<size_t>(window.indices[3])];

        CutSceneCameraState state;
        ASSERT_TRUE(EvaluateAuthoredCutSceneAtTick(programme, tick, &state));
        EXPECT_EQ(state.active_key, window.active_key) << "tick " << tick;
        EXPECT_EQ(state.finished, window.finished) << "tick " << tick;
        for (int axis = 0; axis < 3; ++axis) {
            const long double expected = CutSceneSpline(
                k0.params.position[axis], k1.params.position[axis],
                k2.params.position[axis], k3.params.position[axis],
                k0.start_time, k1.start_time, k2.start_time, k3.start_time, t);
            EXPECT_NEAR(static_cast<double>(expected), state.position[axis], 1e-6)
                << "axis " << axis << " tick " << tick;
        }
    }
}

TEST(CutSceneCameraTest, ANonSmoothActiveKeyCollapsesBothOfItsTangents) {
    // cutscene.c:455-460, in order:
    //   if (!k[0]->smooth) k[0] = k[1];
    //   if (!k[1]->smooth) k[2] = k[1];
    //   if (!k[2]->smooth) k[3] = k[2];
    // The THIRD test reads k[2] as possibly rewritten by the second, so when the
    // ACTIVE key is not smooth BOTH of its tangents collapse to itself: the
    // window becomes [k[0], k[1], k[1], k[1]] and u is 0, so the pose holds the
    // active key's own authored value for the whole of its span. A linear mix
    // would report the halfway value 150 instead of 100.
    const AuthoredCutSceneProgramme programme = BuildAuthoredCutSceneProgramme(
        CutSceneTokens("1204", 1.0f, 1.0f),
        {KeyAt(0, 1.0, true), KeyAt(1, 1.0, false), KeyAt(2, 1.0, true)});
    ASSERT_TRUE(programme.valid);

    // t = 1.5 puts the active key on index 1.
    const CutSceneKeyWindow window = SelectCutSceneKeys(programme.keys, programme.end, 1.5f);
    EXPECT_EQ(window.active_key, 1);
    EXPECT_EQ(window.indices[0], 0);
    EXPECT_EQ(window.indices[1], 1);
    EXPECT_EQ(window.indices[2], 1);
    EXPECT_EQ(window.indices[3], 1);

    CutSceneCameraState state;
    ASSERT_TRUE(EvaluateAuthoredCutSceneAtTick(programme, 45, &state));
    EXPECT_EQ(state.active_key, 1);
    EXPECT_DOUBLE_EQ(PositionX(state), 100.0);
    EXPECT_NE(PositionX(state), 150.0);
}

TEST(CutSceneCameraTest, FinishedIsSetPastTheEndAndTheLastKeyStillHolds) {
    // cutscene.c:442-446: `t > end` sets isFinished and rewinds the tick, but
    // does NOT move `cur`, so the finished pose is the last key's.
    const AuthoredCutSceneProgramme programme = BuildAuthoredCutSceneProgramme(
        CutSceneTokens("1204", 1.0f, 1.0f),
        {KeyAt(0, 1.0, true), KeyAt(1, 2.0, true), KeyAt(2, 3.0, true)});
    ASSERT_TRUE(programme.valid);
    ASSERT_FLOAT_EQ(programme.end, 6.0f);

    CutSceneCameraState before;
    ASSERT_TRUE(EvaluateAuthoredCutSceneAtTick(programme, 180, &before));  // t == end
    EXPECT_FALSE(before.finished);

    CutSceneCameraState after;
    ASSERT_TRUE(EvaluateAuthoredCutSceneAtTick(programme, 181, &after));  // t > end
    EXPECT_TRUE(after.finished);
    EXPECT_EQ(after.active_key, 2);
    EXPECT_DOUBLE_EQ(PositionX(after), 200.0);
}

TEST(CutSceneCameraTest, TickToTimeIsTheEnginesThirtyHertzClock) {
    // `float t = (long double)c->tick / gCutSceneTicksPerSec` (cutscene.c:427)
    // with CutScene_SetTicksPerSecond(30) (game.c:465). Tick 30 is the second
    // key's start time in a one-second-per-key timeline, so the pose is key 1's.
    const AuthoredCutSceneProgramme programme = BuildAuthoredCutSceneProgramme(
        CutSceneTokens("1204", 1.0f, 1.0f),
        {KeyAt(0, 1.0, true), KeyAt(1, 1.0, true)});
    ASSERT_TRUE(programme.valid);

    CutSceneCameraState state;
    ASSERT_TRUE(EvaluateAuthoredCutSceneAtTick(programme, 30, &state));
    EXPECT_DOUBLE_EQ(PositionX(state), 100.0);
    EXPECT_EQ(state.active_key, 1);
}

// ---------------------------------------------------------------------------
// The projection: the editor's flat object list -> the key chain
// ---------------------------------------------------------------------------

namespace {

igi::AuthoredCameraKeyObject Object(int parent_index, const std::string& type,
                                    const std::string& tag) {
    igi::AuthoredCameraKeyObject object;
    object.parent_index = parent_index;
    object.type = type;
    object.arg_tokens = {tag};
    return object;
}

// The first token of each projected key, so an order assertion reads as a list
// of tags.
std::vector<std::string> Tags(const std::vector<std::vector<std::string>>& keys) {
    std::vector<std::string> tags;
    for (const std::vector<std::string>& key : keys) {
        tags.push_back(key.empty() ? std::string() : key.front());
    }
    return tags;
}

}  // namespace

TEST(CutSceneCameraProjectionTest, TakesTheEditCameraChildrenInListOrder) {
    //  0 CutScene
    //    1 EditCamera "a"
    //    2 EditCamera "b"
    //    3 Smoke        (a child, but not a key)
    //    4 EditCamera "c"
    const std::vector<igi::AuthoredCameraKeyObject> objects = {
        Object(-1, "CutScene", "scene"),
        Object(0, "EditCamera", "a"),
        Object(0, "EditCamera", "b"),
        Object(0, "Smoke", "smoke"),
        Object(0, "EditCamera", "c"),
    };

    EXPECT_EQ(
        Tags(igi::CollectAuthoredCameraKeyTokens(objects, 0)),
        std::vector<std::string>({"a", "b", "c"}));
}

TEST(CutSceneCameraProjectionTest, VisitsANestedChainBeforeTheNextSibling) {
    //  0 CutScene
    //    1 EditCamera "a"
    //       4 EditCamera "a-child"
    //    2 EditCamera "b"
    // CutScene_CollectKeys emits a key, recurses into that key's OWN EditCamera
    // children, and only then continues with the key's next sibling
    // (cutscene.c:167-179) -- so the order is a, a-child, b. A plain list-order
    // filter would give a, b, a-child.
    const std::vector<igi::AuthoredCameraKeyObject> objects = {
        Object(-1, "CutScene", "scene"),
        Object(0, "EditCamera", "a"),
        Object(0, "EditCamera", "b"),
        Object(-1, "Smoke", "elsewhere"),
        Object(1, "EditCamera", "a-child"),
    };

    EXPECT_EQ(
        Tags(igi::CollectAuthoredCameraKeyTokens(objects, 0)),
        std::vector<std::string>({"a", "a-child", "b"}));
}

TEST(CutSceneCameraProjectionTest, SkipsADeletedKeyTogetherWithItsSubtree) {
    std::vector<igi::AuthoredCameraKeyObject> objects = {
        Object(-1, "CutScene", "scene"),
        Object(0, "EditCamera", "a"),
        Object(0, "EditCamera", "b"),
        Object(1, "EditCamera", "a-child"),
    };
    objects[1].deleted = true;  // "a", and with it "a-child"

    EXPECT_EQ(
        Tags(igi::CollectAuthoredCameraKeyTokens(objects, 0)),
        std::vector<std::string>({"b"}));
}

TEST(CutSceneCameraProjectionTest, ADeletedCutSceneHasNoKeys) {
    std::vector<igi::AuthoredCameraKeyObject> objects = {
        Object(-1, "CutScene", "scene"),
        Object(0, "EditCamera", "a"),
    };
    objects[0].deleted = true;

    EXPECT_TRUE(igi::CollectAuthoredCameraKeyTokens(objects, 0).empty());
}

TEST(CutSceneCameraProjectionTest, AnOutOfRangeCutSceneIndexHasNoKeys) {
    const std::vector<igi::AuthoredCameraKeyObject> objects = {
        Object(-1, "CutScene", "scene"),
        Object(0, "EditCamera", "a"),
    };

    EXPECT_TRUE(igi::CollectAuthoredCameraKeyTokens(objects, -1).empty());
    EXPECT_TRUE(igi::CollectAuthoredCameraKeyTokens(objects, 2).empty());
}

TEST(CutSceneCameraProjectionTest, AnEmptyKeyChainIsNotAProgramme) {
    // The projection and the builder agree on the refusal: no key child means
    // CutScene_Update returns before CollectKeys (cutscene.c:429-430).
    const std::vector<igi::AuthoredCameraKeyObject> objects = {
        Object(-1, "CutScene", "scene"),
        Object(0, "Smoke", "smoke"),
    };
    const std::vector<std::vector<std::string>> keys =
        igi::CollectAuthoredCameraKeyTokens(objects, 0);
    ASSERT_TRUE(keys.empty());

    const AuthoredCutSceneProgramme programme = BuildAuthoredCutSceneProgramme(
        CutSceneTokens("1204", 1.0f, 1.0f), keys);
    EXPECT_FALSE(programme.valid);
}
