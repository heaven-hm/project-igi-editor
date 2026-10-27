/******************************************************************************
 * @file    anim_player.h
 * @brief   The editor's animation PLAYBACK path and SKINNING call, driven by
 *          the engine's own methods (#73).
 *
 * `anim_rules.h` is the pure translation of IGI-RE's animation rules: it takes
 * engine-shaped data and returns engine-shaped answers, and it reads no files.
 * This file is the DRIVER on top of it: it turns one of the editor's
 * `AnimationClip`s (the BEF text that `igi1conv` emits from the retail
 * `common/ANIMS` `IFF` files) into the engine's own records, runs the engine's
 * own functions on them, and hands back what the renderer needs.
 *
 * Every step below is a call into `anim_rules.h`, not a re-implementation, and
 * every step is cited to IGI-RE:
 *
 *   Anim_Play            0x4D61D0  src/boneobj/anim.c:38-58
 *   Anim_PlayNow         0x4D6110  src/boneobj/anim.c:13-16
 *   BoneObj_InitAnimInst 0x4D4F60  src/boneobj/boneobj.c:988-1021
 *   BoneObj_Update       0x4D38C0  src/boneobj/boneobj.c:371-419
 *   BoneObj_UpdateBone   0x4D3A90  src/boneobj/boneobj.c:423-525
 *   BoneObj_GetEvents    0x4D3210  src/boneobj/boneobj.c:112-129
 *   BoneObj_AnimEventsCB 0x4D4B60  src/boneobj/boneobj.c:873-941
 *   Anim_PrepareSlerps   0x4D6D40  src/haze/anim.c:40-84
 *   Anim_GetTime         0x4D6500  src/boneobj/anim.c:149-153
 *   Anim_GetLength       0x4D64E0  src/boneobj/anim.c:143-147
 *   Anim_GetPhase        0x4D6520  src/boneobj/anim.c:155-161
 *   Anim_SetSpeed        0x4D6580  src/boneobj/anim.c:172-179
 *   Anim_SetTime         0x4D65B0  src/boneobj/anim.c:181-185
 *   BoneObj_SetFrameRate 0x4D2D30  src/lod/boneobj.c:86-90
 *   BoneObj_GetRootMotion 0x4D2FB0 src/lod/boneobj.c:202-211
 *   BoneMesh_Skin        0x49B700  src/dxinit/d3drender.c:226-313
 *
 * The engine function the issue calls `MefSkinner` does not exist in IGI-RE;
 * the retail skinner is `BoneMesh_Skin`, and `AnimPlayer::Skin` below is that
 * function over the engine's own vertex records.
 *
 * ---------------------------------------------------------------------------
 * What the BEF text does and does NOT carry
 * ---------------------------------------------------------------------------
 * `igi1conv` is an external tool and the BEF format is fixed, so the driver has
 * to map a lossy text form back onto the engine's records. Measured over the
 * shipped corpus of 233 clips in `cache/anims/{000,001,006}` (the skeleton
 * hierarchies `common/ANIMS/{000,001,006}.IFF`):
 *
 *   * TIME IS THE ENGINE'S TICK COUNT (4800/second), NOT MILLISECONDS. The
 *     editor's field names call it `time_ms`; that is a misnomer. Independent
 *     check on the same corpus: `AnimInit`'s third field is `1 mod 160` for all
 *     233 clips, i.e. engine_length + 1 where engine_length is a whole number of
 *     30 fps frames (160 ticks); 55733 of 66659 rotation key times are exact
 *     multiples of 160 and the rest are sub-frame ticks. In 51 clips the last
 *     rotation key sits exactly one frame (160 ticks) PAST the length - the
 *     wrap key - which the sampler only reaches for a looping clip.
 *   * `AnimInit(name, flags, length, tp_flag)`:
 *       - `length` is `(int)engine_length + 1`, so the engine length is
 *         `length - 1`;
 *       - `tp_flag` is `(engine d->flags & ANIM_LOOPS) != 0` (0/1 in the
 *         corpus);
 *       - the second field is 0 for all 233 clips, so the REST OF the engine's
 *         `d->flags` word is NOT recoverable from the BEF. Only ANIM_LOOPS is
 *         read by the playback path (boneobj.c:1117), so this is harmless here
 *         - but it is a real information loss and is named, not hidden.
 *   * EVERY POSITION in the BEF is divided by 40.96 (raw game units per metre):
 *     `Bone()` rest offsets, `TranslationKeyFrameData`, and `TriggerData`. The
 *     driver multiplies back, so the pipeline runs in RAW units exactly like the
 *     engine and only converts to metres on the way out. (Cross-checked against
 *     000.IFF TLST in anim_rules.h; the BEF itself is self-consistent with
 *     40.96, e.g. bone 1 = 13.5146 m = 553.56 raw.)
 *   * `RotationKeyFrameData(bone, flag, time, q0, q1, q2)` carries all three
 *     quaternions the engine's RotKey holds (`q`, `a`, `b`), so the Hermite
 *     rotation path IS reproducible.
 *   * `TranslationKeyFrameData(track, flag, time, pos)` does NOT carry the
 *     engine PosKey's `out`/`in` tangents (boneobj.c:1606-1608 stores 40 bytes
 *     per key; the BEF emits 12). The engine's default position sampler is the
 *     Hermite one (gBoneSmoothAnims = 1, lod/boneobj.c:65), so the position
 *     track cannot be reproduced bit-for-bit from a BEF. The driver zeroes the
 *     tangents, which makes the Hermite form collapse to the linear one. This
 *     affects ONLY the root-motion value and the wrap-tick root fixup - never a
 *     bone's position in the pose, because the pose places bones from their rest
 *     offsets (boneobj.c:434-439).
 *   * The BEF prints floats with at most 9 significant digits and drops trailing
 *     zeroes; a handful of small components do not round-trip to the same
 *     float32 (measured: 000.IFF clip 2 bone 12 `q.z`, 1 ULP). NAMED.
 *****************************************************************************/
#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "animation.h"
#include "runtime/anim_rules.h"

namespace igi {

// The engine's `BoneObj` plus one clip's decoded tracks: exactly the records
// `BoneObj_Load` 0x4D67A0 (src/boneobj/boneobj.c:1543-1636) builds, owned so the
// `EngineClip`'s pointers stay valid.
struct EngineClipData {
    BonePoseState state;              // the bone array (`BoneObj::bones`)
    std::vector<PosKey> pos_keys;     // BOTD, 40 bytes per key
    PosTrack pos_track;               // `AnimDesc::pos`
    std::vector<RotKey> rot_keys;     // BORD, 52 bytes per key, per bone
    std::vector<RotTrack> rot_tracks; // `AnimDesc::rot`, one per bone
    std::vector<SlerpInfo> slerp;     // built by Anim_PrepareSlerps
    std::vector<SlerpInfo> slerp2;
    std::vector<EventKey> event_keys; // BOED, 24 bytes per key
    EventTrack event_track;           // `AnimDesc::events`
    EngineClip clip;                  // `AnimDesc`

    bool valid() const { return !state.bones.empty() && clip.rot != nullptr; }
};

// One playing clip on one skeleton.
//
// The lifetime of an AnimPlayer is the lifetime of ONE clip: the engine rebuilds
// its tables per skeleton at load time and keeps them, and the editor only ever
// previews one clip at a time per object.
class AnimPlayer {
public:
    // 4800 ticks/second (src/boneobj/anim.c:9 `kTicksPerSecond`).
    static constexpr float kTicksPerSecond = kAnimTicksPerSecond;
    // The frame rate the game installs once (src/game/game.c:449
    // `BoneObj_SetFrameRate(30.0f)`), and the ticks per frame it implies.
    static constexpr float kDefaultFps = kAnimDefaultFps;
    static constexpr float kDefaultTicksPerFrame = kAnimDefaultTicksPerFrame;
    // Raw game units per metre. The engine works in raw units; the renderer's
    // `Renderer::DrawSkinnedMesh` takes its bone transforms in metres (it scales
    // the vertex by `kMefNativeScale` = 1/40.96 itself). Restated here because
    // `anim_rules.h` must not include a renderer header; the .cpp static_asserts
    // the literal, and `source/renderer/mef_native.h` carries the same 40.96.
    static constexpr float kRawUnitsPerMetre = 40.96f;

    // Builds the skeleton and the clip's tracks from one editor clip.
    // `anim_id` is the engine animation id this clip IS (the BEF name carries
    // it, `AnimationClip::animId`). Returns false for a clip with no bones or no
    // rotation keys - the engine has no guard for either (boneobj.c:1091
    // dereferences `desc->rot` unconditionally).
    bool Build(const AnimationClip& clip, int anim_id);

    bool valid() const { return data_.valid(); }
    int BoneCount() const { return data_.state.boneCount(); }
    int AnimId() const { return anim_id_; }
    bool Playing() const { return playing_; }

    // BoneObj_SetFrameRate 0x4D2D30 (src/lod/boneobj.c:86-90):
    // `gTicksPerFrame = kTicksPerSecond / fps;` - an unguarded divide, so fps 0
    // gives +inf and every subsequent advance is inf/NaN, exactly as in retail.
    void SetFrameRate(float fps);
    float TicksPerFrame() const { return ticks_per_frame_; }

    // Anim_Play HOOK 0x4D61D0 (src/boneobj/anim.c:38-58) with the IMMEDIATE
    // path, i.e. the blend argument every shipped caller passes (0). The fade
    // path is a different blend tree and is not modelled here; `AnimPlayModeFor`
    // in anim_rules.h reports which path a given blend would take.
    //
    // `flags` is Anim_Play's flags word: kAnimLoop (1) loops the instance,
    // kAnimReverse (2) plays backwards, 0 is a one-shot (see kAnimSlot*).
    bool Play(uint32_t flags);

    // Anim_PlayNow HOOK 0x4D6110 (src/boneobj/anim.c:13-16) = Anim_Play(..., 0).
    bool PlayNow() { return Play(0); }

    // One engine frame, in the retail order: POSE FIRST, ADVANCE SECOND.
    // `BoneMagicObj_Run` (src/freecam/bonemagic.c:101-112) calls
    // BoneObj_Update at :105 and BoneObj_GetEvents at :110; `HumanPlayer_CreateHandler`
    // (src/humanplayer/humanplayer.c:1214-1215) plays and then poses. Advancing
    // first would shift every frame by one tick.
    void Step();

    // Editor convenience, NOT engine logic: accumulate wall-clock seconds and
    // call Step() once per whole engine frame at the current frame rate. The
    // engine itself advances exactly one `gTicksPerFrame` per Step, which is why
    // it needs no accumulator.
    void AdvanceSeconds(float seconds);

    // Anim_SetSpeed HOOK 0x4D6580 (src/boneobj/anim.c:172-179):
    // `inst->speed = gTicksPerFrame * speed;` then the reverse bit is set iff
    // the result is negative.
    void SetSpeed(float multiplier);
    // Anim_SetTime HOOK 0x4D65B0 (src/boneobj/anim.c:181-185): a raw store.
    void SetTime(float ticks);

    // Anim_GetTime 0x4D6500 / Anim_GetLength 0x4D64E0 / Anim_GetPhase 0x4D6520 /
    // Anim_GetId 0x4D64C0 (src/boneobj/anim.c:137-161).
    float Time() const;
    float Length() const;
    float Phase() const;
    int Id() const { return anim_id_; }

    // The clip's end test, read the way `HumanPlayer_AnimDone` 0x489F50
    // (src/loadscreen/human.c:196-204) reads it: BONE_EVENTS_DONE on the bone.
    bool Done() const;

    // The pose, one glm matrix per engine bone, in METRES and in the model's own
    // frame (the object's world placement is applied by the caller, exactly as
    // `AnimationRegistry::EvaluateWorld`'s output is). This is a drop-in for
    // `Renderer::DrawSkinnedMesh`'s `boneWorldTransforms` and for
    // `Renderer::DrawAnimSkeleton`.
    //
    // The upper-left 3x3 is the engine's own `Bone::matrix` - already transposed
    // from its row-major layout, and NOT rebuilt from the quaternion, so it is
    // the same nine numbers `BoneMesh_Skin` multiplies by. The translation is
    // `Bone::pos / 40.96`.
    const std::vector<glm::mat4>& PoseModelLocal();
    // The same with the object's world orientation applied first, i.e.
    // `BoneObj_Update(m, o)` with a non-identity `m`. `m` is the engine's
    // ROW-MAJOR 3x3 (src/types.h:13-17), which is what a task's `matrix.m` is.
    const std::vector<glm::mat4>& PoseWithObjectRotation(const float m[9]);

    // The pose the LAST Step() produced, converted to the same glm form as
    // `PoseModelLocal` but WITHOUT re-running `BoneObj_Update`. This is the pose
    // retail actually draws: `BoneMagicObj_Run` (src/freecam/bonemagic.c:101-112)
    // poses at :105 and only then advances the clock at :110, so the bones hold
    // the frame that was posed while `inst.time` has already moved on. Calling
    // `PoseModelLocal` at draw time instead would sample the sampler at the
    // ALREADY-ADVANCED time and show every frame one engine frame late.
    // Before the first Step() this returns what `Play()` posed.
    const std::vector<glm::mat4>& PoseFromLastStep();

    // The bones as `BoneMesh_Skin` wants them: `Bone::matrix` (row-major 3x3)
    // and `Bone::pos`, in RAW units.
    const std::vector<BoneTransform>& RawBoneTransforms() const { return raw_bones_; }

    // The root bone's animation position, RAW units: the value
    // `BoneObj_GetRootMotion` 0x4D2FB0 hands to `Human_UpdateBody`
    // (src/soldier/soldier.c:870). `RootDelta` is the same frame's change.
    const float* RootPosition() const;
    const float* RootDelta() const;

    // The events this frame's advance crossed, in the engine's EventKey order
    // (`BoneObj_AnimCollectEvents` 0x4D4DC0, src/boneobj/boneobj.c:944-980).
    // Cleared by each Step().
    const std::vector<EngineBoneEvent>& Events() const { return events_; }

    // BoneMesh_Skin 0x49B700 (src/dxinit/d3drender.c:226-313) over the engine's
    // own vertex records. `vertices` holds `base_vertex_count` base vertices
    // followed by `extra_influence_count` extra influences, i.e. the engine's
    // two XTRV runs. `out` is resized to `base_vertex_count`.
    bool Skin(const SkinTransformInput* vertices, int base_vertex_count, int extra_influence_count, bool rigid,
              std::vector<SkinTransformOutput>& out) const;

    // The decoded engine records, for callers that need to inspect them.
    const EngineClipData& Data() const { return data_; }

private:
    // Converts the CURRENT `data_.state.bones` (whatever posed them last) into
    // `out` and `raw_bones_`. No engine call: `BoneObj_Update` is the caller's
    // decision, because posing twice at two different clock values is exactly
    // the one-frame shift `PoseFromLastStep` exists to avoid.
    void CapturePose(std::vector<glm::mat4>& out);
    // The engine's records are POINTER-linked: `EngineClip::pos/rot/events`,
    // every `RotTrack::keys/slerp/slerp2`, and the source's `clip`. `Build`
    // links them once; a copy or a move must link them again, at the new
    // object's own buffers, or the copy reads freed memory. Both the copy and
    // the move constructor/assignment below therefore go through here.
    void LinkEnginePointers();

    EngineClipData data_;
    std::vector<int> key_offsets_;   // per bone, plus one past the last
    int anim_id_ = -1;
    bool playing_ = false;
    float ticks_per_frame_ = kDefaultTicksPerFrame;

    // The single animation instance, on bone 0 (`Anim_Play(o, 0, ...)` is what
    // every shipped caller does: src/pistol/soldier.c:124, src/rpg/rpg.c:166,
    // src/humanplayer/humanplayer.c:1214, ...).
    int source_index_ = -1;
    int node_index_ = -1;

    std::vector<glm::mat4> pose_;
    std::vector<glm::mat4> pose_object_;
    std::vector<BoneTransform> raw_bones_;
    std::vector<EngineBoneEvent> events_;
    float advance_seconds_ = 0.0f;
    bool pose_dirty_ = true;

public:
    AnimPlayer() = default;
    AnimPlayer(const AnimPlayer& other) { AssignFrom(other); }
    AnimPlayer(AnimPlayer&& other) noexcept { AssignFrom(other); }
    AnimPlayer& operator=(const AnimPlayer& other) {
        if (this != &other) AssignFrom(other);
        return *this;
    }
    AnimPlayer& operator=(AnimPlayer&& other) noexcept {
        if (this != &other) AssignFrom(other);
        return *this;
    }

private:
    // Deep-copies the state and re-links the engine records at this object's
    // own buffers. Not a move: the clip data is small (a few thousand floats)
    // and the pointers make a shallow transfer unsafe.
    void AssignFrom(const AnimPlayer& other);
};

}  // namespace igi
