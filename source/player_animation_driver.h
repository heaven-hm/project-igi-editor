#pragma once

#include <unordered_map>

#include "animation_motion.h"
#include "player_controller.h"

namespace igi {

class RuntimeWorld;

// Fixed-step adapter between imported vanilla human animations and the
// simulation command consumed by PlayerController/RuntimeWorld. Asset lookup
// remains owned by App; this class only owns deterministic playback state.
class PlayerAnimationDriver final {
public:
    static constexpr float AnimationFrameStepMilliseconds = 160.0f;

    void SetAnimationClip(int animation_id, const AnimationClip* animation_clip);
    void ClearAnimationClips();
    void Reset();

    // Enriches one already-routed gameplay command. It must be called once per
    // fixed simulation tick, before RuntimeWorld::UpdateSimulationTick.
    void AugmentInput(const RuntimeWorld& runtime_world, PlayerInputCmd& input_command);

    int GetActiveAnimationId() const { return active_animation_id_; }
    float GetCurrentTimeMilliseconds() const { return current_time_ms_; }

private:
    enum class PlaybackKind {
        None,
        Locomotion,
        LadderClimb,
        LadderTopEntry,
        LadderTopExit,
        LadderSlide,
    };

    const AnimationClip* FindAnimationClip(int animation_id) const;
    void BeginPlayback(
        PlaybackKind playback_kind,
        int animation_id,
        const AnimationClip* animation_clip,
        float start_time_ms,
        bool loops);
    AnimationMotionStep AdvancePlayback(float delta_time_ms, bool loops);
    void ClearMotionAndEvents(PlayerInputCmd& input_command) const;
    void AugmentLocomotion(
        const RuntimeWorld& runtime_world,
        PlayerInputCmd& input_command);
    void AugmentLadder(
        const RuntimeWorld& runtime_world,
        PlayerInputCmd& input_command);

    std::unordered_map<int, const AnimationClip*> animation_clips_;
    const AnimationClip* active_animation_clip_ = nullptr;
    PlaybackKind active_playback_kind_ = PlaybackKind::None;
    int active_animation_id_ = -1;
    float current_time_ms_ = 0.0f;
    bool active_playback_loops_ = false;
};

} // namespace igi
