// player_motion.cpp - Deterministic human motion primitives shared by gameplay states.
#include "player_motion.h"

#include <algorithm>
#include <cmath>

namespace igi {

namespace {

constexpr float kDegreesToRadians = 0.017453292519943295769f;

} // namespace

glm::vec3 PlayerMotion::IntegrateAirborneVelocity(
    const glm::vec3& velocity,
    float gravity_per_tick) {
    return glm::vec3(
        velocity.x,
        velocity.y,
        velocity.z - gravity_per_tick);
}

glm::vec3 PlayerMotion::IntegrateLadderSlideVelocity(const glm::vec3& velocity) {
    return glm::vec3(
        velocity.x * LadderSlideDragPerTick,
        velocity.y * LadderSlideDragPerTick,
        (velocity.z - LadderSlideGravityPerTick) * LadderSlideDragPerTick);
}

glm::vec3 PlayerMotion::CalculateAirControl(
    float forward_input,
    float strafe_input,
    float yaw_degrees,
    float speed_units_per_tick) {
    const float clamped_forward_input = std::clamp(forward_input, -1.0f, 1.0f);
    const float clamped_strafe_input = std::clamp(strafe_input, -1.0f, 1.0f);
    const float yaw_radians = yaw_degrees * kDegreesToRadians;
    const float cosine_yaw = std::cos(yaw_radians);
    const float sine_yaw = std::sin(yaw_radians);

    const float right_motion = clamped_strafe_input * speed_units_per_tick;
    const float forward_motion = clamped_forward_input * speed_units_per_tick;
    return glm::vec3(
        cosine_yaw * right_motion - sine_yaw * forward_motion,
        sine_yaw * right_motion + cosine_yaw * forward_motion,
        0.0f);
}

glm::vec3 PlayerMotion::ApplyRootMotion(
    const glm::vec3& local_delta,
    float yaw_degrees,
    float delta_translation_scale,
    bool scale_suppressed) {
    glm::vec3 scaled_delta = local_delta;
    if (!scale_suppressed) {
        scaled_delta *= delta_translation_scale;
    }

    const float yaw_radians = yaw_degrees * kDegreesToRadians;
    const float cosine_yaw = std::cos(yaw_radians);
    const float sine_yaw = std::sin(yaw_radians);
    return glm::vec3(
        cosine_yaw * scaled_delta.x - sine_yaw * scaled_delta.y,
        sine_yaw * scaled_delta.x + cosine_yaw * scaled_delta.y,
        scaled_delta.z);
}

} // namespace igi
