// weapon_system.h - Runtime weapon state machines, recoil, and ballistics simulation
#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "runtime/projectile_types.h"

namespace igi {

struct WeaponDefinition {
    uint32_t id = 0;
    std::string script_id;
    std::string name = "M16A2";
    std::string model_id;
    float muzzle_velocity = 900.0f; // m/s
    float rounds_per_minute = 600.0f;
    uint32_t clip_capacity = 30;
    uint32_t maximum_rounds_per_burst = 30;
    uint32_t bullets_per_round = 1;
    float recoil_pitch = 1.5f; // degrees
    float recoil_yaw = 0.5f;   // degrees
    float random_recoil_pitch = 0.0f; // degrees
    float random_recoil_yaw = 0.0f;   // degrees
    float base_spread = 0.02f; // radians, derived from the retail minimum spread
    float minimum_spread_degrees = 0.02f;
    float maximum_spread_degrees = 0.02f;
    // The editor's own playtest damage.  It is NOT the retail value; see
    // `engine_damage` for that.
    float damage = 35.0f;
    // The RETAIL per-weapon damage: `DefineWeaponType` HOOK 0x413F90 argument
    // 10 -> `WeaponType` + 0x654 (`unk654[0]`), src/weapon/weapontype.c:317-318
    // and src/weapon/weapontypes.h:44-51.  It is read as damage by
    // `KnifeStab_Fire` (src/comprun/knifestab.c:108) and copied into
    // `Bullet.unk38` by `GunShot_AddBullet` HOOK 0x47B190
    // (src/ammotype/gunshot.c:204); `Bullet_Update` forwards that field as
    // `BulletHitMsg.unk38` (src/ammotype/bullets.c:139-142, :165-166), which the
    // human hit handlers read as `HumanHit.damage`
    // (src/loadscreen/loadscreentypes.h:13-18) and apply at
    // src/weapon/humanplayer.c:704 and src/soldier/soldier.c:947-949.
    //
    // There is no damage field in the ammunition table: `DefineAmmoType`
    // HOOK 0x47CAD0 (src/ammotype/ammotype.c:88-115) declares none.  The
    // per-weapon value below is checked against the shipping
    // WEAPONS/*/WEAPON.QVM files by tests/test_weapon_config.cpp.
    float engine_damage = 0.0f;
    float effective_range_meters = 500.0f;
    float reload_time_seconds = 2.2f;
    int32_t calibre_id = 919;
    std::string fire_sound;
    std::string fire_loop_end_sound;
    ProjectileType projectile_type = ProjectileType::None;
    bool uses_ammunition = true;
    bool is_automatic = true;
};

struct BulletTrace {
    glm::vec3 origin = glm::vec3(0.0f);
    glm::vec3 direction = glm::vec3(0.0f);
    glm::vec3 hit_position = glm::vec3(0.0f);
    bool hit = false;
    bool hit_world_geometry = false;
    uint32_t hit_entity_id = 0;
    float distance = 0.0f;
    float damage = 0.0f;
};

class WeaponSystem {
public:
    WeaponSystem();

    bool SelectWeapon(uint32_t weapon_id);
    bool SelectWeaponByScriptId(const std::string& script_id);
    bool SelectWeaponSlot(uint32_t player_cycle_slot);
    bool SelectNextWeapon();
    bool SelectPreviousWeapon();
    bool TryFire(const glm::vec3& muzzle_pos, const glm::vec3& aim_dir, BulletTrace& out_trace);
    bool TryFire(
        const glm::vec3& muzzle_pos,
        const glm::vec3& aim_dir,
        std::vector<BulletTrace>& out_traces);
    void Reload();
    void Update(double delta_seconds, bool trigger_held = false);

    // Ammunition
    uint32_t GetCurrentClipAmmo() const { return current_clip_ammo_; }
    uint32_t GetReserveAmmo() const { return reserve_ammo_; }
    bool IsReloading() const { return is_reloading_; }
    const WeaponDefinition& GetActiveWeapon() const { return active_weapon_; }
    float GetLastRecoilPitchDegrees() const { return last_recoil_pitch_degrees_; }
    float GetLastRecoilYawDegrees() const { return last_recoil_yaw_degrees_; }

    void SetReserveAmmo(uint32_t count);
    void AddReserveAmmo(uint32_t count);
    void SetPlayerWeaponCycle(const std::vector<uint32_t>& weapon_cycle);

    // The retail roster and the retail slot order, exposed so the weapon
    // configurator (issue #49) can enumerate what the engine defines without
    // instantiating a playtest session.  Both are the shipping values from
    // WEAPONS/*/WEAPON.QVM and humanplayer/humanplayer.qvm; every field is
    // checked against those files by tests/test_weapon_config.cpp.
    static const std::vector<WeaponDefinition>& GetVanillaWeaponCatalog();
    static const std::vector<uint32_t>& GetVanillaPlayerWeaponCycle();

private:
    struct AmmoState {
        uint32_t clip_ammo = 0;
        uint32_t reserve_ammo = 0;
    };

    float NextRandomUnit();
    void SaveActiveAmmoState();
    void RestoreOrInitializeAmmoState();
    void ResetTransientState();

    WeaponDefinition active_weapon_;
    uint32_t current_clip_ammo_ = 0;
    uint32_t reserve_ammo_ = 120;
    uint32_t rounds_this_burst_ = 0;
    uint32_t shot_cooldown_ticks_ = 0;
    double reload_timer_ = 0.0;
    bool is_reloading_ = false;
    uint32_t random_state_ = 0x1F123BB5U;
    float last_recoil_pitch_degrees_ = 0.0f;
    float last_recoil_yaw_degrees_ = 0.0f;
    std::unordered_map<uint32_t, AmmoState> ammo_state_by_weapon_id_;
    std::vector<uint32_t> player_weapon_cycle_;
};

} // namespace igi
