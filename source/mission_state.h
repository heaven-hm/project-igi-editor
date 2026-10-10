#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace igi {

// Immutable authored mission volume copied into the runtime session. The
// runtime computes nActive from the player's fixed-step position; editor
// objects never become mutable simulation state.
struct AuthoredMissionAreaActivation {
    std::string task_id;
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 orientation = glm::vec3(0.0f);
    glm::vec3 dimensions = glm::vec3(0.0f);
    std::string criteria;
};

// Immutable authored EditVariable task. Its current integer value belongs to
// RuntimeWorld and is reset from initial_value for every new gameplay session.
struct AuthoredMissionEditVariable {
    std::string task_id;
    int initial_value = 0;
    std::string add_expression;
    std::string subtract_expression;
};

// Authored LevelTimer state published to the mission expression namespace.
// Tick advancement is owned by RuntimeWorld and reset per gameplay session.
struct AuthoredMissionLevelTimer {
    std::string task_id;
    std::string on_expression;
    std::string reset_expression;
    bool initial_run = false;
};

// One authored EditCamera shot nested below a CutScene task. Positions use the
// same native world-unit space as LevelObject and RuntimeWorld.
struct AuthoredMissionCutSceneShot {
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 orientation = glm::vec3(0.0f);
    float field_of_view_radians = 1.0f;
    float duration_seconds = 0.0f;
    bool smooth_to_next = false;
};

// Authored CutScene timing, expressions, and camera presentation state.
struct AuthoredMissionCutScene {
    std::string task_id;
    std::string run_expression;
    std::string reset_expression;
    std::string time_delta_expression;
    std::string start_expression;
    std::string stop_expression;
    float start_time_seconds = 0.0f;
    bool initial_run = false;
    float time_scale = 1.0f;
    float duration_seconds = 0.0f;
    float viewport_height_factor = 1.0f;
    float viewport_fade_in_seconds = 0.0f;
    float viewport_fade_out_seconds = 0.0f;
    float time_of_day = -1.0f;
    std::vector<AuthoredMissionCutSceneShot> camera_shots;
};

// Authored ConditionalSound edge definition. RuntimeWorld evaluates the
// condition at 30 Hz and emits only the rising edge for one-shot sounds.
struct AuthoredMissionConditionalSound {
    std::string task_id;
    std::string condition_expression;
    std::string sound_name;
    glm::vec3 position = glm::vec3(0.0f);
    bool simple = false;
    bool one_shot = false;
    bool relative_to_microphone = false;
};

// Authored ConditionalContainer state. The runtime owns the gate latch while
// the editor supplies the stable descendant object indices used to hide or
// restore the copied gameplay scene without mutating authoring data.
struct AuthoredMissionConditionalContainer {
    int object_index = -1;
    std::string task_id;
    std::string condition_expression;
    std::string run_at_start_expression;
    std::string run_at_stop_expression;
    std::vector<int> descendant_object_indices;
};

// Authored GuardGenerator state. The retail task creates soldiers below the
// generator at runtime; the current vertical slice keeps those pre-authored
// soldiers as stable editor objects and gates their AI/render visibility from
// this condition. maximum_spawns remains explicit metadata until dynamic
// soldier allocation is ported.
struct AuthoredMissionGuardGenerator {
    int object_index = -1;
    std::string task_id;
    std::string condition_expression;
    int maximum_spawns = 0;
    std::vector<int> guard_object_indices;
};

// Authored ExplodeObject state. The object index is the stable identity for
// the common -1 task ids used by vanilla placed props; named tasks retain
// their task id for mission-expression publication and diagnostics.
struct AuthoredMissionExplodeObject {
    int object_index = -1;
    std::string task_id;
    glm::vec3 position = glm::vec3(0.0f);
    std::string model_name;
    std::string destroyed_model_name;
    float damage_scale = 1.0f;
    float explosion_radius_meters = 0.0f;
    float explosion_falloff_radius_meters = 0.0f;
    float explosion_damage_scale = 1.0f;
    float explosion_delay_seconds = 0.0f;
    std::string explosion_expression;
    std::string explosion_sound;
};

// Authored StatusMessage definition. display_text is resolved from the
// localized resource by the Windows application before entering the runtime.
struct AuthoredMissionStatusMessage {
    std::string task_id;
    std::string send_expression;
    std::string text_resource;
    std::string display_text;
    std::string sound_name;
    bool send_once = false;
    bool cutscene_message = false;
    float duration_seconds = 2.0f;
};

// Immutable render-facing view of one authored status message currently in a
// retail message slot. The renderer owns no mission state and only receives
// this presentation snapshot.
struct MissionStatusMessageDisplay {
    std::string text;
    uint32_t revealed_characters = 0;
    uint32_t display_frame = 0;
    bool cutscene_message = false;
};

} // namespace igi
