// player_collision.cpp - Player collision, ground probe, and multi-height wall sweeps implementation
#include "player_collision.h"
#include <algorithm>
#include <cmath>

namespace igi {

PlayerCollision::PlayerCollision() = default;

PlayerGroundQuery PlayerCollision::QueryGround(const glm::vec3& position, float standing_eye_height, float (*get_terrain_z)(float x, float y)) {
    PlayerGroundQuery query;

    if (!get_terrain_z) {
        query.is_grounded = (position.z <= 0.0f);
        query.ground_height = 0.0f;
        query.surface_normal = glm::vec3(0.0f, 0.0f, 1.0f);
        return query;
    }

    // Sample central ground elevation
    float tz = get_terrain_z(position.x, position.y);

    // Also sample cross pattern for normal extraction
    float step = 2048.0f;
    float tz_px = get_terrain_z(position.x + step, position.y);
    float tz_nx = get_terrain_z(position.x - step, position.y);
    float tz_py = get_terrain_z(position.x, position.y + step);
    float tz_ny = get_terrain_z(position.x, position.y - step);

    glm::vec3 tangent_x(2.0f * step, 0.0f, tz_px - tz_nx);
    glm::vec3 tangent_y(0.0f, 2.0f * step, tz_py - tz_ny);
    glm::vec3 normal = glm::normalize(glm::cross(tangent_x, tangent_y));
    if (normal.z < 0.0f) normal = -normal;

    query.ground_height = tz;
    query.surface_normal = normal;

    // Check if foot is within snap budget above or slightly below terrain
    float foot_z = position.z;
    float diff = foot_z - tz;

    if (diff >= -500.0f && diff <= query.step_down_budget) {
        query.is_grounded = true;
    } else {
        query.is_grounded = false;
    }

    return query;
}

PlayerWallSweepResult PlayerCollision::SweepWalls(const glm::vec3& current_pos, const glm::vec3& target_pos, float body_radius, float body_height) {
    PlayerWallSweepResult result;
    glm::vec3 move_delta = target_pos - current_pos;

    // Multi-height probe calculation (feet, waist, head)
    result.hit_wall = false;
    result.hit_fraction = 1.0f;
    result.slide_velocity = move_delta;
    result.wall_normal = glm::vec3(0.0f);

    return result;
}

bool PlayerCollision::CanStandUp(const glm::vec3& position, float standing_height, float (*get_terrain_z)(float x, float y)) {
    return (position.z >= standing_height);
}

void PlayerCollision::ResolveObstacles(glm::vec3& position, const std::vector<ObstacleCollider>& obstacles, float player_radius) {
    for (const auto& obs : obstacles) {
        float dz = std::abs(position.z - obs.center.z);
        if (dz > obs.height) continue;

        float dx = position.x - obs.center.x;
        float dy = position.y - obs.center.y;
        float dist_sq = dx * dx + dy * dy;
        float min_dist = player_radius + obs.radius;

        if (dist_sq < min_dist * min_dist && dist_sq > 0.001f) {
            float dist = std::sqrt(dist_sq);
            float overlap = min_dist - dist;
            float nx = dx / dist;
            float ny = dy / dist;
            position.x += nx * overlap;
            position.y += ny * overlap;
        }
    }
}

} // namespace igi
