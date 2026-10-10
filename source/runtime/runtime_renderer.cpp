#include "runtime_renderer.h"

#include "runtime_world.h"
#include <utility>

#include <algorithm>
#include <cmath>

namespace igi {

void RuntimeRenderer::Capture(
    const RuntimeWorld& world,
    const RuntimeRenderCamera& camera) {
    RuntimeRenderSnapshot next_snapshot;
    next_snapshot.camera = camera;

    const PlayerController& player = world.GetPlayer();
    next_snapshot.player_position = player.GetPosition();
    next_snapshot.player_eye_position = player.GetEyePosition();
    next_snapshot.player_yaw = player.GetYaw();
    next_snapshot.player_pitch = player.GetPitch();
    next_snapshot.player_health = player.GetHealth();
    next_snapshot.player_maximum_health = player.GetMaximumHealth();
    next_snapshot.player_armor = player.GetArmor();
    next_snapshot.player_maximum_armor = player.GetMaximumArmor();
    next_snapshot.player_alive = player.IsAlive();

    const WeaponSystem& weapons = world.GetWeapons();
    const WeaponDefinition& active_weapon = weapons.GetActiveWeapon();
    next_snapshot.active_weapon_name = active_weapon.name;
    next_snapshot.active_weapon_model_id = active_weapon.model_id;
    next_snapshot.weapon_view_pitch_radians =
        world.GetWeaponViewSway().GetPitchRadians();
    next_snapshot.weapon_view_yaw_radians =
        world.GetWeaponViewSway().GetYawRadians();
    next_snapshot.weapon_recoil_pitch_radians =
        world.GetWeaponViewRecoil().GetPitchRadians();
    next_snapshot.weapon_recoil_yaw_radians =
        world.GetWeaponViewRecoil().GetYawRadians();
    next_snapshot.weapon_view_transitioning =
        world.IsWeaponViewTransitioning();
    next_snapshot.clip_ammo = weapons.GetCurrentClipAmmo();
    next_snapshot.clip_capacity = active_weapon.clip_capacity;
    next_snapshot.reserve_ammo = weapons.GetReserveAmmo();

    next_snapshot.zoom_active = world.IsZoomActive();
    next_snapshot.map_computer_open = world.IsMapComputerOpen();
    if (next_snapshot.map_computer_open) {
        constexpr size_t kMapComputerObjectiveSlotCount = 6;
        const std::vector<MissionObjective>& objectives =
            world.GetLevelFlow().GetObjectives();
        const size_t objective_count = std::min(
            objectives.size(),
            kMapComputerObjectiveSlotCount);
        next_snapshot.map_computer_objectives.reserve(objective_count);
        for (size_t objective_index = 0;
             objective_index < objective_count;
             ++objective_index) {
            const MissionObjective& objective = objectives[objective_index];
            RuntimeMapComputerObjective row;
            row.text = objective.description;
            row.link_task_id = objective.link_task_id;
            row.state = objective.state;
            row.has_location = objective.has_location;
            if (objective.has_location) {
                row.location = glm::vec3(
                    static_cast<float>(objective.location.x),
                    static_cast<float>(objective.location.y),
                    static_cast<float>(objective.location.z));
            }
            next_snapshot.map_computer_objectives.push_back(std::move(row));
        }
    }
    const std::vector<AiGuardEntity>& guards = world.GetAi().GetGuards();
    next_snapshot.guards.reserve(guards.size());
    for (const AiGuardEntity& guard : guards) {
        RuntimeGuardRenderState guard_snapshot;
        guard_snapshot.guard_id = guard.id;
        guard_snapshot.position = guard.position;
        guard_snapshot.yaw = guard.yaw;
        guard_snapshot.state = guard.state;
        guard_snapshot.runtime_enabled = guard.runtime_enabled;
        guard_snapshot.requested_animation = guard.requested_animation;
        guard_snapshot.animation_request_serial = guard.animation_request_serial;
        next_snapshot.guards.push_back(guard_snapshot);
    }
    next_snapshot.objective_text = world.GetLevelFlow().GetObjectiveDisplayText();
    for (const MissionObjective& objective : world.GetLevelFlow().GetObjectives()) {
        if (!objective.is_primary || objective.state != ObjectiveState::Pending) {
            continue;
        }

        next_snapshot.objective_link_task_id = objective.link_task_id;
        if (objective.has_location) {
            next_snapshot.has_objective_location = true;
            next_snapshot.objective_location = glm::vec3(
                static_cast<float>(objective.location.x),
                static_cast<float>(objective.location.y),
                static_cast<float>(objective.location.z));
        }
        break;
    }
    if (world.GetLevelFlow().HasAuthoredMissionFlow() &&
        world.GetLevelFlow().IsInterfaceTimerEnabled() &&
        world.GetLevelFlow().GetMaximumLevelPlayTimeSeconds() > 0.0) {
        const int64_t maximum_ticks = static_cast<int64_t>(std::llround(
            world.GetLevelFlow().GetMaximumLevelPlayTimeSeconds() *
            static_cast<double>(GameClock::TICK_RATE_HZ)));
        next_snapshot.mission_timer_remaining_ticks = std::max<int64_t>(
            0,
            maximum_ticks - static_cast<int64_t>(
                world.GetLevelFlow().GetMissionFlowTick()));
    }
    next_snapshot.mission_status_messages =
        world.GetDisplayedMissionStatusMessages();
    next_snapshot.flash_effect_strength = world.GetFlashEffectStrength();
    next_snapshot.muzzle_flash_strength = world.GetMuzzleFlashStrength();
    next_snapshot.player_damage_effect_strength =
        world.GetPlayerDamageEffectStrength();
    next_snapshot.guard_muzzle_flashes = world.GetGuardMuzzleFlashStates();
    next_snapshot.explosions = world.GetExplosionRenderStates();

    const auto& live_projectiles = world.GetProjectiles().GetProjectiles();
    next_snapshot.projectiles.reserve(live_projectiles.size());
    for (const LiveProjectile& projectile : live_projectiles) {
        next_snapshot.projectiles.push_back({
            projectile.type,
            projectile.position,
            projectile.tumble_ticks,
        });
    }

    snapshot_ = std::move(next_snapshot);
}

void RuntimeRenderer::Clear() {
    snapshot_ = RuntimeRenderSnapshot();
}

} // namespace igi
