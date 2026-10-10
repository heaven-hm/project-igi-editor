#include "animation_motion.h"

#include <algorithm>
#include <cmath>

namespace igi {

namespace {

constexpr float kAnimationTimeEpsilon = 0.001f;

glm::vec3 SampleModelTranslation(const AnimationClip& animation_clip, float time_ms) {
    if (animation_clip.translationKeys.empty()) {
        return glm::vec3(0.0f);
    }

    const float clamped_time_ms = std::max(0.0f, time_ms);
    size_t key_index = 0;
    for (size_t index = 0; index < animation_clip.translationKeys.size(); ++index) {
        if (animation_clip.translationKeys[index].time_ms <=
            clamped_time_ms + kAnimationTimeEpsilon) {
            key_index = index;
            continue;
        }
        break;
    }

    const AnimTranslationKey& current_key = animation_clip.translationKeys[key_index];
    glm::vec3 sampled_position = current_key.pos;
    if (key_index + 1 >= animation_clip.translationKeys.size()) {
        return sampled_position;
    }

    const AnimTranslationKey& next_key = animation_clip.translationKeys[key_index + 1];
    const float key_interval_ms = static_cast<float>(next_key.time_ms - current_key.time_ms);
    if (key_interval_ms <= kAnimationTimeEpsilon) {
        return next_key.pos;
    }

    const float interpolation = std::clamp(
        (clamped_time_ms - static_cast<float>(current_key.time_ms)) / key_interval_ms,
        0.0f,
        1.0f);
    return glm::mix(current_key.pos, next_key.pos, interpolation);
}

void AddRootMotionDelta(
    glm::vec3& accumulated_delta,
    const AnimationClip& animation_clip,
    float from_time_ms,
    float to_time_ms) {
    const glm::vec3 model_delta =
        SampleModelTranslation(animation_clip, to_time_ms) -
        SampleModelTranslation(animation_clip, from_time_ms);
    accumulated_delta += model_delta * AnimationMotionSampler::ModelToWorldUnitScale;
}

void AppendForwardEvents(
    std::vector<int>& event_ids,
    const AnimationClip& animation_clip,
    float start_time_ms,
    float end_time_ms,
    bool include_start) {
    for (const AnimEvent& animation_event : animation_clip.events) {
        const float event_time_ms = static_cast<float>(animation_event.time_ms);
        const bool crossed = include_start
            ? event_time_ms >= start_time_ms - kAnimationTimeEpsilon &&
                event_time_ms <= end_time_ms + kAnimationTimeEpsilon
            : event_time_ms > start_time_ms + kAnimationTimeEpsilon &&
                event_time_ms <= end_time_ms + kAnimationTimeEpsilon;
        if (crossed) {
            event_ids.push_back(animation_event.event_id);
        }
    }
}

void AppendBackwardEvents(
    std::vector<int>& event_ids,
    const AnimationClip& animation_clip,
    float start_time_ms,
    float end_time_ms,
    bool include_start) {
    for (const AnimEvent& animation_event : animation_clip.events) {
        const float event_time_ms = static_cast<float>(animation_event.time_ms);
        const bool crossed = include_start
            ? event_time_ms <= start_time_ms + kAnimationTimeEpsilon &&
                event_time_ms >= end_time_ms - kAnimationTimeEpsilon
            : event_time_ms < start_time_ms - kAnimationTimeEpsilon &&
                event_time_ms >= end_time_ms - kAnimationTimeEpsilon;
        if (crossed) {
            event_ids.push_back(animation_event.event_id);
        }
    }
}

} // namespace

glm::vec3 AnimationMotionSampler::SampleRootTranslation(
    const AnimationClip& animation_clip,
    float time_ms) {
    return SampleModelTranslation(animation_clip, time_ms) * ModelToWorldUnitScale;
}

AnimationMotionStep AnimationMotionSampler::Advance(
    const AnimationClip& animation_clip,
    float current_time_ms,
    float delta_time_ms,
    bool loops) {
    AnimationMotionStep result;
    const float duration_ms = static_cast<float>(animation_clip.duration_ms());
    result.current_time_ms = std::clamp(current_time_ms, 0.0f, std::max(0.0f, duration_ms));

    if (duration_ms <= 0.0f || std::abs(delta_time_ms) <= kAnimationTimeEpsilon) {
        return result;
    }

    float cursor_time_ms = result.current_time_ms;
    float remaining_time_ms = std::abs(delta_time_ms);
    const bool advances_forward = delta_time_ms > 0.0f;

    while (remaining_time_ms > kAnimationTimeEpsilon) {
        if (advances_forward) {
            const float available_time_ms = duration_ms - cursor_time_ms;
            if (available_time_ms <= kAnimationTimeEpsilon) {
                if (!loops) {
                    result.ended = true;
                    cursor_time_ms = duration_ms;
                    break;
                }

                cursor_time_ms = 0.0f;
                result.wrapped = true;
                AppendForwardEvents(
                    result.crossed_event_ids,
                    animation_clip,
                    0.0f,
                    0.0f,
                    true);
                continue;
            }

            const float step_time_ms = std::min(remaining_time_ms, available_time_ms);
            const float next_time_ms = cursor_time_ms + step_time_ms;
            AddRootMotionDelta(
                result.root_motion_delta,
                animation_clip,
                cursor_time_ms,
                next_time_ms);
            AppendForwardEvents(
                result.crossed_event_ids,
                animation_clip,
                cursor_time_ms,
                next_time_ms,
                false);
            cursor_time_ms = next_time_ms;
            remaining_time_ms -= step_time_ms;

            if (cursor_time_ms >= duration_ms - kAnimationTimeEpsilon) {
                if (!loops) {
                    result.ended = true;
                    cursor_time_ms = duration_ms;
                    break;
                }

                cursor_time_ms = 0.0f;
                result.wrapped = true;
                if (remaining_time_ms <= kAnimationTimeEpsilon) {
                    AppendForwardEvents(
                        result.crossed_event_ids,
                        animation_clip,
                        0.0f,
                        0.0f,
                        true);
                }
            }
            continue;
        }

        const float available_time_ms = cursor_time_ms;
        if (available_time_ms <= kAnimationTimeEpsilon) {
            if (!loops) {
                result.ended = true;
                cursor_time_ms = 0.0f;
                break;
            }

            cursor_time_ms = duration_ms;
            result.wrapped = true;
            AppendBackwardEvents(
                result.crossed_event_ids,
                animation_clip,
                duration_ms,
                duration_ms,
                true);
            continue;
        }

        const float step_time_ms = std::min(remaining_time_ms, available_time_ms);
        const float next_time_ms = cursor_time_ms - step_time_ms;
        AddRootMotionDelta(
            result.root_motion_delta,
            animation_clip,
            cursor_time_ms,
            next_time_ms);
        AppendBackwardEvents(
            result.crossed_event_ids,
            animation_clip,
            cursor_time_ms,
            next_time_ms,
            false);
        cursor_time_ms = next_time_ms;
        remaining_time_ms -= step_time_ms;

        if (cursor_time_ms <= kAnimationTimeEpsilon) {
            if (!loops) {
                result.ended = true;
                cursor_time_ms = 0.0f;
                break;
            }

            cursor_time_ms = duration_ms;
            result.wrapped = true;
            if (remaining_time_ms <= kAnimationTimeEpsilon) {
                AppendBackwardEvents(
                    result.crossed_event_ids,
                    animation_clip,
                    duration_ms,
                    duration_ms,
                    true);
            }
        }
    }

    result.current_time_ms = cursor_time_ms;
    return result;
}

} // namespace igi
