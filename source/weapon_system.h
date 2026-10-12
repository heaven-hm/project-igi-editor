// weapon_system.h - Runtime weapon state machines, recoil, and ballistics simulation
#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <cstdint>

namespace igi {

struct WeaponDefinition {
    uint32_t id = 0;
    std::string name = "M16A2";
    float muzzle_velocity = 900.0f; // m/s
    float rounds_per_minute = 600.0f;
    uint32_t clip_capacity = 30;
    float recoil_pitch = 1.5f; // degrees
    float recoil_yaw = 0.5f;   // degrees
    float base_spread = 0.02f; // radians
    float damage = 35.0f;
    bool is_automatic = true;
};

struct BulletTrace {
    glm::vec3 origin = glm::vec3(0.0f);
    glm::vec3 direction = glm::vec3(0.0f);
    glm::vec3 hit_position = glm::vec3(0.0f);
    bool hit = false;
    uint32_t hit_entity_id = 0;
    float distance = 0.0f;
    float damage = 0.0f;
};

class WeaponSystem {
public:
    WeaponSystem();

    void SelectWeapon(uint32_t weapon_id);
    bool TryFire(const glm::vec3& muzzle_pos, const glm::vec3& aim_dir, BulletTrace& out_trace);
    void Reload();
    void Update(double delta_seconds);

    // Ammunition
    uint32_t GetCurrentClipAmmo() const { return current_clip_ammo_; }
    uint32_t GetReserveAmmo() const { return reserve_ammo_; }
    bool IsReloading() const { return is_reloading_; }
    const WeaponDefinition& GetActiveWeapon() const { return active_weapon_; }

    void SetReserveAmmo(uint32_t count) { reserve_ammo_ = count; }

private:
    WeaponDefinition active_weapon_;
    uint32_t current_clip_ammo_ = 30;
    uint32_t reserve_ammo_ = 120;
    double shot_cooldown_ = 0.0;
    double reload_timer_ = 0.0;
    bool is_reloading_ = false;
};

} // namespace igi
