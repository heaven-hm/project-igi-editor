#include "map_computer_projection.h"

#include <algorithm>
#include <cmath>

namespace igi {

namespace {

constexpr float kMinimumFieldOfViewRadians = 0.05f;
constexpr float kMaximumFieldOfViewRadians = 2.8f;
constexpr float kMinimumMapHeightUnits = 4096.0f;

} // namespace

glm::vec2 RuntimeMapComputerProjection::Project(
    const glm::vec3& world_position) const {
    const float normalized_x =
        (world_position.x - world_center.x) /
        std::max(1.0f, world_units_per_screen_width);
    const float normalized_y =
        (world_position.y - world_center.y) /
        std::max(1.0f, world_units_per_screen_height);
    const float screen_width = static_cast<float>(screen_right - screen_left);
    const float screen_height = static_cast<float>(screen_bottom - screen_top);
    return glm::vec2(
        static_cast<float>(screen_left) + (normalized_x + 0.5f) * screen_width,
        static_cast<float>(screen_top) + (normalized_y + 0.5f) * screen_height);
}

bool RuntimeMapComputerProjection::Contains(
    const glm::vec2& screen_position) const {
    return screen_position.x >= static_cast<float>(screen_left) &&
        screen_position.x <= static_cast<float>(screen_right) &&
        screen_position.y >= static_cast<float>(screen_top) &&
        screen_position.y <= static_cast<float>(screen_bottom);
}

RuntimeMapComputerProjection BuildRuntimeMapComputerProjection(
    const glm::vec3& camera_position,
    float field_of_view_y_radians,
    int viewport_width,
    int viewport_height,
    int screen_left,
    int screen_top,
    int screen_right,
    int screen_bottom) {
    RuntimeMapComputerProjection projection;
    projection.world_center = glm::vec2(camera_position.x, camera_position.y);
    projection.screen_left = std::min(screen_left, screen_right);
    projection.screen_top = std::min(screen_top, screen_bottom);
    projection.screen_right = std::max(screen_left, screen_right);
    projection.screen_bottom = std::max(screen_top, screen_bottom);

    const float safe_field_of_view = std::clamp(
        field_of_view_y_radians,
        kMinimumFieldOfViewRadians,
        kMaximumFieldOfViewRadians);
    const float safe_height = std::max(
        kMinimumMapHeightUnits,
        std::abs(camera_position.z));
    projection.world_units_per_screen_height = std::max(
        kMinimumMapHeightUnits,
        2.0f * safe_height * std::tan(safe_field_of_view * 0.5f));

    const float safe_viewport_height = static_cast<float>(
        std::max(1, viewport_height));
    const float viewport_aspect = static_cast<float>(std::max(1, viewport_width)) /
        safe_viewport_height;
    projection.world_units_per_screen_width =
        projection.world_units_per_screen_height * std::max(0.01f, viewport_aspect);
    return projection;
}

} // namespace igi
