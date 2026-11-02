// cutscene_camera.cpp -- see cutscene_camera.h for the issue mapping (#80), the
// oracle files and what is deliberately out of scope.
//
// This file is glue, not rules. Every value it produces comes out of
// `cutscene_rules.h`; the only decisions made here are which authored tasks form
// the programme and which of `EvaluateCutScene`'s two arms applies.
#include "runtime/cutscene_camera.h"

#include <utility>

namespace igi {

AuthoredCutSceneProgramme BuildAuthoredCutSceneProgramme(
    const std::vector<std::string>& cut_scene_arg_tokens,
    const std::vector<std::vector<std::string>>& edit_camera_arg_tokens) {
    AuthoredCutSceneProgramme programme;

    // The CutScene header (14 rows, CutScene_Open 0x4F4B00, cutscene.c:227-263).
    // A truncated header is a refusal, not a defaults-filled timeline.
    if (!ParseCutSceneScreen(cut_scene_arg_tokens, &programme.screen)) {
        return programme;
    }

    // CutScene_Update's first guard is `Task_FindChild(&c->task,
    // EditCamera_GetType())` (cutscene.c:429-430): no key child, no update.
    if (edit_camera_arg_tokens.empty()) {
        return programme;
    }

    // One node per authored key. A key that does not parse -- fewer than the 23
    // tokens the 16-row EditCamera table consumes (editcamera.c:88-107) --
    // refuses the WHOLE programme rather than dropping that key, because a
    // silently shortened chain still evaluates to a plausible-looking pose.
    std::vector<CutSceneKeyNode> nodes;
    nodes.reserve(edit_camera_arg_tokens.size());
    for (const std::vector<std::string>& key_tokens : edit_camera_arg_tokens) {
        CutSceneKeyNode node;
        if (!ParseCutSceneKey(key_tokens, &node.params)) {
            return programme;
        }
        nodes.push_back(std::move(node));
    }

    // CutScene_CollectKeys 0x4F4880 (cutscene.c:163-180): emit the key, advance
    // the clock by `scale * duration`, recurse into the key's own EditCamera
    // children, then continue with the key's next EditCamera sibling.
    programme.keys = CollectCutSceneKeys(nodes, programme.screen.time_scale, &programme.end);
    if (programme.keys.empty()) {
        return programme;
    }

    // CutScene_UnwrapAngles 0x4F4940 (cutscene.c:193-211), in place, before any
    // window is selected -- CutScene_Update's order (cutscene.c:435-436).
    UnwrapCutSceneAngles(&programme.keys);

    programme.valid = true;
    return programme;
}

bool EvaluateAuthoredCutSceneAtTick(const AuthoredCutSceneProgramme& programme, int tick,
                                    CutSceneCameraState* out) {
    if (!programme.valid || programme.keys.empty() || out == nullptr) {
        return false;
    }

    // `float t = (long double)c->tick / gCutSceneTicksPerSec` (cutscene.c:427).
    const float t = CutSceneTimeFromTick(tick);

    // `c->bandFilter` is never set in the editor (no screen filters are drawn),
    // so CutScene_Interpolate takes its plain `f = (t - k[1]->time) / span`
    // colour arm (cutscene.c:549-551).
    return EvaluateCutScene(programme.keys, programme.end, programme.screen, t,
                            /*band_filter_active=*/false, out);
}

namespace {

// One level of CutScene_CollectKeys' walk: for every EditCamera child of
// `parent_index`, in list order, emit it and then recurse into its own children.
// The engine's `do { emit; recurse(child); k = next sibling; } while (k)`
// (cutscene.c:167-179) visits exactly this order, because `Task_FindChild` /
// `Task_FindNext` walk the child chain in file order.
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
