#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "animation.h"

namespace igi {

// The BEF importer stores animation translations in the same model-local unit
// space used by AnimationRegistry::Evaluate. Runtime world positions use the
// native IGI unit space, whose conversion is applied by the renderer's 40.96
// model scale.
struct AnimationMotionStep {
    glm::vec3 root_motion_delta = glm::vec3(0.0f);
    std::vector<int> crossed_event_ids;
    float current_time_ms = 0.0f;
    bool wrapped = false;
    bool ended = false;
};

class AnimationMotionSampler {
public:
    static constexpr float ModelToWorldUnitScale = 40.96f;

    // Samples the imported root translation and returns native IGI world units.
    static glm::vec3 SampleRootTranslation(
        const AnimationClip& animation_clip,
        float time_ms);

    // Advances one deterministic animation interval. Positive intervals play
    // forward; negative intervals play backward, which is required by the
    // vanilla ladder-down transition. The interval is open at its old time and
    // closed at its new time, matching the reference event dispatcher.
    static AnimationMotionStep Advance(
        const AnimationClip& animation_clip,
        float current_time_ms,
        float delta_time_ms,
        bool loops);
};

} // namespace igi
