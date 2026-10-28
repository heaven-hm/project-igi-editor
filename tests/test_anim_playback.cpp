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
