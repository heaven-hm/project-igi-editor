// ai_system.h - Runtime AI guard simulation, dual-cone vision, and combat behavior
#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <string>
#include <cstdint>
#include "ai_events.h"

namespace igi {

enum class AiGuardState {
    Idle,
    Patrol,
    Suspicious,
    Combat,
    Dead
};

struct AiVisionConeConfig {
    float patrol_sight_range = 5000.0f; // 50 meters (100 units = 1m)
    float alert_sight_range = 9000.0f;  // 90 meters
    float primary_fov_yaw = glm::radians(45.0f);
    float primary_fov_pitch = glm::radians(30.0f);
    float periph_sight_range = 3000.0f; // 30 meters
    float periph_fov_yaw = glm::radians(85.0f);
    float periph_fov_pitch = glm::radians(45.0f);
};

enum class AiVisionResult {
    None = 0,
    Peripheral = 1,
    Primary = 2
};

struct AiGuardEntity {
    uint32_t id = 0;
    std::string name;
    glm::vec3 position = glm::vec3(0.0f);
    float yaw = 0.0f;
    float pitch = 0.0f;
    float health = 100.0f;
    AiGuardState state = AiGuardState::Patrol;
    float suspicion = 0.0f; // [0.0 - 1.0]
    uint32_t current_waypoint = 0;
    std::vector<glm::vec3> waypoints;
    AiVisionConeConfig vision_config;
};

class AiSystem {
public:
    AiSystem();

    void Clear();
    void RegisterGuard(const AiGuardEntity& guard);
    AiGuardEntity* FindGuard(uint32_t guard_id);

    // Fixed-step simulation tick
    void Update(double delta_seconds, const glm::vec3& player_pos, bool player_alive);

    // Perception
    AiVisionResult CheckVision(const AiGuardEntity& guard, const glm::vec3& target_pos, bool is_alerted) const;
    void ApplyDamage(uint32_t guard_id, float damage);

    AiEventQueue& GetEventQueue() { return event_queue_; }
    const std::vector<AiGuardEntity>& GetGuards() const { return guards_; }

private:
    std::vector<AiGuardEntity> guards_;
    AiEventQueue event_queue_;
};

} // namespace igi
