#include "runtime_world.h"

#include "../player_separation.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <utility>

namespace igi {

namespace {

constexpr float kTransientPresentationStrengthDecayPerTick = 0.5f;
constexpr int kStatusMessageSendCooldownTicks = 210;
constexpr int kStatusMessageRevealWarmupTicks = 9;
constexpr float kMapComputerVantageHeightUnits = 900.0f * 4096.0f;
constexpr std::array<float, 4> kMapComputerFieldOfViews = {
    0.7853982f,
    0.3272492f,
    0.1396263f,
    0.009599311f,
};
constexpr float kMapComputerViewportAspect = 16.0f / 9.0f;

bool MissionCriteriaAcceptsPlayer(const std::string& criteria) {
    std::string normalized_criteria;
    normalized_criteria.reserve(criteria.size());
    for (const unsigned char character : criteria) {
        normalized_criteria.push_back(static_cast<char>(std::tolower(character)));
    }

    return normalized_criteria.empty() ||
        normalized_criteria.find("criteria_human0") != std::string::npos ||
        normalized_criteria.find("humanplayer") != std::string::npos ||
        normalized_criteria.find("player") != std::string::npos;
}

glm::mat3 BuildEngineOrientation(const glm::vec3& angles) {
    glm::mat4 orientation(1.0f);
    orientation = glm::rotate(
        orientation,
        angles.z,
        glm::vec3(0.0f, 0.0f, 1.0f));
    orientation = glm::rotate(
        orientation,
        angles.y,
        glm::vec3(0.0f, 1.0f, 0.0f));
    orientation = glm::rotate(
        orientation,
        angles.x,
        glm::vec3(1.0f, 0.0f, 0.0f));
    return glm::mat3(orientation);
}

bool TryTruncateMissionExpressionValue(double value, int& result) {
    if (!std::isfinite(value) ||
        value < static_cast<double>(std::numeric_limits<int>::min()) ||
        value >= static_cast<double>(std::numeric_limits<int>::max()) + 1.0) {
        return false;
    }
    result = static_cast<int>(value);
    return true;
}

int SecondsToSimulationTicks(float seconds) {
    if (!std::isfinite(seconds) || seconds <= 0.0f) {
        return 0;
    }

    const double tick_count = static_cast<double>(seconds) *
        static_cast<double>(GameClock::TICK_RATE_HZ);
    if (tick_count >= static_cast<double>(std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(tick_count);
}

double PositiveFiniteSeconds(float seconds) {
    return std::isfinite(seconds) && seconds > 0.0f
        ? static_cast<double>(seconds)
        : 0.0;
}

glm::vec3 NormalizeOrFallback(
    const glm::vec3& value,
    const glm::vec3& fallback) {
    const float length = glm::length(value);
    if (!std::isfinite(length) || length <= 0.000001f) {
        return fallback;
    }
    return value / length;
}

std::string GuardGeneratorIdentity(
    const AuthoredMissionGuardGenerator& definition) {
    if (!definition.task_id.empty() && definition.task_id != "-1") {
        return definition.task_id;
    }
    return std::to_string(definition.object_index);
}

void ApplyMissionVariableDelta(int& value, double expression_value, int direction) {
    int delta = 0;
    if (!TryTruncateMissionExpressionValue(expression_value, delta)) {
        return;
    }

    const long long updated_value = static_cast<long long>(value) +
        static_cast<long long>(direction) * delta;
    value = static_cast<int>(std::clamp(
        updated_value,
        static_cast<long long>(std::numeric_limits<int>::min()),
        static_cast<long long>(std::numeric_limits<int>::max())));
}

bool WeaponProducesMuzzleFlash(const WeaponDefinition& weapon) {
    switch (weapon.id) {
        case 1:  // Glock 17
        case 3:  // Desert Eagle
        case 4:  // M16A2
        case 5:  // AK47
        case 6:  // Uzi
        case 7:  // MP5SD
        case 8:  // SPAS12
        case 9:  // Jackhammer
        case 10: // Minimi
        case 11: // Dragunov
        case 21: // Colt Anaconda
            return true;
        default:
            return false;
    }
}

} // namespace

RuntimeWorld::RuntimeWorld()
    : ai_script_host_(qvm_registry_) {
    ai_script_host_.SetScriptSoundHandler(
        [this](
            uint32_t,
            const glm::vec3& guard_position,
            const std::string& authored_sound,
            bool relative_to_guard) {
            if (authored_sound.empty()) {
                return;
            }

            RuntimeAudioEvent audio_event;
            audio_event.type = RuntimeAudioEventType::OneShot;
            // Keep missing authored guard dialogue audible without conflating
            // a failed voice lookup with a weapon discharge.
            audio_event.fallback_effect = SoundEffect::ObjectiveComplete;
            audio_event.authored_sound = authored_sound;
            audio_event.position = guard_position;
            audio_event.relative_to_microphone = relative_to_guard;
            QueueAudioEvent(std::move(audio_event));
        });
}
RuntimeWorld::~RuntimeWorld() = default;

std::vector<RuntimeAudioEvent> RuntimeWorld::ConsumePendingAudioEvents() {
    std::vector<RuntimeAudioEvent> audio_events = std::move(pending_audio_events_);
    pending_audio_events_.clear();
    return audio_events;
}

void RuntimeWorld::QueueAudioEvent(RuntimeAudioEvent audio_event) {
    pending_audio_events_.push_back(std::move(audio_event));
}

void RuntimeWorld::QueueOneShotAudio(
    SoundEffect fallback_effect,
    const std::string& authored_sound) {
    RuntimeAudioEvent audio_event;
    audio_event.type = RuntimeAudioEventType::OneShot;
    audio_event.fallback_effect = fallback_effect;
    audio_event.authored_sound = authored_sound;
    QueueAudioEvent(std::move(audio_event));
}

void RuntimeWorld::QueueLoopStart(
    const std::string& channel_id,
    const std::string& authored_sound,
    SoundEffect fallback_effect,
    const glm::vec3& position,
    bool relative_to_microphone) {
    RuntimeAudioEvent audio_event;
    audio_event.type = RuntimeAudioEventType::StartLoop;
    audio_event.fallback_effect = fallback_effect;
    audio_event.authored_sound = authored_sound;
    audio_event.channel_id = channel_id;
    audio_event.position = position;
    audio_event.relative_to_microphone = relative_to_microphone;
    QueueAudioEvent(std::move(audio_event));
}

void RuntimeWorld::QueueLoopStop(const std::string& channel_id) {
    RuntimeAudioEvent audio_event;
    audio_event.type = RuntimeAudioEventType::StopLoop;
    audio_event.channel_id = channel_id;
    QueueAudioEvent(std::move(audio_event));
}

void RuntimeWorld::Initialize(float (*get_terrain_z)(float x, float y), bool (*check_collision)(float x, float y, float z)) {
    get_terrain_z_ = get_terrain_z;
    check_collision_ = check_collision;
    if (check_collision_) {
        player_.SetCeilingQuery([this](const glm::vec3& body_position) {
            return FindWorldCeilingHeight(body_position);
        });
        ai_.SetMovementCollisionQuery([check_collision](const glm::vec3& position) {
            return check_collision(
                position.x,
                position.y,
                position.z + 0.9f * PlayerController::WORLD_METER);
        });
        ai_.SetLineOfSightQuery([this](
            const glm::vec3& line_origin,
            const glm::vec3& line_target) {
            return !IsWorldLineBlocked(line_origin, line_target);
        });
    } else {
        player_.SetCeilingQuery({});
        ai_.SetMovementCollisionQuery({});
        ai_.SetLineOfSightQuery({});
    }
    projectiles_.SetCollisionQuery(
        [this](
            const glm::vec3& start_position,
            const glm::vec3& end_position,
            ProjectileCollisionHit& collision_hit) {
            return FindProjectileCollision(
                start_position,
                end_position,
                collision_hit);
        });
    projectiles_.SetProximityTriggerQuery(
        [this](const glm::vec3& center, float radius_units) {
            return IsProjectileTargetInRange(center, radius_units);
        });
    Reset();
}

float RuntimeWorld::FindWorldCeilingHeight(const glm::vec3& body_position) const {
    if (!check_collision_) {
        return std::numeric_limits<float>::max();
    }

    // Only the standing envelope is needed for stance transitions. Keeping
    // this probe bounded prevents the per-tick ground query from scanning the
    // entire level while still detecting roofs that can block the player.
    constexpr float probe_start_offset = PlayerCollision::SkinWidthInUnits;
    constexpr float probe_limit = PlayerController::STANDING_EYE_HEIGHT +
        PlayerCollision::SkinWidthInUnits;
    constexpr float probe_step = 256.0f;
    constexpr int refinement_iterations = 12;

    const float lower_bound = body_position.z + probe_start_offset;
    const float upper_bound = body_position.z + probe_limit;
    const auto is_solid = [this, &body_position](float height) {
        return check_collision_(body_position.x, body_position.y, height);
    };

    if (is_solid(lower_bound)) {
        return lower_bound;
    }

    float previous_height = lower_bound;
    const int sample_count = std::max(
        1,
        static_cast<int>(std::ceil((upper_bound - lower_bound) / probe_step)));
    for (int sample_index = 1; sample_index <= sample_count; ++sample_index) {
        const float sample_fraction = static_cast<float>(sample_index) /
            static_cast<float>(sample_count);
        const float current_height = lower_bound +
            (upper_bound - lower_bound) * sample_fraction;
        if (!is_solid(current_height)) {
            previous_height = current_height;
            continue;
        }

        float lower_height = previous_height;
        float upper_height = current_height;
        for (int refinement_index = 0;
             refinement_index < refinement_iterations;
             ++refinement_index) {
            const float middle_height = (lower_height + upper_height) * 0.5f;
            if (is_solid(middle_height)) {
                upper_height = middle_height;
            } else {
                lower_height = middle_height;
            }
        }
        return upper_height;
    }

    return std::numeric_limits<float>::max();
}

bool RuntimeWorld::FindProjectileCollision(
    const glm::vec3& start_position,
    const glm::vec3& end_position,
    ProjectileCollisionHit& collision_hit) const {
    const glm::vec3 movement = end_position - start_position;
    if (glm::dot(movement, movement) <= 0.00000001f) {
        return false;
    }

    const auto normalize_surface_normal = [](const glm::vec3& normal) {
        const float length_squared = glm::dot(normal, normal);
        if (length_squared <= 0.00000001f) {
            return glm::vec3(0.0f, 0.0f, 1.0f);
        }
        return normal / std::sqrt(length_squared);
    };

    const auto is_solid = [this](const glm::vec3& sample_position) {
        if (get_terrain_z_ && sample_position.z <= get_terrain_z_(
                sample_position.x,
                sample_position.y)) {
            return true;
        }
        return check_collision_ && check_collision_(
            sample_position.x,
            sample_position.y,
            sample_position.z);
    };

    bool found_world_collision = is_solid(end_position);
    float world_collision_fraction = 1.0f;
    if (found_world_collision) {
        float lower_fraction = 0.0f;
        if (is_solid(start_position)) {
            world_collision_fraction = 0.0f;
        } else {
            float upper_fraction = 1.0f;
            for (int refinement_index = 0;
                 refinement_index < 12;
                 ++refinement_index) {
                const float middle_fraction =
                    (lower_fraction + upper_fraction) * 0.5f;
                const glm::vec3 middle_position = start_position +
                    movement * middle_fraction;
                if (is_solid(middle_position)) {
                    upper_fraction = middle_fraction;
                } else {
                    lower_fraction = middle_fraction;
                }
            }
            world_collision_fraction = upper_fraction;
        }
    }

    // Projectiles use a compact swept sphere for living guards. The hit radius
    // matches the hitscan body envelope while keeping the projectile query
    // independent from the AI's movement collision implementation.
    constexpr float guard_collision_radius = 0.6f * PlayerController::WORLD_METER;
    const float movement_length_squared = glm::dot(movement, movement);
    float guard_collision_fraction = 1.0f;
    glm::vec3 guard_collision_center(0.0f);
    bool found_guard_collision = false;
    for (const AiGuardEntity& guard : ai_.GetGuards()) {
        if (!guard.runtime_enabled || guard.state == AiGuardState::Dead) {
            continue;
        }

        const glm::vec3 guard_center = guard.position + glm::vec3(
            0.0f,
            0.0f,
            0.9f * PlayerController::WORLD_METER);
        const float projected_fraction = glm::dot(
            guard_center - start_position,
            movement) / movement_length_squared;
        const float candidate_fraction = std::clamp(
            projected_fraction,
            0.0f,
            1.0f);
        const glm::vec3 closest_position = start_position +
            movement * candidate_fraction;
        if (glm::distance(closest_position, guard_center) >
            guard_collision_radius) {
            continue;
        }

        if (!found_guard_collision ||
            candidate_fraction < guard_collision_fraction) {
            found_guard_collision = true;
            guard_collision_fraction = candidate_fraction;
            guard_collision_center = guard_center;
        }
    }

    if (found_guard_collision &&
        (!found_world_collision ||
         guard_collision_fraction < world_collision_fraction)) {
        collision_hit.position = start_position + movement *
            guard_collision_fraction;
        collision_hit.normal = normalize_surface_normal(
            collision_hit.position - guard_collision_center);
        if (glm::dot(collision_hit.normal, collision_hit.normal) <=
            0.00000001f) {
            collision_hit.normal = normalize_surface_normal(-movement);
        }
        return true;
    }

    if (!found_world_collision) {
        return false;
    }

    collision_hit.position = start_position + movement * world_collision_fraction;

    // A terrain crossing has an actual height-field normal. Static model
    // collision only supplies a boolean in the existing editor callback, so
    // use the incoming horizontal direction as a conservative wall normal.
    bool crossed_terrain = false;
    if (get_terrain_z_) {
        const float start_terrain_height = get_terrain_z_(
            start_position.x,
            start_position.y);
        const float end_terrain_height = get_terrain_z_(
            end_position.x,
            end_position.y);
        crossed_terrain = start_position.z > start_terrain_height &&
            end_position.z <= end_terrain_height;
        if (crossed_terrain) {
            constexpr float terrain_probe_spacing = 64.0f;
            const float left_height = get_terrain_z_(
                collision_hit.position.x - terrain_probe_spacing,
                collision_hit.position.y);
            const float right_height = get_terrain_z_(
                collision_hit.position.x + terrain_probe_spacing,
                collision_hit.position.y);
            const float down_height = get_terrain_z_(
                collision_hit.position.x,
                collision_hit.position.y - terrain_probe_spacing);
            const float up_height = get_terrain_z_(
                collision_hit.position.x,
                collision_hit.position.y + terrain_probe_spacing);
            collision_hit.normal = normalize_surface_normal(
                glm::vec3(
                    -(right_height - left_height),
                    -(up_height - down_height),
                    terrain_probe_spacing * 2.0f));
        }
    }

    if (!crossed_terrain) {
        collision_hit.normal = normalize_surface_normal(
            glm::vec3(-movement.x, -movement.y, 0.0f));
        if (glm::dot(collision_hit.normal, collision_hit.normal) <= 0.00000001f) {
            collision_hit.normal = glm::vec3(0.0f, 0.0f, 1.0f);
        }
    }
    return true;
}

bool RuntimeWorld::IsProjectileTargetInRange(
    const glm::vec3& center,
    float radius_units) const {
    if (player_.IsAlive() && glm::distance(
            center,
            player_.GetPosition()) <= radius_units) {
        return true;
    }

    for (const AiGuardEntity& guard : ai_.GetGuards()) {
        if (guard.runtime_enabled && guard.state != AiGuardState::Dead && glm::distance(
            center,
            guard.position) <= radius_units) {
            return true;
        }
    }
    return false;
}

void RuntimeWorld::LaunchPlayerProjectile(
    const glm::vec3& origin,
    const glm::vec3& direction,
    const WeaponDefinition& weapon) {
    const float direction_length = glm::length(direction);
    if (direction_length <= 0.0001f) {
        return;
    }

    ProjectileLaunch launch;
    launch.position = origin;
    launch.velocity = direction / direction_length * (
        weapon.muzzle_velocity * PlayerController::WORLD_METER / 30.0f);
    launch.type = weapon.projectile_type;
    launch.owner_entity_id = 0;
    launch.fuse_ticks = ProjectileSystem::ReferenceGrenadeFuseTicks;
    launch.damage = weapon.damage;
    launch.damage_factor = 1.0f;
    launch.explosion_radius_units = 5.0f * PlayerController::WORLD_METER;
    launch.explosion_falloff_units = 0.0f;

    switch (weapon.projectile_type) {
        case ProjectileType::ProximityMine:
            launch.fuse_ticks = 0;
            launch.proximity_trigger_radius_units =
                1.5f * PlayerController::WORLD_METER;
            break;
        case ProjectileType::Rocket:
            // OpenIGI's missile path is impact-driven and uses the fixed
            // 2.5-metre/1.75-metre blast profile. Steering remains a later
            // seam; the current launch is still a real moving projectile.
            launch.damage_factor = 5.0f;
            launch.explosion_radius_units = 2.5f * PlayerController::WORLD_METER;
            launch.explosion_falloff_units = 1.75f * PlayerController::WORLD_METER;
            launch.detonate_on_impact = true;
            launch.affected_by_gravity = false;
            break;
        case ProjectileType::FragGrenade:
        case ProjectileType::Flashbang:
        case ProjectileType::None:
            break;
    }

    projectiles_.Spawn(launch);
}

void RuntimeWorld::ApplyProjectileDetonations() {
    constexpr float flash_maximum_distance = 15.0f * PlayerController::WORLD_METER;
    constexpr float flash_minimum_duration_seconds = 0.75f;
    constexpr float flash_maximum_duration_seconds = 1.5f;

    for (const ProjectileDetonation& detonation : projectiles_.GetDetonations()) {
        QueueExplosionRenderState(
            detonation.position,
            detonation.explosion_radius_units,
            detonation.type == ProjectileType::Flashbang);
        if (detonation.type == ProjectileType::Flashbang) {
            QueueOneShotAudio(SoundEffect::Flashbang);
            if (player_.IsAlive()) {
                const float player_distance = glm::distance(
                    detonation.position,
                    player_.GetEyePosition());
                if (player_distance < flash_maximum_distance &&
                    !IsWorldLineBlocked(
                        detonation.position,
                        player_.GetEyePosition())) {
                    const float exposure = std::clamp(
                        1.0f - player_distance / flash_maximum_distance,
                        0.0f,
                        1.0f);
                    const float duration = flash_minimum_duration_seconds +
                        exposure * (flash_maximum_duration_seconds -
                            flash_minimum_duration_seconds);
                    flash_effect_strength_ = std::max(
                        flash_effect_strength_,
                        exposure);
                    flash_effect_decay_per_second_ = std::max(
                        flash_effect_decay_per_second_,
                        exposure / duration);
                    flash_effect_remaining_seconds_ = std::max(
                        flash_effect_remaining_seconds_,
                        duration);
                }
            }
            continue;
        }

        QueueOneShotAudio(SoundEffect::Explosion);
        ApplyExplosionDamage(
            detonation.position,
            detonation.explosion_radius_units,
            detonation.explosion_falloff_units,
            detonation.damage,
            detonation.damage_factor,
            detonation.owner_entity_id);
    }

    // Projectile resolution can trigger authored props after the mission
    // update has already published its regular snapshot.
    RefreshAuthoredExplodeObjectSnapshots();
}

void RuntimeWorld::AdvanceExplosionRenderStates() {
    for (auto iterator = explosion_render_states_.begin();
         iterator != explosion_render_states_.end();) {
        if (iterator->remaining_ticks <= 1) {
            iterator = explosion_render_states_.erase(iterator);
        } else {
            --iterator->remaining_ticks;
            ++iterator;
        }
    }
}

void RuntimeWorld::QueueExplosionRenderState(
    const glm::vec3& explosion_position,
    float explosion_radius_units,
    bool is_flashbang) {
    RuntimeExplosionRenderState render_state;
    render_state.position = explosion_position;
    render_state.radius_units = std::isfinite(explosion_radius_units)
        ? std::max(0.0f, explosion_radius_units)
        : 0.0f;
    render_state.remaining_ticks =
        RuntimeExplosionRenderState::DISPLAY_DURATION_TICKS;
    render_state.is_flashbang = is_flashbang;
    explosion_render_states_.push_back(render_state);
}

void RuntimeWorld::AdvanceGuardMuzzleFlashStates() {
    for (RuntimeGuardMuzzleFlashState& flash : guard_muzzle_flash_states_) {
        flash.strength = std::max(
            0.0f,
            flash.strength - kTransientPresentationStrengthDecayPerTick);
    }

    guard_muzzle_flash_states_.erase(
        std::remove_if(
            guard_muzzle_flash_states_.begin(),
            guard_muzzle_flash_states_.end(),
            [](const RuntimeGuardMuzzleFlashState& flash) {
                return flash.strength <= 0.0f;
            }),
        guard_muzzle_flash_states_.end());
}

void RuntimeWorld::QueueGuardMuzzleFlash(
    uint32_t guard_id,
    const glm::vec3& muzzle_position) {
    for (RuntimeGuardMuzzleFlashState& flash : guard_muzzle_flash_states_) {
        if (flash.guard_id != guard_id) {
            continue;
        }
        flash.position = muzzle_position;
        flash.strength = 1.0f;
        return;
    }

    guard_muzzle_flash_states_.push_back({guard_id, muzzle_position, 1.0f});
}

void RuntimeWorld::ApplyPlayerDamage(float damage_amount) {
    if (damage_amount <= 0.0f || !player_.IsAlive()) {
        return;
    }

    const float health_before = player_.GetHealth();
    const float armor_before = player_.GetArmor();
    player_.ApplyDamage(damage_amount);
    if (player_.GetHealth() < health_before ||
        player_.GetArmor() < armor_before) {
        player_damage_effect_strength_ = 1.0f;
    }
}

void RuntimeWorld::ApplyExplosionDamage(
    const glm::vec3& explosion_position,
    float explosion_radius_units,
    float explosion_falloff_units,
    float base_damage,
    float damage_factor,
    uint32_t owner_entity_id) {
    constexpr float target_collision_radius = 0.6f * PlayerController::WORLD_METER;
    constexpr float body_radius_scale = 0.33333334f;

    const float safe_radius = std::isfinite(explosion_radius_units)
        ? std::max(0.0f, explosion_radius_units)
        : 0.0f;
    const float safe_falloff = std::isfinite(explosion_falloff_units)
        ? std::max(0.0f, explosion_falloff_units)
        : 0.0f;
    const float safe_damage = std::isfinite(base_damage)
        ? std::max(0.0f, base_damage)
        : 0.0f;
    const float safe_damage_factor = std::isfinite(damage_factor)
        ? std::max(0.0f, damage_factor)
        : 0.0f;
    const float maximum_damage_distance = safe_radius + safe_falloff;

    const auto calculate_damage = [safe_radius, safe_falloff, safe_damage,
                                   safe_damage_factor](float target_distance) {
        const float effective_distance = target_distance -
            target_collision_radius * body_radius_scale - safe_radius;
        if (effective_distance < 0.0f) {
            return safe_damage * safe_damage_factor;
        }
        if (effective_distance >= safe_falloff || safe_falloff <= 0.0f) {
            return 0.0f;
        }
        const float dose = (safe_falloff - effective_distance) / safe_falloff;
        return safe_damage * safe_damage_factor * dose;
    };

    // Explosive props are part of the blast graph: a grenade can start a
    // barrel chain, and a barrel can start the next authored prop. Mark the
    // target before applying its own blast so a prop cannot trigger itself
    // recursively.
    for (AuthoredExplodeObjectRuntime& runtime_object : mission_explode_objects_) {
        if (runtime_object.is_exploded) {
            continue;
        }

        const float object_distance = glm::distance(
            explosion_position,
            runtime_object.definition.position);
        if (object_distance > maximum_damage_distance ||
            IsWorldLineBlocked(
                explosion_position,
                runtime_object.definition.position)) {
            continue;
        }

        if (calculate_damage(object_distance) > 0.0f) {
            TriggerAuthoredExplodeObject(runtime_object);
        }
    }

    if (player_.IsAlive()) {
        const float player_distance = glm::distance(
            explosion_position,
            player_.GetPosition());
        if (player_distance <= maximum_damage_distance &&
            !IsWorldLineBlocked(
                explosion_position,
                player_.GetEyePosition())) {
            ApplyPlayerDamage(calculate_damage(player_distance));
        }
    }

    for (const AiGuardEntity& guard : ai_.GetGuards()) {
        if (!guard.runtime_enabled || guard.state == AiGuardState::Dead ||
            guard.id == owner_entity_id) {
            continue;
        }
        const float guard_distance = glm::distance(
            explosion_position,
            guard.position);
        if (guard_distance > maximum_damage_distance ||
            IsWorldLineBlocked(
                explosion_position,
                guard.position + glm::vec3(
                    0.0f,
                    0.0f,
                    1.0f * PlayerController::WORLD_METER))) {
            continue;
        }
        ai_.ApplyDamage(guard.id, calculate_damage(guard_distance));
    }
}

void RuntimeWorld::Reset() {
    // Discard stale one-shots from the previous run. Conditional loops are
    // stopped below and their teardown events are retained for the host.
    pending_audio_events_.clear();

    glm::vec3 spawn_pos(0.0f, 0.0f, 100.0f);
    if (get_terrain_z_) {
        spawn_pos.z = get_terrain_z_(spawn_pos.x, spawn_pos.y);
    }

    player_.Reset(spawn_pos, 0.0f);
    // The spawn position sits on the queried surface, so the body starts
    // grounded; otherwise the first tick would report a phantom landing
    // impact and alert nearby guards through its hearing event.
    player_.PlaceOnGround();
    player_.ApplyTuning(player_tuning_);
    weapons_ = WeaponSystem();
    weapons_.SetPlayerWeaponCycle(player_weapon_cycle_);
    weapons_.SelectWeaponSlot(0);
    weapon_view_sway_.Reset();
    weapon_view_recoil_.Reset();
    weapon_selection_phase_ = WeaponSelectionPhase::Ready;
    pending_weapon_slot_ = -1;
    ladder_placements_.clear();
    ladder_traversal_ = LadderTraversal();
    active_ladder_index_ = -1;
    ladder_slide_velocity_ = glm::vec3(0.0f);
    projectiles_.Clear();
    ai_.Clear();
    ClearGuardScripts();
    task_tree_.Clear();
    level_flow_.InitializeMission(1);
    mission_expression_state_.Clear();
    mission_area_activations_.clear();
    mission_edit_variables_.clear();
    mission_edit_variable_values_.clear();
    mission_level_timers_.clear();
    mission_level_timer_ticks_.clear();
    mission_level_timer_running_.clear();
    mission_cut_scenes_.clear();
    mission_cut_scene_ticks_.clear();
    mission_cut_scene_running_.clear();
    mission_cut_scene_finished_.clear();
    active_cut_scene_camera_ = RuntimeCutSceneCamera();
    for (const AuthoredConditionalSoundRuntime& runtime_sound :
         mission_conditional_sounds_) {
        QueueLoopStop(runtime_sound.definition.task_id);
    }
    mission_conditional_sounds_.clear();
    mission_conditional_containers_.clear();
    authored_conditional_container_snapshots_.clear();
    mission_guard_generators_.clear();
    authored_guard_generator_snapshots_.clear();
    mission_explode_objects_.clear();
    authored_explode_object_snapshots_.clear();
    explosion_render_states_.clear();
    mission_status_messages_.clear();
    mission_status_message_slots_.fill(-1);
    displayed_mission_status_messages_.clear();
    mission_state_pulse_names_.clear();
    for (AuthoredDoorRuntime& authored_door : authored_doors_) {
        authored_door.state = RuntimeDoorState(authored_door.definition);
        authored_door.is_locked = false;
        authored_door.is_picked = false;
    }
    authored_door_snapshots_.clear();
    guard_combat_states_.clear();
    guard_muzzle_flash_states_.clear();
    fire_was_held_ = false;
    zoom_active_ = false;
    map_computer_open_ = false;
    map_computer_input_was_held_ = false;
    map_computer_zoom_level_ = 1;
    map_computer_center_ = glm::vec2(
        player_.GetPosition().x,
        player_.GetPosition().y);
    flash_effect_strength_ = 0.0f;
    flash_effect_decay_per_second_ = 0.0f;
    flash_effect_remaining_seconds_ = 0.0f;
    muzzle_flash_strength_ = 0.0f;
    player_damage_effect_strength_ = 0.0f;
    footstep_timer_seconds_ = 0.0;
    extraction_zone_center_ = glm::vec3(1000.0f, 1000.0f, 0.0f);
    extraction_zone_radius_ = 8.0f * PlayerController::WORLD_METER;
}

bool RuntimeWorld::UpdateWeaponSelection(const PlayerInputCmd& input_command) {
    const bool has_valid_requested_slot =
        input_command.switch_weapon >= 0 &&
        input_command.switch_weapon <= 17;

    if (weapon_selection_phase_ == WeaponSelectionPhase::Ready) {
        if (!has_valid_requested_slot) {
            return true;
        }

        pending_weapon_slot_ = input_command.switch_weapon;
        weapon_view_sway_.Lower();
        weapon_selection_phase_ = WeaponSelectionPhase::Lowering;
        return false;
    }

    if (weapon_selection_phase_ == WeaponSelectionPhase::Lowering) {
        if (has_valid_requested_slot) {
            pending_weapon_slot_ = input_command.switch_weapon;
        }
        weapon_view_sway_.Advance();
        if (!weapon_view_sway_.IsSettled()) {
            return false;
        }

        if (pending_weapon_slot_ >= 0) {
            weapons_.SelectWeaponSlot(static_cast<uint32_t>(pending_weapon_slot_));
        }
        pending_weapon_slot_ = -1;
        weapon_view_sway_.Raise();
        weapon_selection_phase_ = WeaponSelectionPhase::Raising;
        return false;
    }

    weapon_view_sway_.Advance();
    if (weapon_view_sway_.IsSettled()) {
        weapon_selection_phase_ = WeaponSelectionPhase::Ready;
    }
    return false;
}

void RuntimeWorld::UpdateAuthoredMissionState() {
    for (const MissionAreaActivationState& area : mission_area_activations_) {
        const glm::vec3 player_position = player_.GetPosition();
        const bool active = area.accepts_player &&
            player_position.x >= area.minimum.x &&
            player_position.x <= area.maximum.x &&
            player_position.y >= area.minimum.y &&
            player_position.y <= area.maximum.y &&
            player_position.z >= area.minimum.z &&
            player_position.z <= area.maximum.z;
        mission_expression_state_.SetNumber(
            "AreaActivate_" + area.task_id + ".nActive",
            active ? 1.0 : 0.0);
    }

    for (size_t variable_index = 0;
         variable_index < mission_edit_variables_.size();
         ++variable_index) {
        const AuthoredMissionEditVariable& edit_variable =
            mission_edit_variables_[variable_index];
        int& current_value = mission_edit_variable_values_[variable_index];
        const std::string variable_name =
            "EditVariable_" + edit_variable.task_id + ".nValue";

        mission_expression_state_.SetNumber("this.nValue", current_value);
        double expression_value = 0.0;
        if (!edit_variable.add_expression.empty() &&
            mission_expression_state_.TryEvaluateNumber(
                edit_variable.add_expression,
                expression_value)) {
            ApplyMissionVariableDelta(current_value, expression_value, 1);
        }

        mission_expression_state_.SetNumber(variable_name, current_value);
        mission_expression_state_.SetNumber("this.nValue", current_value);
        if (!edit_variable.subtract_expression.empty() &&
            mission_expression_state_.TryEvaluateNumber(
                edit_variable.subtract_expression,
                expression_value)) {
            ApplyMissionVariableDelta(current_value, expression_value, -1);
        }
        mission_expression_state_.SetNumber(variable_name, current_value);
    }

    const auto evaluate_expression = [this](const std::string& expression) {
        bool result = false;
        return !expression.empty() &&
            mission_expression_state_.TryEvaluate(expression, result) && result;
    };
    for (size_t timer_index = 0;
         timer_index < mission_level_timers_.size();
         ++timer_index) {
        const AuthoredMissionLevelTimer& timer = mission_level_timers_[timer_index];
        int& tick_count = mission_level_timer_ticks_[timer_index];
        bool is_running = mission_level_timer_running_[timer_index] != 0;

        if (evaluate_expression(timer.reset_expression)) {
            tick_count = 0;
        }
        is_running = timer.on_expression.empty()
            ? timer.initial_run
            : evaluate_expression(timer.on_expression);
        mission_level_timer_running_[timer_index] = is_running ? 1 : 0;
        if (is_running && tick_count < std::numeric_limits<int>::max()) {
            ++tick_count;
        }

        const std::string prefix = "LevelTimer_" + timer.task_id;
        mission_expression_state_.SetBoolean(prefix + ".isRun", is_running);
        mission_expression_state_.SetNumber(prefix + ".nTick", tick_count);
    }
}

void RuntimeWorld::UpdateMissionActorState() {
    mission_expression_state_.SetBoolean(
        "HumanPlayer_0.isDead",
        !player_.IsAlive());

    for (const AiGuardEntity& guard : ai_.GetGuards()) {
        if (guard.mission_state_type.empty() || guard.mission_task_id.empty()) {
            continue;
        }

        mission_expression_state_.SetBoolean(
            guard.mission_state_type + "_" + guard.mission_task_id + ".isDead",
            guard.state == AiGuardState::Dead);
    }
}

glm::vec3 RuntimeWorld::GetMapComputerCenter() const {
    return glm::vec3(
        map_computer_center_.x,
        map_computer_center_.y,
        player_.GetPosition().z);
}

float RuntimeWorld::GetMapComputerFieldOfView() const {
    return kMapComputerFieldOfViews[static_cast<size_t>(
        std::clamp(map_computer_zoom_level_, 0, 3))];
}

void RuntimeWorld::UpdateMapComputerViewport(
    const PlayerInputCmd& input_command) {
    map_computer_zoom_level_ = std::clamp(
        map_computer_zoom_level_ + input_command.map_zoom_delta,
        0,
        static_cast<int>(kMapComputerFieldOfViews.size()) - 1);

    const float half_map_height = kMapComputerVantageHeightUnits *
        std::tan(GetMapComputerFieldOfView() * 0.5f);
    const float half_map_width = half_map_height * kMapComputerViewportAspect;
    map_computer_center_.x -= input_command.map_pan_delta_x * 2.0f * half_map_width;
    map_computer_center_.y += input_command.map_pan_delta_y * 2.0f * half_map_height;
}

bool RuntimeWorld::AttachGuardScript(
    uint32_t guard_id,
    const QVMFile& parsed_file,
    const std::string& source_path) {
    if (ai_.FindGuard(guard_id) == nullptr) {
        return false;
    }

    GuardScriptState script_state;
    script_state.source_path = source_path;
    if (!ai_script_host_.LoadProgram(parsed_file, script_state.program)) {
        script_state.last_error = ai_script_host_.GetLastError();
        script_state.faulted = true;
        guard_scripts_[guard_id] = std::move(script_state);
        return false;
    }

    guard_scripts_[guard_id] = std::move(script_state);
    return true;
}

bool RuntimeWorld::AttachGuardScriptFromFile(uint32_t guard_id, const std::string& path) {
    const QVMFile parsed_file = QVM_Parse(path);
    return AttachGuardScript(guard_id, parsed_file, path);
}

void RuntimeWorld::ClearGuardScripts() {
    guard_scripts_.clear();
    ai_script_host_.Reset();
}

bool RuntimeWorld::HasGuardScript(uint32_t guard_id) const {
    const auto script = guard_scripts_.find(guard_id);
    return script != guard_scripts_.end() && !script->second.faulted;
}

void RuntimeWorld::SetExtractionZone(const glm::vec3& center, float radius) {
    extraction_zone_center_ = center;
    extraction_zone_radius_ = std::max(0.0f, radius);
}

void RuntimeWorld::ClearExtractionZone() {
    extraction_zone_center_ = glm::vec3(0.0f);
    extraction_zone_radius_ = 0.0f;
}

void RuntimeWorld::SetMissionStateBoolean(
    const std::string& variable_name,
    bool value) {
    mission_expression_state_.SetBoolean(variable_name, value);
}

void RuntimeWorld::SetMissionStateNumber(
    const std::string& variable_name,
    double value) {
    mission_expression_state_.SetNumber(variable_name, value);
}

void RuntimeWorld::SetMissionStatePulse(const std::string& variable_name) {
    if (std::find(
            mission_state_pulse_names_.begin(),
            mission_state_pulse_names_.end(),
            variable_name) == mission_state_pulse_names_.end()) {
        mission_state_pulse_names_.push_back(variable_name);
    }
    mission_expression_state_.SetBoolean(variable_name, true);
}

void RuntimeWorld::SetAuthoredDoors(
    std::vector<RuntimeDoorDefinition> door_definitions) {
    authored_doors_.clear();
    authored_doors_.reserve(door_definitions.size());

    for (RuntimeDoorDefinition& definition : door_definitions) {
        AuthoredDoorRuntime authored_door;
        authored_door.definition = std::move(definition);
        authored_door.state = RuntimeDoorState(authored_door.definition);
        bool locked = false;
        if (!authored_door.definition.locked_expression.empty()) {
            mission_expression_state_.TryEvaluate(
                authored_door.definition.locked_expression,
                locked);
        }
        authored_door.is_locked = locked;
        authored_doors_.push_back(std::move(authored_door));
    }

    for (const AuthoredDoorRuntime& authored_door : authored_doors_) {
        PublishAuthoredDoorState(
            authored_door.definition,
            authored_door.state,
            authored_door.is_locked,
            authored_door.is_picked);
    }
    RefreshAuthoredDoorSnapshots();
    UpdateAuthoredGuardGenerators();
}

bool RuntimeWorld::ToggleDoor(int object_index) {
    for (AuthoredDoorRuntime& authored_door : authored_doors_) {
        if (authored_door.definition.object_index != object_index) {
            continue;
        }

        if (authored_door.is_locked && !authored_door.definition.pickable) {
            return false;
        }

        authored_door.state.Toggle();
        authored_door.is_picked = true;
        PublishAuthoredDoorState(
            authored_door.definition,
            authored_door.state,
            authored_door.is_locked,
            authored_door.is_picked);
        return true;
    }
    return false;
}

bool RuntimeWorld::IsDoorFullyOpen(int object_index) const {
    for (const AuthoredDoorRuntime& authored_door : authored_doors_) {
        if (authored_door.definition.object_index == object_index) {
            return authored_door.state.IsFullyOpen();
        }
    }
    return false;
}

void RuntimeWorld::PublishAuthoredDoorState(
    const RuntimeDoorDefinition& definition,
    const RuntimeDoorState& door_state,
    bool is_locked,
    bool is_picked) {
    if (definition.task_id.empty() || definition.task_id == "-1") {
        return;
    }

    const std::string prefix = "Door_" + definition.task_id;
    mission_expression_state_.SetBoolean(
        prefix + ".isOpen",
        door_state.IsFullyOpen());
    mission_expression_state_.SetBoolean(
        prefix + ".isClosed",
        door_state.IsFullyClosed());
    mission_expression_state_.SetBoolean(
        prefix + ".isLastOpen",
        door_state.WasFullyOpen());
    mission_expression_state_.SetBoolean(
        prefix + ".isLastClosed",
        door_state.WasFullyClosed());
    mission_expression_state_.SetBoolean(prefix + ".isLocked", is_locked);
    mission_expression_state_.SetBoolean(prefix + ".isPicked", is_picked);
    mission_expression_state_.SetNumber(
        prefix + ".nDoorOpenTicks",
        door_state.GetTicksOpen());
}

void RuntimeWorld::RefreshAuthoredDoorSnapshots() {
    authored_door_snapshots_.clear();
    authored_door_snapshots_.reserve(authored_doors_.size());
    for (const AuthoredDoorRuntime& authored_door : authored_doors_) {
        RuntimeDoorSnapshot snapshot;
        snapshot.object_index = authored_door.definition.object_index;
        snapshot.task_id = authored_door.definition.task_id;
        snapshot.closed_position_units = authored_door.definition.closed_position_units;
        snapshot.closed_rotation_radians = authored_door.definition.closed_rotation_radians;
        snapshot.angle_radians = authored_door.state.GetAngleRadians();
        snapshot.slide_fraction = authored_door.state.GetSlideFraction();
        snapshot.slide_offset_units = authored_door.state.GetSlideOffsetUnits();
        snapshot.is_fully_open = authored_door.state.IsFullyOpen();
        snapshot.is_fully_closed = authored_door.state.IsFullyClosed();
        snapshot.was_fully_open = authored_door.state.WasFullyOpen();
        snapshot.was_fully_closed = authored_door.state.WasFullyClosed();
        snapshot.is_locked = authored_door.is_locked;
        snapshot.is_picked = authored_door.is_picked;
        snapshot.ticks_open = authored_door.state.GetTicksOpen();
        authored_door_snapshots_.push_back(std::move(snapshot));
    }
}

void RuntimeWorld::UpdateAuthoredDoors() {
    const auto evaluate_expression = [this](const std::string& expression) {
        bool result = false;
        return !expression.empty() &&
            mission_expression_state_.TryEvaluate(expression, result) && result;
    };

    for (AuthoredDoorRuntime& authored_door : authored_doors_) {
        const RuntimeDoorDefinition& definition = authored_door.definition;
        authored_door.is_locked = !authored_door.is_picked &&
            evaluate_expression(definition.locked_expression);

        if (authored_door.state.IsFullyClosed() &&
            evaluate_expression(definition.open_expression)) {
            authored_door.state.CommandOpen();
        }
        if (authored_door.state.IsFullyOpen() &&
            evaluate_expression(definition.close_expression)) {
            authored_door.state.CommandClosed();
        }

        const bool was_moving = authored_door.state.IsMoving();
        authored_door.state.Tick();

        if (authored_door.state.IsFullyOpen() &&
            !authored_door.state.WasFullyOpen() &&
            !definition.open_sound.empty()) {
            QueueOneShotAudio(
                SoundEffect::ObjectiveComplete,
                definition.open_sound);
        }
        if (authored_door.state.IsFullyClosed() &&
            !authored_door.state.WasFullyClosed() &&
            !definition.close_sound.empty()) {
            QueueOneShotAudio(
                SoundEffect::ObjectiveComplete,
                definition.close_sound);
        }
        if (!was_moving && authored_door.state.IsMoving() &&
            !definition.move_sound.empty()) {
            QueueOneShotAudio(
                SoundEffect::ObjectiveComplete,
                definition.move_sound);
        }

        PublishAuthoredDoorState(
            definition,
            authored_door.state,
            authored_door.is_locked,
            authored_door.is_picked);
    }

    RefreshAuthoredDoorSnapshots();
}

void RuntimeWorld::PublishMissionStatusMessageState(
    const AuthoredMissionStatusMessage& definition,
    bool is_sent,
    int64_t sent_tick,
    bool is_finished_display,
    int64_t finished_display_tick,
    int64_t ticks_since_finished_display) {
    const std::string prefix = "StatusMessage_" + definition.task_id;
    mission_expression_state_.SetBoolean(prefix + ".isSendt", is_sent);
    mission_expression_state_.SetNumber(prefix + ".nTickSendt", sent_tick);
    mission_expression_state_.SetBoolean(
        prefix + ".isFinishedDisplay",
        is_finished_display);
    mission_expression_state_.SetNumber(
        prefix + ".nFinishedDisplay",
        finished_display_tick);
    mission_expression_state_.SetNumber(
        prefix + ".nTicksSinceFinishedDisplay",
        ticks_since_finished_display);
}

void RuntimeWorld::UpdateAuthoredMissionStatusMessages(uint64_t tick_number) {
    const int64_t current_tick = tick_number >
            static_cast<uint64_t>(std::numeric_limits<int64_t>::max())
        ? std::numeric_limits<int64_t>::max()
        : static_cast<int64_t>(tick_number);
    const auto evaluate_expression = [this](const std::string& expression) {
        bool result = false;
        return !expression.empty() &&
            mission_expression_state_.TryEvaluate(expression, result) && result;
    };

    for (size_t message_index = 0;
         message_index < mission_status_messages_.size();
         ++message_index) {
        MissionStatusMessageRuntime& message = mission_status_messages_[message_index];
        const AuthoredMissionStatusMessage& definition = message.definition;

        if ((!definition.send_once || !message.is_sent) &&
            current_tick > message.sent_tick + kStatusMessageSendCooldownTicks &&
            evaluate_expression(definition.send_expression)) {
            message.is_sent = true;
            message.sent_tick = current_tick;

            if (message.slot_index < 0 && !definition.display_text.empty()) {
                for (size_t slot_index = 0;
                     slot_index < mission_status_message_slots_.size();
                     ++slot_index) {
                    if (mission_status_message_slots_[slot_index] < 0) {
                        message.slot_index = static_cast<int>(slot_index);
                        mission_status_message_slots_[slot_index] =
                            static_cast<int>(message_index);
                        break;
                    }
                }
            }

            if (message.slot_index >= 0) {
                message.is_displaying = true;
                message.display_frame = 0;
                message.characters_remaining = definition.cutscene_message
                    ? 0
                    : static_cast<int>(definition.display_text.size());
                message.hold_ticks = 0;
                message.is_finished_display = false;
                message.ticks_since_finished_display = 0;
                if (!definition.sound_name.empty()) {
                    QueueOneShotAudio(
                        SoundEffect::ObjectiveComplete,
                        definition.sound_name);
                }
            }
        }

        if (message.is_finished_display) {
            ++message.ticks_since_finished_display;
        } else if (message.is_displaying) {
            const int hold_ticks = static_cast<int>(
                std::max(0.0f, definition.duration_seconds) *
                static_cast<float>(GameClock::TICK_RATE_HZ));
            bool display_finished = false;
            if (definition.cutscene_message) {
                display_finished = message.hold_ticks++ > hold_ticks;
            } else if (message.display_frame >= kStatusMessageRevealWarmupTicks) {
                if (message.characters_remaining > 0) {
                    if (((message.display_frame - kStatusMessageRevealWarmupTicks) & 1) == 0) {
                        --message.characters_remaining;
                    }
                } else {
                    display_finished = ++message.hold_ticks > hold_ticks;
                }
            }
            ++message.display_frame;

            if (display_finished) {
                message.is_displaying = false;
                message.is_finished_display = true;
                message.finished_display_tick = current_tick;
                message.ticks_since_finished_display = 0;
                if (message.slot_index >= 0 &&
                    mission_status_message_slots_[static_cast<size_t>(message.slot_index)] ==
                        static_cast<int>(message_index)) {
                    mission_status_message_slots_[static_cast<size_t>(message.slot_index)] = -1;
                }
                message.slot_index = -1;
            }
        }

        PublishMissionStatusMessageState(
            definition,
            message.is_sent,
            message.sent_tick,
            message.is_finished_display,
            message.finished_display_tick,
            message.ticks_since_finished_display);
    }

    displayed_mission_status_messages_.clear();
    for (const int slot_message_index : mission_status_message_slots_) {
        if (slot_message_index < 0 ||
            slot_message_index >= static_cast<int>(mission_status_messages_.size())) {
            continue;
        }
        const MissionStatusMessageRuntime& message =
            mission_status_messages_[static_cast<size_t>(slot_message_index)];
        if (!message.is_displaying) {
            continue;
        }

        MissionStatusMessageDisplay display;
        display.text = message.definition.display_text;
        display.revealed_characters = static_cast<uint32_t>(std::max(
            0,
            static_cast<int>(display.text.size()) - message.characters_remaining));
        display.display_frame = static_cast<uint32_t>(std::max(0, message.display_frame));
        display.cutscene_message = message.definition.cutscene_message;
        displayed_mission_status_messages_.push_back(std::move(display));
    }
}

void RuntimeWorld::SetAuthoredMissionState(
    std::vector<AuthoredMissionAreaActivation> area_activations,
    std::vector<AuthoredMissionEditVariable> edit_variables,
    std::vector<AuthoredMissionLevelTimer> level_timers,
    std::vector<AuthoredMissionStatusMessage> status_messages,
    std::vector<AuthoredMissionCutScene> cut_scenes,
    std::vector<AuthoredMissionConditionalSound> conditional_sounds,
    std::vector<AuthoredMissionExplodeObject> explode_objects,
    std::vector<AuthoredMissionConditionalContainer> conditional_containers,
    std::vector<AuthoredMissionGuardGenerator> guard_generators) {
    mission_expression_state_.Clear();
    mission_area_activations_.clear();
    mission_edit_variables_ = std::move(edit_variables);
    mission_level_timers_ = std::move(level_timers);
    mission_level_timer_ticks_.assign(mission_level_timers_.size(), 0);
    mission_level_timer_running_.assign(mission_level_timers_.size(), false);
    mission_cut_scenes_ = std::move(cut_scenes);
    mission_cut_scene_ticks_.clear();
    mission_cut_scene_running_.clear();
    mission_cut_scene_finished_.clear();
    active_cut_scene_camera_ = RuntimeCutSceneCamera();
    for (const AuthoredConditionalSoundRuntime& runtime_sound :
         mission_conditional_sounds_) {
        QueueLoopStop(runtime_sound.definition.task_id);
    }
    mission_conditional_sounds_.clear();
    mission_conditional_sounds_.reserve(conditional_sounds.size());
    for (AuthoredMissionConditionalSound& definition : conditional_sounds) {
        AuthoredConditionalSoundRuntime runtime_sound;
        runtime_sound.definition = std::move(definition);
        mission_conditional_sounds_.push_back(std::move(runtime_sound));
    }
    mission_conditional_containers_.clear();
    mission_conditional_containers_.reserve(conditional_containers.size());
    for (AuthoredMissionConditionalContainer& definition : conditional_containers) {
        AuthoredConditionalContainerRuntime runtime_container;
        runtime_container.definition = std::move(definition);
        mission_conditional_containers_.push_back(std::move(runtime_container));
    }
    authored_conditional_container_snapshots_.clear();
    mission_guard_generators_.clear();
    mission_guard_generators_.reserve(guard_generators.size());
    for (AuthoredMissionGuardGenerator& definition : guard_generators) {
        AuthoredGuardGeneratorRuntime runtime_generator;
        runtime_generator.definition = std::move(definition);
        mission_guard_generators_.push_back(std::move(runtime_generator));
    }
    authored_guard_generator_snapshots_.clear();
    mission_explode_objects_.clear();
    mission_explode_objects_.reserve(explode_objects.size());
    for (AuthoredMissionExplodeObject& definition : explode_objects) {
        AuthoredExplodeObjectRuntime runtime_object;
        runtime_object.definition = std::move(definition);
        mission_explode_objects_.push_back(std::move(runtime_object));
    }
    authored_explode_object_snapshots_.clear();
    explosion_render_states_.clear();
    mission_cut_scene_ticks_.reserve(mission_cut_scenes_.size());
    mission_cut_scene_running_.reserve(mission_cut_scenes_.size());
    mission_cut_scene_finished_.reserve(mission_cut_scenes_.size());
    mission_status_messages_.clear();
    mission_status_messages_.reserve(status_messages.size());
    mission_status_message_slots_.fill(-1);
    displayed_mission_status_messages_.clear();
    mission_edit_variable_values_.clear();
    mission_edit_variable_values_.reserve(mission_edit_variables_.size());

    for (const AuthoredMissionAreaActivation& authored_area : area_activations) {
        const glm::mat3 orientation = BuildEngineOrientation(authored_area.orientation);
        const glm::vec3 half_dimensions = glm::abs(authored_area.dimensions) * 0.5f;
        glm::vec3 minimum(std::numeric_limits<float>::max());
        glm::vec3 maximum(std::numeric_limits<float>::lowest());

        for (int corner_index = 0; corner_index < 8; ++corner_index) {
            const glm::vec3 local_corner(
                (corner_index & 1) == 0 ? half_dimensions.x : -half_dimensions.x,
                (corner_index & 2) == 0 ? half_dimensions.y : -half_dimensions.y,
                (corner_index & 4) == 0 ? half_dimensions.z : -half_dimensions.z);
            const glm::vec3 world_corner =
                authored_area.position + orientation * local_corner;
            minimum = glm::min(minimum, world_corner);
            maximum = glm::max(maximum, world_corner);
        }

        MissionAreaActivationState area_state;
        area_state.task_id = authored_area.task_id;
        area_state.minimum = minimum;
        area_state.maximum = maximum;
        area_state.accepts_player = MissionCriteriaAcceptsPlayer(authored_area.criteria);
        mission_area_activations_.push_back(std::move(area_state));
    }

    for (const AuthoredMissionEditVariable& edit_variable : mission_edit_variables_) {
        mission_edit_variable_values_.push_back(edit_variable.initial_value);
        mission_expression_state_.SetNumber(
            "EditVariable_" + edit_variable.task_id + ".nValue",
            edit_variable.initial_value);
        mission_expression_state_.SetNumber(
            "this.nValue",
            edit_variable.initial_value);
    }

    for (const MissionAreaActivationState& area : mission_area_activations_) {
        mission_expression_state_.SetNumber(
            "AreaActivate_" + area.task_id + ".nActive",
            0.0);
    }

    for (size_t timer_index = 0;
         timer_index < mission_level_timers_.size();
         ++timer_index) {
        const AuthoredMissionLevelTimer& timer = mission_level_timers_[timer_index];
        mission_level_timer_running_[timer_index] = timer.initial_run;
        const std::string prefix = "LevelTimer_" + timer.task_id;
        mission_expression_state_.SetBoolean(
            prefix + ".isRun",
            timer.initial_run);
        mission_expression_state_.SetNumber(prefix + ".nTick", 0.0);
    }

    for (const AuthoredMissionCutScene& cut_scene : mission_cut_scenes_) {
        const int initial_tick = SecondsToSimulationTicks(
            cut_scene.start_time_seconds);
        mission_cut_scene_ticks_.push_back(initial_tick);
        mission_cut_scene_running_.push_back(cut_scene.initial_run ? 1 : 0);
        mission_cut_scene_finished_.push_back(0);
        PublishAuthoredCutSceneState(
            cut_scene,
            cut_scene.initial_run,
            false,
            initial_tick);
    }

    for (AuthoredMissionStatusMessage& status_message : status_messages) {
        if (status_message.display_text.empty()) {
            status_message.display_text = status_message.text_resource;
        }
        MissionStatusMessageRuntime runtime_message;
        runtime_message.definition = std::move(status_message);
        mission_status_messages_.push_back(std::move(runtime_message));
        PublishMissionStatusMessageState(
            mission_status_messages_.back().definition,
            false,
            -100000,
            false,
            -1,
            0);
    }

    // ConditionalContainer conditions are evaluated once after all authored
    // task variables have their initial values. This removes cut-scene-only
    // actors before AI setup and gives the first gameplay frame the same
    // registration state as the fixed-step updates that follow.
    UpdateMissionActorState();
    UpdateAuthoredConditionalContainers();

    for (const AuthoredDoorRuntime& authored_door : authored_doors_) {
        PublishAuthoredDoorState(
            authored_door.definition,
            authored_door.state,
            authored_door.is_locked,
            authored_door.is_picked);
    }
    RefreshAuthoredDoorSnapshots();
    UpdateAuthoredGuardGenerators();
}

void RuntimeWorld::SetPlayerTuning(const PlayerController::Tuning& tuning) {
    player_tuning_ = tuning;
    player_.ApplyTuning(player_tuning_);
}

void RuntimeWorld::SetPlayerWeaponCycle(const std::vector<uint32_t>& weapon_cycle) {
    player_weapon_cycle_ = weapon_cycle;
    weapons_.SetPlayerWeaponCycle(player_weapon_cycle_);
}

void RuntimeWorld::SetLadderPlacements(std::vector<LadderPlacement> ladder_placements) {
    if (ladder_traversal_.IsOnLadder()) {
        EndLadderTraversal();
    }
    ladder_placements_ = std::move(ladder_placements);
    ladder_traversal_ = LadderTraversal();
    active_ladder_index_ = -1;
    ladder_slide_velocity_ = glm::vec3(0.0f);
}

bool RuntimeWorld::TryMountNearestLadder() {
    if (!player_.IsAlive() || ladder_placements_.empty()) {
        return false;
    }

    const glm::vec3 activation_position = player_.GetEyePosition();
    const float player_yaw_degrees = player_.GetYaw();
    int nearest_ladder_index = -1;
    float nearest_distance_squared = std::numeric_limits<float>::max();
    bool nearest_ladder_is_at_top = false;

    for (size_t ladder_index = 0;
         ladder_index < ladder_placements_.size();
         ++ladder_index) {
        bool at_top = false;
        const LadderPlacement& ladder = ladder_placements_[ladder_index];
        if (!ladder.CanActivate(activation_position, player_yaw_degrees, at_top)) {
            continue;
        }

        const glm::vec3 distance = ladder.GetOrigin() - activation_position;
        const float distance_squared = glm::dot(distance, distance);
        if (nearest_ladder_index >= 0 &&
            distance_squared >= nearest_distance_squared) {
            continue;
        }

        nearest_ladder_index = static_cast<int>(ladder_index);
        nearest_distance_squared = distance_squared;
        nearest_ladder_is_at_top = at_top;
    }

    if (nearest_ladder_index < 0) {
        return false;
    }

    active_ladder_index_ = nearest_ladder_index;
    ladder_traversal_.Mount(
        ladder_placements_[static_cast<size_t>(active_ladder_index_)],
        nearest_ladder_is_at_top);
    ladder_slide_velocity_ = glm::vec3(0.0f);

    player_.SetPosition(ladder_traversal_.GetPosition());
    player_.SetOrientation(
        ladder_traversal_.GetFacingYawDegrees(),
        player_.GetPitch());
    return true;
}

bool RuntimeWorld::TickLadderTraversal(const PlayerInputCmd& input_command) {
    if (!ladder_traversal_.IsOnLadder()) {
        return false;
    }

    if (!player_.IsAlive() || active_ladder_index_ < 0 ||
        active_ladder_index_ >= static_cast<int>(ladder_placements_.size())) {
        EndLadderTraversal();
        return true;
    }

    const LadderPlacement& ladder = ladder_placements_[
        static_cast<size_t>(active_ladder_index_)];
    const bool has_root_motion = glm::dot(
        input_command.root_motion_delta,
        input_command.root_motion_delta) > 0.00000001f;
    const auto apply_root_motion = [this, &input_command]() {
        const glm::vec3 world_delta = PlayerMotion::ApplyRootMotion(
            input_command.root_motion_delta,
            ladder_traversal_.GetFacingYawDegrees(),
            input_command.root_motion_scale,
            true);
        ladder_traversal_.Move(world_delta);
    };
    const auto complete_inferred_step = [this, &ladder]() {
        const int current_step = ladder_traversal_.GetStep();
        const int next_step = current_step + ladder_traversal_.GetDirection();
        const glm::vec3 current_position = ladder.GetClimbLine()
            .PositionAtStep(current_step);
        const glm::vec3 next_position = ladder.GetClimbLine()
            .PositionAtStep(next_step);
        ladder_traversal_.Move(next_position - current_position);
        ladder_traversal_.CompleteStep();
    };

    switch (ladder_traversal_.GetPhase()) {
        case LadderTraversalPhase::Climbing: {
            if (input_command.interact) {
                ladder_traversal_.Decide(false, false, true);
                if (ladder_traversal_.GetPhase() ==
                    LadderTraversalPhase::SlidingDown) {
                    ladder_slide_velocity_ = glm::vec3(0.0f);
                }
                break;
            }

            if (ladder_traversal_.GetDirection() == 0) {
                ladder_traversal_.Decide(
                    input_command.forward > 0.01f,
                    input_command.forward < -0.01f,
                    false);
            }

            if (ladder_traversal_.GetPhase() != LadderTraversalPhase::Climbing) {
                // The animation driver evaluates input before this state
                // transition, so consume the first top-exit root-motion
                // interval in the same fixed tick that reaches the boundary.
                if (ladder_traversal_.GetPhase() ==
                        LadderTraversalPhase::GettingOffTop &&
                    has_root_motion) {
                    apply_root_motion();
                    if (input_command.ladder_top_transition_complete) {
                        ladder_traversal_.CompleteTopTransition();
                    }
                }
                break;
            }

            if (ladder_traversal_.GetDirection() != 0) {
                if (has_root_motion) {
                    apply_root_motion();
                    if (input_command.ladder_step_complete) {
                        ladder_traversal_.CompleteStep();
                    }
                } else if (input_command.ladder_step_complete) {
                    // Authored event streams may provide a completion edge
                    // before a visible translation sample reaches the seam.
                    ladder_traversal_.CompleteStep();
                } else {
                    // inferred fallback: keep Play mode usable when the
                    // selected vanilla climb clip is unavailable.
                    complete_inferred_step();
                }
            }
            break;
        }
        case LadderTraversalPhase::GettingOnTop:
        case LadderTraversalPhase::GettingOffTop: {
            if (has_root_motion) {
                apply_root_motion();
                if (input_command.ladder_top_transition_complete) {
                    ladder_traversal_.CompleteTopTransition();
                }
            } else {
                // inferred fallback: the authored top transition is an
                // animation event; without a resolved clip, preserve the
                // mount point and complete the state on the next fixed tick.
                if (ladder_traversal_.GetPhase() ==
                    LadderTraversalPhase::GettingOffTop) {
                    ladder_traversal_.Move(
                        ladder.GetTopMount() - ladder_traversal_.GetPosition());
                }
                ladder_traversal_.CompleteTopTransition();
            }
            break;
        }
        case LadderTraversalPhase::SlidingDown: {
            if (has_root_motion) {
                const glm::vec3 world_delta = PlayerMotion::ApplyRootMotion(
                    input_command.root_motion_delta,
                    ladder_traversal_.GetFacingYawDegrees(),
                    input_command.root_motion_scale,
                    true);
                ladder_traversal_.UpdateSlidePosition(
                    ladder_traversal_.GetPosition() + world_delta);
                if (input_command.ladder_slide_complete) {
                    ladder_traversal_.CompleteSlide();
                }
            } else {
                ladder_slide_velocity_ =
                    PlayerMotion::IntegrateLadderSlideVelocity(
                        ladder_slide_velocity_);
                glm::vec3 next_position = ladder_traversal_.GetPosition() +
                    ladder_slide_velocity_;
                float slide_floor_height = ladder.GetBottom().z;
                if (get_terrain_z_) {
                    slide_floor_height = std::max(
                        slide_floor_height,
                        get_terrain_z_(next_position.x, next_position.y));
                }
                if (next_position.z <= slide_floor_height) {
                    next_position.z = slide_floor_height;
                    ladder_traversal_.UpdateSlidePosition(next_position);
                    ladder_traversal_.CompleteSlide();
                } else {
                    ladder_traversal_.UpdateSlidePosition(next_position);
                }
            }
            break;
        }
        case LadderTraversalPhase::Inactive:
            break;
    }

    if (ladder_traversal_.IsOnLadder()) {
        player_.SetPosition(ladder_traversal_.GetPosition());
        player_.SetOrientation(
            ladder_traversal_.GetFacingYawDegrees(),
            player_.GetPitch());
    } else {
        EndLadderTraversal();
    }
    return true;
}

void RuntimeWorld::EndLadderTraversal() {
    if (active_ladder_index_ >= 0 &&
        active_ladder_index_ < static_cast<int>(ladder_placements_.size())) {
        player_.SetPosition(ladder_traversal_.GetPosition());
        player_.SetOrientation(
            ladder_traversal_.GetFacingYawDegrees(),
            player_.GetPitch());
    }
    active_ladder_index_ = -1;
    ladder_slide_velocity_ = glm::vec3(0.0f);
    ladder_traversal_ = LadderTraversal();
}

void RuntimeWorld::PublishAuthoredCutSceneState(
    const AuthoredMissionCutScene& definition,
    bool is_running,
    bool is_finished,
    int tick_count) {
    if (definition.task_id.empty() || definition.task_id == "-1") {
        return;
    }

    const std::string prefix = "CutScene_" + definition.task_id;
    mission_expression_state_.SetBoolean(prefix + ".isRun", is_running);
    mission_expression_state_.SetBoolean(prefix + ".isFinished", is_finished);
    mission_expression_state_.SetNumber(prefix + ".nTick", tick_count);
}

void RuntimeWorld::UpdateAuthoredCutSceneCamera(
    const AuthoredMissionCutScene& definition,
    int tick_count) {
    if (definition.camera_shots.empty()) {
        return;
    }

    const double current_time_seconds = static_cast<double>(tick_count) /
        static_cast<double>(GameClock::TICK_RATE_HZ);
    const double time_scale = PositiveFiniteSeconds(definition.time_scale);
    double shot_start_seconds = 0.0;

    for (size_t shot_index = 0;
         shot_index < definition.camera_shots.size();
         ++shot_index) {
        const AuthoredMissionCutSceneShot& shot =
            definition.camera_shots[shot_index];
        const double shot_duration_seconds =
            PositiveFiniteSeconds(shot.duration_seconds) * time_scale;
        const bool is_active_shot = current_time_seconds <
                shot_start_seconds + shot_duration_seconds ||
            shot_index + 1 == definition.camera_shots.size();
        if (!is_active_shot) {
            shot_start_seconds += shot_duration_seconds;
            continue;
        }

        const double shot_progress = shot_duration_seconds > 0.0
            ? (current_time_seconds - shot_start_seconds) / shot_duration_seconds
            : 0.0;
        const float interpolation = static_cast<float>(std::clamp(
            shot_progress,
            0.0,
            1.0));
        const glm::mat3 active_orientation = BuildEngineOrientation(
            shot.orientation);
        glm::vec3 position = shot.position;
        glm::mat3 orientation = active_orientation;
        float field_of_view = shot.field_of_view_radians;

        if (shot.smooth_to_next && shot_index + 1 < definition.camera_shots.size()) {
            const AuthoredMissionCutSceneShot& next_shot =
                definition.camera_shots[shot_index + 1];
            position = glm::mix(shot.position, next_shot.position, interpolation);
            const glm::mat3 next_orientation = BuildEngineOrientation(
                next_shot.orientation);
            const glm::vec3 forward = NormalizeOrFallback(
                glm::mix(active_orientation[1], next_orientation[1], interpolation),
                active_orientation[1]);
            glm::vec3 up = NormalizeOrFallback(
                glm::mix(active_orientation[2], next_orientation[2], interpolation),
                active_orientation[2]);
            const glm::vec3 right = NormalizeOrFallback(
                glm::cross(forward, up),
                active_orientation[0]);
            up = NormalizeOrFallback(glm::cross(right, forward), up);
            orientation[0] = right;
            orientation[1] = forward;
            orientation[2] = up;
            field_of_view = glm::mix(
                shot.field_of_view_radians,
                next_shot.field_of_view_radians,
                interpolation);
        }

        active_cut_scene_camera_.active = true;
        active_cut_scene_camera_.position = position;
        active_cut_scene_camera_.right = orientation[0];
        active_cut_scene_camera_.forward = orientation[1];
        active_cut_scene_camera_.up = orientation[2];
        active_cut_scene_camera_.field_of_view_y_radians =
            std::isfinite(field_of_view) && field_of_view > 0.0f
            ? field_of_view
            : 1.0f;
        active_cut_scene_camera_.viewport_height_factor =
            std::isfinite(definition.viewport_height_factor)
            ? std::max(0.0f, definition.viewport_height_factor)
            : 1.0f;
        active_cut_scene_camera_.time_of_day =
            std::isfinite(definition.time_of_day)
            ? definition.time_of_day
            : -1.0f;
        active_cut_scene_camera_.shot_index = static_cast<int>(shot_index);
        return;
    }
}

void RuntimeWorld::UpdateAuthoredCutScenes() {
    active_cut_scene_camera_ = RuntimeCutSceneCamera();

    const auto evaluate_condition = [this](const std::string& expression) {
        bool result = false;
        return !expression.empty() &&
            mission_expression_state_.TryEvaluate(expression, result) && result;
    };

    for (size_t scene_index = 0;
         scene_index < mission_cut_scenes_.size();
         ++scene_index) {
        const AuthoredMissionCutScene& definition = mission_cut_scenes_[scene_index];
        int& tick_count = mission_cut_scene_ticks_[scene_index];
        bool is_running = mission_cut_scene_running_[scene_index] != 0;
        bool is_finished = mission_cut_scene_finished_[scene_index] != 0;

        if (evaluate_condition(definition.reset_expression)) {
            tick_count = 0;
            is_finished = false;
        }

        is_running = definition.run_expression.empty()
            ? definition.initial_run && !is_finished
            : evaluate_condition(definition.run_expression);

        if (is_running) {
            const float scaled_duration = definition.duration_seconds *
                std::max(0.0f, definition.time_scale);
            if (static_cast<double>(tick_count) /
                    static_cast<double>(GameClock::TICK_RATE_HZ) >
                static_cast<double>(scaled_duration)) {
                tick_count = std::max(
                    0,
                    SecondsToSimulationTicks(scaled_duration));
                is_finished = true;
            }

            int tick_delta = 1;
            if (!definition.time_delta_expression.empty()) {
                double authored_delta_seconds = 0.0;
                if (mission_expression_state_.TryEvaluateNumber(
                        definition.time_delta_expression,
                        authored_delta_seconds)) {
                    const double authored_delta_ticks = authored_delta_seconds *
                        static_cast<double>(GameClock::TICK_RATE_HZ);
                    if (std::isfinite(authored_delta_ticks)) {
                        if (authored_delta_ticks >=
                            static_cast<double>(std::numeric_limits<int>::max())) {
                            tick_delta = std::numeric_limits<int>::max();
                        } else if (authored_delta_ticks > 0.0) {
                            tick_delta = std::max(
                                1,
                                static_cast<int>(authored_delta_ticks));
                        }
                    }
                }
            }
            if (tick_count > std::numeric_limits<int>::max() - tick_delta) {
                tick_count = std::numeric_limits<int>::max();
            } else {
                tick_count += tick_delta;
            }
        }

        mission_cut_scene_running_[scene_index] = is_running ? 1 : 0;
        mission_cut_scene_finished_[scene_index] = is_finished ? 1 : 0;
        if (is_running) {
            UpdateAuthoredCutSceneCamera(definition, tick_count);
        }
        PublishAuthoredCutSceneState(
            definition,
            is_running,
            is_finished,
            tick_count);
    }
}

void RuntimeWorld::PublishAuthoredConditionalContainerState(
    const AuthoredMissionConditionalContainer& definition,
    bool is_running) {
    if (definition.task_id.empty() || definition.task_id == "-1") {
        return;
    }

    mission_expression_state_.SetBoolean(
        "ConditionalContainer_" + definition.task_id + ".isRun",
        is_running);
}

void RuntimeWorld::RefreshAuthoredConditionalContainerSnapshots() {
    authored_conditional_container_snapshots_.clear();
    authored_conditional_container_snapshots_.reserve(
        mission_conditional_containers_.size());

    for (const AuthoredConditionalContainerRuntime& runtime_container :
         mission_conditional_containers_) {
        RuntimeConditionalContainerSnapshot snapshot;
        snapshot.object_index = runtime_container.definition.object_index;
        snapshot.task_id = runtime_container.definition.task_id;
        snapshot.is_running = runtime_container.is_running;
        snapshot.descendant_object_indices =
            runtime_container.definition.descendant_object_indices;
        authored_conditional_container_snapshots_.push_back(std::move(snapshot));
    }
}

void RuntimeWorld::UpdateAuthoredConditionalContainers() {
    // Publish the previous latch before resolving the next one. This gives
    // expressions using `this.isRun` the same self-reference semantics as the
    // vanilla task variable, while cross-container references resolve in the
    // authored task order supplied by the editor snapshot.
    for (const AuthoredConditionalContainerRuntime& runtime_container :
         mission_conditional_containers_) {
        PublishAuthoredConditionalContainerState(
            runtime_container.definition,
            runtime_container.is_running);
    }

    for (AuthoredConditionalContainerRuntime& runtime_container :
         mission_conditional_containers_) {
        const AuthoredMissionConditionalContainer& definition =
            runtime_container.definition;
        mission_expression_state_.SetBoolean(
            "this.isRun",
            runtime_container.is_running);

        bool next_is_running = true;
        if (!definition.condition_expression.empty()) {
            bool condition_result = false;
            next_is_running = mission_expression_state_.TryEvaluate(
                definition.condition_expression,
                condition_result) && condition_result;
        }

        runtime_container.is_running = next_is_running;
        PublishAuthoredConditionalContainerState(
            definition,
            runtime_container.is_running);

        // Run-at-start/stop expressions are retained in the authored
        // definition for diagnostics, but the editor's mission bridge does
        // not execute arbitrary QVM/native statements from this state-only
        // path. Visibility remains deterministic and fail-closed for an
        // unsupported condition rather than leaking cut-scene actors.
    }

    RefreshAuthoredConditionalContainerSnapshots();
}

void RuntimeWorld::RefreshAuthoredGuardGeneratorStates() {
    UpdateAuthoredGuardGenerators();
}

void RuntimeWorld::UpdateAuthoredGuardGenerators() {
    // Every guard starts enabled for this evaluation. A guard referenced by
    // multiple authored generators is enabled only when all of its generator
    // gates allow it; this prevents object order from changing the result.
    for (AiGuardEntity& guard : ai_.GetGuards()) {
        guard.runtime_enabled = true;
    }

    std::unordered_map<uint32_t, bool> guard_enablement;
    for (AuthoredGuardGeneratorRuntime& runtime_generator :
         mission_guard_generators_) {
        const AuthoredMissionGuardGenerator& definition =
            runtime_generator.definition;
        bool condition_result = false;
        runtime_generator.is_on = !definition.condition_expression.empty() &&
            mission_expression_state_.TryEvaluate(
                definition.condition_expression,
                condition_result) &&
            condition_result;

        const size_t maximum_spawn_count = runtime_generator.is_on
            ? std::min(
                definition.guard_object_indices.size(),
                static_cast<size_t>(std::max(0, definition.maximum_spawns)))
            : 0U;
        for (size_t guard_index = 0;
             guard_index < definition.guard_object_indices.size();
             ++guard_index) {
            const int object_index = definition.guard_object_indices[guard_index];
            if (object_index < 0) {
                continue;
            }

            const bool should_enable = guard_index < maximum_spawn_count;
            const uint32_t guard_id = static_cast<uint32_t>(object_index);
            const auto [iterator, inserted] = guard_enablement.emplace(
                guard_id,
                should_enable);
            if (!inserted) {
                iterator->second = iterator->second && should_enable;
            }
        }

        const std::string expression_prefix =
            "GuardGenerator_" + GuardGeneratorIdentity(definition);
        mission_expression_state_.SetBoolean(
            expression_prefix + ".isOn",
            runtime_generator.is_on);
        mission_expression_state_.SetNumber(
            expression_prefix + ".nMaxSpawns",
            definition.maximum_spawns);
    }

    for (const auto& [guard_id, should_enable] : guard_enablement) {
        AiGuardEntity* guard = ai_.FindGuard(guard_id);
        if (guard != nullptr) {
            guard->runtime_enabled = should_enable;
        }
    }

    RefreshAuthoredGuardGeneratorSnapshots();
}

void RuntimeWorld::RefreshAuthoredGuardGeneratorSnapshots() {
    authored_guard_generator_snapshots_.clear();
    authored_guard_generator_snapshots_.reserve(
        mission_guard_generators_.size());

    for (const AuthoredGuardGeneratorRuntime& runtime_generator :
         mission_guard_generators_) {
        RuntimeGuardGeneratorSnapshot snapshot;
        snapshot.object_index = runtime_generator.definition.object_index;
        snapshot.task_id = runtime_generator.definition.task_id;
        snapshot.is_on = runtime_generator.is_on;
        snapshot.maximum_spawns = runtime_generator.definition.maximum_spawns;
        snapshot.guard_object_indices =
            runtime_generator.definition.guard_object_indices;
        const size_t active_guard_count = runtime_generator.is_on
            ? std::min(
                snapshot.guard_object_indices.size(),
                static_cast<size_t>(std::max(
                    0,
                    snapshot.maximum_spawns)))
            : 0U;
        snapshot.active_guard_object_indices.assign(
            snapshot.guard_object_indices.begin(),
            snapshot.guard_object_indices.begin() + active_guard_count);
        authored_guard_generator_snapshots_.push_back(std::move(snapshot));
    }
}

void RuntimeWorld::UpdateAuthoredConditionalSounds() {
    const auto evaluate_condition = [this](const std::string& expression) {
        bool result = false;
        return mission_expression_state_.TryEvaluate(expression, result) && result;
    };

    for (AuthoredConditionalSoundRuntime& runtime_sound : mission_conditional_sounds_) {
        const bool is_running = evaluate_condition(
            runtime_sound.definition.condition_expression);
        if (is_running && !runtime_sound.is_running &&
            (!runtime_sound.definition.one_shot || !runtime_sound.has_played)) {
            runtime_sound.has_played = true;
            if (runtime_sound.definition.simple) {
                QueueOneShotAudio(
                    SoundEffect::ObjectiveComplete,
                    runtime_sound.definition.sound_name);
            } else {
                QueueLoopStart(
                    runtime_sound.definition.task_id,
                    runtime_sound.definition.sound_name,
                    SoundEffect::ObjectiveComplete,
                    runtime_sound.definition.position,
                    runtime_sound.definition.relative_to_microphone);
            }
            if (mission_sound_event_handler_) {
                mission_sound_event_handler_({
                    runtime_sound.definition.task_id,
                    runtime_sound.definition.sound_name,
                    runtime_sound.definition.position,
                    true,
                    runtime_sound.definition.simple,
                    runtime_sound.definition.relative_to_microphone,
                });
            }
        } else if (!is_running && runtime_sound.is_running &&
                   !runtime_sound.definition.simple &&
                   runtime_sound.has_played) {
            QueueLoopStop(runtime_sound.definition.task_id);
            if (mission_sound_event_handler_) {
                mission_sound_event_handler_({
                    runtime_sound.definition.task_id,
                    runtime_sound.definition.sound_name,
                    runtime_sound.definition.position,
                    false,
                    runtime_sound.definition.simple,
                    runtime_sound.definition.relative_to_microphone,
                });
            }
        }
        runtime_sound.is_running = is_running;
    }
}

void RuntimeWorld::TriggerAuthoredExplodeObject(
    AuthoredExplodeObjectRuntime& runtime_object) {
    runtime_object.delay_pending = false;
    runtime_object.delay_ticks_remaining = 0;
    runtime_object.is_exploded = true;

    const std::string object_identity = runtime_object.definition.task_id == "-1"
        ? std::to_string(runtime_object.definition.object_index)
        : runtime_object.definition.task_id;
    mission_expression_state_.SetBoolean(
        "ExplodeObject_" + object_identity + ".isExploded",
        true);

    if (runtime_object.definition.explosion_sound.empty()) {
        QueueOneShotAudio(SoundEffect::Explosion);
    } else {
        QueueOneShotAudio(
            SoundEffect::Explosion,
            runtime_object.definition.explosion_sound);
    }
    QueueExplosionRenderState(
        runtime_object.definition.position,
        runtime_object.definition.explosion_radius_meters *
            PlayerController::WORLD_METER,
        false);

    // Authored object damage uses the player health scale as the C++ runtime's
    // baseline. The two authored multipliers remain independent so scripted
    // visual explosions can set Explosion damage scale to zero.
    const float authored_damage_factor = runtime_object.definition.damage_scale *
        runtime_object.definition.explosion_damage_scale;
    ApplyExplosionDamage(
        runtime_object.definition.position,
        runtime_object.definition.explosion_radius_meters *
            PlayerController::WORLD_METER,
        runtime_object.definition.explosion_falloff_radius_meters *
            PlayerController::WORLD_METER,
        player_.GetMaximumHealth(),
        authored_damage_factor,
        0);
}

void RuntimeWorld::UpdateAuthoredExplodeObjects() {
    const auto evaluate_condition = [this](const std::string& expression) {
        bool result = false;
        return !expression.empty() &&
            mission_expression_state_.TryEvaluate(expression, result) && result;
    };

    for (AuthoredExplodeObjectRuntime& runtime_object : mission_explode_objects_) {
        const bool is_condition_active = evaluate_condition(
            runtime_object.definition.explosion_expression);
        const bool condition_started = is_condition_active &&
            !runtime_object.condition_active;

        if (runtime_object.delay_pending) {
            if (!is_condition_active) {
                runtime_object.delay_pending = false;
                runtime_object.delay_ticks_remaining = 0;
            } else if (runtime_object.delay_ticks_remaining > 0) {
                --runtime_object.delay_ticks_remaining;
                if (runtime_object.delay_ticks_remaining == 0) {
                    TriggerAuthoredExplodeObject(runtime_object);
                }
            }
        } else if (condition_started && !runtime_object.is_exploded) {
            const int delay_ticks = SecondsToSimulationTicks(
                runtime_object.definition.explosion_delay_seconds);
            if (delay_ticks == 0) {
                TriggerAuthoredExplodeObject(runtime_object);
            } else {
                runtime_object.delay_pending = true;
                runtime_object.delay_ticks_remaining = delay_ticks;
            }
        }

        runtime_object.condition_active = is_condition_active;
        const std::string object_identity = runtime_object.definition.task_id == "-1"
            ? std::to_string(runtime_object.definition.object_index)
            : runtime_object.definition.task_id;
        mission_expression_state_.SetBoolean(
            "ExplodeObject_" + object_identity + ".isExploded",
            runtime_object.is_exploded);
    }
    RefreshAuthoredExplodeObjectSnapshots();
}

void RuntimeWorld::RefreshAuthoredExplodeObjectSnapshots() {
    authored_explode_object_snapshots_.clear();
    authored_explode_object_snapshots_.reserve(mission_explode_objects_.size());
    for (const AuthoredExplodeObjectRuntime& runtime_object :
         mission_explode_objects_) {
        authored_explode_object_snapshots_.push_back({
            runtime_object.definition.object_index,
            runtime_object.definition.task_id,
            runtime_object.definition.destroyed_model_name,
            runtime_object.is_exploded,
        });
    }
}

void RuntimeWorld::SetInteractionQuery(InteractionQuery interaction_query) {
    interaction_query_ = std::move(interaction_query);
}

void RuntimeWorld::UpdateSimulationTick(uint64_t tick_number, const PlayerInputCmd& input_cmd) {
    AdvanceExplosionRenderStates();
    AdvanceGuardMuzzleFlashStates();
    constexpr double dt = GameClock::TICK_INTERVAL_SECONDS;

    // The map computer is a player weapon state in vanilla. Once it is open,
    // retain only simulation inputs that belong to the map-computer toggle;
    // movement, look, traversal, and interaction input must not leak into the
    // player controller or authored interaction bridge.
    PlayerInputCmd player_input = input_cmd;
    if (map_computer_open_) {
        UpdateMapComputerViewport(input_cmd);
        player_input.forward = 0.0f;
        player_input.strafe = 0.0f;
        player_input.yaw_delta = 0.0f;
        player_input.pitch_delta = 0.0f;
        player_input.jump = false;
        player_input.crouch = false;
        player_input.sprint = false;
        player_input.interact = false;
        player_input.root_motion_delta = glm::vec3(0.0f);
        player_input.ladder_step_complete = false;
        player_input.ladder_top_transition_complete = false;
        player_input.ladder_slide_complete = false;
    }

    muzzle_flash_strength_ = std::max(
        0.0f,
        muzzle_flash_strength_ - kTransientPresentationStrengthDecayPerTick);
    player_damage_effect_strength_ = std::max(
        0.0f,
        player_damage_effect_strength_ - kTransientPresentationStrengthDecayPerTick);
    weapon_view_recoil_.Advance();

    if (flash_effect_remaining_seconds_ > 0.0f) {
        flash_effect_remaining_seconds_ = std::max(
            0.0f,
            flash_effect_remaining_seconds_ - static_cast<float>(dt));
        flash_effect_strength_ = std::max(
            0.0f,
            flash_effect_strength_ - flash_effect_decay_per_second_ *
                static_cast<float>(dt));
        if (flash_effect_remaining_seconds_ <= 0.0f) {
            flash_effect_strength_ = 0.0f;
            flash_effect_decay_per_second_ = 0.0f;
        }
    }

    // Live human blockers for vanilla separation (A11): bodies block moves by
    // rejection, not push-out. Static geometry still uses the obstacle sweep.
    std::vector<glm::vec3> human_blockers;
    human_blockers.reserve(ai_.GetGuards().size());
    for (const auto& guard : ai_.GetGuards()) {
        if (guard.state == AiGuardState::Dead) {
            continue;
        }
        human_blockers.push_back(guard.position);
    }

    // 1. Tick player physics, obstacle & 3D building collision, and movement.
    // Ladder traversal owns the player transform while its authored or
    // inferred fixed-step state machine is active.
    const bool was_grounded = player_.IsGrounded();
    bool ladder_tick_handled = ladder_traversal_.IsOnLadder() &&
        TickLadderTraversal(player_input);
    if (!ladder_tick_handled && player_input.interact) {
        ladder_tick_handled = TryMountNearestLadder();
    }

    if (!ladder_tick_handled) {
        player_.Tick(player_input, get_terrain_z_, {}, check_collision_);
        const PlayerFallImpact& landing_impact = player_.GetLastLandingImpact();
        if (landing_impact.hearing_radius_units > 0.0f) {
            AiStimulusEvent ground_impact;
            ground_impact.type = AiEventType::GroundImpact;
            ground_impact.position = player_.GetPosition();
            ground_impact.hearing_radius_units = landing_impact.hearing_radius_units;
            ground_impact.originator_id = 0;
            ground_impact.tick_timestamp = tick_number;
            ai_.GetEventQueue().Post(ground_impact);
        }
        if (landing_impact.damage > 0.0f) {
            player_damage_effect_strength_ = 1.0f;
            QueueOneShotAudio(SoundEffect::Pain, landing_impact.sound_name);
        }
        if (player_input.jump && was_grounded && !player_.IsGrounded()) {
            QueueOneShotAudio(SoundEffect::Jump);
        }
        PlayFootstepIfNeeded(player_input, was_grounded);
    }

    // OpenIGI identifies MapComputer as a grounded rising-edge weapon action.
    // Evaluate the edge after the first terrain tick so a freshly reset player
    // can open the computer on the first playable frame.
    const bool map_computer_rising = input_cmd.map_computer &&
        !map_computer_input_was_held_;
    map_computer_input_was_held_ = input_cmd.map_computer;
    if (map_computer_rising) {
        if (map_computer_open_) {
            map_computer_open_ = false;
        } else if (player_.IsGrounded() && player_.IsAlive()) {
            map_computer_center_ = glm::vec2(
                player_.GetPosition().x,
                player_.GetPosition().y);
            map_computer_zoom_level_ = 1;
            map_computer_open_ = true;
        }
    }
    zoom_active_ = input_cmd.zoom && !map_computer_open_;

    // Vanilla updates AreaActivate and EditVariable before dependent mission
    // expressions are evaluated at the end of this fixed simulation tick.
    UpdateAuthoredMissionState();
    UpdateAuthoredCutScenes();
    UpdateAuthoredConditionalSounds();
    UpdateAuthoredExplodeObjects();
    UpdateAuthoredDoors();
    UpdateMissionActorState();
    UpdateAuthoredGuardGenerators();

    // 2. Weapon switching, firing & cooldowns. The vanilla first-person rig
    // lowers before the active weapon changes and raises after the new model
    // is selected; keep those transition ticks out of the fire/reload path.
	const bool weapon_controls_ready = !map_computer_open_ &&
		UpdateWeaponSelection(input_cmd);
	// Weapon presentation/fireplay is disabled by product decision: play mode
	// is the engine/interaction slice. The weapon system still ticks its
	// selection state machine but never fires, zooms, or reloads.
	weapons_.Update(dt, false);
    const WeaponDefinition& active_weapon = weapons_.GetActiveWeapon();
    const bool is_projectile_weapon =
        active_weapon.projectile_type != ProjectileType::None;
    if (weapon_controls_ready &&
        input_cmd.fire && (!is_projectile_weapon || !fire_was_held_)) {
        float yaw_rad = glm::radians(player_.GetYaw());
        float pitch_rad = glm::radians(player_.GetPitch());
        float sin_y = std::sin(yaw_rad);
        float cos_y = std::cos(yaw_rad);
        float sin_p = std::sin(pitch_rad);
        float cos_p = std::cos(pitch_rad);
        glm::vec3 aim_dir(-sin_y * cos_p, cos_y * cos_p, sin_p);

        const auto apply_recoil = [this]() {
            player_.SetOrientation(
                player_.GetYaw() + weapons_.GetLastRecoilYawDegrees(),
                std::clamp(
                    player_.GetPitch() + weapons_.GetLastRecoilPitchDegrees(),
                    -89.0f,
                    89.0f));
        };
        if (is_projectile_weapon) {
            BulletTrace fired_trace;
            if (weapons_.TryFire(
                    player_.GetEyePosition(),
                    aim_dir,
                    fired_trace)) {
                apply_recoil();
                LaunchPlayerProjectile(
                    player_.GetEyePosition(),
                    fired_trace.direction,
                    active_weapon);
                QueueOneShotAudio(
                    SoundEffect::ProjectileLaunch,
                    active_weapon.fire_sound);
            }
        } else {
            std::vector<BulletTrace> traces;
            if (weapons_.TryFire(
                    player_.GetEyePosition(),
                    aim_dir,
                    traces)) {
                apply_recoil();
                if (WeaponProducesMuzzleFlash(active_weapon)) {
                    muzzle_flash_strength_ = 1.0f;
                    weapon_view_recoil_.TriggerDegrees(
                        weapons_.GetLastRecoilPitchDegrees(),
                        weapons_.GetLastRecoilYawDegrees());
                }
                bool hit_guard = false;
                bool hit_world_geometry = false;
                for (BulletTrace& trace : traces) {
                    hit_guard = ApplyPlayerShotDamage(trace) || hit_guard;
                    hit_world_geometry = trace.hit_world_geometry || hit_world_geometry;
                }
                QueueOneShotAudio(
                    SoundEffect::Gunshot,
                    active_weapon.fire_sound);
                if (hit_guard || hit_world_geometry) {
                    QueueOneShotAudio(SoundEffect::BulletImpact);
                }
                // Post gunshot stimulus to AI
                AiStimulusEvent gunshot;
                gunshot.type = AiEventType::Gunshot;
                gunshot.position = player_.GetPosition();
                gunshot.loudness = 1.0f;
                gunshot.originator_id = 0; // Player ID
                gunshot.tick_timestamp = tick_number;
                ai_.GetEventQueue().Post(gunshot);
            }
        }
    }
    fire_was_held_ = weapon_controls_ready && input_cmd.fire;

    if (weapon_controls_ready && input_cmd.reload) {
        weapons_.Reload();
        QueueOneShotAudio(SoundEffect::Reload);
    }

    if (player_input.interact && !ladder_tick_handled) {
        RuntimeInteractionResult interaction_result;
        if (interaction_query_) {
            const float yaw_radians = glm::radians(player_.GetYaw());
            const float pitch_radians = glm::radians(player_.GetPitch());
            const float cosine_pitch = std::cos(pitch_radians);
            interaction_result = interaction_query_(
                player_.GetEyePosition(),
                glm::vec3(
                    -std::sin(yaw_radians) * cosine_pitch,
                    std::cos(yaw_radians) * cosine_pitch,
                    std::sin(pitch_radians)));
        }

        const bool objective_completed = interaction_query_
            ? (interaction_result.completed_objective &&
                level_flow_.CompleteFirstPendingPrimaryObjective())
            : level_flow_.CompleteFirstPendingPrimaryObjective();
        if (objective_completed) {
            QueueOneShotAudio(SoundEffect::ObjectiveComplete);
        }
    }

    // 3. Tick AI perception & state machine
    ai_.Update(
        dt,
        player_.GetPosition(),
        player_.GetEyePosition(),
        player_.IsAlive());
    DispatchGuardScripts();
    ApplyGuardCombatDamage(tick_number);
    projectiles_.Tick();
    ApplyProjectileDetonations();
    UpdateMissionActorState();
    UpdateAuthoredConditionalContainers();

    // 4. Tick runtime task tree
    task_tree_.Update(dt);

    // 5. Status messages run after live actor/task state and before LevelFlow;
    // authored completion/failure expressions can therefore observe a message
    // sent by this same fixed tick.
    UpdateAuthoredMissionStatusMessages(tick_number);

    // 6. Evaluate mission flow & objectives
    bool in_extraction = (glm::distance(
        player_.GetPosition(),
        glm::vec3(
            extraction_zone_center_.x,
            extraction_zone_center_.y,
            player_.GetPosition().z)) < extraction_zone_radius_);
    level_flow_.Update(
        player_.IsAlive(),
        in_extraction,
        [this](const std::string& expression) {
            bool result = false;
            return mission_expression_state_.TryEvaluate(expression, result) && result;
        });

    // Pulse fields are observable for the entire fixed tick, including the
    // mission-flow evaluation above, and clear before the next tick begins.
    for (const std::string& pulse_name : mission_state_pulse_names_) {
        mission_expression_state_.SetBoolean(pulse_name, false);
    }
}

void RuntimeWorld::DispatchGuardScripts() {
    // Iterate in the same stable order in which guards were registered. The
    // script registry carries the current guard binding, so hash-map order
    // must never become part of the simulation result.
    for (AiGuardEntity& guard : ai_.GetGuards()) {
        if (!guard.runtime_enabled) {
            continue;
        }
        const auto script_iterator = guard_scripts_.find(guard.id);
        if (script_iterator == guard_scripts_.end()) {
            continue;
        }
        GuardScriptState& script = script_iterator->second;
        if (script.faulted) {
            continue;
        }

        int32_t event_type = 4; // AIEVENT_IDLE
        if (!script.dispatched_create) {
            event_type = 0; // AIEVENT_CREATE
            script.dispatched_create = true;
        } else if (guard.state == AiGuardState::Dead) {
            event_type = 2; // AIEVENT_DEAD
        } else if (guard.state == AiGuardState::Combat) {
            event_type = 7; // AIEVENT_COMBAT
        } else if (guard.state == AiGuardState::Suspicious) {
            event_type = 5; // AIEVENT_ALERT
        }

        if (!ai_script_host_.Run(script.program, guard, event_type)) {
            script.faulted = true;
            script.last_error = ai_script_host_.GetLastError();
            continue;
        }

        ApplyScriptPatrolRoute(guard);
    }
}

void RuntimeWorld::ApplyScriptPatrolRoute(AiGuardEntity& guard) const {
    if (guard.script_patrol_path_id < 0
        || guard.script_patrol_path_id == guard.active_patrol_path_id) {
        return;
    }

    const auto route = guard.patrol_routes.find(guard.script_patrol_path_id);
    if (route == guard.patrol_routes.end()) {
        return;
    }

    guard.patrol_commands = route->second;
    guard.active_patrol_path_id = guard.script_patrol_path_id;
    guard.command_index = -1;
    guard.loop_start_index = -1;
    guard.last_move_index = -1;
    guard.prev_move_index = -1;
    guard.end_index = -1;
    guard.deadline_tick = -1;
    guard.patrol_started = false;
    guard.patrol_stopped = false;
}

bool RuntimeWorld::ApplyPlayerShotDamage(BulletTrace& bullet_trace) {
    constexpr float guard_aim_height = 1.7f * PlayerController::WORLD_METER;
    constexpr float guard_hit_radius = 0.6f * PlayerController::WORLD_METER;

    const float direction_length = glm::length(bullet_trace.direction);
    if (direction_length <= 0.0001f) {
        return false;
    }

    const glm::vec3 shot_direction = bullet_trace.direction / direction_length;
    float closest_hit_distance = bullet_trace.distance;
    uint32_t closest_guard_id = 0;
    glm::vec3 closest_hit_position = bullet_trace.hit_position;
    bool found_guard = false;

    float world_impact_distance = bullet_trace.distance;
    if (FindWorldShotImpact(bullet_trace, world_impact_distance)) {
        closest_hit_distance = world_impact_distance;
        bullet_trace.distance = world_impact_distance;
        bullet_trace.hit_position = bullet_trace.origin + shot_direction * world_impact_distance;
        bullet_trace.hit_world_geometry = true;
    }

    if (ApplyPlayerExplodeObjectDamage(bullet_trace)) {
        return true;
    }

    for (const AiGuardEntity& guard : ai_.GetGuards()) {
        if (!guard.runtime_enabled || guard.state == AiGuardState::Dead) {
            continue;
        }

        const glm::vec3 guard_aim_position = guard.position + glm::vec3(
            0.0f, 0.0f, guard_aim_height);
        const glm::vec3 to_guard = guard_aim_position - bullet_trace.origin;
        const float projected_distance = glm::dot(to_guard, shot_direction);
        if (projected_distance < 0.0f || projected_distance > closest_hit_distance) {
            continue;
        }

        const glm::vec3 closest_point = bullet_trace.origin + shot_direction * projected_distance;
        const float miss_distance = glm::distance(closest_point, guard_aim_position);
        if (miss_distance > guard_hit_radius) {
            continue;
        }

        closest_hit_distance = projected_distance;
        closest_guard_id = guard.id;
        closest_hit_position = closest_point;
        found_guard = true;
    }

    if (!found_guard) {
        return false;
    }

    bullet_trace.hit_entity_id = closest_guard_id;
    bullet_trace.distance = closest_hit_distance;
    bullet_trace.hit_position = closest_hit_position;
    ai_.ApplyDamage(closest_guard_id, bullet_trace.damage);
    return true;
}

bool RuntimeWorld::ApplyPlayerExplodeObjectDamage(BulletTrace& bullet_trace) {
    constexpr float minimum_object_radius_units = 0.25f *
        PlayerController::WORLD_METER;
    const float direction_length = glm::length(bullet_trace.direction);
    if (direction_length <= 0.0001f) {
        return false;
    }

    const glm::vec3 shot_direction = bullet_trace.direction / direction_length;
    float closest_hit_distance = bullet_trace.distance;
    AuthoredExplodeObjectRuntime* closest_object = nullptr;

    for (AuthoredExplodeObjectRuntime& runtime_object : mission_explode_objects_) {
        if (runtime_object.is_exploded) {
            continue;
        }

        const glm::vec3 to_object = runtime_object.definition.position -
            bullet_trace.origin;
        const float projected_distance = glm::dot(to_object, shot_direction);
        if (projected_distance < 0.0f || projected_distance > closest_hit_distance) {
            continue;
        }

        const glm::vec3 closest_point = bullet_trace.origin +
            shot_direction * projected_distance;
        const float object_radius_units = std::max(
            minimum_object_radius_units,
            runtime_object.definition.explosion_radius_meters *
                PlayerController::WORLD_METER * 0.5f);
        if (glm::distance(closest_point, runtime_object.definition.position) >
            object_radius_units) {
            continue;
        }

        closest_hit_distance = projected_distance;
        closest_object = &runtime_object;
        bullet_trace.hit_position = closest_point;
    }

    if (closest_object == nullptr) {
        return false;
    }

    bullet_trace.distance = closest_hit_distance;
    bullet_trace.hit_world_geometry = true;
    TriggerAuthoredExplodeObject(*closest_object);
    RefreshAuthoredExplodeObjectSnapshots();
    return true;
}

bool RuntimeWorld::FindWorldShotImpact(
    const BulletTrace& bullet_trace,
    float& impact_distance) const {
    if (!check_collision_ || bullet_trace.distance <= 0.0f) {
        return false;
    }

    constexpr int sample_count = 512;
    constexpr int refinement_iterations = 12;
    const float direction_length = glm::length(bullet_trace.direction);
    if (direction_length <= 0.0001f) {
        return false;
    }
    const glm::vec3 shot_direction = bullet_trace.direction / direction_length;

    const auto is_solid = [this, &bullet_trace, &shot_direction](float distance) {
        const glm::vec3 sample_position =
            bullet_trace.origin + shot_direction * distance;
        return check_collision_(sample_position.x, sample_position.y, sample_position.z);
    };

    float previous_distance = 0.0f;
    if (is_solid(previous_distance)) {
        impact_distance = 0.0f;
        return true;
    }

    for (int sample_index = 1; sample_index <= sample_count; ++sample_index) {
        const float sample_fraction = static_cast<float>(sample_index) /
            static_cast<float>(sample_count);
        const float current_distance = bullet_trace.distance * sample_fraction;
        if (!is_solid(current_distance)) {
            previous_distance = current_distance;
            continue;
        }

        float lower_distance = previous_distance;
        float upper_distance = current_distance;
        for (int refinement_index = 0;
             refinement_index < refinement_iterations;
             ++refinement_index) {
            const float middle_distance = (lower_distance + upper_distance) * 0.5f;
            if (is_solid(middle_distance)) {
                upper_distance = middle_distance;
            } else {
                lower_distance = middle_distance;
            }
        }
        impact_distance = upper_distance;
        return true;
    }

    return false;
}

bool RuntimeWorld::ApplyGuardShotDamage(BulletTrace& bullet_trace) {
    constexpr float player_hit_radius = 0.6f * PlayerController::WORLD_METER;
    const float direction_length = glm::length(bullet_trace.direction);
    if (direction_length <= 0.0001f) {
        return false;
    }

    const glm::vec3 shot_direction = bullet_trace.direction / direction_length;
    float world_impact_distance = bullet_trace.distance;
    if (FindWorldShotImpact(bullet_trace, world_impact_distance)) {
        bullet_trace.distance = world_impact_distance;
        bullet_trace.hit_position = bullet_trace.origin +
            shot_direction * world_impact_distance;
        bullet_trace.hit_world_geometry = true;
    }

    const glm::vec3 to_player = player_.GetEyePosition() - bullet_trace.origin;
    const float projected_distance = glm::dot(to_player, shot_direction);
    if (projected_distance < 0.0f || projected_distance > bullet_trace.distance) {
        return false;
    }

    const glm::vec3 closest_point = bullet_trace.origin +
        shot_direction * projected_distance;
    if (glm::distance(closest_point, player_.GetEyePosition()) > player_hit_radius) {
        return false;
    }

    bullet_trace.hit_entity_id = 0;
    bullet_trace.hit_position = closest_point;
    bullet_trace.distance = projected_distance;
    ApplyPlayerDamage(bullet_trace.damage);
    return true;
}

bool RuntimeWorld::IsWorldLineBlocked(
    const glm::vec3& line_origin,
    const glm::vec3& line_target) const {
    const glm::vec3 line_delta = line_target - line_origin;
    const float line_length = glm::length(line_delta);
    if (line_length <= 0.0001f) {
        return false;
    }

    BulletTrace visibility_trace;
    visibility_trace.origin = line_origin;
    visibility_trace.direction = line_delta;
    visibility_trace.distance = line_length;

    float impact_distance = line_length;
    return FindWorldShotImpact(visibility_trace, impact_distance);
}

void RuntimeWorld::ApplyGuardCombatDamage(uint64_t tick_number) {
    constexpr float combat_range = 50.0f * PlayerController::WORLD_METER;

    if (!player_.IsAlive()) {
        return;
    }

    for (const AiGuardEntity& guard : ai_.GetGuards()) {
        if (!guard.runtime_enabled ||
            guard.state != AiGuardState::Combat ||
            guard.health <= 0.0f) {
            continue;
        }
        if (glm::distance(guard.position, player_.GetPosition()) > combat_range) {
            continue;
        }

        const glm::vec3 guard_eye_position = guard.position + glm::vec3(
            0.0f,
            0.0f,
            1.7f * PlayerController::WORLD_METER);
        if (IsWorldLineBlocked(guard_eye_position, player_.GetEyePosition())) {
            continue;
        }

        auto [combat_state_iterator, inserted] = guard_combat_states_.try_emplace(guard.id);
        GuardCombatState& combat_state = combat_state_iterator->second;
        if (inserted) {
            // Vanilla guards commonly use the M16 family in the supplied
            // missions. Keeping the weapon in the runtime state makes cadence,
            // spread, recoil metadata, and ammunition obey the same fixed-tick
            // path as the player weapon instead of using a damage shortcut.
            const bool selected_authored_weapon =
                !guard.weapon_script_id.empty() &&
                combat_state.weapon.SelectWeaponByScriptId(guard.weapon_script_id);
            if (!selected_authored_weapon) {
                combat_state.weapon.SelectWeapon(4); // WEAPON_ID_M16A2 fallback
            }
        }
        // Treat the reload interval as a released trigger so the weapon's
        // burst counter resets before the first post-reload shot. Keeping the
        // trigger held forever would leave automatic weapons permanently
        // blocked by maximum_rounds_per_burst after their first magazine.
        const bool was_reloading = combat_state.weapon.IsReloading();
        combat_state.weapon.Update(
            GameClock::TICK_INTERVAL_SECONDS,
            !was_reloading);

        // Guards use the same fixed-step weapon state machine as the player.
        // Retail combat does not permanently disarm a guard when its current
        // magazine is empty, so begin the authored weapon's reload before
        // attempting another shot and keep the guard silent while it runs.
        if (combat_state.weapon.GetCurrentClipAmmo() == 0U) {
            combat_state.weapon.Reload();
            continue;
        }
        if (combat_state.weapon.IsReloading()) {
            continue;
        }

        BulletTrace trace;
        const glm::vec3 aim_direction = player_.GetEyePosition() - guard_eye_position;
        if (!combat_state.weapon.TryFire(guard_eye_position, aim_direction, trace)) {
            continue;
        }

        if (WeaponProducesMuzzleFlash(combat_state.weapon.GetActiveWeapon())) {
            const glm::vec3 muzzle_direction = NormalizeOrFallback(
                aim_direction,
                glm::vec3(0.0f, 1.0f, 0.0f));
            QueueGuardMuzzleFlash(
                guard.id,
                guard.position + glm::vec3(
                    0.0f,
                    0.0f,
                    1.45f * PlayerController::WORLD_METER) +
                    muzzle_direction * (0.45f * PlayerController::WORLD_METER));
        }

        const bool hit_player = ApplyGuardShotDamage(trace);
        QueueOneShotAudio(
            SoundEffect::Gunshot,
            combat_state.weapon.GetActiveWeapon().fire_sound);
        if (hit_player) {
            QueueOneShotAudio(SoundEffect::Pain);
        }
        if (hit_player || trace.hit_world_geometry) {
            QueueOneShotAudio(SoundEffect::BulletImpact);
        }

        AiStimulusEvent gunshot;
        gunshot.type = AiEventType::Gunshot;
        gunshot.position = guard.position;
        gunshot.loudness = 1.0f;
        gunshot.originator_id = guard.id;
        gunshot.tick_timestamp = tick_number;
        ai_.GetEventQueue().Post(gunshot);
    }
}

void RuntimeWorld::PlayFootstepIfNeeded(const PlayerInputCmd& input_command, bool was_grounded) {
    const bool has_movement_input =
        std::abs(input_command.forward) > 0.01f || std::abs(input_command.strafe) > 0.01f;
    if (!was_grounded || !player_.IsGrounded() || !has_movement_input) {
        footstep_timer_seconds_ = 0.0;
        return;
    }

    footstep_timer_seconds_ -= GameClock::TICK_INTERVAL_SECONDS;
    if (footstep_timer_seconds_ > 0.0) {
        return;
    }

    QueueOneShotAudio(SoundEffect::Footstep);
    footstep_timer_seconds_ = 0.35;
}

} // namespace igi
