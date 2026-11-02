// player_motion.h - Deterministic human motion primitives shared by gameplay states.
#pragma once

#include <glm/glm.hpp>

namespace igi {

class PlayerMotion final {
public:
    // verified-reference: OpenIGI HumanMotion constants recovered from the
    // vanilla per-tick human integrators.
    static constexpr float GravityPerTick = 84.741692f;
    static constexpr float LadderSlideGravityPerTick = 44.600887f;
    static constexpr float LadderSlideDragPerTick = 0.99000001f;
    static constexpr float DefaultDeltaTranslationScale = 1.75f;

    // Advances ordinary airborne velocity by one 30 Hz simulation tick.
    static glm::vec3 IntegrateAirborneVelocity(
        const glm::vec3& velocity,
        float gravity_per_tick = GravityPerTick);

    // Advances the separate ladder-slide velocity state. Ladder slides use
    // their own gravity and damp the result after applying gravity.
    static glm::vec3 IntegrateLadderSlideVelocity(const glm::vec3& velocity);

    // Computes the per-tick airborne steering delta in the body's yaw frame.
    static glm::vec3 CalculateAirControl(
        float forward_input,
        float strafe_input,
        float yaw_degrees,
        float speed_units_per_tick);

    // Converts animation-local root translation into a world-space step.
    // The scale is intentionally applied before yaw rotation, matching the
    // reference transform order.
    static glm::vec3 ApplyRootMotion(
        const glm::vec3& local_delta,
        float yaw_degrees,
        float delta_translation_scale = DefaultDeltaTranslationScale,
        bool scale_suppressed = false);
};

} // namespace igi
