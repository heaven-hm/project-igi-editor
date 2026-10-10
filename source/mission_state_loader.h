#pragma once

#include "mission_state.h"

#include <string>
#include <vector>

namespace igi {

// Lossless Task_New data projected from the editor snapshot. Keeping this
// source type independent of LevelObject preserves the runtime/editor boundary.
struct MissionStateTaskSource {
    std::string task_type;
    std::string task_id;
    int object_index = -1;
    std::vector<std::string> argument_tokens;
    float authored_duration_seconds = 0.0f;
    std::vector<AuthoredMissionCutSceneShot> authored_camera_shots;
    std::vector<int> descendant_object_indices;
    std::vector<int> guard_object_indices;
};

struct AuthoredMissionStateDefinitions {
    std::vector<AuthoredMissionAreaActivation> area_activations;
    std::vector<AuthoredMissionEditVariable> edit_variables;
    std::vector<AuthoredMissionLevelTimer> level_timers;
    std::vector<AuthoredMissionCutScene> cut_scenes;
    std::vector<AuthoredMissionConditionalSound> conditional_sounds;
    std::vector<AuthoredMissionConditionalContainer> conditional_containers;
    std::vector<AuthoredMissionGuardGenerator> guard_generators;
    std::vector<AuthoredMissionExplodeObject> explode_objects;
    std::vector<AuthoredMissionStatusMessage> status_messages;
};

// Loads the small authored task subset needed to advance vanilla objective
// expressions through player movement and fixed-step variable latching.
AuthoredMissionStateDefinitions LoadAuthoredMissionStateDefinitions(
    const std::vector<MissionStateTaskSource>& task_sources);

} // namespace igi
