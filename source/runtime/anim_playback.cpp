/******************************************************************************
 * @file    anim_playback.cpp
 * @brief   `AnimPlayback` - the editor's per-object playback state - driven by
 *          the ENGINE's own animation methods (#73).
 *
 * `AnimPlayback` is declared in source/animation.h next to the editor's own
 * `AnimationClip` and BEF reader. Its implementation lives HERE, with the engine
 * driver, for one concrete reason: animation.cpp includes utils.h (windows.h)
 * and is therefore not host-portable, so a playback implementation in that file
 * could only ever be exercised by the Windows test binary. Everything this file
 * needs - the clip data (animation.h), the engine records and the pose/skin
 * arithmetic (anim_rules.h), the player that runs them (anim_player.h) - is
 * portable, so `tests/test_anim_playback.cpp` runs in the host lane too.
 *
 * Every step is a call into the engine translation, never a re-implementation:
 *
 *   Anim_Play             0x4D61D0  src/boneobj/anim.c:38-58
 *   BoneObj_InitAnimInst  0x4D4F60  src/boneobj/boneobj.c:988-1021
 *   BoneObj_Update        0x4D38C0  src/boneobj/boneobj.c:371-419
 *   BoneObj_GetEvents     0x4D3210  src/boneobj/boneobj.c:112-129
 *   Anim_SetSpeed         0x4D6580  src/boneobj/anim.c:172-179
 *   Anim_SetTime          0x4D65B0  src/boneobj/anim.c:181-185
 *   Anim_GetTime          0x4D6500  src/boneobj/anim.c:149-153
 *   BoneObj_SetFrameRate  0x4D2D30  src/lod/boneobj.c:86-90
 *   HumanPlayer_AnimDone  0x489F50  src/loadscreen/human.c:196-204  (the end test)
 *   kTicksPerSecond       4800.0f   src/boneobj/anim.c:9
 *
 * THE CLOCK IS THE ENGINE'S. `Update` hands real elapsed milliseconds to
 * `AnimPlayer::AdvanceSeconds`, which steps WHOLE engine frames of
 * gTicksPerFrame (160 ticks at the 30 fps the game installs once,
 * src/game/game.c:449 `BoneObj_SetFrameRate(30.0f)`), exactly as the retail game
 * loop does. `currentTimeMs` is then read back out of the engine with
 * `Anim_GetTime`; the editor never adds to it itself.
 *****************************************************************************/
#include "animation.h"

#include <utility>

#include "runtime/anim_player.h"

namespace {

// Anim_Play's instance flags word (src/boneobj/anim.c:38-58). kAnimLoop is the
// only bit this driver sets: the loop flag is what `BoneObj_AnimEventsCB`
// (boneobj.c:873-941, ported at anim_rules.cpp:1282) tests when the clock
// reaches the clip length.
uint32_t PlayFlagsFor(const AnimationClip& clip, bool force_loop) {
    return (force_loop || clip.loop()) ? igi::kAnimLoop : 0u;
}

}  // namespace

AnimPlayback::AnimPlayback() = default;

AnimPlayback::~AnimPlayback() = default;

AnimPlayback::AnimPlayback(AnimPlayback&& other) noexcept
    : clip(other.clip),
      currentTimeMs(other.currentTimeMs),
      playing(other.playing),
      speed(other.speed),
      forceLoop(other.forceLoop),
      engine_(std::move(other.engine_)),
      engine_flags_(other.engine_flags_),
      engine_speed_(other.engine_speed_) {
    other.clip = nullptr;
    other.currentTimeMs = 0.0f;
    other.playing = false;
}

AnimPlayback& AnimPlayback::operator=(AnimPlayback&& other) noexcept {
    if (this != &other) {
        clip = other.clip;
        currentTimeMs = other.currentTimeMs;
        playing = other.playing;
        speed = other.speed;
        forceLoop = other.forceLoop;
        engine_ = std::move(other.engine_);
        engine_flags_ = other.engine_flags_;
        engine_speed_ = other.engine_speed_;
        other.clip = nullptr;
        other.currentTimeMs = 0.0f;
        other.playing = false;
    }
    return *this;
}

void AnimPlayback::BuildEnginePlayer() {
    engine_.reset();
    engine_flags_ = 0;
    engine_speed_ = 1.0f;
    if (clip == nullptr) return;

    // `AnimPlayer::Build` constructs the engine's own records - the Bone array
    // (`BoneObj_Build`, lod/boneobj.c:153-188), the BOTD/BORD/BOED tracks and
    // the Anim_PrepareSlerps tables - and refuses the two clips retail would
    // fault on (no bones, no position track: boneobj.c:1014 dereferences
    // `desc->pos->keys[count - 1]`).
    auto player = std::make_unique<igi::AnimPlayer>();
    if (!player->Build(*clip, clip->animId)) return;

    // Anim_Play 0x4D61D0 with blend 0 (the immediate path): the instance is
    // installed at tick 0 with `flags`, and the engine poses once immediately
    // afterwards - src/humanplayer/humanplayer.c:1214-1215 plays and then calls
    // BoneObj_Update, so the first frame is never a rest pose.
    engine_flags_ = PlayFlagsFor(*clip, forceLoop);
    if (!player->Play(engine_flags_)) return;
    // InitAnimInst overwrites `inst.speed` with gTicksPerFrame (boneobj.c:1013),
    // so the multiplier has to be applied after the play, not before.
    player->SetSpeed(speed);
    engine_speed_ = speed;
    engine_ = std::move(player);
}

void AnimPlayback::SyncEngineFlags() {
    if (!engine_) return;

    // Anim_Play bakes the loop bit into the instance (anim.c:38-58 ->
    // BoneObj_InitAnimInst, boneobj.c:1013) and there is no setter for it, so a
    // change to the editor's `forceLoop` is a re-play - which is also what a
    // retail caller does when it wants a different loop mode. It rewinds to
    // tick 0; the editor only ever flips this flag before the first frame.
    const uint32_t flags = (clip != nullptr) ? PlayFlagsFor(*clip, forceLoop) : 0u;
    if (flags != engine_flags_) {
        engine_->Play(flags);
        engine_flags_ = flags;
        engine_speed_ = 1.0f;   // Play reset inst.speed; force a re-apply below
    }
    if (speed != engine_speed_) {
        // Anim_SetSpeed 0x4D6580: `inst->speed = gTicksPerFrame * speed`.
        engine_->SetSpeed(speed);
        engine_speed_ = speed;
    }
}

void AnimPlayback::Reset() {
    currentTimeMs = 0.0f;
    playing = false;
    BuildEnginePlayer();
    // Rewound and paused: the player sits at tick 0 and `playing` is false, so
    // Update will not advance it. (Play() leaves the instance itself "playing";
    // the editor's gate is the only thing that stops the clock.)
    if (engine_) engine_->PoseFromLastStep();
}

void AnimPlayback::Start(const AnimationClip* c) {
    clip = c;
    currentTimeMs = 0.0f;
    playing = true;
    BuildEnginePlayer();
}

void AnimPlayback::Update(float dtMs) {
    if (clip == nullptr) return;
    if (!engine_) return;          // the engine refuses this clip; see EngineReady
    SyncEngineFlags();

    if (playing && dtMs > 0.0f) {
        // REAL milliseconds in, engine ticks out: one Step() per whole engine
        // frame at the current frame rate (160 ticks at 30 fps), which is the
        // only way the retail loop advances the clock.
        engine_->AdvanceSeconds(dtMs * 0.001f);
    }

    // Mirror the engine clock. Anim_GetTime 0x4D6500 returns the instance's
    // tick count; the pose drawn this frame is one Step() behind it by design
    // (retail poses, then advances - freecam/bonemagic.c:105,110).
    currentTimeMs = engine_->Time();

    if (playing && !forceLoop && !clip->loop() && engine_->Done()) {
        // The engine's own end test: HumanPlayer_AnimDone 0x489F50 reads
        // BONE_EVENTS_DONE off the root bone, which `BoneObj_AnimEventsCB` sets
        // when a NON-looping clock passes the clip length (boneobj.c:873-941).
        playing = false;
    }
}

bool AnimPlayback::EngineReady() const {
    return engine_ != nullptr && engine_->valid();
}

const std::vector<glm::mat4>& AnimPlayback::WorldTransforms() {
    if (!engine_) {
        static const std::vector<glm::mat4> kEmpty;
        return kEmpty;
    }
    // The pose the last Step produced, NOT a fresh pose at the mirrored clock:
    // retail draws the frame it posed before advancing (see the header).
    return engine_->PoseFromLastStep();
}

const std::vector<igi::BoneTransform>& AnimPlayback::RawBoneTransforms() {
    static const std::vector<igi::BoneTransform> kEmpty;
    if (!engine_) return kEmpty;
    // `Skin()`'s stale guard (anim_player.cpp) re-poses only when the bone
    // records were never captured; after Play()/Update() they always are.
    if (engine_->RawBoneTransforms().size() !=
        static_cast<std::size_t>(engine_->BoneCount())) {
        engine_->PoseFromLastStep();
    }
    return engine_->RawBoneTransforms();
}
