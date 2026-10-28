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
