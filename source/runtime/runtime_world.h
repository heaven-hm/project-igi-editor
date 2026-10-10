// runtime_world.h - Isolated simulation world and entity snapshot manager
#pragma once

#include <glm/glm.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>
#include <string>
#include <utility>
#include "../game_clock.h"
#include "../player_controller.h"
#include "../player_ladder.h"
#include "../weapon_system.h"
#include "../ai_system.h"
#include "../ai_script_host.h"
#include "../level_flow.h"
#include "../level/task_tree.h"
#include "../level/qvm_interpreter.h"
#include "projectile_system.h"
#include "door_state.h"
#include "../mission_expression.h"
#include "../mission_state.h"
#include "../weapon_view_sway.h"
#include "../weapon_view_recoil.h"
#include "sound_effect.h"

namespace igi {

struct RuntimeInteractionResult {
    bool handled = false;
    bool completed_objective = false;
};

// Immutable presentation snapshot for the currently running authored
// CutScene. RuntimeWorld computes it on the fixed-step boundary; App consumes
// the basis without reading mutable mission containers.
struct RuntimeCutSceneCamera {
    bool active = false;
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 forward = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 right = glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 up = glm::vec3(0.0f, 0.0f, 1.0f);
    float field_of_view_y_radians = 1.0f;
    float viewport_height_factor = 1.0f;
    float time_of_day = -1.0f;
    int shot_index = -1;
};

struct RuntimeMissionSoundEvent {
    std::string task_id;
    std::string sound_name;
    glm::vec3 position = glm::vec3(0.0f);
    bool playing = false;
    bool simple = false;
    bool relative_to_microphone = false;
};

enum class RuntimeAudioEventType {
    OneShot,
    StartLoop,
    StopLoop,
};

// Fixed-step sound intent emitted by the simulation. The gameplay host drains
// these events and forwards them to the platform audio adapter; RuntimeWorld
// never needs a Windows audio device to execute deterministic gameplay tests.
struct RuntimeAudioEvent {
    RuntimeAudioEventType type = RuntimeAudioEventType::OneShot;
    SoundEffect fallback_effect = SoundEffect::ObjectiveComplete;
    std::string authored_sound;
    std::string channel_id;
    glm::vec3 position = glm::vec3(0.0f);
    bool relative_to_microphone = false;
};

struct RuntimeExplodeObjectSnapshot {
    int object_index = -1;
    std::string task_id;
    std::string destroyed_model_name;
    bool is_exploded = false;
};

struct RuntimeExplosionRenderState {
    static constexpr uint32_t DISPLAY_DURATION_TICKS = 6;

    glm::vec3 position = glm::vec3(0.0f);
    float radius_units = 0.0f;
    uint32_t remaining_ticks = 0;
    bool is_flashbang = false;
};

// Immutable presentation cue for one guard firearm discharge. The simulation
// owns the lifetime; the renderer only consumes the short-lived world-space
// position and strength.
struct RuntimeGuardMuzzleFlashState {
    uint32_t guard_id = 0;
    glm::vec3 position = glm::vec3(0.0f);
    float strength = 0.0f;
};

// Immutable presentation state for one authored ConditionalContainer. The
// descendant indices belong to the editor-owned runtime object copy; keeping
// them in the snapshot lets rendering, collision, interaction, and AI setup
// apply one consistent visibility decision.
struct RuntimeConditionalContainerSnapshot {
    int object_index = -1;
    std::string task_id;
    bool is_running = true;
    std::vector<int> descendant_object_indices;
};

// Immutable presentation state for one authored GuardGenerator. The runtime
// gates pre-authored child guards deterministically and publishes the active
// child set so presentation does not duplicate mission-state evaluation.
struct RuntimeGuardGeneratorSnapshot {
    int object_index = -1;
    std::string task_id;
    bool is_on = false;
    int maximum_spawns = 0;
    std::vector<int> guard_object_indices;
    std::vector<int> active_guard_object_indices;
};

class RuntimeWorld {
public:
    RuntimeWorld();
    ~RuntimeWorld();

    // Lifecycle
    void Initialize(float (*get_terrain_z)(float x, float y), bool (*check_collision)(float x, float y, float z) = nullptr);
    void Reset();
    void SetPlayerTuning(const PlayerController::Tuning& tuning);
    void SetPlayerWeaponCycle(const std::vector<uint32_t>& weapon_cycle);
    void SetLadderPlacements(std::vector<LadderPlacement> ladder_placements);
    void SetExtractionZone(const glm::vec3& center, float radius);
    void ClearExtractionZone();
    void SetMissionStateBoolean(const std::string& variable_name, bool value);
    void SetMissionStateNumber(const std::string& variable_name, double value);
    void SetMissionStatePulse(const std::string& variable_name);
    std::vector<RuntimeAudioEvent> ConsumePendingAudioEvents();
    using MissionSoundEventHandler = std::function<void(
        const RuntimeMissionSoundEvent& sound_event)>;
    void SetMissionSoundEventHandler(MissionSoundEventHandler event_handler) {
        mission_sound_event_handler_ = std::move(event_handler);
    }
    void SetAuthoredDoors(std::vector<RuntimeDoorDefinition> door_definitions);
    bool ToggleDoor(int object_index);
    const std::vector<RuntimeDoorSnapshot>& GetDoorSnapshots() const {
        return authored_door_snapshots_;
    }
    bool IsDoorFullyOpen(int object_index) const;
    void SetAuthoredMissionState(
        std::vector<AuthoredMissionAreaActivation> area_activations,
        std::vector<AuthoredMissionEditVariable> edit_variables,
        std::vector<AuthoredMissionLevelTimer> level_timers = {},
        std::vector<AuthoredMissionStatusMessage> status_messages = {},
        std::vector<AuthoredMissionCutScene> cut_scenes = {},
        std::vector<AuthoredMissionConditionalSound> conditional_sounds = {},
        std::vector<AuthoredMissionExplodeObject> explode_objects = {},
        std::vector<AuthoredMissionConditionalContainer> conditional_containers = {},
        std::vector<AuthoredMissionGuardGenerator> guard_generators = {});
    const std::vector<RuntimeExplodeObjectSnapshot>&
    GetExplodeObjectSnapshots() const {
        return authored_explode_object_snapshots_;
    }
    const std::vector<RuntimeExplosionRenderState>&
    GetExplosionRenderStates() const {
        return explosion_render_states_;
    }
    const std::vector<RuntimeConditionalContainerSnapshot>&
    GetConditionalContainerSnapshots() const {
        return authored_conditional_container_snapshots_;
    }
    const std::vector<RuntimeGuardGeneratorSnapshot>&
    GetGuardGeneratorSnapshots() const {
        return authored_guard_generator_snapshots_;
    }
    // App calls this after registering the editor's pre-authored guards. The
    // mission condition is evaluated during SetAuthoredMissionState as well,
    // but registration order is an application concern rather than simulation
    // state, so this refresh is intentionally explicit.
    void RefreshAuthoredGuardGeneratorStates();
    using InteractionQuery = std::function<RuntimeInteractionResult(
        const glm::vec3& interaction_origin,
        const glm::vec3& interaction_direction)>;
    void SetInteractionQuery(InteractionQuery interaction_query);

    // Binds a parsed retail mission AI program to an already registered guard.
    bool AttachGuardScript(
        uint32_t guard_id,
        const QVMFile& parsed_file,
        const std::string& source_path = {});
    bool AttachGuardScriptFromFile(uint32_t guard_id, const std::string& path);
    void ClearGuardScripts();
    bool HasGuardScript(uint32_t guard_id) const;

    // Simulation tick
    void UpdateSimulationTick(uint64_t tick_number, const PlayerInputCmd& input_cmd);

    // Subsystems
    PlayerController& GetPlayer() { return player_; }
    const PlayerController& GetPlayer() const { return player_; }

    WeaponSystem& GetWeapons() { return weapons_; }
    const WeaponSystem& GetWeapons() const { return weapons_; }

    const WeaponViewSway& GetWeaponViewSway() const {
        return weapon_view_sway_;
    }
    const WeaponViewRecoil& GetWeaponViewRecoil() const {
        return weapon_view_recoil_;
    }
    bool IsWeaponViewTransitioning() const {
        return weapon_selection_phase_ != WeaponSelectionPhase::Ready;
    }

    ProjectileSystem& GetProjectiles() { return projectiles_; }
    const ProjectileSystem& GetProjectiles() const { return projectiles_; }

    const std::vector<LadderPlacement>& GetLadderPlacements() const {
        return ladder_placements_;
    }

    AiSystem& GetAi() { return ai_; }
    const AiSystem& GetAi() const { return ai_; }

    LevelFlow& GetLevelFlow() { return level_flow_; }
    const LevelFlow& GetLevelFlow() const { return level_flow_; }

    TaskTree& GetTaskTree() { return task_tree_; }
    const TaskTree& GetTaskTree() const { return task_tree_; }

    QvmNativeRegistry& GetNativeRegistry() { return qvm_registry_; }

    // Current first-person flash exposure, normalized to [0, 1]. The renderer
    // consumes this value without coupling presentation to projectile state.
    float GetFlashEffectStrength() const { return flash_effect_strength_; }
    // Fixed-step first-person firearm cue, normalized to [0, 1]. This is a
    // presentation signal only; projectile and hit simulation remain separate.
    float GetMuzzleFlashStrength() const { return muzzle_flash_strength_; }
    // Fixed-step incoming-hit cue, normalized to [0, 1]. Health and armor stay
    // authoritative in PlayerController; this value is presentation only.
    float GetPlayerDamageEffectStrength() const {
        return player_damage_effect_strength_;
    }
    const std::vector<RuntimeGuardMuzzleFlashState>&
    GetGuardMuzzleFlashStates() const {
        return guard_muzzle_flash_states_;
    }
    bool IsZoomActive() const { return zoom_active_; }
    bool IsMapComputerOpen() const { return map_computer_open_; }
    glm::vec3 GetMapComputerCenter() const;
    float GetMapComputerFieldOfView() const;
    int GetMapComputerZoomLevel() const { return map_computer_zoom_level_; }
    bool IsPlayerOnLadder() const { return ladder_traversal_.IsOnLadder(); }
    const LadderTraversal& GetLadderTraversal() const { return ladder_traversal_; }

    bool IsMissionActive() const { return level_flow_.GetStatus() == MissionStatus::InProgress; }
    const RuntimeCutSceneCamera& GetActiveCutSceneCamera() const {
        return active_cut_scene_camera_;
    }
    const std::vector<MissionStatusMessageDisplay>&
    GetDisplayedMissionStatusMessages() const {
        return displayed_mission_status_messages_;
    }

private:
    struct AuthoredExplodeObjectRuntime;
    bool ApplyPlayerShotDamage(BulletTrace& bullet_trace);
    void ApplyPlayerDamage(float damage_amount);
    bool ApplyPlayerExplodeObjectDamage(BulletTrace& bullet_trace);
    bool ApplyGuardShotDamage(BulletTrace& bullet_trace);
    bool FindWorldShotImpact(const BulletTrace& bullet_trace, float& impact_distance) const;
    bool IsWorldLineBlocked(const glm::vec3& line_origin, const glm::vec3& line_target) const;
    float FindWorldCeilingHeight(const glm::vec3& body_position) const;
    bool FindProjectileCollision(
        const glm::vec3& start_position,
        const glm::vec3& end_position,
        ProjectileCollisionHit& collision_hit) const;
    bool IsProjectileTargetInRange(
        const glm::vec3& center,
        float radius_units) const;
    void ApplyProjectileDetonations();
    void ApplyExplosionDamage(
        const glm::vec3& explosion_position,
        float explosion_radius_units,
        float explosion_falloff_units,
        float base_damage,
        float damage_factor,
        uint32_t owner_entity_id);
    void AdvanceExplosionRenderStates();
    void QueueExplosionRenderState(
        const glm::vec3& explosion_position,
        float explosion_radius_units,
        bool is_flashbang);
    void LaunchPlayerProjectile(
        const glm::vec3& origin,
        const glm::vec3& direction,
        const WeaponDefinition& weapon);
    void DispatchGuardScripts();
    void ApplyScriptPatrolRoute(AiGuardEntity& guard) const;
    void ApplyGuardCombatDamage(uint64_t tick_number);
    void AdvanceGuardMuzzleFlashStates();
    void QueueGuardMuzzleFlash(
        uint32_t guard_id,
        const glm::vec3& muzzle_position);
    void PlayFootstepIfNeeded(const PlayerInputCmd& input_command, bool was_grounded);
    bool UpdateWeaponSelection(const PlayerInputCmd& input_command);
    void UpdateAuthoredMissionState();
    void UpdateAuthoredCutScenes();
    void UpdateAuthoredConditionalContainers();
    void UpdateAuthoredGuardGenerators();
    void UpdateAuthoredConditionalSounds();
    void UpdateAuthoredExplodeObjects();
    void RefreshAuthoredExplodeObjectSnapshots();
    void RefreshAuthoredConditionalContainerSnapshots();
    void RefreshAuthoredGuardGeneratorSnapshots();
    void PublishAuthoredConditionalContainerState(
        const AuthoredMissionConditionalContainer& definition,
        bool is_running);
    void TriggerAuthoredExplodeObject(
        struct AuthoredExplodeObjectRuntime& runtime_object);
    void UpdateAuthoredCutSceneCamera(
        const AuthoredMissionCutScene& definition,
        int tick_count);
    void PublishAuthoredCutSceneState(
        const AuthoredMissionCutScene& definition,
        bool is_running,
        bool is_finished,
        int tick_count);
    void UpdateAuthoredDoors();
    void PublishAuthoredDoorState(
        const RuntimeDoorDefinition& definition,
        const RuntimeDoorState& door_state,
        bool is_locked,
        bool is_picked);
    void RefreshAuthoredDoorSnapshots();
    void UpdateAuthoredMissionStatusMessages(uint64_t tick_number);
    void PublishMissionStatusMessageState(
        const AuthoredMissionStatusMessage& definition,
        bool is_sent,
        int64_t sent_tick,
        bool is_finished_display,
        int64_t finished_display_tick,
        int64_t ticks_since_finished_display);
    void UpdateMissionActorState();
    void UpdateMapComputerViewport(const PlayerInputCmd& input_command);
    bool TryMountNearestLadder();
    bool TickLadderTraversal(const PlayerInputCmd& input_command);
    void EndLadderTraversal();
    void QueueAudioEvent(RuntimeAudioEvent audio_event);
    void QueueOneShotAudio(
        SoundEffect fallback_effect,
        const std::string& authored_sound = {});
    void QueueLoopStart(
        const std::string& channel_id,
        const std::string& authored_sound,
        SoundEffect fallback_effect,
        const glm::vec3& position,
        bool relative_to_microphone);
    void QueueLoopStop(const std::string& channel_id);

    struct GuardScriptState {
        QvmProgram program;
        std::string source_path;
        std::string last_error;
        bool dispatched_create = false;
        bool faulted = false;
    };

    float (*get_terrain_z_)(float x, float y) = nullptr;
    bool (*check_collision_)(float x, float y, float z) = nullptr;

    PlayerController player_;
    PlayerController::Tuning player_tuning_;
    std::vector<uint32_t> player_weapon_cycle_;
    std::vector<LadderPlacement> ladder_placements_;
    LadderTraversal ladder_traversal_;
    int active_ladder_index_ = -1;
    glm::vec3 ladder_slide_velocity_ = glm::vec3(0.0f);
    WeaponSystem weapons_;
    ProjectileSystem projectiles_;
    AiSystem ai_;
    LevelFlow level_flow_;
    TaskTree task_tree_;
    QvmNativeRegistry qvm_registry_;
    MissionExpressionState mission_expression_state_;
    struct MissionAreaActivationState {
        std::string task_id;
        glm::vec3 minimum = glm::vec3(0.0f);
        glm::vec3 maximum = glm::vec3(0.0f);
        bool accepts_player = false;
    };
    std::vector<MissionAreaActivationState> mission_area_activations_;
    std::vector<AuthoredMissionEditVariable> mission_edit_variables_;
    std::vector<int> mission_edit_variable_values_;
    std::vector<AuthoredMissionLevelTimer> mission_level_timers_;
    std::vector<int> mission_level_timer_ticks_;
    std::vector<uint8_t> mission_level_timer_running_;
    std::vector<AuthoredMissionCutScene> mission_cut_scenes_;
    std::vector<int> mission_cut_scene_ticks_;
    std::vector<uint8_t> mission_cut_scene_running_;
    std::vector<uint8_t> mission_cut_scene_finished_;
    RuntimeCutSceneCamera active_cut_scene_camera_;
    struct AuthoredConditionalSoundRuntime {
        AuthoredMissionConditionalSound definition;
        bool is_running = false;
        bool has_played = false;
    };
    std::vector<AuthoredConditionalSoundRuntime> mission_conditional_sounds_;
    MissionSoundEventHandler mission_sound_event_handler_;
    std::vector<RuntimeAudioEvent> pending_audio_events_;
    struct AuthoredConditionalContainerRuntime {
        AuthoredMissionConditionalContainer definition;
        bool is_running = false;
    };
    std::vector<AuthoredConditionalContainerRuntime>
        mission_conditional_containers_;
    std::vector<RuntimeConditionalContainerSnapshot>
        authored_conditional_container_snapshots_;
    struct AuthoredGuardGeneratorRuntime {
        AuthoredMissionGuardGenerator definition;
        bool is_on = false;
    };
    std::vector<AuthoredGuardGeneratorRuntime> mission_guard_generators_;
    std::vector<RuntimeGuardGeneratorSnapshot>
        authored_guard_generator_snapshots_;
    struct AuthoredExplodeObjectRuntime {
        AuthoredMissionExplodeObject definition;
        bool condition_active = false;
        bool delay_pending = false;
        int delay_ticks_remaining = 0;
        bool is_exploded = false;
    };
    std::vector<AuthoredExplodeObjectRuntime> mission_explode_objects_;
    std::vector<RuntimeExplodeObjectSnapshot>
        authored_explode_object_snapshots_;
    std::vector<RuntimeExplosionRenderState> explosion_render_states_;
    struct AuthoredDoorRuntime {
        RuntimeDoorDefinition definition;
        RuntimeDoorState state;
        bool is_locked = false;
        bool is_picked = false;
    };
    std::vector<AuthoredDoorRuntime> authored_doors_;
    std::vector<RuntimeDoorSnapshot> authored_door_snapshots_;
    struct MissionStatusMessageRuntime {
        AuthoredMissionStatusMessage definition;
        bool is_sent = false;
        int64_t sent_tick = -100000;
        bool is_finished_display = false;
        int64_t finished_display_tick = -1;
        int64_t ticks_since_finished_display = 0;
        bool is_displaying = false;
        int display_frame = 0;
        int characters_remaining = 0;
        int hold_ticks = 0;
        int slot_index = -1;
    };
    static constexpr size_t kMissionStatusMessageSlotCount = 24;
    std::vector<MissionStatusMessageRuntime> mission_status_messages_;
    std::array<int, kMissionStatusMessageSlotCount> mission_status_message_slots_{};
    std::vector<MissionStatusMessageDisplay> displayed_mission_status_messages_;
    std::vector<std::string> mission_state_pulse_names_;
    AiScriptHost ai_script_host_;
    std::unordered_map<uint32_t, GuardScriptState> guard_scripts_;
    struct GuardCombatState {
        WeaponSystem weapon;
    };

    std::unordered_map<uint32_t, GuardCombatState> guard_combat_states_;
    enum class WeaponSelectionPhase {
        Ready,
        Lowering,
        Raising,
    };
    WeaponViewSway weapon_view_sway_;
    WeaponViewRecoil weapon_view_recoil_;
    WeaponSelectionPhase weapon_selection_phase_ = WeaponSelectionPhase::Ready;
    int pending_weapon_slot_ = -1;
    InteractionQuery interaction_query_;
    bool fire_was_held_ = false;
    bool zoom_active_ = false;
    bool map_computer_open_ = false;
    bool map_computer_input_was_held_ = false;
    int map_computer_zoom_level_ = 1;
    glm::vec2 map_computer_center_ = glm::vec2(0.0f);
    float flash_effect_strength_ = 0.0f;
    float flash_effect_decay_per_second_ = 0.0f;
    float flash_effect_remaining_seconds_ = 0.0f;
    float muzzle_flash_strength_ = 0.0f;
    float player_damage_effect_strength_ = 0.0f;
    std::vector<RuntimeGuardMuzzleFlashState> guard_muzzle_flash_states_;
    double footstep_timer_seconds_ = 0.0;
    glm::vec3 extraction_zone_center_ = glm::vec3(1000.0f, 1000.0f, 0.0f);
    float extraction_zone_radius_ = 8.0f * PlayerController::WORLD_METER;
};

} // namespace igi
