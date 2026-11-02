// cutscene_camera.cpp - CutScene task and EditCamera evaluation.
#include "runtime/cutscene_camera.h"

#include <utility>

namespace igi {

AuthoredCutSceneProgramme BuildAuthoredCutSceneProgramme(
    const std::vector<std::string>& cut_scene_arg_tokens,
    const std::vector<std::vector<std::string>>& edit_camera_arg_tokens) {
    AuthoredCutSceneProgramme programme;

    if (!ParseCutSceneScreen(cut_scene_arg_tokens, &programme.screen)) {
        return programme;
    }

    if (edit_camera_arg_tokens.empty()) {
        return programme;
    }

    std::vector<CutSceneKeyNode> nodes;
    nodes.reserve(edit_camera_arg_tokens.size());
    for (const std::vector<std::string>& key_tokens : edit_camera_arg_tokens) {
        CutSceneKeyNode node;
        if (!ParseCutSceneKey(key_tokens, &node.params)) {
            return programme;
        }
        nodes.push_back(std::move(node));
    }

    programme.keys = CollectCutSceneKeys(nodes, programme.screen.time_scale, &programme.end);
    if (programme.keys.empty()) {
        return programme;
    }

    UnwrapCutSceneAngles(&programme.keys);

    programme.valid = true;
    return programme;
}

bool EvaluateAuthoredCutSceneAtTick(const AuthoredCutSceneProgramme& programme, int tick,
                                    CutSceneCameraState* out) {
    if (!programme.valid || programme.keys.empty() || out == nullptr) {
        return false;
    }

    const float t = CutSceneTimeFromTick(tick);

    return EvaluateCutScene(programme.keys, programme.end, programme.screen, t,
                            /*band_filter_active=*/false, out);
}

namespace {

void CollectKeysAtLevel(const std::vector<AuthoredCameraKeyObject>& objects, int parent_index,
                        std::vector<std::vector<std::string>>* out) {
    for (int index = 0; index < static_cast<int>(objects.size()); ++index) {
        const AuthoredCameraKeyObject& object = objects[static_cast<std::size_t>(index)];
        if (object.deleted || object.parent_index != parent_index ||
            object.type != "EditCamera") {
            continue;
        }
        out->push_back(object.arg_tokens);
        CollectKeysAtLevel(objects, index, out);
    }
}

}  // namespace

std::vector<std::vector<std::string>> CollectAuthoredCameraKeyTokens(
    const std::vector<AuthoredCameraKeyObject>& objects, int cut_scene_index) {
    std::vector<std::vector<std::string>> keys;
    if (cut_scene_index < 0 || cut_scene_index >= static_cast<int>(objects.size())) {
        return keys;
    }
    // A deleted CutScene authors nothing, which is the same refusal as a task
    // with no EditCamera child at all.
    if (objects[static_cast<std::size_t>(cut_scene_index)].deleted) {
        return keys;
    }
    CollectKeysAtLevel(objects, cut_scene_index, &keys);
    return keys;
}

}  // namespace igi
