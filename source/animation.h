#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "runtime/anim_rules.h"   // igi::BoneTransform (the engine's Bone records)

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
// This is the editor's ONE playback path, and it is the engine's: the struct
// owns an `igi::AnimPlayer` (source/runtime/anim_player.{h,cpp}) built from the
// same clip, and every clock and pose it reports comes out of that player -
// `Anim_Play` 0x4D61D0, `BoneObj_Update` 0x4D38C0, `BoneObj_GetEvents` 0x4D3210.
// The implementation is in source/runtime/anim_playback.cpp, next to the engine
// driver, so the host test lane can link it without this file's Windows
// dependencies (animation.cpp pulls in utils.h).
//
// The fields below stay editor-owned because they are editor decisions:
//   * `clip`      - which clip the editor chose for this object;
//   * `playing`   - the editor's pause gate (the engine has no Pause);
//   * `speed`     - handed to the engine through Anim_SetSpeed 0x4D6580;
//   * `forceLoop` - keeps the preview running for a clip whose own tp_flag is
//                   non-looping; it becomes Anim_Play's kAnimLoop instance flag.
// `currentTimeMs` is a MIRROR of the engine clock (`Anim_GetTime` 0x4D6500),
// which counts TICKS at 4800/second - the name is the editor's, the unit is the
// engine's (src/boneobj/anim.c:9).
//
// Copying is deleted on purpose: a copy would duplicate the engine records and
// silently give one object a second, independent clock. Move it instead.
struct AnimPlayback {
    const AnimationClip* clip = nullptr;
    float currentTimeMs = 0.f;
    bool  playing = false;
    float speed = 1.0f;
    bool  forceLoop = false;  // editor: keep looping even if clip's tp_flag is non-looping

    // Constructed, destroyed and moved out of line: `igi::AnimPlayer` is
    // incomplete in this header, and a defaulted constructor or destructor here
    // would need its complete type for the member's cleanup path.
    AnimPlayback();
    ~AnimPlayback();
    AnimPlayback(const AnimPlayback&) = delete;
    AnimPlayback& operator=(const AnimPlayback&) = delete;
    AnimPlayback(AnimPlayback&& other) noexcept;
    AnimPlayback& operator=(AnimPlayback&& other) noexcept;

    // Rewinds to the clip's first frame and stops the clock.
    void Reset();
    // Builds the engine player for `c` and starts it from tick 0.
    void Start(const AnimationClip* c);
    void Stop() { playing = false; }
    void Pause() { playing = false; }
    void Resume() { if (clip) playing = true; }
    // Advances the engine's own clock. `dtMs` is REAL elapsed milliseconds; the
    // conversion to engine ticks happens inside the engine's frame arithmetic
    // (one Step is gTicksPerFrame = 160 ticks, src/lod/boneobj.c:86-90), never
    // by feeding a tick value into a millisecond parameter.
    //
    // A long frame steps as many engine frames as the wall clock covered - the
    // preview stays in real time - but only the frame this draw shows is POSED;
    // the rest are clock-only. Retail pays one pose per drawn object per frame
    // whatever the frame time (`Human_WakeIfVisible` 0x45F140 -> `Human_Wake`
    // 0x45F240, src/humanbase/human.c:262-297) while the clock advances once per
    // frame (`inst->time = inst->speed + inst->time`, src/boneobj/boneobj.c:884-885);
    // see `AnimPlayer::AdvanceSeconds` for the full rule. Returns the number of
    // engine frames stepped.
    int Update(float dtMs);
    // The same clock advance WITHOUT the pose, for a playback whose pose is not
    // drawn this frame. The engine does exactly this split for a human: the
    // per-frame update advances the clock and nothing else
    // (`Soldier_Update` 0x45D080 -> `Human_FireAnimTrigger` 0x4613D0 ->
    // `BoneObj_GetEvents` 0x4D3210, src/humanbase/human.c:1368-1387), and the
    // pose is computed later, at draw time, and only for an object the renderer
    // actually draws (`Human_WakeIfVisible` 0x45F140, src/humanbase/human.c:262-287
    // -> `Human_Wake` 0x45F240 -> `BoneObj_Update` 0x4D38C0). Skipping the pose
    // is therefore faithful; stopping the clock would not be.
    //
    // The frame arithmetic, the sub-frame accumulator and the end test are
    // identical to `Update`, so a playback that switches back to `Update` when
    // it becomes visible poses at the tick it would have reached anyway.
    // Returns the number of engine frames stepped.
    int AdvanceClock(float dtMs);
    // The pose half of `Update`, at the CURRENT clock, without advancing it -
    // `Human_Wake` 0x45F240 (src/humanbase/human.c:289-297). Called by the pose
    // getters when the clock has moved without a pose (`AdvanceClock`), which is
    // the engine's own "wake a frozen object at draw time" step; a caller only
    // needs it directly if it wants the bones current before asking for them.
    void PoseNow();
    // True when the bones were posed at the tick the next drawn frame should
    // show. `Update` leaves it true (its pose IS the drawn frame's tick);
    // `AdvanceClock` clears it, so the next `WorldTransforms()` poses first -
    // retail's `Human_Wake` 0x45F240 posing an object that has just become
    // visible. Exposed so the host lane can assert the transition.
    bool PoseCurrent() const { return pose_current_; }
    // The clip's end, in the engine's own terms: the BEF stores the engine length
    // + 1 (`AnimInit`'s third field), and `BoneObj_AnimEventsCB` calls a
    // non-looping clock finished once `time >= length`
    // (src/boneobj/boneobj.c:873-941). So the test is `length_ms - 1`.
    bool IsFinished() const {
        if (!clip) return true;
        const int engine_length = clip->duration_ms() - 1;
        return !playing && currentTimeMs >= (float)engine_length;
    }

    // ── the engine path ─────────────────────────────────────────────────────
    // True when the engine player was built and can pose this clip. A clip the
    // engine cannot play (no bones, or no position track - retail faults on the
    // second, boneobj.c:1014) is reported here instead of being faked. Out of
    // line: `igi::AnimPlayer` is incomplete in this header.
    bool EngineReady() const;

    // Deterministic cost counter, NOT engine logic: how many times the engine's
    // full bone pose (`BoneObj_Update` 0x4D38C0) has run for this playback,
    // including the draw-time wake. `[AnimProfile]` reports the per-frame delta as
    // `poses=`; a drawn playback must add at most ONE per rendered frame however
    // long that frame took. See `AnimPlayer::PoseSteps`.
    int EnginePoseSteps() const;

    // The pose the engine produced for the frame it last stepped, in METRES and
    // in the model's own frame - the space `Renderer::DrawAnimSkeleton` and the
    // weapon-attachment matrix expect. Empty when `EngineReady()` is false.
    const std::vector<glm::mat4>& WorldTransforms();

    // The same bones in the engine's own form: `Bone::matrix` (row-major 3x3)
    // and `Bone::pos` in RAW units, which is what `BoneMesh_Skin` reads. Valid
    // after `WorldTransforms()`/`Update()`; both refresh the same records.
    const std::vector<igi::BoneTransform>& RawBoneTransforms();

private:
    // Creates the engine player for `clip` and plays it with the current flags.
    void BuildEnginePlayer();
    // Re-plays when `forceLoop` changed and re-sets the speed when it changed:
    // Anim_Play bakes the loop flag into the instance (src/boneobj/anim.c:38-58,
    // stored by BoneObj_InitAnimInst, boneobj.c:1013) and InitAnimInst overwrites
    // `inst.speed` with gTicksPerFrame, so both have to be re-applied after a
    // (re)play rather than set once.
    void SyncEngineFlags();
    // The tail both `Update` and `AdvanceClock` share: mirror the engine clock
    // into `currentTimeMs` (`Anim_GetTime` 0x4D6500) and apply the engine's own
    // end test. Keeping it in one place is what makes the two paths
    // interchangeable - a playback may switch between them frame by frame and
    // the clock, the end test and the sub-frame remainder stay identical.
    void FinishAdvance();

    std::unique_ptr<igi::AnimPlayer> engine_;
    uint32_t engine_flags_ = 0;   // the flags the player was last played with
    float engine_speed_ = 1.0f;   // the multiplier the player was last set to
    // See `PoseCurrent()`. Starts true: `BuildEnginePlayer` plays the clip and
    // `Anim_Play`'s immediate path poses once (src/humanplayer/humanplayer.c:1214-1215).
    bool pose_current_ = true;
};

// The static mesh may only be replaced when playback is active, the engine can
// actually pose the clip, and the skeletal geometry was loaded. This keeps the
// render gate independently testable and avoids making an object invisible on a
// skin-load failure OR on a clip the engine refuses to play (an empty pose would
// suppress the static mesh and draw nothing at all).
inline bool ShouldUseSkinnedReplacement(const AnimPlayback& playback,
                                        int object_index,
                                        size_t object_count,
                                        bool object_deleted,
                                        bool has_skin_geometry) {
    return playback.clip != nullptr && playback.playing && playback.EngineReady() &&
           object_index >= 0 && static_cast<size_t>(object_index) < object_count &&
           !object_deleted && has_skin_geometry;
}

// ── The engine's own playback path (#73) ─────────────────────────────────────
//
// `AnimationRegistry::Evaluate`/`EvaluateWorld` above are the editor's own
// reconstruction from the BEF text: they slerp with glm and place each bone from
// a local translation. They are still used to answer "does this clip move at
// all" and for offline inspection, but they are NO LONGER the editor's playback
// path: `AnimPlayback` (above) owns an `igi::AnimPlayer` and every clock and
// pose the editor renders comes from it.
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
// `AnimPlayer::PoseFromLastStep()` is a drop-in for the pose the renderer wants:
// one glm matrix per engine bone, in METRES and in the model's own frame.
// `BoneMesh_Skin` is called by `Renderer::DrawSkinnedMesh` over the engine's
// vertex records (HOOK 0x49B700; the retail caller is
// `Direct3DRender_DrawBoneMesh` 0x49F700, src/bonemesh/d3drender.c:164).
//
// TWO THINGS A CALLER MUST NOT MIX, both measured against the shipped data:
//
//   1. The BEF text's key times are engine TICKS (4800/second), NOT
//      milliseconds - `AnimationClip::time_ms` and `AnimationClip::length_ms`
//      are misnomers. `AnimPlayer` consumes ticks; `AnimPlayback::Update` takes
//      real elapsed MILLISECONDS and hands them to `AnimPlayer::AdvanceSeconds`,
//      which converts through gTicksPerFrame (src/lod/boneobj.c:86-90), so
//      `currentTimeMs` holds ticks despite its name. Never feed one clock's
//      value to the other.
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
