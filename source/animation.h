#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// `igi::AnimPlayer` lives in source/runtime/anim_player.h, which needs this
// header for `AnimationClip`. Only a forward declaration can live here; a
// caller that uses the returned player includes "runtime/anim_player.h".
namespace igi { class AnimPlayer; }

// ── Animation data structures (editor-side, read-only) ─────────────────────
// These are populated by parsing the .BEF text files produced by igi1conv.exe.
// NO converter code is duplicated — we only parse the text format.

struct AnimBone {
    int    index = 0;
    std::string name;
    int    parent = -1;
    glm::vec3 restPos{0.f};  // rest position in RAW IGI units (NOT yet divided by 40.96 — see Evaluate())
};

struct AnimRotationKey {
    int    bone = 0;
    int    time_ms = 0;
    glm::quat q0{1.f,0.f,0.f,0.f};
    glm::quat q1{1.f,0.f,0.f,0.f};
    glm::quat q2{1.f,0.f,0.f,0.f};
};

struct AnimTranslationKey {
    int    track = 0;
    int    time_ms = 0;
    glm::vec3 pos{0.f};
};

struct AnimEvent {
    int    index = 0;
    int    event_id = 0;
    int    time_ms = 0;
    int    bone_id = -1;
    glm::vec3 pos{0.f};
};

struct AnimationClip {
    std::string name;
    int    animId = -1;      // animation_id, parsed from the "<NNN>_anim_<animId>" name suffix
    int    flags = 0;
    int    length_ms = 0;    // stored as duration+1 (engine subtracts 1)
    int    tp_flag = 0;      // loop flag (0 = no loop, 1 = loop)
    int    duration_ms() const { return length_ms > 0 ? length_ms : 0; }
    bool   loop() const { return tp_flag != 0; }

    std::vector<AnimBone> bones;
    std::vector<AnimRotationKey> rotationKeys;
    std::vector<AnimTranslationKey> translationKeys;
    std::vector<AnimEvent> events;
};

// Returns true when a clip contains an actual time-varying pose. Some retail
// action clips are intentionally single-frame poses; they are valid assets but
// are not useful as the editor's continuous AI preview animation.
bool AnimationClipHasTemporalMotion(const AnimationClip& clip);

// ── Animation registry ──────────────────────────────────────────────────────
// Manages imported animation sets per bone hierarchy. A "bone hierarchy" is
// the index (HumanSoldier-family arg@10) into the shared, model-independent
// common/ANIMS/<NNN>.IFF skeleton+animation file. Multiple models can (and
// do) share the same bone hierarchy. Auto-imports via igi1conv.exe and caches
// the resulting .BEF files in cache/anims/<NNN>/.

class AnimationRegistry {
public:
    AnimationRegistry();

    // Import animations for a bone hierarchy from its .IFF file. Returns true
    // if animations were imported or already up-to-date.
    bool ImportAnimations(int boneHierarchy);

    // Get all clip names for a bone hierarchy (sorted).
    std::vector<std::string> GetClipNames(int boneHierarchy) const;

    // Get clip by bone hierarchy + clip name. Returns nullptr if not found.
    const AnimationClip* GetClip(int boneHierarchy, const std::string& clipName) const;

    // Get the clip whose animation_id matches standAnimation. Returns nullptr if not found.
    const AnimationClip* GetClipByAnimId(int boneHierarchy, int animId) const;

    // Get the first available clip for a bone hierarchy (for auto-play). Returns nullptr if none.
    const AnimationClip* GetFirstClip(int boneHierarchy) const;

    // Get the DEFAULT clip for a bone hierarchy: the one with the lowest animId.
    // Used as the auto-play fallback for AI that reference no specific animation,
    // so "every AI animates" without depending on unordered_map iteration order
    // (which is arbitrary). Returns nullptr if the hierarchy has no clips.
    const AnimationClip* GetDefaultClip(int boneHierarchy) const;
    // Get a deterministic clip containing time-varying keys for continuous
    // preview. Action-only clips remain available through the normal lookup.
    const AnimationClip* GetFirstTemporalClip(int boneHierarchy) const;

    // Evaluate bone transforms at a given time (ms). Fills boneTransforms with
    // local-space transforms for each bone in the clip's skeleton.
    void Evaluate(const AnimationClip* clip, float timeMs, std::vector<glm::mat4>& boneTransforms) const;

    // Evaluate bone transforms in world space (parent-relative chain resolved).
    void EvaluateWorld(const AnimationClip* clip, float timeMs, std::vector<glm::mat4>& worldTransforms) const;

    // Clear all cached animation data.
    void Clear();

    // Returns true if the bone hierarchy has any imported animations.
    bool HasAnimations(int boneHierarchy) const;

private:
    struct BoneHierarchyAnimSet {
        int boneHierarchy = -1;
        std::string cacheDir;
        std::vector<std::string> clipNames;
        std::unordered_map<std::string, AnimationClip> clips;
    };

    std::unordered_map<int, BoneHierarchyAnimSet> registry_;

    // Parse a single .BEF text file into an AnimationClip.
    bool ParseBefFile(const std::string& path, AnimationClip& out);

    // Parse Anims.qsc to get sorted clip names.
    std::vector<std::string> ParseAnimsQsc(const std::string& path);

    // Get cache directory path for a bone hierarchy.
    std::string GetCacheDir(int boneHierarchy) const;

    // Find the common/ANIMS/<NNN>.IFF file for a bone hierarchy.
    std::string FindIffFile(int boneHierarchy) const;
};

// Align the world-space transforms emitted by a BEF clip with the bind pose
// already baked into a MEF mesh. BEF exports may omit the mesh root offset;
// applying one bind correction preserves animation deltas while making the
// first frame coincide with the static model.
void AlignAnimationWorldToMefBind(
    const std::vector<glm::mat4>& animation_bind_world,
    const std::vector<glm::vec3>& mef_bind_world,
    std::vector<glm::mat4>& animation_world);

// Parses ai/<aiTaskId>.qvm directly and resolves literal
// AIAction_PlayAnimation(<id>, ...) arguments in first-seen order. Dynamic
// animation expressions are intentionally omitted until their native contract
// is verified; the editor-side QVM disassembler remains available for evidence.
std::vector<int> FindAiScriptAnimationIds(const std::string& qvmPath);

// Parses ai/<aiTaskId>.qvm directly and resolves the first literal
// AIAction_Patrol(<id>, ...) argument. Returns the patrol path (PatrolPath task)
// id, or -1 when the script has no statically resolvable patrol action.
int FindAiScriptPatrolPathId(const std::string& qvmPath);

// ── Per-object animation playback state ─────────────────────────────────────
struct AnimPlayback {
    const AnimationClip* clip = nullptr;
    float currentTimeMs = 0.f;
    bool  playing = false;
    float speed = 1.0f;
    bool  forceLoop = false;  // editor: keep looping even if clip's tp_flag is non-looping

    void Reset() { currentTimeMs = 0.f; playing = false; }
    void Start(const AnimationClip* c) { clip = c; currentTimeMs = 0.f; playing = true; }
    void Stop() { playing = false; }
    void Pause() { playing = false; }
    void Resume() { if (clip) playing = true; }
    void Update(float dtMs);
    bool IsFinished() const {
        if (!clip) return true;
        return !playing && currentTimeMs >= (float)clip->duration_ms();
    }
};

// The static mesh may only be replaced when playback is active and the
// skeletal geometry was loaded. This keeps the render gate independently
// testable and avoids making an object invisible on a skin-load failure.
inline bool ShouldUseSkinnedReplacement(const AnimPlayback& playback,
                                        int object_index,
                                        size_t object_count,
                                        bool object_deleted,
                                        bool has_skin_geometry) {
    return playback.clip != nullptr && playback.playing &&
           object_index >= 0 && static_cast<size_t>(object_index) < object_count &&
           !object_deleted && has_skin_geometry;
}

// ── The engine's own playback path (#73) ─────────────────────────────────────
//
// Everything above this line is the editor's own reconstruction from the BEF
// text: `Evaluate`/`EvaluateWorld` slerp with glm and place each bone from a
// local translation, and `AnimPlayback::Update` counts MILLISECONDS.
//
// `igi::AnimPlayer` (source/runtime/anim_player.{h,cpp}, on top of the pure
// translation in source/runtime/anim_rules.{h,cpp}) is the ENGINE's path
// instead: it builds the engine's own records from the same clip and then runs
// the engine's own functions on them -
//
//   Anim_Play            0x4D61D0  src/boneobj/anim.c:38-58
//   BoneObj_InitAnimInst 0x4D4F60  src/boneobj/boneobj.c:988-1021
//   BoneObj_Update       0x4D38C0  src/boneobj/boneobj.c:371-419
//   BoneObj_UpdateBone   0x4D3A90  src/boneobj/boneobj.c:423-525
//   BoneObj_GetEvents    0x4D3210  src/boneobj/boneobj.c:112-129
//   Anim_PrepareSlerps   0x4D6D40  src/haze/anim.c:40-84
//   BoneMesh_Skin        0x49B700  src/dxinit/d3drender.c:226-313
//
// `AnimPlayer::PoseModelLocal()` is a drop-in for the `boneWorldTransforms`
// argument of `Renderer::DrawSkinnedMesh` and `Renderer::DrawAnimSkeleton`:
// one glm matrix per engine bone, in METRES and in the model's own frame.
// `AnimPlayer::Skin` is `BoneMesh_Skin` itself, over the engine's vertex
// records.
//
// TWO THINGS A CALLER MUST NOT MIX, both measured against the shipped data:
//
//   1. The BEF text's key times are engine TICKS (4800/second), NOT
//      milliseconds - `AnimationClip::time_ms` and `AnimationClip::length_ms`
//      are misnomers. `AnimPlayer` consumes ticks; `AnimPlayback::Update` takes
//      real elapsed MILLISECONDS and converts to ticks (`kTicksPerMs`, from
//      kTicksPerSecond at src/boneobj/anim.c:9), so `currentTimeMs` holds ticks
//      despite its name. Never feed one clock's value to the other.
//   2. `AnimPlayer::Step()` is ONE engine frame (gTicksPerFrame = 160 ticks at
//      the game's 30 fps), which is what the retail game loop does.
//      `AdvanceSeconds` is the editor-side accumulator that calls it.
//
// Build one player per playing clip; it owns the engine records for that clip.

// Builds the engine-exact player for `clip`. Returns a player whose `valid()`
// is false when the clip has no bones or no rotation keys - the engine has no
// guard for either and would fault (src/boneobj/boneobj.c:1091).
// The returned type is complete only where "runtime/anim_player.h" is included.
igi::AnimPlayer MakeEngineAnimPlayer(const AnimationClip& clip);
