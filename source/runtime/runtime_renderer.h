#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "runtime_world.h"
#include "projectile_types.h"

namespace igi {

struct RuntimeRenderCamera {
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 forward = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 right = glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 up = glm::vec3(0.0f, 0.0f, 1.0f);
    float field_of_view_y_radians = 0.0f;
    int viewport_width = 1;
    int viewport_height = 1;
};

struct RuntimeProjectileRenderState {
    ProjectileType type = ProjectileType::None;
    glm::vec3 position = glm::vec3(0.0f);
    uint32_t tumble_ticks = 0;
};

// Immutable map-computer row copied from the active vanilla objective set.
// Keeping the row in the presentation snapshot prevents the HUD from
// reading or mutating LevelFlow while a fixed-step update is in progress.
struct RuntimeMapComputerObjective {
    std::string text;
    int32_t link_task_id = -1;
    ObjectiveState state = ObjectiveState::Pending;
    bool has_location = false;
    glm::vec3 location = glm::vec3(0.0f);
};

// Immutable transform and lifecycle state for one guard at a render boundary.
// Animation request metadata is included so the Windows presentation adapter
// can select authored clips without reading the mutable AI container.
struct RuntimeGuardRenderState {
    uint32_t guard_id = 0;
    glm::vec3 position = glm::vec3(0.0f);
    float yaw = 0.0f;
    AiGuardState state = AiGuardState::Patrol;
    bool runtime_enabled = true;
    int requested_animation = -1;
    uint64_t animation_request_serial = 0;
};

// Immutable presentation data for one gameplay render. The simulation world
// can continue to advance on the fixed-step boundary without exposing its
// mutable containers directly to a renderer.
struct RuntimeRenderSnapshot {
    RuntimeRenderCamera camera;
    glm::vec3 player_position = glm::vec3(0.0f);
    glm::vec3 player_eye_position = glm::vec3(0.0f);
    float player_yaw = 0.0f;
    float player_pitch = 0.0f;
    float player_health = 100.0f;
    float player_maximum_health = 100.0f;
    float player_armor = 100.0f;
    float player_maximum_armor = 100.0f;
    bool player_alive = true;
    bool zoom_active = false;
    std::string active_weapon_name = "M16A2";
    std::string active_weapon_model_id;
    float weapon_view_pitch_radians = 0.0f;
    float weapon_view_yaw_radians = 0.0f;
    float weapon_recoil_pitch_radians = 0.0f;
    float weapon_recoil_yaw_radians = 0.0f;
    bool weapon_view_transitioning = false;
    uint32_t clip_ammo = 0;
    uint32_t clip_capacity = 0;
    uint32_t reserve_ammo = 0;
    bool map_computer_open = false;
    std::vector<RuntimeMapComputerObjective> map_computer_objectives;
    std::vector<RuntimeGuardRenderState> guards;
    std::string objective_text;
    int32_t objective_link_task_id = -1;
    bool has_objective_location = false;
    glm::vec3 objective_location = glm::vec3(0.0f);
    int64_t mission_timer_remaining_ticks = -1;
    std::vector<MissionStatusMessageDisplay> mission_status_messages;
    float flash_effect_strength = 0.0f;
    float muzzle_flash_strength = 0.0f;
    float player_damage_effect_strength = 0.0f;
    std::vector<RuntimeGuardMuzzleFlashState> guard_muzzle_flashes;
    std::vector<RuntimeProjectileRenderState> projectiles;
    std::vector<RuntimeExplosionRenderState> explosions;
};

class RuntimeRenderer {
public:
    // Captures simulation state at the presentation boundary. This method is
    // deliberately OpenGL-free so the contract can be tested on any host.
    void Capture(const RuntimeWorld& world, const RuntimeRenderCamera& camera);
    void Clear();

    const RuntimeRenderSnapshot& GetSnapshot() const { return snapshot_; }

private:
    RuntimeRenderSnapshot snapshot_;
};

} // namespace igi
