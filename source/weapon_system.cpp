// weapon_system.cpp - Vanilla weapon roster and deterministic fixed-tick firing state machine.
#include "weapon_system.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iterator>
#include <utility>

namespace igi {

namespace {

constexpr float kWorldUnitsPerMeter = 4096.0f;
constexpr float kDegreesToRadians = 0.017453292f;
constexpr float kTwoPi = 6.2831855f;
constexpr int kRoundsAtMinimumSpread = 3;

WeaponDefinition CreateVanillaWeapon(
    uint32_t weapon_id,
    const char* script_id,
    const char* display_name,
    const char* model_id,
    float muzzle_velocity,
    float rounds_per_minute,
    uint32_t clip_capacity,
    uint32_t maximum_rounds_per_burst,
    uint32_t bullets_per_round,
    float minimum_spread_degrees,
    float maximum_spread_degrees,
    float fixed_recoil_pitch,
    float fixed_recoil_yaw,
    float random_recoil_pitch,
    float random_recoil_yaw,
    float effective_range_meters,
    float gameplay_damage,
    int32_t calibre_id,
    const char* fire_sound,
    const char* fire_loop_end_sound,
    bool uses_ammunition = true,
    ProjectileType projectile_type = ProjectileType::None) {
    WeaponDefinition definition;
    definition.id = weapon_id;
    definition.script_id = script_id;
    definition.name = display_name;
    definition.model_id = model_id;
    definition.muzzle_velocity = muzzle_velocity;
    definition.rounds_per_minute = rounds_per_minute;
    definition.clip_capacity = clip_capacity;
    definition.maximum_rounds_per_burst = maximum_rounds_per_burst;
    definition.bullets_per_round = bullets_per_round;
    definition.minimum_spread_degrees = minimum_spread_degrees;
    definition.maximum_spread_degrees = maximum_spread_degrees;
    definition.base_spread = std::abs(minimum_spread_degrees) * kDegreesToRadians;
    definition.recoil_pitch = fixed_recoil_pitch;
    definition.recoil_yaw = fixed_recoil_yaw;
    definition.random_recoil_pitch = random_recoil_pitch;
    definition.random_recoil_yaw = random_recoil_yaw;
    definition.effective_range_meters = effective_range_meters;
    definition.damage = gameplay_damage;
    definition.calibre_id = calibre_id;
    definition.fire_sound = fire_sound;
    definition.fire_loop_end_sound = fire_loop_end_sound;
    definition.projectile_type = projectile_type;
    definition.uses_ammunition = uses_ammunition;
    definition.is_automatic = maximum_rounds_per_burst > 1;
    return definition;
}

uint32_t ReserveForWeapon(const WeaponDefinition& weapon) {
    switch (weapon.id) {
        case 1: return 68;   // Glock 17, calibre 919.
        case 3: return 30;   // Desert Eagle.
        case 4: return 120;  // M16A2, calibre 556.
        case 5: return 120;  // AK47, calibre 762.
        case 6: return 150;  // Uzi.
        case 7: return 150;  // MP5SD.
        case 8: return 42;   // SPAS12 shells.
        case 9: return 48;   // Jackhammer shells.
        case 10: return 120; // Minimi.
        case 11: return 30;  // Dragunov.
        case 12: return 4;   // LAW 80.
        case 13: return 150; // Uzi x2.
        case 14: return 4;   // Hand grenades.
        case 15: return 4;   // Flashbangs.
        case 16: return 4;   // Proximity mines.
        case 18: return 0;   // Binoculars.
        case 19: return 4;   // Medipacks.
        case 20: return 0;   // Knife.
        case 21: return 24;  // Colt Anaconda.
        default: return 0;
    }
}

int FixedCadenceTicks(float rounds_per_minute) {
    if (rounds_per_minute <= 0.0f) {
        return 1;
    }
    return std::max(
        1,
        static_cast<int>(60.0 / static_cast<double>(rounds_per_minute) * 30.0));
}

glm::vec3 NormalizeOrFallback(const glm::vec3& vector, const glm::vec3& fallback) {
    const float length_squared = glm::dot(vector, vector);
    if (length_squared <= 0.00000001f) {
        return fallback;
    }
    return vector / std::sqrt(length_squared);
}

std::string NormalizeScriptIdentifier(std::string script_identifier) {
    const auto is_whitespace = [](unsigned char character) {
        return std::isspace(character) != 0;
    };

    const auto first_non_whitespace = std::find_if_not(
        script_identifier.begin(),
        script_identifier.end(),
        is_whitespace);
    const auto last_non_whitespace = std::find_if_not(
        script_identifier.rbegin(),
        script_identifier.rend(),
        is_whitespace).base();
    if (first_non_whitespace >= last_non_whitespace) {
        return {};
    }

    script_identifier = std::string(first_non_whitespace, last_non_whitespace);
    if (script_identifier.size() >= 2 &&
        script_identifier.front() == '"' &&
        script_identifier.back() == '"') {
        script_identifier = script_identifier.substr(
            1,
            script_identifier.size() - 2);
    }
    return script_identifier;
}

} // namespace

const std::vector<WeaponDefinition>& WeaponSystem::GetVanillaWeaponCatalog() {
    static const std::vector<WeaponDefinition> catalog = {
        // Values below are the fixed DefineWeaponType fields from the vanilla
        // WEAPONS/*/WEAPON.QVM roster. Damage remains the runtime bridge value;
        // the separate ammo QVM damage table is the next ballistics seam.
        CreateVanillaWeapon(1, "WEAPON_ID_GLOCK", "Glock 17", "117_01_1",
            400.0f, 400.0f, 17, 1, 1, -1.5f, 1.5f, 1.5f, 0.0f, 0.0f, 0.0f,
            80.0f, 25.0f, 919, "glock_shot_1", "glock_shot_2"),
        CreateVanillaWeapon(3, "WEAPON_ID_DESERTEAGLE", "Desert Eagle", "106_01_1",
            400.0f, 200.0f, 10, 1, 1, -1.5f, 1.5f, 4.0f, 0.0f, 0.0f, 0.0f,
            40.0f, 50.0f, 919, "deagle_shot_1", "deagle_shot_1"),
        CreateVanillaWeapon(4, "WEAPON_ID_M16A2", "M16 A2", "108_01_1",
            950.0f, 550.0f, 20, 20, 1, -2.0f, 2.0f, 0.8f, 0.0f, 1.2f, 0.4f,
            200.0f, 35.0f, 556, "m16_loop", "m16_loop_e"),
        CreateVanillaWeapon(5, "WEAPON_ID_AK47", "AK47", "100_01_1",
            710.0f, 650.0f, 30, 30, 1, -3.5f, 3.5f, 0.8f, 0.0f, 0.6f, 0.6f,
            2000.0f, 42.0f, 762, "ak47_loop", "ak47_loop_e"),
        CreateVanillaWeapon(6, "WEAPON_ID_UZI", "Uzi", "111_01_1",
            400.0f, 700.0f, 32, 32, 1, -5.0f, 5.0f, 0.9f, 0.0f, 0.7f, 0.7f,
            50.0f, 28.0f, 919, "uzi_loop", "uzi_loop_e"),
        CreateVanillaWeapon(7, "WEAPON_ID_MP5SD", "Mp5 SD3", "103_01_1",
            400.0f, 700.0f, 32, 32, 1, -2.0f, 2.0f, 0.8f, 0.0f, 0.8f, 0.0f,
            80.0f, 28.0f, 919, "mp5sd_loop", "mp5sd_loop_e"),
        CreateVanillaWeapon(8, "WEAPON_ID_SPAS12", "Spas 12", "109_01_1",
            600.0f, 40.0f, 7, 1, 30, 20.0f, 20.0f, 8.0f, 3.0f, 0.0f, 0.0f,
            30.0f, 45.0f, 12, "spas12_shot_1", "spas12_shot_1"),
        CreateVanillaWeapon(9, "WEAPON_ID_JACKHAMMER", "Jackhammer", "125_01_1",
            280.0f, 180.0f, 12, 12, 30, -14.0f, 14.0f, 5.0f, 0.0f, 0.0f, 0.0f,
            50.0f, 45.0f, 12, "jackh_loop", "jackh_loop_e"),
        CreateVanillaWeapon(10, "WEAPON_ID_MINIMI", "Minimi", "107_01_1",
            1200.0f, 800.0f, 60, 60, 1, -4.5f, 4.5f, 0.2f, 0.0f, -1.0f, 0.0f,
            300.0f, 35.0f, 556, "minimi_loop", "minimi_loop_e"),
        CreateVanillaWeapon(11, "WEAPON_ID_DRAGUNOV", "Dragunov", "105_01_1",
            6000.0f, 50.0f, 10, 1, 1, -0.05f, 0.05f, 3.5f, 0.0f, 0.0f, 0.0f,
            300.0f, 95.0f, 10004, "svddrag_shot_1", "svddrag_shot_1"),
        CreateVanillaWeapon(12, "WEAPON_ID_RPG18", "LAW 80", "110_01_1",
            5.0f, 40.0f, 1, 1, 1, 0.0f, 0.0f, 12.0f, 0.0f, 0.0f, 0.0f,
            600.0f, 100.0f, 1000, "rpg_launch_1", "rpg_launch_1", true,
            ProjectileType::Rocket),
        CreateVanillaWeapon(13, "WEAPON_ID_UZIX2", "Uzi x 2", "111_04_1",
            400.0f, 1400.0f, 64, 64, 1, -5.0f, 5.0f, 0.9f, 0.0f, 0.7f, 0.7f,
            50.0f, 28.0f, 919, "uzix2_loop", "uzi_loop_e"),
        CreateVanillaWeapon(14, "WEAPON_ID_GRENADE", "Hand Grenade", "135_01_1",
            15.0f, 50.0f, 1, 1, 1, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            1.0f, 100.0f, 10000, "grenade_shot_1", "grenade_shot_1", true,
            ProjectileType::FragGrenade),
        CreateVanillaWeapon(15, "WEAPON_ID_FLASHBANG", "FlashBang", "137_01_1",
            15.0f, 50.0f, 1, 1, 1, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            1.0f, 1.0f, 10003, "grenade_shot_1", "grenade_shot_1", true,
            ProjectileType::Flashbang),
        CreateVanillaWeapon(16, "WEAPON_ID_PROXIMITYMINE", "Proximitymine", "136_01_1",
            15.0f, 60.0f, 1, 1, 1, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            1.5f, 100.0f, 10001, "grenade_shot_1", "grenade_shot_1", true,
            ProjectileType::ProximityMine),
        CreateVanillaWeapon(18, "WEAPON_ID_BINOCULARS", "Binoculars", "102_01_1",
            8.0f, 50.0f, 1, 1, 1, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            1.0f, 0.0f, 919, "", "", false),
        CreateVanillaWeapon(19, "WEAPON_ID_MEDIPACK", "Medipack", "121_01_1",
            0.0f, 120.0f, 1, 60, 1, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            1.0f, 0.0f, 10005, "glock_shot_1", "glock_shot_2"),
        CreateVanillaWeapon(20, "WEAPON_ID_KNIFE", "Knife", "133_01_1",
            10.0f, 140.0f, 1, 1, 1, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            1.0f, 100.0f, -1, "knife_1", "knife_2", false),
        CreateVanillaWeapon(21, "WEAPON_ID_COLT", "Colt Anaconda", "101_01_1",
            360.0f, 60.0f, 6, 1, 1, -2.0f, 2.0f, 6.0f, 0.0f, 0.0f, 0.0f,
            80.0f, 50.0f, 44, "colt_shot_1", "colt_shot_1"),
    };
    return catalog;
}

const std::vector<uint32_t>& WeaponSystem::GetVanillaPlayerWeaponCycle() {
    static const std::vector<uint32_t> cycle = {
        20, 1, 21, 3, 6, 13, 7, 4, 5, 8, 9, 11, 10, 14, 15, 16, 12, 19,
    };
    return cycle;
}

WeaponSystem::WeaponSystem() {
    player_weapon_cycle_ = GetVanillaPlayerWeaponCycle();
    SelectWeaponSlot(0);
}

bool WeaponSystem::SelectWeapon(uint32_t weapon_id) {
    const auto& catalog = GetVanillaWeaponCatalog();
    const auto weapon = std::find_if(
        catalog.begin(),
        catalog.end(),
        [weapon_id](const WeaponDefinition& definition) {
            return definition.id == weapon_id;
        });
    if (weapon == catalog.end()) {
        return false;
    }

    SaveActiveAmmoState();
    active_weapon_ = *weapon;
    RestoreOrInitializeAmmoState();
    ResetTransientState();
    return true;
}

bool WeaponSystem::SelectWeaponByScriptId(const std::string& script_id) {
    const std::string normalized_script_id = NormalizeScriptIdentifier(script_id);
    if (normalized_script_id.empty()) {
        return false;
    }

    const auto& catalog = GetVanillaWeaponCatalog();
    const auto weapon = std::find_if(
        catalog.begin(),
        catalog.end(),
        [&normalized_script_id](const WeaponDefinition& definition) {
            return definition.script_id == normalized_script_id;
        });
    return weapon != catalog.end() && SelectWeapon(weapon->id);
}

bool WeaponSystem::SelectWeaponSlot(uint32_t player_cycle_slot) {
    const auto& cycle = player_weapon_cycle_;
    if (player_cycle_slot >= cycle.size()) {
        return false;
    }
    return SelectWeapon(cycle[player_cycle_slot]);
}

bool WeaponSystem::SelectNextWeapon() {
    const auto& cycle = player_weapon_cycle_;
    const auto current = std::find(cycle.begin(), cycle.end(), active_weapon_.id);
    const size_t current_slot = current == cycle.end()
        ? 0
        : static_cast<size_t>(std::distance(cycle.begin(), current));
    return SelectWeaponSlot(static_cast<uint32_t>((current_slot + 1) % cycle.size()));
}

bool WeaponSystem::SelectPreviousWeapon() {
    const auto& cycle = player_weapon_cycle_;
    const auto current = std::find(cycle.begin(), cycle.end(), active_weapon_.id);
    const size_t current_slot = current == cycle.end()
        ? 0
        : static_cast<size_t>(std::distance(cycle.begin(), current));
    const size_t previous_slot = current_slot == 0 ? cycle.size() - 1 : current_slot - 1;
    return SelectWeaponSlot(static_cast<uint32_t>(previous_slot));
}

void WeaponSystem::SaveActiveAmmoState() {
    if (active_weapon_.id == 0) {
        return;
    }
    ammo_state_by_weapon_id_[active_weapon_.id] = {
        current_clip_ammo_,
        reserve_ammo_,
    };
}

void WeaponSystem::RestoreOrInitializeAmmoState() {
    const auto [state, inserted] = ammo_state_by_weapon_id_.try_emplace(
        active_weapon_.id,
        AmmoState{
            active_weapon_.clip_capacity,
            active_weapon_.uses_ammunition ? ReserveForWeapon(active_weapon_) : 0,
        });
    if (inserted) {
        current_clip_ammo_ = state->second.clip_ammo;
        reserve_ammo_ = state->second.reserve_ammo;
        return;
    }

    current_clip_ammo_ = std::min(
        state->second.clip_ammo,
        active_weapon_.clip_capacity);
    reserve_ammo_ = state->second.reserve_ammo;
}

void WeaponSystem::ResetTransientState() {
    rounds_this_burst_ = 0;
    shot_cooldown_ticks_ = 0;
    is_reloading_ = false;
    reload_timer_ = 0.0;
    last_recoil_pitch_degrees_ = 0.0f;
    last_recoil_yaw_degrees_ = 0.0f;
}

void WeaponSystem::SetReserveAmmo(uint32_t count) {
    reserve_ammo_ = count;
    SaveActiveAmmoState();
}

void WeaponSystem::AddReserveAmmo(uint32_t count) {
    reserve_ammo_ += count;
    SaveActiveAmmoState();
}

void WeaponSystem::SetPlayerWeaponCycle(const std::vector<uint32_t>& weapon_cycle) {
    std::vector<uint32_t> validated_cycle;
    const auto& catalog = GetVanillaWeaponCatalog();
    for (const uint32_t weapon_id : weapon_cycle) {
        const bool is_known_weapon = std::any_of(
            catalog.begin(),
            catalog.end(),
            [weapon_id](const WeaponDefinition& definition) {
                return definition.id == weapon_id;
            });
        const bool is_duplicate = std::find(
            validated_cycle.begin(),
            validated_cycle.end(),
            weapon_id) != validated_cycle.end();
        if (is_known_weapon && !is_duplicate) {
            validated_cycle.push_back(weapon_id);
        }
    }

    if (!validated_cycle.empty()) {
        player_weapon_cycle_ = std::move(validated_cycle);
    }
}

float WeaponSystem::NextRandomUnit() {
    random_state_ = random_state_ * 1664525U + 1013904223U;
    return static_cast<float>(random_state_ & 0x00FFFFFFU) /
        static_cast<float>(0x01000000U);
}

bool WeaponSystem::TryFire(
    const glm::vec3& muzzle_pos,
    const glm::vec3& aim_dir,
    BulletTrace& out_trace) {
    std::vector<BulletTrace> traces;
    if (!TryFire(muzzle_pos, aim_dir, traces)) {
        out_trace = BulletTrace();
        return false;
    }
    out_trace = traces.front();
    return true;
}

bool WeaponSystem::TryFire(
    const glm::vec3& muzzle_pos,
    const glm::vec3& aim_dir,
    std::vector<BulletTrace>& out_traces) {
    out_traces.clear();
    last_recoil_pitch_degrees_ = 0.0f;
    last_recoil_yaw_degrees_ = 0.0f;

    if (is_reloading_ || shot_cooldown_ticks_ > 0 ||
        rounds_this_burst_ >= active_weapon_.maximum_rounds_per_burst ||
        (active_weapon_.uses_ammunition && current_clip_ammo_ == 0)) {
        return false;
    }

    const float aim_length_squared = glm::dot(aim_dir, aim_dir);
    if (aim_length_squared <= 0.0001f) {
        return false;
    }

    const glm::vec3 forward = aim_dir / std::sqrt(aim_length_squared);
    const glm::vec3 world_up(0.0f, 0.0f, 1.0f);
    const glm::vec3 right = NormalizeOrFallback(
        glm::cross(forward, world_up),
        glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::vec3 up = NormalizeOrFallback(glm::cross(right, forward), world_up);

    const float spread_degrees = rounds_this_burst_ > kRoundsAtMinimumSpread
        ? active_weapon_.maximum_spread_degrees
        : active_weapon_.minimum_spread_degrees;
    const float spread_radians = std::abs(spread_degrees) * kDegreesToRadians;
    out_traces.reserve(active_weapon_.bullets_per_round);
    for (uint32_t pellet_index = 0;
         pellet_index < std::max(1U, active_weapon_.bullets_per_round);
         ++pellet_index) {
        glm::vec3 shot_direction = forward;
        if (spread_radians > 0.0f) {
            const float radius = NextRandomUnit() * NextRandomUnit() *
                NextRandomUnit() * spread_radians;
            const float bearing = NextRandomUnit() * kTwoPi;
            const float sine = std::sin(radius);
            shot_direction = NormalizeOrFallback(
                forward * std::cos(radius) +
                    right * (std::cos(bearing) * sine) +
                    up * (std::sin(bearing) * sine),
                forward);
        }

        BulletTrace trace;
        trace.origin = muzzle_pos;
        trace.direction = shot_direction;
        trace.damage = active_weapon_.damage;
        trace.hit = true;
        trace.distance = active_weapon_.effective_range_meters * kWorldUnitsPerMeter;
        trace.hit_position = muzzle_pos + shot_direction * trace.distance;
        out_traces.push_back(trace);
    }

    if (active_weapon_.uses_ammunition && current_clip_ammo_ > 0) {
        --current_clip_ammo_;
    }
    ++rounds_this_burst_;
    shot_cooldown_ticks_ = static_cast<uint32_t>(
        FixedCadenceTicks(active_weapon_.rounds_per_minute));

    if (active_weapon_.random_recoil_pitch != 0.0f ||
        active_weapon_.random_recoil_yaw != 0.0f) {
        float recoil_x = 0.0f;
        float recoil_y = 0.0f;
        do {
            recoil_x = (NextRandomUnit() - 0.5f) * 2.0f;
            recoil_y = (NextRandomUnit() - 0.5f) * 2.0f;
        } while (recoil_x * recoil_x + recoil_y * recoil_y > 1.0f);
        last_recoil_pitch_degrees_ = active_weapon_.recoil_pitch +
            active_weapon_.random_recoil_pitch * recoil_x;
        last_recoil_yaw_degrees_ = active_weapon_.recoil_yaw +
            active_weapon_.random_recoil_yaw * recoil_y;
    } else {
        last_recoil_pitch_degrees_ = active_weapon_.recoil_pitch;
        last_recoil_yaw_degrees_ = active_weapon_.recoil_yaw;
    }

    return true;
}

void WeaponSystem::Reload() {
    if (!active_weapon_.uses_ammunition || is_reloading_ ||
        current_clip_ammo_ >= active_weapon_.clip_capacity || reserve_ammo_ == 0) {
        return;
    }
    is_reloading_ = true;
    reload_timer_ = active_weapon_.reload_time_seconds;
}

void WeaponSystem::Update(double delta_seconds, bool trigger_held) {
    if (shot_cooldown_ticks_ > 0) {
        --shot_cooldown_ticks_;
    }
    if (!trigger_held) {
        rounds_this_burst_ = 0;
    }

    if (!is_reloading_) {
        return;
    }

    reload_timer_ -= std::max(0.0, delta_seconds);
    if (reload_timer_ > 0.0) {
        return;
    }

    const uint32_t rounds_needed = active_weapon_.clip_capacity - current_clip_ammo_;
    const uint32_t transferred_rounds = std::min(rounds_needed, reserve_ammo_);
    current_clip_ammo_ += transferred_rounds;
    reserve_ammo_ -= transferred_rounds;
    is_reloading_ = false;
}

} // namespace igi
