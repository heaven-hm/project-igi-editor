// player_collision.h - Runtime ground, ceiling, and wall collision boundaries.
#pragma once

#include <functional>
#include <vector>

#include <glm/glm.hpp>

namespace igi {

struct PlayerGroundQuery {
    bool is_grounded = false;
    float ground_height = 0.0f;
    glm::vec3 surface_normal = glm::vec3(0.0f, 0.0f, 1.0f);
    float step_down_budget = 2048.0f;
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
    float radius = 1638.4f;
    float height = 7372.8f;
};

class PlayerCollision {
public:
    using TerrainHeightQuery = float (*)(float x, float y);
    using SolidGeometryQuery = std::function<bool(const glm::vec3& sample_position)>;
    using CeilingHeightQuery = std::function<float(const glm::vec3& body_position)>;

    // Verified-reference values from OpenIGI HumanWallProbe/HumanGroundQuery.
    static constexpr float SkinWidthInUnits = 10.24f;
    static constexpr float LowProbeHeightInUnits = 409.6f;
    static constexpr float MiddleProbeStandingHeightInUnits = 1638.4f;
    static constexpr float MiddleProbeCrouchingHeightInUnits = 819.2f;
    static constexpr float HighProbeStandingHeightInUnits = 7372.8f;
    static constexpr float HighProbeCrouchingHeightInUnits = 5324.8f;
    static constexpr float WallNormalZLimit = 0.1f;
    static constexpr int MaximumSweepIterations = 10;
    static constexpr float AirborneStepDownInUnits = 0.0f;
    static constexpr float GroundedStepDownInUnits = 2048.0f;
    static constexpr float SlidingStepDownInUnits = 8192.0f;
    static constexpr float RoofClearanceInUnits = 7372.8f;

    // Verified-reference slope bands from OpenIGI HumanSlopeSlide.
    static constexpr float VerySteepSlopeSpeedInUnitsPerTick = 1137.0f;
    static constexpr float SteepSlopeSpeedInUnitsPerTick = 948.0f;
    static constexpr float ModerateSlopeSpeedInUnitsPerTick = 56.888889f;
    static constexpr float ShallowSlopeSpeedInUnitsPerTick = 18.962963f;
    static constexpr float VerySteepSlopeDegrees = 80.0f;
    static constexpr float SteepSlopeDegrees = 60.0f;
    static constexpr float ModerateSlopeDegrees = 50.0f;
    static constexpr float ShallowSlopeDegrees = 45.0f;
    static constexpr float SlopeSlideDamping = 0.89999998f;

    PlayerCollision() = default;

    void SetSolidQuery(SolidGeometryQuery solid_geometry_query);
    void SetCeilingQuery(CeilingHeightQuery ceiling_height_query);

    PlayerGroundQuery QueryGround(
        const glm::vec3& body_position,
        float current_eye_height,
        TerrainHeightQuery terrain_height_query,
        bool was_grounded = true,
        bool crouching = false,
        bool sliding = false) const;

    static glm::vec3 AccumulateSlopeSlide(
        const glm::vec3& current_slide_velocity,
        const glm::vec3& ground_normal,
        bool grounded);

    PlayerWallSweepResult SweepWalls(
        const glm::vec3& current_position,
        const glm::vec3& target_position,
        float body_radius,
        float body_height,
        bool grounded = true,
        bool crouching = false) const;

    bool CanStandUp(
        const glm::vec3& body_position,
        float standing_height,
        TerrainHeightQuery terrain_height_query) const;

    void ResolveObstacles(
        glm::vec3& body_position,
        const std::vector<ObstacleCollider>& obstacles,
        float player_radius = 1638.4f) const;

private:
    struct RaycastHit {
        glm::vec3 position = glm::vec3(0.0f);
        glm::vec3 normal = glm::vec3(0.0f, 0.0f, 1.0f);
        float distance = 0.0f;
    };

    bool RaycastSolidGeometry(
        const glm::vec3& ray_origin,
        const glm::vec3& ray_end,
        RaycastHit& hit) const;

    glm::vec3 EstimateSurfaceNormal(
        const glm::vec3& surface_position,
        const glm::vec3& movement_direction) const;

    SolidGeometryQuery solid_geometry_query_;
    CeilingHeightQuery ceiling_height_query_;
};

} // namespace igi
