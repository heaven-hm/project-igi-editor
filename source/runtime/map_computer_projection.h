#pragma once

#include <glm/glm.hpp>

namespace igi {

// Top-down world-to-screen mapping used by the tactical map overlay. The
// projection is kept outside OpenGL so marker placement remains deterministic
// and testable while the renderer owns only the drawing state.
struct RuntimeMapComputerProjection {
    glm::vec2 world_center = glm::vec2(0.0f);
    float world_units_per_screen_width = 1.0f;
    float world_units_per_screen_height = 1.0f;
    int screen_left = 0;
    int screen_top = 0;
    int screen_right = 1;
    int screen_bottom = 1;

    glm::vec2 Project(const glm::vec3& world_position) const;
    bool Contains(const glm::vec2& screen_position) const;
};

RuntimeMapComputerProjection BuildRuntimeMapComputerProjection(
    const glm::vec3& camera_position,
    float field_of_view_y_radians,
    int viewport_width,
    int viewport_height,
    int screen_left,
    int screen_top,
    int screen_right,
    int screen_bottom);

} // namespace igi
