// weapon_system.cpp - Runtime weapon state machines, recoil, and ballistics simulation implementation
#include "weapon_system.h"
#include <algorithm>

namespace igi {

WeaponSystem::WeaponSystem() {
    SelectWeapon(0);
}

void WeaponSystem::SelectWeapon(uint32_t weapon_id) {
    active_weapon_.id = weapon_id;
    active_weapon_.name = "M16A2";
    active_weapon_.muzzle_velocity = 900.0f;
    active_weapon_.rounds_per_minute = 600.0f;
    active_weapon_.clip_capacity = 30;
    active_weapon_.recoil_pitch = 1.5f;
    active_weapon_.recoil_yaw = 0.5f;
    active_weapon_.base_spread = 0.02f;
    active_weapon_.damage = 35.0f;
    active_weapon_.is_automatic = true;

    current_clip_ammo_ = active_weapon_.clip_capacity;
    reserve_ammo_ = 120;
    shot_cooldown_ = 0.0;
    is_reloading_ = false;
    reload_timer_ = 0.0;
}

bool WeaponSystem::TryFire(const glm::vec3& muzzle_pos, const glm::vec3& aim_dir, BulletTrace& out_trace) {
    if (is_reloading_ || shot_cooldown_ > 0.0 || current_clip_ammo_ == 0) {
        return false;
    }

    current_clip_ammo_--;
    shot_cooldown_ = 60.0 / active_weapon_.rounds_per_minute;

    // Simulate ballistic bullet raycast trace
    out_trace.origin = muzzle_pos;
    out_trace.direction = aim_dir;
    out_trace.damage = active_weapon_.damage;
    out_trace.hit = true;
    out_trace.distance = 500.0f; // Max effective range in meters
    out_trace.hit_position = muzzle_pos + aim_dir * out_trace.distance;

    return true;
}

void WeaponSystem::Reload() {
    if (is_reloading_ || current_clip_ammo_ >= active_weapon_.clip_capacity || reserve_ammo_ == 0) {
        return;
    }
    is_reloading_ = true;
    reload_timer_ = 2.2; // 2.2 second reload animation time
}

void WeaponSystem::Update(double delta_seconds) {
    if (shot_cooldown_ > 0.0) {
        shot_cooldown_ = std::max(0.0, shot_cooldown_ - delta_seconds);
    }

    if (is_reloading_) {
        reload_timer_ -= delta_seconds;
        if (reload_timer_ <= 0.0) {
            uint32_t needed = active_weapon_.clip_capacity - current_clip_ammo_;
            uint32_t transferred = std::min(needed, reserve_ammo_);
            reserve_ammo_ -= transferred;
            current_clip_ammo_ += transferred;
            is_reloading_ = false;
        }
    }
}

} // namespace igi
