// The editor's animation playback path, driven by the ENGINE's own methods (#73).
//
// `AnimPlayback` (source/animation.h, implemented in source/runtime/anim_playback.cpp)
// owns an `igi::AnimPlayer`, so every clock value and every pose it reports is the
// engine's own arithmetic:
//
//   Anim_Play             0x4D61D0  src/boneobj/anim.c:38-58
//   BoneObj_InitAnimInst  0x4D4F60  src/boneobj/boneobj.c:988-1021
//   BoneObj_Update        0x4D38C0  src/boneobj/boneobj.c:371-419
//   BoneObj_GetEvents     0x4D3210  src/boneobj/boneobj.c:112-129
//   Anim_SetSpeed         0x4D6580  src/boneobj/anim.c:172-179
//   Anim_GetTime          0x4D6500  src/boneobj/anim.c:149-153
//   BoneObj_SetFrameRate  0x4D2D30  src/lod/boneobj.c:86-90
//   HumanPlayer_AnimDone  0x489F50  src/loadscreen/human.c:196-204
//   kTicksPerSecond       4800.0f   src/boneobj/anim.c:9
//
// These tests are HOST-PORTABLE on purpose: the playback implementation lives in
// runtime/ rather than in animation.cpp (which pulls in windows.h via utils.h) so
// the host lane links and runs them.
//
// The clip fixtures below are synthetic, but they are shaped exactly like the
// shipped BEF clips: 33 bones in the retail rigs is the only difference, and one
// bone is enough to exercise the clock, the wrap and the pose.

#include <gtest/gtest.h>

#include "animation.h"
#include "runtime/anim_player.h"
// The pure frame-step arithmetic (#73) this file pins against the real clock.
#include "runtime/anim_panel.h"
// The engine's "pose only what is drawn" rule, mapped onto the editor's
// viewport (Human_WakeIfVisible 0x45F140) - see the header.
#include "runtime/anim_pose_set.h"

#include <cmath>
#include <utility>
#include <vector>

namespace {

constexpr float kTicksPerSecond = 4800.0f;
constexpr float kTicksPerFrame = 160.0f;          // 4800 / 30, src/lod/boneobj.c:88
constexpr float kFrameMs = kTicksPerFrame * 1000.0f / kTicksPerSecond;   // 33.333 ms

// A clip the engine can actually play: bones, one position key (the engine
// dereferences `desc->pos->keys[count - 1]`, boneobj.c:1014) and a two-key
// rotation track on the child bone so the pose visibly changes with time.
// `engine_length_ticks` is the ENGINE length; the BEF stores it + 1.
AnimationClip MakeEngineClip(float engine_length_ticks, bool clip_loops) {
    AnimationClip clip;
    clip.name = "000_anim_002";
    clip.animId = 2;
    clip.length_ms = static_cast<int>(engine_length_ticks) + 1;
    clip.tp_flag = clip_loops ? 1 : 0;
    clip.bones = {
        AnimBone{0, "Bone_00", -1, glm::vec3(0.0f)},
        AnimBone{1, "Bone_01", 0, glm::vec3(1.0f, 0.0f, 0.0f)},
    };
    clip.translationKeys = {AnimTranslationKey{0, 0, glm::vec3(0.0f)}};
    AnimRotationKey first;
    first.bone = 1;
    first.time_ms = 0;
    first.q0 = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    AnimRotationKey last = first;
    last.time_ms = static_cast<int>(engine_length_ticks);
    last.q0 = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    clip.rotationKeys = {first, last};
    return clip;
}

// A clip with no skeleton at all: `AnimPlayer::Build` refuses it exactly as the
// engine would fault on it.
AnimationClip MakeBonelessClip() {
    AnimationClip clip;
    clip.name = "empty";
    clip.animId = 0;
    clip.length_ms = 1000;
    return clip;
}

// Largest absolute component difference between two poses. Zero means the two
// poses are BIT-identical, which is the bar the clock-only split has to clear:
// a skipped pose must not perturb the frame that is later drawn.
float MaxAbsPoseDiff(const std::vector<glm::mat4>& a, const std::vector<glm::mat4>& b) {
    if (a.size() != b.size()) return 1.0e30f;
    float worst = 0.0f;
    for (std::size_t i = 0; i < a.size(); ++i) {
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                const float d = std::fabs(a[i][c][r] - b[i][c][r]);
                if (d > worst) worst = d;
            }
        }
    }
    return worst;
}

// One simulation tick of the GAMEPLAY clock: `AdvanceGameplayAnimationClocks`
// converts fixed-step tick deltas with `1000 / GameClock::TICK_RATE_HZ`
// (src/game_clock.h:18, TICK_RATE_HZ = 30).  33.33 ms is deliberately very
// nearly, but not exactly, one engine frame (160 ticks at 30 fps,
// src/lod/boneobj.c:86-90), so this value also exercises the sub-frame
// accumulator rather than only whole-frame steps.
constexpr float kSimulationTickMs = 1000.0f / 30.0f;

}  // namespace

TEST(AnimPlaybackEngineTest, AdvancesInWholeEngineFrames) {
    const AnimationClip clip = MakeEngineClip(4800.0f, false);
    AnimPlayback playback;
    playback.Start(&clip);
    ASSERT_TRUE(playback.EngineReady());
    EXPECT_FLOAT_EQ(playback.currentTimeMs, 0.0f);

    // 120 ms is 3.6 engine frames: the engine steps WHOLE frames only
    // (gTicksPerFrame = 160 ticks, src/lod/boneobj.c:86-90), so the clock lands
    // on 3 * 160 = 480 ticks, not on 120 * 4.8 = 576.
    playback.Update(120.0f);

    EXPECT_FLOAT_EQ(playback.currentTimeMs, 480.0f);
    EXPECT_TRUE(playback.playing);
    EXPECT_EQ(playback.RawBoneTransforms().size(), 2u);
    EXPECT_EQ(playback.WorldTransforms().size(), 2u);
    // The sub-frame remainder is BANKED, not dropped: 120 ms leaves ~20 ms, so
    // 240 ms in total is 7 whole frames (7 * 160 = 1120), not 6.
    playback.Update(120.0f);
    EXPECT_FLOAT_EQ(playback.currentTimeMs, 1120.0f);
}

TEST(AnimPlaybackEngineTest, SubFrameTimeDoesNotAdvanceTheClock) {
    const AnimationClip clip = MakeEngineClip(800.0f, false);
    AnimPlayback playback;
    playback.Start(&clip);
    playback.Update(10.0f);   // 0.3 of a frame
    EXPECT_FLOAT_EQ(playback.currentTimeMs, 0.0f);
    playback.Update(10.0f);   // 0.6 of a frame
    EXPECT_FLOAT_EQ(playback.currentTimeMs, 0.0f);
    EXPECT_LT(20.0f, kFrameMs);
    playback.Update(20.0f);   // 40 ms in total crosses exactly one frame
    EXPECT_GT(40.0f, kFrameMs);
    EXPECT_FLOAT_EQ(playback.currentTimeMs, kTicksPerFrame);
}

TEST(AnimPlaybackEngineTest, NonLoopingClipStopsAtItsEnd) {
    const AnimationClip clip = MakeEngineClip(480.0f, false);
    AnimPlayback playback;
    playback.Start(&clip);

    playback.Update(120.0f);
    // The engine's own end test: BoneObj_AnimEventsCB sets BONE_EVENTS_DONE on
    // the root bone when a non-looping clock reaches the length
    // (src/boneobj/boneobj.c:873-941), and HumanPlayer_AnimDone 0x489F50 reads it
    // (src/loadscreen/human.c:196-204).
    EXPECT_FLOAT_EQ(playback.currentTimeMs, 480.0f);
    EXPECT_FALSE(playback.playing);

    playback.Update(120.0f);
    EXPECT_FLOAT_EQ(playback.currentTimeMs, 480.0f);
    EXPECT_FALSE(playback.playing);
}

TEST(AnimPlaybackEngineTest, ForcedLoopWrapsTheEngineClockInPlace) {
    // The clip's own tp_flag is 0 (no ANIM_LOOPS), and the editor's forceLoop is
    // what turns it into Anim_Play's kAnimLoop instance flag (anim.c:38-58).
    const AnimationClip clip = MakeEngineClip(800.0f, false);
    AnimPlayback playback;
    playback.Start(&clip);
    playback.forceLoop = true;

    // 220 ms is 6.6 frames -> 6 steps of 160 ticks = 960 ticks. The engine's
    // wrap subtracts the length ONCE per callback (not a while loop):
    // 160, 320, 480, 640, 800 -> wrap -> 0, 160.
    playback.Update(220.0f);

    EXPECT_FLOAT_EQ(playback.currentTimeMs, 160.0f);
    EXPECT_TRUE(playback.playing);
}

TEST(AnimPlaybackEngineTest, DrawnPoseIsThePosedFrameNotTheAdvancedClock) {
    // Retail poses the frame it draws and only then advances the clock
    // (BoneMagicObj_Run 0x531620, src/freecam/bonemagic.c:105,110), so after three
    // steps the bones hold the pose at tick 320 while the clock already reads 480.
    // Re-posing at draw time would show every frame one engine frame late.
    const AnimationClip clip = MakeEngineClip(800.0f, true);
    AnimPlayback playback;
    playback.Start(&clip);
    playback.Update(120.0f);
    ASSERT_FLOAT_EQ(playback.currentTimeMs, 480.0f);

    const std::vector<glm::mat4>& drawn = playback.WorldTransforms();
    ASSERT_EQ(drawn.size(), 2u);

    // The same engine player, sampled explicitly at both ticks, is the reference.
    igi::AnimPlayer reference;
    ASSERT_TRUE(reference.Build(clip, clip.animId));
    ASSERT_TRUE(reference.Play(igi::kAnimLoop));

    reference.SetTime(320.0f);
    const std::vector<glm::mat4> at_posed_tick = reference.PoseModelLocal();
    reference.SetTime(480.0f);
    const std::vector<glm::mat4> at_clock_tick = reference.PoseModelLocal();

    const glm::vec4 drawn_arm = drawn[1] * glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
    const glm::vec4 posed_arm = at_posed_tick[1] * glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
    const glm::vec4 clock_arm = at_clock_tick[1] * glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);

    EXPECT_NEAR(glm::length(drawn_arm - posed_arm), 0.0f, 1e-5f);
    EXPECT_GT(glm::length(clock_arm - posed_arm), 0.01f);
    EXPECT_GT(glm::length(drawn_arm - clock_arm), 0.01f);
}

TEST(AnimPlaybackEngineTest, SpeedIsTheEnginesTicksPerFrameMultiplier) {
    // Anim_SetSpeed 0x4D6580: `inst->speed = gTicksPerFrame * speed`, so at 2x a
    // single Step advances 320 ticks instead of 160.
    const AnimationClip clip = MakeEngineClip(800.0f, false);

    AnimPlayback normal;
    normal.Start(&clip);
    normal.forceLoop = true;
    normal.Update(120.0f);

    AnimPlayback doubled;
    doubled.Start(&clip);
    doubled.forceLoop = true;
    doubled.speed = 2.0f;
    doubled.Update(120.0f);

    EXPECT_FLOAT_EQ(normal.currentTimeMs, 480.0f);    // 3 * 160
    EXPECT_FLOAT_EQ(doubled.currentTimeMs, 160.0f);   // 3 * 320 = 960 -> wrapped
    EXPECT_TRUE(doubled.playing);
}

TEST(AnimPlaybackEngineTest, PauseStopsTheClockAndResumeContinuesIt) {
    const AnimationClip clip = MakeEngineClip(4800.0f, true);
    AnimPlayback playback;
    playback.Start(&clip);
    playback.Update(120.0f);
    EXPECT_FLOAT_EQ(playback.currentTimeMs, 480.0f);

    // The engine has no Pause; the editor gate simply stops calling Step, so the
    // engine clock keeps its value (and the sub-frame remainder is kept too).
    playback.Pause();
    playback.Update(120.0f);
    EXPECT_FLOAT_EQ(playback.currentTimeMs, 480.0f);

    playback.Resume();
    playback.Update(120.0f);
    EXPECT_FLOAT_EQ(playback.currentTimeMs, 1120.0f);   // 240 ms -> 7 frames
}

TEST(AnimPlaybackEngineTest, RefusesAClipTheEngineCannotBuild) {
    const AnimationClip clip = MakeBonelessClip();
    AnimPlayback playback;
    playback.Start(&clip);

    // `BoneObj_InitAnimInst` dereferences the clip's tracks without a guard
    // (src/boneobj/boneobj.c:1013-1021); the port reports instead of faulting.
    EXPECT_FALSE(playback.EngineReady());
    EXPECT_TRUE(playback.WorldTransforms().empty());
    EXPECT_TRUE(playback.RawBoneTransforms().empty());
    playback.Update(120.0f);
    EXPECT_FLOAT_EQ(playback.currentTimeMs, 0.0f);

    // And the render gate keeps the STATIC mesh: an empty pose would otherwise
    // suppress the static draw and leave the object invisible.
    EXPECT_FALSE(ShouldUseSkinnedReplacement(playback, 0, 1, false, true));

    const AnimationClip playable = MakeEngineClip(800.0f, false);
    AnimPlayback ok;
    ok.Start(&playable);
    EXPECT_TRUE(ShouldUseSkinnedReplacement(ok, 0, 1, false, true));
}

TEST(AnimPlaybackEngineTest, MovedPlaybackCarriesItsEnginePlayer) {
    const AnimationClip clip = MakeEngineClip(4800.0f, false);
    AnimPlayback source;
    source.Start(&clip);
    source.Update(120.0f);
    ASSERT_FLOAT_EQ(source.currentTimeMs, 480.0f);

    AnimPlayback moved = std::move(source);
    EXPECT_TRUE(moved.EngineReady());
    EXPECT_FLOAT_EQ(moved.currentTimeMs, 480.0f);
    EXPECT_EQ(moved.clip, &clip);
    // The moved-from object owns no player: a copy would have been a second
    // clock for the same object, which is why the copy operations are deleted.
    EXPECT_FALSE(source.EngineReady());
    EXPECT_EQ(source.clip, nullptr);

    moved.Update(120.0f);
    EXPECT_FLOAT_EQ(moved.currentTimeMs, 1120.0f);   // 240 ms -> 7 frames
}

TEST(AnimPlaybackEngineTest, ResetRewindsToTheFirstFrameAndStops) {
    const AnimationClip clip = MakeEngineClip(800.0f, false);
    AnimPlayback playback;
    playback.Start(&clip);
    playback.Update(120.0f);
    ASSERT_FLOAT_EQ(playback.currentTimeMs, 480.0f);

    playback.Reset();
    EXPECT_FLOAT_EQ(playback.currentTimeMs, 0.0f);
    EXPECT_FALSE(playback.playing);
    EXPECT_TRUE(playback.EngineReady());
    playback.Update(120.0f);
    EXPECT_FLOAT_EQ(playback.currentTimeMs, 0.0f);
}

TEST(AnimPlaybackEngineTest, IsFinishedUsesTheEngineLengthNotTheBefPlusOne) {
    // `AnimationClip::length_ms` is the BEF's stored value, which is the engine
    // length + 1; the engine's end test is `time >= length`.
    const AnimationClip clip = MakeEngineClip(480.0f, false);
    ASSERT_EQ(clip.duration_ms(), 481);
    AnimPlayback playback;
    playback.Start(&clip);
    EXPECT_FALSE(playback.IsFinished());

    playback.Update(120.0f);
    EXPECT_FLOAT_EQ(playback.currentTimeMs, 480.0f);
    EXPECT_FALSE(playback.playing);
    EXPECT_TRUE(playback.IsFinished());
}

// ── The engine's clock-only step (`AnimPlayback::AdvanceClock`) ─────────────
//
// The engine steps a soldier's animation clock every frame it updates the AI
// (`Soldier_Update` 0x45D080 -> `Human_FireAnimTrigger` 0x4613D0 ->
// `BoneObj_GetEvents` 0x4D3210, src/humanbase/human.c:1368-1387) but computes the
// bone POSE only for an object the renderer draws (`Human_WakeIfVisible` 0x45F140
// -> `Human_Wake` 0x45F240 -> `BoneObj_Update` 0x4D38C0, src/humanbase/human.c:262-297,
// reached only from `Human_DrawHandler` 0x45F280 / `Human_NearClipHandler` 0x45F050).
// `AdvanceClock` is that clock-only step, and these tests pin the two things that
// make it a pure cost saving rather than a behaviour change: the CLOCK it
// produces is bit-identical to `Update`'s, and the POSE it leaves is the one
// retail would show, because the next pose getter wakes it at draw time.

TEST(AnimPlaybackEngineTest, AdvanceClockMovesTheClockExactlyLikeUpdate) {
    const AnimationClip clip = MakeEngineClip(4800.0f, false);
    AnimPlayback posed;
    AnimPlayback clock_only;
    posed.Start(&clip);
    clock_only.Start(&clip);

    // 120 ms is 3.6 engine frames -> 3 * 160 ticks, and the remainder is banked.
    for (int i = 0; i < 4; ++i) {
        posed.Update(120.0f);
        clock_only.AdvanceClock(120.0f);
    }
    EXPECT_FLOAT_EQ(posed.currentTimeMs, clock_only.currentTimeMs);
    EXPECT_FLOAT_EQ(posed.currentTimeMs, 2240.0f);   // 480 ms -> 14 frames
    EXPECT_TRUE(clock_only.playing);
}

TEST(AnimPlaybackEngineTest, AdvanceClockStopsPosingAndTheDrawWakesIt) {
    const AnimationClip clip = MakeEngineClip(4800.0f, true);
    AnimPlayback playback;
    playback.Start(&clip);
    playback.Update(120.0f);           // posed at tick 320, clock now 480
    ASSERT_TRUE(playback.PoseCurrent());
    const std::vector<glm::mat4> posed_at_draw = playback.WorldTransforms();
    ASSERT_EQ(posed_at_draw.size(), 2u);

    // Clock-only for the next 120 ms: 120 ms of Update banked 20 ms, so 140 ms
    // is 4 whole engine frames (not 3) - the clock moves, the bones do not.
    // `PoseCurrent()` is the observable that says no pose ran - the bone records
    // themselves are only reachable through a getter, and reading one is the
    // wake-at-draw itself.
    playback.AdvanceClock(120.0f);
    EXPECT_FLOAT_EQ(playback.currentTimeMs, 1120.0f);   // 480 + 4 * 160
    EXPECT_FALSE(playback.PoseCurrent());

    // The first pose getter is the wake-at-draw (`Human_Wake` 0x45F240), and it
    // poses the CURRENT clock, not the stale one.
    const std::vector<glm::mat4> woken = playback.WorldTransforms();
    EXPECT_TRUE(playback.PoseCurrent());
    ASSERT_EQ(woken.size(), 2u);

    igi::AnimPlayer reference;
    ASSERT_TRUE(reference.Build(clip, clip.animId));
    ASSERT_TRUE(reference.Play(igi::kAnimLoop));
    reference.SetTime(1120.0f);
    const std::vector<glm::mat4> at_clock_tick = reference.PoseModelLocal();
    EXPECT_NEAR(glm::length((woken[1] * glm::vec4(0.0f, 1.0f, 0.0f, 1.0f)) -
                            (at_clock_tick[1] * glm::vec4(0.0f, 1.0f, 0.0f, 1.0f))),
                0.0f, 1e-5f);
}

TEST(AnimPlaybackEngineTest, ClockOnlyThenVisibleIsTheSamePlaybackAsAlwaysVisible) {
    // The fidelity claim that makes the skip safe: an object that was clock-only
    // for N frames and is then posed ends up at the same tick and the same pose
    // as one that was posed on every one of those frames. If this holds, the
    // skipped work cannot be observed.
    const AnimationClip clip = MakeEngineClip(4800.0f, true);
    AnimPlayback always_visible;
    AnimPlayback hidden_then_visible;
    always_visible.Start(&clip);
    hidden_then_visible.Start(&clip);

    for (int i = 0; i < 5; ++i) {
        always_visible.Update(120.0f);           // 3 engine frames each time
        hidden_then_visible.AdvanceClock(120.0f);
    }
    ASSERT_FLOAT_EQ(always_visible.currentTimeMs, hidden_then_visible.currentTimeMs);

    // Both are now drawn. The always-visible one poses as usual; the other is
    // woken by its first pose getter.
    always_visible.Update(120.0f);
    hidden_then_visible.Update(120.0f);
    EXPECT_FLOAT_EQ(always_visible.currentTimeMs, hidden_then_visible.currentTimeMs);

    const std::vector<glm::mat4>& a = always_visible.WorldTransforms();
    const std::vector<glm::mat4>& b = hidden_then_visible.WorldTransforms();
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                EXPECT_FLOAT_EQ(a[i][c][r], b[i][c][r]) << "bone " << i;
            }
        }
    }
}

TEST(AnimPlaybackEngineTest, ClockOnlyPlaybackStillStopsAtANonLoopingEnd) {
    // The end test lives in `FinishAdvance`, which both paths share, so a
    // clock-only playback must still stop: `BoneObj_AnimEventsCB` sets
    // BONE_EVENTS_DONE and that is what `HumanPlayer_AnimDone` 0x489F50 reads.
    const AnimationClip clip = MakeEngineClip(480.0f, false);
    AnimPlayback playback;
    playback.Start(&clip);
    playback.AdvanceClock(120.0f);
    EXPECT_FLOAT_EQ(playback.currentTimeMs, 480.0f);
    EXPECT_FALSE(playback.playing);
    EXPECT_TRUE(playback.IsFinished());
}

// ── The pose budget: ONE pose per rendered frame, whatever the frame cost ───
//
// The engine spends two different per-frame budgets and only one of them is per
// drawn object:
//
//   * the CLOCK advances exactly once per frame for every active AI -
//     `Soldier_Update` 0x45D080 -> `Human_FireAnimTrigger` 0x4613D0 ->
//     `BoneObj_GetEvents` 0x4D3210, `inst->time = inst->speed + inst->time`
//     (src/boneobj/boneobj.c:884-885) - and retail's loop calls `Game_Update()`
//     once per iteration with no elapsed time at all (src/win/appmain.c:224-227);
//   * the POSE is built once per frame for each object that is DRAWN, at draw
//     time (`Human_WakeIfVisible` 0x45F140 -> `Human_Wake` 0x45F240 ->
//     `BoneObj_Update` 0x4D38C0, src/humanbase/human.c:262-297).
//
// An editor frame can cover several engine frames (this host's Wine run measures
// 0.25-1.3 s frames), and the editor keeps the preview clock in real time, so the
// clock must step all of them - but posing every one of them was N-1 full
// recursive poses per drawn object whose result was overwritten before anything
// read it, and because that waste lengthens the frame (and so the next frame's N)
// it is a feedback loop.  `EnginePoseSteps()` counts the engine's own pose builds
// (`BoneObj_Update`), so these tests can pin the budget without a clock.

// `frames` engine frames plus 10 ms of slack: the slack is banked (one engine
// frame is 33.33 ms, so 10 ms cannot cross another one) and keeps the expected
// frame count exact instead of resting on float equality at a frame boundary.
float DtForEngineFrames(int frames) {
    return kFrameMs * static_cast<float>(frames) + 10.0f;
}

TEST(AnimPlaybackEngineTest, ALongDrawnFramePosesOnceNotOncePerEngineFrame) {
    const AnimationClip clip = MakeEngineClip(48000.0f, true);
    AnimPlayback playback;
    playback.Start(&clip);
    ASSERT_TRUE(playback.EngineReady());
    // `Start` -> `Anim_Play` poses once immediately
    // (src/humanplayer/humanplayer.c:1214-1215), so the counter starts at 1.
    const int poses_at_start = playback.EnginePoseSteps();
    ASSERT_EQ(poses_at_start, 1);

    const int kFrames = 15;   // ~500 ms at 30 fps: the frame this host really sees
    const int steps = playback.Update(DtForEngineFrames(kFrames));

    // The CLOCK follows the wall clock: every engine frame the frame covered.
    EXPECT_EQ(steps, kFrames);
    EXPECT_FLOAT_EQ(playback.currentTimeMs, static_cast<float>(kFrames) * kTicksPerFrame);
    // The POSE does not: one per drawn frame, like the engine.
    EXPECT_EQ(playback.EnginePoseSteps() - poses_at_start, 1)
        << "a " << kFrames << "-engine-frame draw must build ONE pose, not " << kFrames;
    EXPECT_TRUE(playback.PoseCurrent());
}

TEST(AnimPlaybackEngineTest, SinglePoseLeavesTheSameClockAndPoseAsPosingEveryEngineFrame) {
    // THE FIDELITY BAR for the pose budget.  For every frame length, the state the
    // editor ends the frame in must be BIT-identical to the old loop that called
    // `Step()` (pose + advance) once per engine frame: the same clock and the same
    // drawn pose.  The reference below IS that old loop, written with the engine's
    // own primitive, so this is a differential and not a restatement of the
    // implementation.
    const AnimationClip clip = MakeEngineClip(48000.0f, true);

    for (int frames : {1, 2, 3, 7, 15, 40}) {
        AnimPlayback playback;
        playback.Start(&clip);
        ASSERT_TRUE(playback.EngineReady());

        igi::AnimPlayer reference;
        ASSERT_TRUE(reference.Build(clip, clip.animId));
        ASSERT_TRUE(reference.Play(igi::kAnimLoop));

        ASSERT_EQ(playback.Update(DtForEngineFrames(frames)), frames) << "frames=" << frames;
        for (int i = 0; i < frames; ++i) reference.Step();

        EXPECT_FLOAT_EQ(playback.currentTimeMs, reference.Time()) << "frames=" << frames;
        const std::vector<glm::mat4>& got = playback.WorldTransforms();
        const std::vector<glm::mat4>& want = reference.PoseFromLastStep();
        ASSERT_EQ(got.size(), want.size()) << "frames=" << frames;
        EXPECT_EQ(MaxAbsPoseDiff(got, want), 0.0f)
            << "the drawn pose moved, frames=" << frames;

        // ... and the COST differs by exactly the discarded poses: the reference
        // pays one pose per engine frame (its `Play` posed once, then each `Step`
        // did), the editor pays the play-time pose plus the one drawn frame.
        EXPECT_EQ(reference.PoseSteps(), frames + 1) << "frames=" << frames;
        EXPECT_EQ(playback.EnginePoseSteps(), 2) << "frames=" << frames;
    }
}

TEST(AnimPlaybackEngineTest, ThePoseBudgetTracksFramesNotEngineFrames) {
    // The shape of the feedback loop: a slow frame covers many engine frames and
    // used to pay a pose for each, which made the next frame slower still.  The
    // budget must track the number of RENDERED frames instead - at most one pose
    // per frame, and none at all for a frame shorter than one engine frame (the
    // bones hold the pose the last stepping frame produced).
    const AnimationClip clip = MakeEngineClip(48000.0f, true);
    AnimPlayback playback;
    playback.Start(&clip);
    ASSERT_TRUE(playback.EngineReady());

    const int poses_at_start = playback.EnginePoseSteps();
    int poses = 0;
    int steps = 0;
    int stepping_frames = 0;
    // Deliberately ugly frame times, including two shorter than one engine frame.
    for (float dt_ms : {500.0f, 16.0f, 700.0f, 33.0f, 1200.0f, 10.0f, 60.0f}) {
        const int poses_before = playback.EnginePoseSteps();
        const int stepped = playback.Update(dt_ms);
        const int frame_poses = playback.EnginePoseSteps() - poses_before;
        EXPECT_LE(frame_poses, 1) << "dt=" << dt_ms;
        EXPECT_EQ(frame_poses, stepped > 0 ? 1 : 0) << "dt=" << dt_ms;
        poses += frame_poses;
        steps += stepped;
        if (stepped > 0) ++stepping_frames;
    }

    EXPECT_GT(steps, 60);                       // the clock really did the work
    EXPECT_EQ(poses, stepping_frames);          // one pose per stepping frame
    EXPECT_LT(poses, steps);                    // and NOT one per engine frame
    EXPECT_EQ(playback.EnginePoseSteps() - poses_at_start, poses);
}

TEST(AnimPlaybackEngineTest, AdvanceClockPosesNothingOverManyEngineFrames) {
    // The other half of the budget: a playback the viewport does not draw pays no
    // pose at all, however many engine frames its clock covers - the engine's
    // `Human_FireAnimTrigger` 0x4613D0 path.  The one pose it does pay is the
    // draw-time wake (`Human_Wake` 0x45F240), on the first getter call.
    const AnimationClip clip = MakeEngineClip(48000.0f, true);
    AnimPlayback playback;
    playback.Start(&clip);
    ASSERT_TRUE(playback.EngineReady());
    const int poses_at_start = playback.EnginePoseSteps();

    const int kFrames = 15;
    EXPECT_EQ(playback.AdvanceClock(DtForEngineFrames(kFrames)), kFrames);
    EXPECT_FLOAT_EQ(playback.currentTimeMs, static_cast<float>(kFrames) * kTicksPerFrame);
    EXPECT_EQ(playback.EnginePoseSteps(), poses_at_start);
    EXPECT_FALSE(playback.PoseCurrent());

    playback.WorldTransforms();
    EXPECT_EQ(playback.EnginePoseSteps(), poses_at_start + 1);
    EXPECT_TRUE(playback.PoseCurrent());
}

// ── The GAMEPLAY path's clock (fixed simulation ticks, not wall time) ────────
//
// `App::AdvanceGameplayAnimationClocks` drives the gameplay playbacks from the
// fixed-step simulation tick counter (`GameClock::TICK_RATE_HZ`), not from wall
// time, and splits them the same way the editor path and the engine do: the
// playback the gameplay pass actually DRAWS - the one in the 80 m filter's
// drawn set, `App::GetGameplayDrawnAnimationIndices()` - gets the engine's
// pose+clock step, and every other playback only advances its clock.  The two
// tests below pin the two properties that make that skip unobservable.

TEST(AnimPlaybackEngineTest, AdvanceClockBanksTheSubFrameRemainderLikeUpdate) {
    // The gameplay pass hands BOTH halves the same `elapsed_ms`, and that value
    // is not a whole number of engine frames (33.33 ms against a 33.33 ms
    // frame), so the two paths have to agree on the sub-frame accumulator and
    // not merely on whole-frame steps: a playback that was clock-only and is
    // then drawn must land on the tick it would have reached anyway.  Same
    // 10/10/20 ms shape as SubFrameTimeDoesNotAdvanceTheClock, clock-only, plus
    // a mixed sequence that switches halves mid-accumulation.
    const AnimationClip clip = MakeEngineClip(4800.0f, true);

    AnimPlayback clock_only;
    AnimPlayback mixed;
    clock_only.Start(&clip);
    mixed.Start(&clip);

    clock_only.AdvanceClock(10.0f);
    clock_only.AdvanceClock(10.0f);
    EXPECT_FLOAT_EQ(clock_only.currentTimeMs, 0.0f);   // 20 ms < one frame
    mixed.Update(10.0f);
    mixed.AdvanceClock(10.0f);
    EXPECT_FLOAT_EQ(mixed.currentTimeMs, 0.0f);
    EXPECT_FLOAT_EQ(mixed.currentTimeMs, clock_only.currentTimeMs);

    clock_only.AdvanceClock(20.0f);                    // 40 ms in total
    mixed.Update(20.0f);                               // drawn on this frame
    EXPECT_FLOAT_EQ(clock_only.currentTimeMs, kTicksPerFrame);
    EXPECT_FLOAT_EQ(mixed.currentTimeMs, clock_only.currentTimeMs);
}

TEST(AnimPlaybackEngineTest, GameplayTickSplitKeepsEveryClockAndEverySteppingDrawnPoseIdentical) {
    // The gameplay drawn set changes frame by frame as the player moves: a
    // playback can be inside the 80 m filter, leave it, and come back.  The rule
    // that makes skipping its pose safe is that the skip is invisible - the CLOCK
    // is untouched (`AdvanceGameplayAnimationClocks` still calls `AdvanceClock`
    // for it every frame) and the pose at every frame it IS drawn is bit-identical
    // to a playback that was posed on every frame.
    //
    // The pose comparison is scoped to the frames where the engine clock actually
    // moved, and that is not a weakening: `Update` poses BEFORE it advances
    // (`BoneMagicObj_Run` 0x531620, src/freecam/bonemagic.c:105,110), so on a
    // frame where the accumulator did not cross a whole engine frame a woken
    // playback legitimately shows the CURRENT tick's frame - that is retail's
    // `Human_Wake` 0x45F240 - while an always-posed one redraws the frame it posed
    // last time.  The two re-converge on the next frame that steps, and the CLOCK
    // assertion below holds on every frame without exception.
    constexpr int kCount = 3;
    constexpr int kFrames = 40;

    std::vector<AnimationClip> clips;
    clips.reserve(kCount);
    clips.push_back(MakeEngineClip(4800.0f, true));
    clips.push_back(MakeEngineClip(2400.0f, true));
    clips.push_back(MakeEngineClip(1600.0f, true));

    std::vector<AnimPlayback> reference(kCount);   // posed on EVERY frame
    std::vector<AnimPlayback> split(kCount);       // posed only when drawn
    for (int i = 0; i < kCount; ++i) {
        reference[i].Start(&clips[i]);
        split[i].Start(&clips[i]);
    }

    int drawn_frames_compared = 0;
    for (int frame = 0; frame < kFrames; ++frame) {
        for (int i = 0; i < kCount; ++i) {
            // Rotate membership so every playback is clock-only for some frames
            // and drawn for others, and make the LAST frame draw all of them.
            const bool drawn = (frame == kFrames - 1) || (((frame + i) % 3) != 0);

            reference[i].Update(kSimulationTickMs);
            const float before_ms = split[i].currentTimeMs;
            if (drawn) split[i].Update(kSimulationTickMs);
            else split[i].AdvanceClock(kSimulationTickMs);

            // The clock is the engine's and must not depend on whether the pose
            // was built - this is the "stopping the clock would be unfaithful"
            // half of the split.
            EXPECT_FLOAT_EQ(reference[i].currentTimeMs, split[i].currentTimeMs)
                << "clock, frame " << frame << " playback " << i;

            if (drawn && split[i].currentTimeMs != before_ms) {
                // A drawn frame whose Update really stepped: the pose must be the
                // same numbers, not merely close.
                EXPECT_EQ(MaxAbsPoseDiff(reference[i].WorldTransforms(),
                                         split[i].WorldTransforms()),
                          0.0f)
                    << "pose, frame " << frame << " playback " << i;
                ++drawn_frames_compared;
            }
        }
    }

    // Guard against the test passing because no frame ever posed anything.
    EXPECT_GT(drawn_frames_compared, kFrames);
    EXPECT_GT(split[0].currentTimeMs, 0.0f);
}

// ── The pose SET: which AI the editor poses on a frame ──────────────────────
//
// The engine builds a pose only for the humans it is about to draw:
// `Human_WakeIfVisible` 0x45F140 (src/humanbase/human.c:262-287) wakes one only
// when `Mesh3D_Exclude` 0x4D03D0 says the bounding sphere is in view AND
// `Mesh3D_GetBoneMeshLOD` 0x4CFF70 is not -1 (and is < 4), and it is called from
// the draw handlers alone.  The editor maps that onto its own viewport by
// reusing the visual pass's two tests, which is what lets an AI that is NOT
// selected animate (see source/runtime/anim_pose_set.h).

// The visual pass's cull as it was written inline before the extraction
// (`Renderer_Objects::Draw`), restated here on purpose: this is a DIFFERENTIAL
// against the production helper, so it must stay an independent copy of the two
// original comparisons rather than call the helper it checks.
static bool ViewConeAsTheDrawLoopHadIt(const glm::vec3& to_object, float bound_radius,
                                       const glm::vec3& camera_fwd, float camera_fwd_len) {
    const float dist_sq = glm::dot(to_object, to_object);
    if (camera_fwd_len > 0.01f) {
        const float facing = glm::dot(to_object, camera_fwd);
        if (facing < -bound_radius) return false;
        if (dist_sq > bound_radius * bound_radius &&
            facing < std::sqrt(dist_sq) * 0.5f - bound_radius) {
            return false;
        }
    }
    return true;
}

TEST(AnimPoseSetTest, ViewConeMatchesTheDrawLoopsOwnTestOnEveryConfiguration) {
    // A grid of camera/object configurations, including the two boundaries the
    // cull is written around: the eye plane (facing == -radius) and the cone
    // (facing == sqrt(distSq) * 0.5 - radius).
    const glm::vec3 fwd(0.0f, 1.0f, 0.0f);
    int checked = 0;
    int rejected = 0;
    for (float x = -3000.0f; x <= 3000.0f; x += 500.0f) {
        for (float y = -3000.0f; y <= 3000.0f; y += 500.0f) {
            for (float radius : {0.0f, 50.0f, 400.0f}) {
                const glm::vec3 to_object(x, y, 0.0f);
                const bool expected = ViewConeAsTheDrawLoopHadIt(to_object, radius, fwd, 1.0f);
                const bool actual = igi::PassesEditorViewCone(to_object, radius, fwd, 1.0f);
                EXPECT_EQ(expected, actual) << "x=" << x << " y=" << y << " r=" << radius;
                if (!actual) ++rejected;
                ++checked;
            }
        }
    }
    // Guard against the grid passing because nothing was ever rejected.
    EXPECT_GT(checked, 100);
    EXPECT_GT(rejected, 0);
}

TEST(AnimPoseSetTest, NoUsableCameraForwardMeansNothingIsCulled) {
    // The draw loop only applies the cone test `if (cameraFwdLen > 0.01f)`; with
    // no usable forward vector it draws everything, so an object straight behind
    // the camera is still "in view".
    EXPECT_TRUE(igi::PassesEditorViewCone(glm::vec3(0.0f, -1000.0f, 0.0f), 10.0f,
                                          glm::vec3(0.0f, 0.0f, 0.0f), 0.0f));
    EXPECT_TRUE(igi::PassesEditorViewCone(glm::vec3(0.0f, -1000.0f, 0.0f), 10.0f,
                                          glm::vec3(0.0f, 0.0f, 0.0f), 0.01f));
}

TEST(AnimPoseSetTest, ViewConeRejectsOnlyWhatCannotProjectIntoTheViewport) {
    const glm::vec3 fwd(0.0f, 1.0f, 0.0f);
    // Straight ahead: drawn.
    EXPECT_TRUE(igi::PassesEditorViewCone(glm::vec3(0.0f, 1000.0f, 0.0f), 10.0f, fwd, 1.0f));
    // Wholly behind the eye plane: the engine's cull.
    EXPECT_FALSE(igi::PassesEditorViewCone(glm::vec3(0.0f, -1000.0f, 0.0f), 10.0f, fwd, 1.0f));
    // Behind, but the bounding sphere straddles the eye plane: kept, because the
    // test is on the SPHERE, not the centre.
    EXPECT_TRUE(igi::PassesEditorViewCone(glm::vec3(0.0f, -5.0f, 0.0f), 10.0f, fwd, 1.0f));
    // Far to the side, well outside the >= 120 degree cone: culled.
    EXPECT_FALSE(igi::PassesEditorViewCone(glm::vec3(5000.0f, 100.0f, 0.0f), 10.0f, fwd, 1.0f));
    // Same, but a radius large enough to reach back into the cone: kept.
    EXPECT_TRUE(igi::PassesEditorViewCone(glm::vec3(5000.0f, 100.0f, 0.0f), 4000.0f, fwd, 1.0f));
}

TEST(AnimPoseSetTest, SelectedObjectIsPosedEvenWhenItIsNotDrawn) {
    // The editor affordance: the object being inspected keeps its pose (and its
    // skinned draw) even when it is behind the camera or LOD-culled, which is
    // what the editor did before this rule existed.
    EXPECT_TRUE(igi::ShouldPoseAnimationNow(/*is_selected=*/true, /*in_cone=*/false, -1));
    EXPECT_TRUE(igi::ShouldPoseAnimationNow(true, true, igi::kLastPosedLod));
}

TEST(AnimPoseSetTest, UnselectedAiIsPosedOnlyWhenTheViewportWouldDrawIt) {
    // In view at a drawable LOD: posed.  This is the case that used to be a
    // clock-only step, leaving the AI visibly frozen.
    EXPECT_TRUE(igi::ShouldPoseAnimationNow(false, true, 0));
    EXPECT_TRUE(igi::ShouldPoseAnimationNow(false, true, 1));
    // `Mesh3D_GetBoneMeshLOD` 0x4CFF70's -1: too far to draw, so no pose
    // (src/humanbase/human.c:281-283).
    EXPECT_FALSE(igi::ShouldPoseAnimationNow(false, true, -1));
    // The engine's own `lod < 4` guard: the coarsest level is not woken.
    EXPECT_FALSE(igi::ShouldPoseAnimationNow(false, true, igi::kLastPosedLod));
    EXPECT_TRUE(igi::ShouldPoseAnimationNow(false, true, igi::kLastPosedLod - 1));
    // Behind the camera / outside the view cone: the clock keeps running but no
    // pose is built - the engine's `Mesh3D_Exclude` branch.
    EXPECT_FALSE(igi::ShouldPoseAnimationNow(false, false, 0));
}

TEST(AnimPoseSetTest, ClockOnlyIsWhatAnUndrawnAiGetsAndItStillAdvances) {
    // The two halves the rule keeps apart: an AI the viewport does not draw is
    // NOT posed, but its clock still moves, so when it comes into view it is at
    // the tick the game would show.  `ShouldPoseAnimationNow` decides the first;
    // `AnimPlayback::AdvanceClock` (asserted in the tests above) does the second.
    const bool poses_when_hidden = igi::ShouldPoseAnimationNow(false, false, -1);
    EXPECT_FALSE(poses_when_hidden);

    const AnimationClip clip = MakeEngineClip(4800.0f, true);
    AnimPlayback hidden;
    hidden.Start(&clip);
    ASSERT_TRUE(hidden.EngineReady());
    const float clock_at_start = hidden.currentTimeMs;
    hidden.AdvanceClock(kSimulationTickMs * 4.0f);
    EXPECT_GT(hidden.currentTimeMs, clock_at_start);
    // Not posed while it was hidden ...
    EXPECT_FALSE(hidden.PoseCurrent());
    // ... and the draw wakes it at the tick the clock reached (Human_Wake
    // 0x45F240), which is the same pose an always-visible playback shows.
    AnimPlayback visible;
    visible.Start(&clip);
    ASSERT_TRUE(visible.EngineReady());
    visible.Update(kSimulationTickMs * 4.0f);
    EXPECT_EQ(hidden.WorldTransforms().size(), visible.WorldTransforms().size());
    EXPECT_TRUE(hidden.PoseCurrent());
    // Both clocks are on the same engine tick, so one more drawn frame on each
    // must produce the SAME pose: the wake-at-draw is not a lost frame.
    EXPECT_FLOAT_EQ(hidden.currentTimeMs, visible.currentTimeMs);
    hidden.Update(kSimulationTickMs);
    visible.Update(kSimulationTickMs);
    EXPECT_EQ(MaxAbsPoseDiff(hidden.WorldTransforms(), visible.WorldTransforms()), 0.0f);
}

// ===========================================================================
// Issue #73: the manual frame step - the F10 panel's Step -1 / Step +1 and the
// ',' / '.' keys
// ===========================================================================
//
// `AnimPlayback::StepFrames` runs ONE engine frame of the clock through the
// engine's own `BoneObj_GetEvents` 0x4D3210 -> `BoneObj_AnimEventsCB` 0x4D4B60
// (`inst->time = inst->speed + inst->time`, src/boneobj/boneobj.c:884-885) and
// then poses the frame it landed on, the way retail poses at draw time
// (`Human_Wake` 0x45F240, src/humanbase/human.c:289-297).  The arithmetic those
// steps follow is `igi::AnimStepTargetTick` (source/runtime/anim_panel.h), and
// `PureStepMatchesTheEngineClock` below pins the two against each other: the
// pure function is the panel's model of a step, and this is where it is required
// to agree with the ported engine rather than merely look right.

TEST(AnimPlaybackStepTest, OneStepIsExactlyOneEngineFrameEvenWhilePaused) {
    const AnimationClip clip = MakeEngineClip(4800.0f, false);
    AnimPlayback playback;
    playback.Start(&clip);
    ASSERT_TRUE(playback.EngineReady());
    playback.Pause();
    EXPECT_FALSE(playback.playing);
    EXPECT_FLOAT_EQ(playback.currentTimeMs, 0.0f);

    const int poses_before = playback.EnginePoseSteps();
    EXPECT_EQ(playback.StepFrames(1), 1);
    EXPECT_FLOAT_EQ(playback.currentTimeMs, kTicksPerFrame)
        << "one frame is gTicksPerFrame at the 30 fps the game installs";
    EXPECT_FALSE(playback.playing) << "a manual step must not start the clock";
    EXPECT_EQ(playback.EnginePoseSteps(), poses_before + 1)
        << "the step must pose the frame it landed on, not only move the clock";

    EXPECT_EQ(playback.StepFrames(1), 1);
    EXPECT_FLOAT_EQ(playback.currentTimeMs, 2.0f * kTicksPerFrame);
    // A frame step is a clock move, not a timer: the editor's own sub-frame
    // accumulator (`AnimPlayer::advance_seconds_`) is not involved at all, so a
    // step is exact however long the frames around it were.
    EXPECT_EQ(playback.StepFrames(0), 0) << "direction 0 is not a step";
    EXPECT_FLOAT_EQ(playback.currentTimeMs, 2.0f * kTicksPerFrame);
}

TEST(AnimPlaybackStepTest, AStepScalesWithTheSpeedLadder) {
    const AnimationClip clip = MakeEngineClip(4800.0f, false);
    AnimPlayback playback;
    playback.Start(&clip);
    ASSERT_TRUE(playback.EngineReady());
    playback.Pause();

    // `inst->speed = gTicksPerFrame * speed` (Anim_SetSpeed 0x4D6580,
    // src/boneobj/anim.c:172-179): the frame is the ENGINE's frame, so the step
    // grows with the multiplier the F12 / Shift+F12 keys set.
    playback.speed = 2.0f;
    EXPECT_EQ(playback.StepFrames(1), 1);
    EXPECT_FLOAT_EQ(playback.currentTimeMs, 2.0f * kTicksPerFrame);

    playback.speed = 0.5f;
    EXPECT_EQ(playback.StepFrames(1), 1);
    EXPECT_FLOAT_EQ(playback.currentTimeMs, 2.0f * kTicksPerFrame + 0.5f * kTicksPerFrame);
}

TEST(AnimPlaybackStepTest, SteppingBackWalksTheClockDownAndClampsAtZero) {
    const AnimationClip clip = MakeEngineClip(4800.0f, false);
    AnimPlayback playback;
    playback.Start(&clip);
    ASSERT_TRUE(playback.EngineReady());
    playback.Pause();

    for (int i = 0; i < 3; ++i) EXPECT_EQ(playback.StepFrames(1), 1);
    EXPECT_FLOAT_EQ(playback.currentTimeMs, 3.0f * kTicksPerFrame);

    EXPECT_EQ(playback.StepFrames(-1), 1);
    EXPECT_FLOAT_EQ(playback.currentTimeMs, 2.0f * kTicksPerFrame);
    EXPECT_EQ(playback.StepFrames(-1), 1);
    EXPECT_EQ(playback.StepFrames(-1), 1);
    EXPECT_FLOAT_EQ(playback.currentTimeMs, 0.0f);

    // At 0 a one-shot has nowhere to go: the engine's reverse test
    // `!(inst->time > 0.0f)` clamps it there (src/boneobj/boneobj.c:892-893), so
    // the step reports "nothing to step" instead of running a frame that cannot
    // move the playhead.
    EXPECT_EQ(playback.StepFrames(-1), 0);
    EXPECT_FLOAT_EQ(playback.currentTimeMs, 0.0f);
}

TEST(AnimPlaybackStepTest, AStepOntoTheEndOfAOneShotStopsThePlayback) {
    const AnimationClip clip = MakeEngineClip(3.0f * kTicksPerFrame, false);   // 3 frames
    AnimPlayback playback;
    playback.Start(&clip);
    ASSERT_TRUE(playback.EngineReady());
    EXPECT_TRUE(playback.playing);

    EXPECT_EQ(playback.StepFrames(1), 1);
    EXPECT_FLOAT_EQ(playback.currentTimeMs, kTicksPerFrame);
    EXPECT_TRUE(playback.playing);
    EXPECT_EQ(playback.StepFrames(1), 1);
    EXPECT_FLOAT_EQ(playback.currentTimeMs, 2.0f * kTicksPerFrame);
    EXPECT_EQ(playback.StepFrames(1), 1);
    EXPECT_FLOAT_EQ(playback.currentTimeMs, 3.0f * kTicksPerFrame);
    // The engine's end test: `!(inst->time < d->length)` raises BONE_EVENTS_DONE
    // on the frame that lands ON the length (boneobj.c:892-895), which is what
    // `HumanPlayer_AnimDone` 0x489F50 reads (src/loadscreen/human.c:196-204).
    EXPECT_FALSE(playback.playing) << "stepping onto the end stops a one-shot";
    EXPECT_EQ(playback.StepFrames(1), 0) << "clamped at the length: nothing to step";
    EXPECT_FLOAT_EQ(playback.currentTimeMs, 3.0f * kTicksPerFrame);
    // Backwards from the end still moves, and the playback stays stopped.
    EXPECT_EQ(playback.StepFrames(-1), 1);
    EXPECT_FLOAT_EQ(playback.currentTimeMs, 2.0f * kTicksPerFrame);
    EXPECT_FALSE(playback.playing);
}

TEST(AnimPlaybackStepTest, AStepWrapsALoopingClipByOneDuration) {
    const AnimationClip clip = MakeEngineClip(3.0f * kTicksPerFrame, true);   // loops
    AnimPlayback playback;
    playback.Start(&clip);
    ASSERT_TRUE(playback.EngineReady());
    playback.Pause();

    EXPECT_EQ(playback.StepFrames(1), 1);
    EXPECT_FLOAT_EQ(playback.currentTimeMs, kTicksPerFrame);
    EXPECT_EQ(playback.StepFrames(1), 1);
    EXPECT_FLOAT_EQ(playback.currentTimeMs, 2.0f * kTicksPerFrame);
    // The third step lands exactly on the length, so the loop branch subtracts
    // one duration (boneobj.c:900-905) and the playhead is back at frame 0.
    EXPECT_EQ(playback.StepFrames(1), 1);
    EXPECT_FLOAT_EQ(playback.currentTimeMs, 0.0f);
    EXPECT_FALSE(playback.playing) << "a paused playback stays paused across a wrap";
    // Backwards from 0 wraps up to the last frame (`length + time`).
    EXPECT_EQ(playback.StepFrames(-1), 1);
    EXPECT_FLOAT_EQ(playback.currentTimeMs, 2.0f * kTicksPerFrame);
}

TEST(AnimPlaybackStepTest, PureStepMatchesTheEngineClock) {
    // The oracle-agreement test.  For every rung of the shared speed ladder and
    // both loop modes, drive the REAL playback one engine frame at a time in
    // both directions and require the ported engine's clock to land exactly
    // where `igi::AnimStepTargetTick` says it will - including the two clamp
    // cases, where both must agree that nothing moves.
    struct Scenario {
        float length_ticks;
        bool looping;
    };
    const Scenario scenarios[] = {
        {3.0f * kTicksPerFrame, false},
        {3.0f * kTicksPerFrame, true},
        {30.0f * kTicksPerFrame, false},
        {30.0f * kTicksPerFrame, true},
        {kTicksPerFrame, true},      // a one-frame loop: the wrap lands on the same tick
    };
    const int directions[] = {1, -1};

    for (const Scenario& scenario : scenarios) {
        for (int rung = 0; rung < igi::kAnimTimerSpeedCount; ++rung) {
            const float speed = igi::AnimTimerSpeedAt(rung);
            const AnimationClip clip = MakeEngineClip(scenario.length_ticks, scenario.looping);
            AnimPlayback playback;
            playback.Start(&clip);
            ASSERT_TRUE(playback.EngineReady());
            playback.Pause();
            playback.speed = speed;
            ASSERT_FLOAT_EQ(playback.EngineLength(), scenario.length_ticks);

            for (const int direction : directions) {
                float predicted = playback.currentTimeMs;
                for (int step = 0; step < 12; ++step) {
                    const float step_ticks =
                        igi::AnimFrameStepTicks(speed, playback.TicksPerFrame());
                    const float target = igi::AnimStepTargetTick(
                        predicted, step_ticks, playback.EngineLength(), scenario.looping,
                        direction > 0);
                    const bool should_move = (target != predicted);
                    const int moved = playback.StepFrames(direction);
                    EXPECT_EQ(moved, should_move ? 1 : 0)
                        << "length " << scenario.length_ticks << " loop "
                        << scenario.looping << " speed " << speed << " dir " << direction
                        << " step " << step << ": engine moved=" << moved
                        << " but the pure rule says " << (should_move ? "move" : "stay");
                    predicted = target;
                    EXPECT_FLOAT_EQ(playback.currentTimeMs, predicted)
                        << "length " << scenario.length_ticks << " loop "
                        << scenario.looping << " speed " << speed << " dir " << direction
                        << " step " << step;
                }
            }
        }
    }
}

TEST(AnimPlaybackStepTest, TheStepShowsTheFrameItSteppedTo) {
    // The pose must follow the clock: a step that only moved the playhead would
    // leave the viewport showing the old frame (retail poses at draw time,
    // `Human_Wake` 0x45F240, src/humanbase/human.c:289-297).
    const AnimationClip clip = MakeEngineClip(4800.0f, false);
    AnimPlayback playback;
    playback.Start(&clip);
    ASSERT_TRUE(playback.EngineReady());
    playback.Pause();

    const std::vector<glm::mat4> before = playback.WorldTransforms();
    EXPECT_EQ(playback.StepFrames(1), 1);
    const std::vector<glm::mat4>& after = playback.WorldTransforms();
    EXPECT_EQ(before.size(), after.size());
    EXPECT_GT(MaxAbsPoseDiff(before, after), 0.0f)
        << "the clip's child bone rotates over its length, so the stepped frame "
           "must be a different pose from frame 0";
    EXPECT_TRUE(playback.PoseCurrent());
}

TEST(AnimPlaybackStepTest, SteppingAClipTheEngineRefusedDoesNothing) {
    const AnimationClip boneless = MakeBonelessClip();
    AnimPlayback playback;
    playback.Start(&boneless);
    ASSERT_FALSE(playback.EngineReady());
    EXPECT_EQ(playback.StepFrames(1), 0);
    EXPECT_EQ(playback.StepFrames(-1), 0);
    EXPECT_FLOAT_EQ(playback.currentTimeMs, 0.0f);
}
