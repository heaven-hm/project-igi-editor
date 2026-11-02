#pragma once
// cutscene_camera.h - Authored CutScene task and EditCamera evaluation.
//
// MIT License.
#pragma once

#include "runtime/cutscene_rules.h"

#include <string>
#include <vector>

namespace igi {

// One CutScene task's authored camera programme.
struct AuthoredCutSceneProgramme {
    CutSceneScreenParams screen;
    std::vector<CutSceneKey> keys;
    float end = 0.0f;
    bool valid = false;
};

// Resolves one authored CutScene task into the camera timeline.
AuthoredCutSceneProgramme BuildAuthoredCutSceneProgramme(
    const std::vector<std::string>& cut_scene_arg_tokens,
    const std::vector<std::vector<std::string>>& edit_camera_arg_tokens);

// The camera pose produced at simulation tick `tick`.
bool EvaluateAuthoredCutSceneAtTick(const AuthoredCutSceneProgramme& programme, int tick,
                                    CutSceneCameraState* out);

// ---------------------------------------------------------------------------
// Projection: object list -> key chain
// ---------------------------------------------------------------------------

struct AuthoredCameraKeyObject {
    int parent_index = -1;
    std::string type;
    std::vector<std::string> arg_tokens;
    bool deleted = false;
};

// The argTokens of every EditCamera below the object at cut_scene_index.
std::vector<std::vector<std::string>> CollectAuthoredCameraKeyTokens(
    const std::vector<AuthoredCameraKeyObject>& objects, int cut_scene_index);

}  // namespace igi
