#pragma once
// cutscene_camera.h - Authored CutScene task and EditCamera evaluation.
//
// MIT License.


#include "runtime/cutscene_rules.h"

#include <string>
#include <vector>

namespace igi {

// One CutScene task's authored camera programme.
//
// `keys` is the engine's timeline (CutScene_CollectKeys' output AFTER
// CutScene_UnwrapAngles), so it can be handed straight to `EvaluateCutScene`.
// `end` is the `*time` the walk leaves behind -- the sum of
// `time_scale * duration` over EVERY key including the last, which is what
// `t > end` compares against (cutscene.c:442).
//
// `valid` is false when the CutScene header is truncated, when it authors no
// EditCamera key at all, or when ANY key is truncated. A partial programme is
// never returned: previewing a cutscene whose key chain was silently halved
// would look plausible and be wrong, which is the same refusal
// `ParseCutSceneKey` already documents (CUTSCENES.md section 11).
struct AuthoredCutSceneProgramme {
    CutSceneScreenParams screen;
    std::vector<CutSceneKey> keys;
    float end = 0.0f;
    bool valid = false;
};

// Resolves one authored CutScene task into the engine's camera timeline.
//
// `cut_scene_arg_tokens` is the CutScene object's `LevelObject::argTokens`
// ([0] = task id, [1] = "CutScene", [2] = name, [3] = parameter 0) and
// `edit_camera_arg_tokens` are its EditCamera descendants' argTokens in the
// order `CutScene_CollectKeys` walks them -- depth first: a key, then that
// key's own EditCamera child chain, then the key's next EditCamera sibling
// (cutscene.c:163-180). Every retail key sits at one depth (measured: all 123
// across the three cutscene levels, CUTSCENES.md section 5.1), so for retail
// data the list is simply the sibling chain in file order; the tree order is
// what the caller must supply for a nested chain -- `CollectAuthoredCameraKeyTokens`
// below is the helper that produces it from the editor's flat object list.
//
// `time_scale` is taken from the CutScene header's own "Time scale" argument
// (cutscene.c:431-432 passes `c->timeScale`), NOT from a caller default.
AuthoredCutSceneProgramme BuildAuthoredCutSceneProgramme(
    const std::vector<std::string>& cut_scene_arg_tokens,
    const std::vector<std::vector<std::string>>& edit_camera_arg_tokens);

// The camera pose the engine's own rule produces at simulation tick `tick`.
//
// `t = (long double)c->tick / gCutSceneTicksPerSec` (cutscene.c:427), i.e.
// `CutSceneTimeFromTick(tick)`, and the pose is `CutScene_Interpolate`'s
// authored-data half as `EvaluateCutScene` computes it.
//
// `band_filter_active` is always false: the editor draws no screen filters, so
// `c->bandFilter` is never set and the engine's alternate colour-mix arm
// (cutscene.c:537-546) is unreachable here. The arm stays implemented and
// tested in `EvaluateCutScene`; this entry point says which one it takes
// instead of leaving it implied.
//
// Returns false -- leaving `out` untouched -- when the programme is not valid,
// when the timeline is empty, or when `out` is null.
bool EvaluateAuthoredCutSceneAtTick(const AuthoredCutSceneProgramme& programme, int tick,
                                    CutSceneCameraState* out);

// ---------------------------------------------------------------------------
// The projection: the editor's flat object list -> the key chain
// ---------------------------------------------------------------------------

// One authored object, as the projection needs it. Deliberately NOT
// `LevelObject`: the same reason `MissionStateTaskSource` exists (it keeps this
// module independent of the editor's level types, so it can be tested without a
// level archive). The editor fills three fields and nothing else.
struct AuthoredCameraKeyObject {
    int parent_index = -1;   // the parent's index in the SAME list; -1 for a root
    std::string type;        // the task type, e.g. "CutScene" / "EditCamera"
    std::vector<std::string> arg_tokens;
    // Editor-only tombstone. retail engine has no equivalent: a retail task either
    // exists in the compiled data or does not. A deleted object is skipped
    // together with its whole subtree, so deleting a key cannot leave its
    // children to be spliced into the timeline at the wrong place.
    bool deleted = false;
};

// The `argTokens` of every EditCamera below the object at `cut_scene_index`, in
// the order `CutScene_CollectKeys` 0x4F4880 walks them (cutscene.c:163-180):
// emit the key, recurse into that key's own EditCamera children, then continue
// with the key's next EditCamera sibling. Depth first, and within one level of
// the tree in list order -- which is file order, because the editor appends
// objects as it parses (`LoadRecursive`, source/level/level_objects.cpp:672-676,
// passes `currentObjIdx` down as the child's parent).
//
// Returns an empty vector when `cut_scene_index` is out of range or the object
// has no EditCamera child, which is the state `CutScene_Update` returns early on
// (cutscene.c:429-430).
std::vector<std::vector<std::string>> CollectAuthoredCameraKeyTokens(
    const std::vector<AuthoredCameraKeyObject>& objects, int cut_scene_index);

}  // namespace igi
