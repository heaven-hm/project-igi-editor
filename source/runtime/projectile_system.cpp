// projectile_system.cpp - Deterministic fixed-step thrown-weapon simulation.
#include "projectile_system.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace igi {

void ProjectileSystem::SetCollisionQuery(CollisionQuery collision_query) {
    collision_query_ = std::move(collision_query);
}

void ProjectileSystem::SetProximityTriggerQuery(
    ProximityTriggerQuery proximity_trigger_query) {
    proximity_trigger_query_ = std::move(proximity_trigger_query);
}

glm::vec3 ProjectileSystem::NormalizeOrFallback(
    const glm::vec3& vector,
    const glm::vec3& fallback) {
    const float length_squared = glm::dot(vector, vector);
    if (length_squared <= 0.00000001f) {
        return fallback;
    }
    return vector / std::sqrt(length_squared);
}

bool ProjectileSystem::Spawn(const ProjectileLaunch& launch) {
    if (launch.type == ProjectileType::None ||
        projectiles_.size() >= MaximumLiveProjectiles) {
        return false;
    }

    LiveProjectile projectile;
    projectile.id = next_projectile_id_++;
    projectile.position = launch.position;
    projectile.previous_position = launch.position;
    projectile.velocity = launch.velocity;
    projectile.type = launch.type;
    projectile.owner_entity_id = launch.owner_entity_id;
    projectile.fuse_ticks = launch.fuse_ticks;
    projectile.damage = std::max(0.0f, launch.damage);
    projectile.damage_factor = std::max(0.0f, launch.damage_factor);
    projectile.explosion_radius_units =
        std::max(0.0f, launch.explosion_radius_units);
    projectile.explosion_falloff_units =
        std::max(0.0f, launch.explosion_falloff_units);
    projectile.proximity_trigger_radius_units =
        std::max(0.0f, launch.proximity_trigger_radius_units);
    projectile.detonate_on_impact = launch.detonate_on_impact;
    projectile.affected_by_gravity = launch.affected_by_gravity;
    projectiles_.push_back(projectile);
    return true;
}

void ProjectileSystem::AppendDetonation(const LiveProjectile& projectile) {
    ProjectileDetonation detonation;
    detonation.projectile_id = projectile.id;
    detonation.position = projectile.position;
    detonation.type = projectile.type;
    detonation.owner_entity_id = projectile.owner_entity_id;
    detonation.damage = projectile.damage;
    detonation.damage_factor = projectile.damage_factor;
    detonation.explosion_radius_units = projectile.explosion_radius_units;
    detonation.explosion_falloff_units = projectile.explosion_falloff_units;
    detonations_.push_back(detonation);
}

void ProjectileSystem::Tick() {
    detonations_.clear();

    for (int projectile_index = static_cast<int>(projectiles_.size()) - 1;
         projectile_index >= 0;
         --projectile_index) {
        LiveProjectile& projectile = projectiles_[projectile_index];

        if (projectile.type == ProjectileType::ProximityMine) {
            if (projectile.armed && proximity_trigger_query_ &&
                proximity_trigger_query_(
                    projectile.position,
                    projectile.proximity_trigger_radius_units)) {
                AppendDetonation(projectile);
                projectiles_.erase(projectiles_.begin() + projectile_index);
                continue;
            }
        } else if (projectile.fuse_ticks > 0 && --projectile.fuse_ticks == 0) {
            AppendDetonation(projectile);
            projectiles_.erase(projectiles_.begin() + projectile_index);
            continue;
        }

        if (projectile.resting) {
            projectile.previous_position = projectile.position;
            continue;
        }

        projectile.previous_position = projectile.position;
        ++projectile.tumble_ticks;

        glm::vec3 velocity = projectile.velocity;
        if (projectile.affected_by_gravity) {
            velocity.z -= GravityUnitsPerTick;
        }
        const glm::vec3 candidate_position = projectile.position + velocity;

        ProjectileCollisionHit collision_hit;
        if (collision_query_ && collision_query_(
                projectile.position,
                candidate_position,
                collision_hit)) {
            const glm::vec3 collision_normal = NormalizeOrFallback(
                collision_hit.normal,
                glm::vec3(0.0f, 0.0f, 1.0f));
            const float normal_velocity = glm::dot(velocity, collision_normal);
            const glm::vec3 reflected_velocity = (velocity -
                (1.0f + Restitution) * normal_velocity * collision_normal) *
                TangentialVelocityRetention;

            projectile.position = collision_hit.position +
                collision_normal * CollisionStepOffUnits;

            if (projectile.detonate_on_impact) {
                AppendDetonation(projectile);
                projectiles_.erase(projectiles_.begin() + projectile_index);
                continue;
            }

            const float reflected_speed = glm::length(reflected_velocity);
            if (reflected_speed < RestSpeedUnitsPerTick &&
                collision_normal.z > 0.7f) {
                projectile.resting = true;
                projectile.armed = projectile.type == ProjectileType::ProximityMine;
                projectile.velocity = glm::vec3(0.0f);
            } else {
                projectile.velocity = reflected_velocity;
            }
            continue;
        }

        projectile.position = candidate_position;
        projectile.velocity = velocity;
    }
}

void ProjectileSystem::Clear() {
    projectiles_.clear();
    detonations_.clear();
    next_projectile_id_ = 1;
}

} // namespace igi
