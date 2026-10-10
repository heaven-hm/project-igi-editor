#include "player_animation_driver.h"

#include <array>
#include <algorithm>

#include "runtime/runtime_world.h"

namespace igi {

namespace {

constexpr float kInputThreshold = 0.01f;
constexpr int kLadderClimbAnimationId = 168;
constexpr int kLadderSlideAnimationId = 169;
constexpr int kLadderTopAnimationId = 170;
constexpr int kLadderBoundaryEventId = 8;

// HumanLocomotionStates.cs, indexed by the standing/crouched state number.
// The first entry is the idle clip; entries 1..8 are the eight directional
// clips and 9..16 are their crouched counterparts.
constexpr std::array<int, 17> kLocomotionAnimationIds = {
    2, 4, 37, 35, 64, 70, 68, 11, 10,
    57, 74, 72, 75, 77, 76, 81, 80,
};

bool HasAnimationEvent(const AnimationMotionStep& animation_step, int event_id) {
    return std::find(
        animation_step.crossed_event_ids.begin(),
        animation_step.crossed_event_ids.end(),
        event_id) != animation_step.crossed_event_ids.end();
}

int SelectLocomotionState(const PlayerInputCmd& input_command) {
    const bool moving_forward = input_command.forward > kInputThreshold;
    const bool moving_backward = input_command.forward < -kInputThreshold;
    const bool strafing_right = input_command.strafe > kInputThreshold;
    const bool strafing_left = input_command.strafe < -kInputThreshold;

    int standing_state = 0;
    if (moving_forward && strafing_right) {
        standing_state = 2;
    } else if (moving_forward && strafing_left) {
        standing_state = 3;
    } else if (moving_forward) {
        standing_state = 1;
    } else if (moving_backward && strafing_right) {
        standing_state = 5;
    } else if (moving_backward && strafing_left) {
        standing_state = 6;
    } else if (moving_backward) {
        standing_state = 4;
    } else if (strafing_right) {
        standing_state = 7;
    } else if (strafing_left) {
        standing_state = 8;
    }

    if (standing_state != 0 && input_command.crouch) {
        standing_state += 8;
    }
    return standing_state;
}

} // namespace

void PlayerAnimationDriver::SetAnimationClip(
    int animation_id,
    const AnimationClip* animation_clip) {
    if (animation_id < 0) {
        return;
    }
    if (animation_clip == nullptr) {
        animation_clips_.erase(animation_id);
        return;
    }
    animation_clips_[animation_id] = animation_clip;
}

void PlayerAnimationDriver::ClearAnimationClips() {
    animation_clips_.clear();
    Reset();
}

void PlayerAnimationDriver::Reset() {
    active_animation_clip_ = nullptr;
    active_playback_kind_ = PlaybackKind::None;
    active_animation_id_ = -1;
    current_time_ms_ = 0.0f;
    active_playback_loops_ = false;
}

const AnimationClip* PlayerAnimationDriver::FindAnimationClip(int animation_id) const {
    const auto clip_iterator = animation_clips_.find(animation_id);
    return clip_iterator == animation_clips_.end() ? nullptr : clip_iterator->second;
}

void PlayerAnimationDriver::BeginPlayback(
    PlaybackKind playback_kind,
    int animation_id,
    const AnimationClip* animation_clip,
    float start_time_ms,
    bool loops) {
    const bool playback_changed =
        active_playback_kind_ != playback_kind ||
        active_animation_id_ != animation_id ||
        active_animation_clip_ != animation_clip ||
        active_playback_loops_ != loops;
    if (!playback_changed) {
        return;
    }

    active_playback_kind_ = playback_kind;
    active_animation_id_ = animation_id;
    active_animation_clip_ = animation_clip;
    active_playback_loops_ = loops;
    current_time_ms_ = animation_clip == nullptr
        ? 0.0f
        : std::clamp(start_time_ms, 0.0f, static_cast<float>(animation_clip->duration_ms()));
}

AnimationMotionStep PlayerAnimationDriver::AdvancePlayback(
    float delta_time_ms,
    bool loops) {
    if (active_animation_clip_ == nullptr) {
        AnimationMotionStep no_animation_step;
        no_animation_step.current_time_ms = current_time_ms_;
        return no_animation_step;
    }

    AnimationMotionStep animation_step = AnimationMotionSampler::Advance(
        *active_animation_clip_,
        current_time_ms_,
        delta_time_ms,
        loops);
    current_time_ms_ = animation_step.current_time_ms;
    return animation_step;
}

void PlayerAnimationDriver::ClearMotionAndEvents(PlayerInputCmd& input_command) const {
    input_command.root_motion_delta = glm::vec3(0.0f);
    input_command.root_motion_scale = PlayerMotion::DefaultDeltaTranslationScale;
    input_command.suppress_root_motion_scale = false;
    input_command.ladder_step_complete = false;
    input_command.ladder_top_transition_complete = false;
    input_command.ladder_slide_complete = false;
}

void PlayerAnimationDriver::AugmentInput(
    const RuntimeWorld& runtime_world,
    PlayerInputCmd& input_command) {
    ClearMotionAndEvents(input_command);
    if (!runtime_world.GetPlayer().IsAlive()) {
        Reset();
        return;
    }

    if (runtime_world.IsPlayerOnLadder()) {
        AugmentLadder(runtime_world, input_command);
        return;
    }

    AugmentLocomotion(runtime_world, input_command);
}

void PlayerAnimationDriver::AugmentLocomotion(
    const RuntimeWorld& runtime_world,
    PlayerInputCmd& input_command) {
    if (!runtime_world.GetPlayer().IsGrounded()) {
        BeginPlayback(PlaybackKind::None, -1, nullptr, 0.0f, false);
        return;
    }

    const int locomotion_state = SelectLocomotionState(input_command);
    const int animation_id = kLocomotionAnimationIds[
        static_cast<size_t>(locomotion_state)];
    BeginPlayback(
        PlaybackKind::Locomotion,
        animation_id,
        FindAnimationClip(animation_id),
        0.0f,
        true);

    const AnimationMotionStep animation_step = AdvancePlayback(
        AnimationFrameStepMilliseconds,
        true);
    input_command.root_motion_delta = animation_step.root_motion_delta;
}

void PlayerAnimationDriver::AugmentLadder(
    const RuntimeWorld& runtime_world,
    PlayerInputCmd& input_command) {
    const LadderTraversal& ladder_traversal = runtime_world.GetLadderTraversal();
    switch (ladder_traversal.GetPhase()) {
        case LadderTraversalPhase::Climbing: {
            if (input_command.interact) {
                BeginPlayback(
                    PlaybackKind::LadderSlide,
                    kLadderSlideAnimationId,
                    FindAnimationClip(kLadderSlideAnimationId),
                    0.0f,
                    true);
                // RuntimeWorld owns the gravity/ground collision slide. Keep
                // animation 169's clock alive without letting an unintegrated
                // visual track bypass that collision path.
                AdvancePlayback(AnimationFrameStepMilliseconds, true);
                return;
            }

            const bool moving_up = input_command.forward > kInputThreshold;
            const bool moving_down = input_command.forward < -kInputThreshold;
            if (moving_up && ladder_traversal.GetStep() == ladder_traversal.GetTopStep()) {
                BeginPlayback(
                    PlaybackKind::LadderTopExit,
                    kLadderTopAnimationId,
                    FindAnimationClip(kLadderTopAnimationId),
                    0.0f,
                    false);
                const AnimationMotionStep animation_step = AdvancePlayback(
                    AnimationFrameStepMilliseconds,
                    false);
                input_command.root_motion_delta = animation_step.root_motion_delta;
                input_command.ladder_top_transition_complete = animation_step.ended;
                return;
            }

            BeginPlayback(
                PlaybackKind::LadderClimb,
                kLadderClimbAnimationId,
                FindAnimationClip(kLadderClimbAnimationId),
                0.0f,
                true);
            int playback_direction = ladder_traversal.GetDirection();
            if (playback_direction == 0) {
                playback_direction = moving_up ? 1 : (moving_down ? -1 : 0);
            }
            if (playback_direction == 0) {
                return;
            }

            const AnimationMotionStep animation_step = AdvancePlayback(
                AnimationFrameStepMilliseconds * static_cast<float>(playback_direction),
                true);
            input_command.root_motion_delta = animation_step.root_motion_delta;
            input_command.suppress_root_motion_scale = true;
            input_command.ladder_step_complete = HasAnimationEvent(
                animation_step,
                kLadderBoundaryEventId);
            return;
        }
        case LadderTraversalPhase::GettingOnTop: {
            const AnimationClip* animation_clip = FindAnimationClip(kLadderTopAnimationId);
            const float start_time_ms = animation_clip == nullptr
                ? 0.0f
                : static_cast<float>(animation_clip->duration_ms());
            BeginPlayback(
                PlaybackKind::LadderTopEntry,
                kLadderTopAnimationId,
                animation_clip,
                start_time_ms,
                false);
            const AnimationMotionStep animation_step = AdvancePlayback(
                -AnimationFrameStepMilliseconds,
                false);
            input_command.root_motion_delta = animation_step.root_motion_delta;
            input_command.suppress_root_motion_scale = true;
            input_command.ladder_top_transition_complete = animation_step.ended;
            return;
        }
        case LadderTraversalPhase::GettingOffTop: {
            BeginPlayback(
                PlaybackKind::LadderTopExit,
                kLadderTopAnimationId,
                FindAnimationClip(kLadderTopAnimationId),
                0.0f,
                false);
            const AnimationMotionStep animation_step = AdvancePlayback(
                AnimationFrameStepMilliseconds,
                false);
            input_command.root_motion_delta = animation_step.root_motion_delta;
            input_command.suppress_root_motion_scale = true;
            input_command.ladder_top_transition_complete = animation_step.ended;
            return;
        }
        case LadderTraversalPhase::SlidingDown: {
            BeginPlayback(
                PlaybackKind::LadderSlide,
                kLadderSlideAnimationId,
                FindAnimationClip(kLadderSlideAnimationId),
                0.0f,
                true);
            AdvancePlayback(AnimationFrameStepMilliseconds, true);
            return;
        }
        case LadderTraversalPhase::Inactive:
            return;
    }
}

} // namespace igi
