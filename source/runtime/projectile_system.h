// projectile_system.h - Deterministic fixed-step thrown-weapon simulation.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include <glm/glm.hpp>

#include "projectile_types.h"

namespace igi {

struct ProjectileCollisionHit {
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 normal = glm::vec3(0.0f, 0.0f, 1.0f);
};

struct ProjectileLaunch {
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 velocity = glm::vec3(0.0f);
    ProjectileType type = ProjectileType::None;
    uint32_t owner_entity_id = 0;
    uint32_t fuse_ticks = 120;
    float damage = 0.0f;
    float damage_factor = 1.0f;
    float explosion_radius_units = 5.0f * 4096.0f;
    float explosion_falloff_units = 0.0f;
    float proximity_trigger_radius_units = 1.5f * 4096.0f;
    bool detonate_on_impact = false;
    bool affected_by_gravity = true;
};

struct LiveProjectile {
    uint64_t id = 0;
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 previous_position = glm::vec3(0.0f);
    glm::vec3 velocity = glm::vec3(0.0f);
    ProjectileType type = ProjectileType::None;
    uint32_t owner_entity_id = 0;
    uint32_t fuse_ticks = 0;
    float damage = 0.0f;
    float damage_factor = 1.0f;
    float explosion_radius_units = 0.0f;
    float explosion_falloff_units = 0.0f;
    float proximity_trigger_radius_units = 0.0f;
    bool resting = false;
    bool armed = false;
    bool detonate_on_impact = false;
    bool affected_by_gravity = true;
    uint32_t tumble_ticks = 0;
};

struct ProjectileDetonation {
    uint64_t projectile_id = 0;
    glm::vec3 position = glm::vec3(0.0f);
    ProjectileType type = ProjectileType::None;
    uint32_t owner_entity_id = 0;
    float damage = 0.0f;
    float damage_factor = 1.0f;
    float explosion_radius_units = 0.0f;
    float explosion_falloff_units = 0.0f;
};

class ProjectileSystem {
public:
    static constexpr int MaximumLiveProjectiles = 32;
    static constexpr float GravityUnitsPerTick = 44.600887f;
    static constexpr float Restitution = 0.3f;
    static constexpr float TangentialVelocityRetention = 0.75f;
    static constexpr float RestSpeedUnitsPerTick = 40.96f;
    static constexpr float CollisionStepOffUnits = 20.48f;
    static constexpr uint32_t ReferenceGrenadeFuseTicks = 120;

    using CollisionQuery = std::function<bool(
        const glm::vec3& start_position,
        const glm::vec3& end_position,
        ProjectileCollisionHit& collision_hit)>;
    using ProximityTriggerQuery = std::function<bool(
        const glm::vec3& center,
        float radius_units)>;

    void SetCollisionQuery(CollisionQuery collision_query);
    void SetProximityTriggerQuery(ProximityTriggerQuery proximity_trigger_query);

    bool Spawn(const ProjectileLaunch& launch);
    void Tick();
    void Clear();

    const std::vector<LiveProjectile>& GetProjectiles() const {
        return projectiles_;
    }
    const std::vector<ProjectileDetonation>& GetDetonations() const {
        return detonations_;
    }

private:
    static glm::vec3 NormalizeOrFallback(
        const glm::vec3& vector,
        const glm::vec3& fallback);
    void AppendDetonation(const LiveProjectile& projectile);

    CollisionQuery collision_query_;
    ProximityTriggerQuery proximity_trigger_query_;
    std::vector<LiveProjectile> projectiles_;
    std::vector<ProjectileDetonation> detonations_;
    uint64_t next_projectile_id_ = 1;
};

} // namespace igi
