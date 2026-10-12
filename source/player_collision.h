// player_collision.h - Player collision, ground probe, and multi-height wall sweeps
#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace igi {

struct PlayerGroundQuery {
    bool is_grounded = false;
    float ground_height = 0.0f;
    glm::vec3 surface_normal = glm::vec3(0.0f, 0.0f, 1.0f);
    float step_down_budget = 4096.0f; // Max step down snap distance (1m in IGI world units)
    bool is_under_roof = false;
    float ceiling_height = 100000.0f;
};

struct PlayerWallSweepResult {
    bool hit_wall = false;
    glm::vec3 slide_velocity = glm::vec3(0.0f);
    glm::vec3 wall_normal = glm::vec3(0.0f);
    float hit_fraction = 1.0f;
};

struct ObstacleCollider {
    glm::vec3 center = glm::vec3(0.0f);
    float radius = 1638.4f; // in world units
    float height = 7372.8f;
};

class PlayerCollision {
public:
    PlayerCollision();

    // Query ground elevation and roof clearance
    PlayerGroundQuery QueryGround(const glm::vec3& position, float standing_eye_height, float (*get_terrain_z)(float x, float y));

    // Multi-height probe wall sweep to handle obstacles, corners, and sliding
    PlayerWallSweepResult SweepWalls(const glm::vec3& current_pos, const glm::vec3& target_pos, float body_radius, float body_height);

    // Tests if current stance can stand up without hitting ceiling
    bool CanStandUp(const glm::vec3& position, float standing_height, float (*get_terrain_z)(float x, float y));

    // Resolves circular cylinder collisions against obstacles (enemies, structures, boxes)
    void ResolveObstacles(glm::vec3& position, const std::vector<ObstacleCollider>& obstacles, float player_radius = 1638.4f);
};

} // namespace igi
