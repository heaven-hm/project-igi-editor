// ai_system.cpp - Runtime AI guard simulation, dual-cone vision, and combat behavior implementation
#include "ai_system.h"
#include <algorithm>
#include <cmath>

namespace igi {

AiSystem::AiSystem() = default;

void AiSystem::Clear() {
    guards_.clear();
    event_queue_.Clear();
}

void AiSystem::RegisterGuard(const AiGuardEntity& guard) {
    guards_.push_back(guard);
}

AiGuardEntity* AiSystem::FindGuard(uint32_t guard_id) {
    for (auto& guard : guards_) {
        if (guard.id == guard_id) {
            return &guard;
        }
    }
    return nullptr;
}

void AiSystem::ApplyDamage(uint32_t guard_id, float damage) {
    auto* guard = FindGuard(guard_id);
    if (!guard || guard->state == AiGuardState::Dead) return;

    guard->health -= damage;
    if (guard->health <= 0.0f) {
        guard->state = AiGuardState::Dead;
    } else {
        guard->state = AiGuardState::Combat;
        guard->suspicion = 1.0f;
    }
}

AiVisionResult AiSystem::CheckVision(const AiGuardEntity& guard, const glm::vec3& target_pos, bool is_alerted) const {
    if (guard.state == AiGuardState::Dead) return AiVisionResult::None;

    glm::vec3 eye_pos(guard.position.x, guard.position.y, guard.position.z + 180.0f);
    glm::vec3 delta = target_pos - eye_pos;

    // Facing direction
    float yaw_rad = glm::radians(guard.yaw);
    glm::vec3 facing(std::sin(yaw_rad), std::cos(yaw_rad), 0.0f);
    glm::vec3 right(std::cos(yaw_rad), -std::sin(yaw_rad), 0.0f);
    glm::vec3 up(0.0f, 0.0f, 1.0f);

    float local_forward = glm::dot(delta, facing);
    float local_right   = glm::dot(delta, right);
    float local_up      = glm::dot(delta, up);

    if (local_forward <= 0.0f) return AiVisionResult::None; // Behind guard

    float dist = glm::length(delta);
    float yaw_angle = std::abs(std::atan2(local_right, local_forward));
    float pitch_angle = std::abs(std::atan2(local_up, local_forward));

    const auto& cfg = guard.vision_config;

    // Primary cone test
    float max_primary = is_alerted ? cfg.alert_sight_range : cfg.patrol_sight_range;
    if (dist <= max_primary && yaw_angle < cfg.primary_fov_yaw && pitch_angle < cfg.primary_fov_pitch) {
        return AiVisionResult::Primary;
    }

    // Peripheral cone test
    float max_periph = is_alerted ? cfg.alert_sight_range : cfg.periph_sight_range;
    if (dist <= max_periph && yaw_angle < cfg.periph_fov_yaw && pitch_angle < cfg.periph_fov_pitch) {
        return AiVisionResult::Peripheral;
    }

    return AiVisionResult::None;
}

void AiSystem::Update(double delta_seconds, const glm::vec3& player_pos, bool player_alive) {
    // Process acoustic stimuli from event queue
    std::vector<AiStimulusEvent> events;
    event_queue_.Pump(events);

    for (auto& guard : guards_) {
        if (guard.state == AiGuardState::Dead) continue;

        // 1. Process hearing
        for (const auto& evt : events) {
            float dist = glm::distance(guard.position, evt.position);
            float heard_loudness = evt.loudness / (1.0f + 0.05f * dist);
            if (heard_loudness > 0.35f) {
                guard.state = AiGuardState::Suspicious;
                guard.suspicion = 1.0f;
            }
        }

        // 2. Process vision
        if (player_alive) {
            AiVisionResult vis = CheckVision(guard, player_pos, guard.state == AiGuardState::Combat);
            if (vis == AiVisionResult::Primary) {
                guard.state = AiGuardState::Combat;
                guard.suspicion = 1.0f;
            } else if (vis == AiVisionResult::Peripheral) {
                guard.suspicion = std::min(1.0f, guard.suspicion + 0.2f);
                if (guard.suspicion >= 0.8f) {
                    guard.state = AiGuardState::Suspicious;
                }
            }
        }

        // 3. Patrol & suspicion decay
        if (guard.state == AiGuardState::Suspicious) {
            guard.suspicion = std::max(0.0f, guard.suspicion - static_cast<float>(0.15 * delta_seconds));
            if (guard.suspicion == 0.0f) {
                guard.state = AiGuardState::Patrol;
            }
        } else if (guard.state == AiGuardState::Patrol && !guard.waypoints.empty()) {
            // Advance along waypoints
            glm::vec3 target_wp = guard.waypoints[guard.current_waypoint];
            glm::vec3 to_wp = target_wp - guard.position;
            float wp_dist = glm::length(glm::vec2(to_wp.x, to_wp.y));

            if (wp_dist < 50.0f) {
                guard.current_waypoint = (guard.current_waypoint + 1) % guard.waypoints.size();
            } else {
                glm::vec2 dir = glm::normalize(glm::vec2(to_wp.x, to_wp.y));
                guard.position.x += dir.x * 120.0f * static_cast<float>(delta_seconds); // 120 units/s patrol speed
                guard.position.y += dir.y * 120.0f * static_cast<float>(delta_seconds);
                guard.yaw = glm::degrees(std::atan2(dir.x, dir.y));
            }
        }
    }
}

} // namespace igi
