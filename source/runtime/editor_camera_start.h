#pragma once

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace igi {

struct EditorCameraPose {
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
};

// Place the editor outside a selected model and aim toward its center.  The
// distance is derived from the model's transformed bounds so large authored
// buildings are not entered by the navigation command.
inline EditorCameraPose FocusCameraOnModel(
    const glm::vec3& target, float bound_radius) noexcept {
    const float radius = std::clamp(bound_radius * 1.4f, 500.0f, 500000.0f);
    const float height = radius * 0.3f;
    return {target + glm::vec3(0.0f, -radius, height),
            0.0f,
            -std::atan2(height, radius) * 57.29577951308232f};
}

// Returns whether a persisted editor camera may be reused for this load.
inline bool ShouldUseSavedEditorCamera(
    int last_loaded_level,
    int requested_level,
    const glm::vec3& saved_position) noexcept {
    const bool has_saved_position = saved_position.x != 0.0f ||
        saved_position.y != 0.0f || saved_position.z != 0.0f;
    return has_saved_position && last_loaded_level == requested_level;
}

// ALT+LMB orbits a fixed target: yaw/pitch change, distance stays, camera
// always looks at the object. Pitch is clamped so the view cannot flip.
struct ObjectOrbitCamera {
    glm::vec3 target{0.0f};
    float distance = 1.0f;
    float yaw_degrees = 0.0f;
    float pitch_degrees = 0.0f;
};

inline int ResolveOrbitObjectIndex(int clicked, int hover, int selected,
                                   int object_count) noexcept {
    auto valid = [object_count](int index) {
        return index >= 0 && index < object_count;
    };
    if (valid(clicked)) return clicked;
    if (valid(hover)) return hover;
    if (valid(selected)) return selected;
    return -1;
}

inline glm::vec3 ObjectOrbitForward(float yaw_degrees, float pitch_degrees) noexcept {
    const float yaw = glm::radians(yaw_degrees);
    const float pitch = glm::radians(pitch_degrees);
    const float cos_p = std::cos(pitch);
    return {-std::sin(yaw) * cos_p, std::cos(yaw) * cos_p, std::sin(pitch)};
}

inline ObjectOrbitCamera BeginObjectOrbit(const glm::vec3& camera_pos,
                                          const glm::vec3& target) noexcept {
    glm::vec3 to_target = target - camera_pos;
    float distance = glm::length(to_target);
    if (distance < 0.1f) {
        distance = 1.0f;
        to_target = glm::vec3(0.0f, 1.0f, 0.0f);
    } else {
        to_target /= distance;
    }
    const float horizontal = std::sqrt(to_target.x * to_target.x +
                                       to_target.y * to_target.y);
    ObjectOrbitCamera orbit;
    orbit.target = target;
    orbit.distance = distance;
    orbit.yaw_degrees = glm::degrees(std::atan2(-to_target.x, to_target.y));
    orbit.pitch_degrees = glm::degrees(
        std::atan2(to_target.z, std::max(0.000001f, horizontal)));
    return orbit;
}

inline ObjectOrbitCamera StepObjectOrbit(ObjectOrbitCamera orbit,
                                         float delta_yaw_degrees,
                                         float delta_pitch_degrees) noexcept {
    orbit.yaw_degrees += delta_yaw_degrees;
    orbit.pitch_degrees = std::clamp(orbit.pitch_degrees + delta_pitch_degrees,
                                     -89.0f, 89.0f);
    return orbit;
}

inline glm::vec3 ObjectOrbitCameraPosition(const ObjectOrbitCamera& orbit) noexcept {
    return orbit.target -
           ObjectOrbitForward(orbit.yaw_degrees, orbit.pitch_degrees) *
               orbit.distance;
}

} // namespace igi
