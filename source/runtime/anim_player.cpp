/******************************************************************************
 * @file    anim_player.cpp
 * @brief   The editor's animation playback path and skinning call (#73).
 *          See anim_player.h for the citation map and for what the BEF text
 *          carries and does not carry.
 *****************************************************************************/
#include "runtime/anim_player.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace igi {

// The engine's raw-units-per-metre, restated because `anim_rules.h` must not
// include a renderer header. `source/renderer/mef_native.h` carries the same
// number as `kMefNativeScale = 1.0f / 40.96f`; if either moves, both must.
static_assert(AnimPlayer::kRawUnitsPerMetre == 40.96f,
              "kRawUnitsPerMetre must stay the engine's 40.96 raw units per metre");

namespace {

// The engine's object matrix is a ROW-MAJOR 3x3 (src/types.h:13-17) plus a depth
// counter. `BoneObj_Update` only reads the nine floats, and `m.count` reaches
// the quaternion depth counter, which is renormalised only past 1000
// (src/boneobj/boneobj.c:457). A fresh task matrix is what the engine passes,
// and its count is whatever the last `mat_mul` left there; 0 is used here so the
// counter grows by exactly one per bone level, as it does for an object whose
// matrix was never multiplied.
constexpr int kFreshMatrixCount = 0;

const float kIdentity3x3[9] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};

// One engine frame's worth of wall-clock seconds, from gTicksPerFrame. This is
// `BoneObj_SetFrameRate` 0x4D2D30 (src/lod/boneobj.c:86-90) read backwards:
// gTicksPerFrame = 4800 / fps, so fps = 4800 / gTicksPerFrame. The divide is
// guarded here because the engine's own is not, and a NaN frame rate would spin
// the accumulator loop forever.
float FrameSeconds(float ticks_per_frame) {
    if (!std::isfinite(ticks_per_frame) || ticks_per_frame <= 0.0f)
        return 1.0f / AnimPlayer::kDefaultFps;
    return ticks_per_frame / AnimPlayer::kTicksPerSecond;
}

// A bound on `AdvanceSeconds`, so a bogus dt (a paused debugger, a resumed
// window) cannot run the engine thousands of frames in one call. Not engine
// logic; the engine has no accumulator at all.
constexpr int kMaxStepsPerAdvance = 240;

}  // namespace

// ---------------------------------------------------------------------------
// Build: the editor's clip in the engine's own records
// ---------------------------------------------------------------------------

bool AnimPlayer::Build(const AnimationClip& clip, int anim_id) {
    data_ = EngineClipData{};
    anim_id_ = anim_id;
    playing_ = false;
    source_index_ = -1;
    node_index_ = -1;
    pose_.clear();
    pose_object_.clear();
    raw_bones_.clear();
    events_.clear();
    advance_seconds_ = 0.0f;
    pose_dirty_ = true;

    if (clip.bones.empty()) return false;

    // The engine indexes bones by their position in the Bone array, which
    // `BoneObj_Build` fills from PLST/TLST in index order (src/lod/boneobj.c:153-173).
    // The BEF's `Bone(idx, ...)` carries that index explicitly, so the array is
    // sized to the largest index and every index is filled.
    int bone_count = 0;
    for (const AnimBone& b : clip.bones) {
        if (b.index >= 0 && b.index + 1 > bone_count) bone_count = b.index + 1;
    }
    if (bone_count <= 0) return false;

    std::vector<int> parents(static_cast<std::size_t>(bone_count), -1);
    std::vector<float> offsets(static_cast<std::size_t>(bone_count) * 3, 0.0f);
    for (const AnimBone& b : clip.bones) {
        if (b.index < 0 || b.index >= bone_count) continue;
        const std::size_t i = static_cast<std::size_t>(b.index);
        // The BEF's parent field is PLST (verified against 000.IFF: parents
        // (-1, 0, 1, 2, 3, 4, 4, 6, ...) match the BEF's Bone() third field).
        parents[i] = (b.parent >= -1 && b.parent < bone_count) ? b.parent : -1;
        // The BEF's rest position is TLST / 40.96 (verified against 000.IFF:
        // bone 1 raw TLST = (-0.513892, 183.352112, 553.557983) and the BEF has
        // (-0.0125462, 4.47637, 13.5146) = that / 40.96). The pipeline runs in
        // RAW units, like the engine, so multiply back.
        offsets[i * 3 + 0] = b.restPos.x * kRawUnitsPerMetre;
        offsets[i * 3 + 1] = b.restPos.y * kRawUnitsPerMetre;
        offsets[i * 3 + 2] = b.restPos.z * kRawUnitsPerMetre;
    }
    BoneObjBuild(data_.state, parents.data(), offsets.data(), bone_count);

    // --- the position track (BOTD) -----------------------------------------
    // One track per clip: `AnimDesc::pos` (src/boneobj/boneobjtypes.h:44-50).
    // The engine stores `out` and `in` tangents per key (40 bytes); the BEF emits
    // only the position and the time, so the tangents are zero. The pose does not
    // read this track at all - `BoneObj_UpdateBone` places bones from their rest
    // offsets (src/boneobj/boneobj.c:434-439) - so the only effect is on the
    // root-motion value and the wrap-tick fixup.
    data_.pos_keys.clear();
    for (const AnimTranslationKey& k : clip.translationKeys) {
        PosKey key;
        key.pos[0] = k.pos.x * kRawUnitsPerMetre;
        key.pos[1] = k.pos.y * kRawUnitsPerMetre;
        key.pos[2] = k.pos.z * kRawUnitsPerMetre;
        key.time = static_cast<float>(k.time_ms);   // TICKS, not milliseconds
        key.out[0] = key.out[1] = key.out[2] = 0.0f;
        key.in[0] = key.in[1] = key.in[2] = 0.0f;
        data_.pos_keys.push_back(key);
    }
    if (data_.pos_keys.empty()) {
        // `BoneObj_Load` allocates `d->pos` unconditionally and `InitAnimInst`
        // dereferences `keys[count - 1]`, so a clip with no position key is an
        // access violation in retail (src/boneobj/boneobj.c:1598, 1014). Every
        // shipped clip has at least one key at time 0; the port refuses.
        return false;
    }
    data_.pos_track.keys = data_.pos_keys.data();
    data_.pos_track.count = static_cast<int>(data_.pos_keys.size());

    // --- the rotation tracks (BORD) ----------------------------------------
    std::vector<std::vector<const AnimRotationKey*>> by_bone(static_cast<std::size_t>(bone_count));
    for (const AnimRotationKey& k : clip.rotationKeys) {
        if (k.bone < 0 || k.bone >= bone_count) continue;
        by_bone[static_cast<std::size_t>(k.bone)].push_back(&k);
    }
    data_.rot_keys.clear();
    data_.rot_tracks.assign(static_cast<std::size_t>(bone_count), RotTrack{});
    std::vector<int> key_offsets(static_cast<std::size_t>(bone_count) + 1, 0);
    for (int b = 0; b < bone_count; ++b) {
        std::vector<const AnimRotationKey*>& keys = by_bone[static_cast<std::size_t>(b)];
        // The engine's sampler assumes each track's keys are in ascending time -
        // its search breaks at the first key whose time is not `< tm`
        // (src/boneobj/boneobj.c:1104-1108) - and igi1conv writes them that way.
        // A stable sort keeps that invariant if a BEF ever violates it.
        std::stable_sort(keys.begin(), keys.end(),
                         [](const AnimRotationKey* a, const AnimRotationKey* b) {
                             return a->time_ms < b->time_ms;
                         });
        key_offsets[static_cast<std::size_t>(b)] = static_cast<int>(data_.rot_keys.size());
        for (const AnimRotationKey* k : keys) {
            RotKey key;
            // BEF `RotationKeyFrameData(bone, flag, time, q0, q1, q2)` carries
            // all three engine quaternions: q0 is the key, q1 the OUT tangent
            // (`a`) and q2 the IN tangent (`b`). Verified against 000.IFF
            // clip 204 bone 0, key 1.
            key.q[0] = k->q0.x;
            key.q[1] = k->q0.y;
            key.q[2] = k->q0.z;
            key.q[3] = k->q0.w;
            key.time = static_cast<float>(k->time_ms);   // TICKS
            key.a[0] = k->q1.x;
            key.a[1] = k->q1.y;
            key.a[2] = k->q1.z;
            key.a[3] = k->q1.w;
            key.b[0] = k->q2.x;
            key.b[1] = k->q2.y;
            key.b[2] = k->q2.z;
            key.b[3] = k->q2.w;
            data_.rot_keys.push_back(key);
        }
        data_.rot_tracks[static_cast<std::size_t>(b)].count = static_cast<int>(keys.size());
        // Pointers are set after the vector stops growing.
    }
    key_offsets[static_cast<std::size_t>(bone_count)] = static_cast<int>(data_.rot_keys.size());

    // --- the events (BOED) -------------------------------------------------
    data_.event_keys.clear();
    for (const AnimEvent& e : clip.events) {
        EventKey key;
        key.id = e.event_id;
        // The engine's EventKey::unk4 is never read - `BoneObj_AnimCollectEvents`
        // copies `id` and `time` only (src/boneobj/boneobj.c:970-974) - so the
        // BEF's third integer is parked here rather than dropped.
        key.unk4 = e.bone_id;
        key.time = static_cast<float>(e.time_ms);   // TICKS
        key.pos[0] = e.pos.x * kRawUnitsPerMetre;
        key.pos[1] = e.pos.y * kRawUnitsPerMetre;
        key.pos[2] = e.pos.z * kRawUnitsPerMetre;
        data_.event_keys.push_back(key);
    }

    // --- the clip record (AnimDesc) ----------------------------------------
    // `AnimInit(name, flags, length, tp_flag)`: the engine length is
    // `length - 1` for all 233 shipped clips, and `tp_flag` is the ANIM_LOOPS
    // bit for all 233. The BEF's second field is 0 everywhere, so the rest of
    // the flags word is not recoverable - only ANIM_LOOPS is read by the
    // playback path (src/boneobj/boneobj.c:1117).
    data_.clip.length = clip.length_ms > 0 ? static_cast<float>(clip.length_ms - 1) : 0.0f;
    data_.clip.flags = clip.tp_flag != 0 ? kAnimDescLoops : 0u;

    // The per-bone key offsets have to be in place BEFORE the links below,
    // because the links are derived from them: `RotTrack::keys` for bone b is
    // `rot_keys + key_offsets_[b]`. The engine has the same dependency - the
    // loader links `rot[b].keys` from the per-bone counts it just read
    // (boneobj.c:1616-1621) and only THEN calls Anim_PrepareSlerps on them
    // (boneobj.c:1632).
    key_offsets_ = key_offsets;

    // Every pointer link the engine's records need, at THIS object's buffers.
    // This runs BEFORE `Anim_PrepareSlerps` because that walk reads
    // `clip.rot[b].count` and `clip.rot[b].keys`, and `clip.rot` is one of the
    // links set here (haze/anim.c:60-62 walks `set->clips[i].rot[b]` after the
    // loader has linked it, boneobj.c:1626-1632).
    LinkEnginePointers();

    // --- Anim_PrepareSlerps 0x4D6D40 ---------------------------------------
    // The engine builds these at LOAD time, once per skeleton, for every clip
    // (src/boneobj/boneobj.c:1632). Doing it here is the same thing for a
    // one-clip set.
    const int total_keys = key_offsets_[static_cast<std::size_t>(bone_count)];
    data_.slerp.assign(static_cast<std::size_t>(total_keys), SlerpInfo{});
    data_.slerp2.assign(static_cast<std::size_t>(total_keys), SlerpInfo{});
    AnimPrepareSlerps(data_.clip, bone_count, kAnimSmoothAnimsDefault, data_.slerp.data(),
                      data_.slerp2.data(), key_offsets_.data(), nullptr);

    // Link again: the two tables above are NEW buffers, so every
    // `RotTrack::slerp`/`slerp2` has to be re-derived at them. (The engine
    // stores those pointers inside the tracks at load time, haze/anim.c:62-84.)
    LinkEnginePointers();

    ticks_per_frame_ = kDefaultTicksPerFrame;
    return data_.valid();
}

void AnimPlayer::LinkEnginePointers() {
    // `AnimDesc::pos/rot/events`, each `RotTrack::keys/slerp/slerp2`, and each
    // source's `clip`. Nothing here computes a value; it only restores the links
    // `BoneObj_Load` 0x4D67A0 makes at load time, so a copied or moved player
    // reads its own memory.
    data_.pos_track.keys = data_.pos_keys.empty() ? nullptr : data_.pos_keys.data();
    data_.pos_track.count = static_cast<int>(data_.pos_keys.size());
    data_.event_track.keys = data_.event_keys.empty() ? nullptr : data_.event_keys.data();
    data_.event_track.count = static_cast<int>(data_.event_keys.size());

    data_.clip.pos = data_.pos_keys.empty() ? nullptr : &data_.pos_track;
    data_.clip.rot = data_.rot_tracks.empty() ? nullptr : data_.rot_tracks.data();
    data_.clip.events = data_.event_keys.empty() ? nullptr : &data_.event_track;

    const std::size_t bones = data_.rot_tracks.size();
    for (std::size_t b = 0; b < bones; ++b) {
        RotTrack& t = data_.rot_tracks[b];
        const bool have_offsets = b + 1 < key_offsets_.size();
        if (!have_offsets || data_.rot_keys.empty()) {
            t.keys = nullptr;
            t.slerp = nullptr;
            t.slerp2 = nullptr;
            continue;
        }
        const int off = key_offsets_[b];
        t.keys = data_.rot_keys.data() + off;
        if (t.count > 1 && !data_.slerp.empty()) {
            t.slerp = data_.slerp.data() + off;
            t.slerp2 = data_.slerp2.empty() ? nullptr : data_.slerp2.data() + off;
        } else {
            t.slerp = nullptr;
            t.slerp2 = nullptr;
        }
    }
    for (BlendSourceAnim& source : data_.state.sources) source.clip = &data_.clip;
}

void AnimPlayer::AssignFrom(const AnimPlayer& other) {
    data_ = other.data_;
    key_offsets_ = other.key_offsets_;
    anim_id_ = other.anim_id_;
    playing_ = other.playing_;
    ticks_per_frame_ = other.ticks_per_frame_;
    source_index_ = other.source_index_;
    node_index_ = other.node_index_;
    pose_ = other.pose_;
    pose_object_ = other.pose_object_;
    raw_bones_ = other.raw_bones_;
    events_ = other.events_;
    advance_seconds_ = other.advance_seconds_;
    pose_dirty_ = other.pose_dirty_;
    pose_steps_ = other.pose_steps_;
    LinkEnginePointers();
}

// ---------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------

void AnimPlayer::SetFrameRate(float fps) {
    // src/lod/boneobj.c:88 `gTicksPerFrame = kTicksPerSecond / fps;` - an
    // unguarded divide, so fps 0 gives +inf and every advance becomes inf/NaN,
    // exactly as in retail. Reproduced, not clamped.
    ticks_per_frame_ = kAnimTicksPerSecond / fps;
}

bool AnimPlayer::Play(uint32_t flags) {
    if (!data_.valid()) return false;

    // Anim_Play 0x4D61D0 (src/boneobj/anim.c:38-58) with the IMMEDIATE path.
    // `t = blend * kTicksPerSecond / gTicksPerFrame` with blend 0 is 0, and the
    // branch test is `if (old || b->parent) { if (!(t < 1.0f)) { fade; } }`:
    // `!(0 < 1)` is false, so the fade is not taken and the old node - there is
    // none, the tree is reset here - is deleted. The tail installs the node and
    // sets the flags.
    data_.state.nodes.clear();
    data_.state.sources.clear();
    for (EngineBone& b : data_.state.bones) b.blend = -1;

    data_.state.sources.resize(1);
    BlendSourceAnim& source = data_.state.sources[0];
    source.clip = &data_.clip;
    // BoneObj_NewAnimSource 0x4D47D0 passes `flags | ANIM_FIRST`
    // (src/boneobj/boneobj.c:781), and Anim_Play(o, 0, ...) is what every
    // shipped caller uses.
    if (!InitAnimInstFromClip(source.inst, data_.clip, 0, anim_id_, flags | kAnimFirst, ticks_per_frame_, 0,
                              kAnimSmoothAnimsDefault)) {
        data_.state.sources.clear();
        return false;
    }
    source_index_ = 0;

    // The blend node: branch 0 is the source, branch 1 is BRANCH_NONE
    // (src/boneobj/boneobj.c:54-56 `BoneObj_SetBlendNode(n, NULL, BRANCH_SOURCE,
    // src, BRANCH_NONE, NULL, 0, ...)`). `BlendNodeState` has no weight/free/
    // select/data because those belong to the fade tree.
    data_.state.nodes.resize(1);
    BlendNodeState& node = data_.state.nodes[0];
    node.branchType[0] = kBranchSource;
    node.branchSource[0] = 0;
    node.branchType[1] = kBranchNone;
    node.branchSource[1] = -1;
    node.branchNode[0] = -1;
    node.branchNode[1] = -1;
    node_index_ = 0;

    // src/boneobj/anim.c:56-57.
    data_.state.bones[0].blend = 0;
    data_.state.bones[0].flags = (data_.state.bones[0].flags & kAnimPlayFlagsMask) | kBoneAnimated;

    playing_ = true;
    pose_dirty_ = true;
    events_.clear();
    // The engine poses once immediately after playing (src/humanplayer/humanplayer.c:1214-1215
    // plays and then calls BoneObj_Update), so the first frame is never a rest
    // pose.
    PoseModelLocal();
    return true;
}

void AnimPlayer::Step() {
    if (!data_.valid() || !playing_) return;

    // Retail order: BoneObj_Update first, BoneObj_GetEvents second. See
    // `BoneMagicObj_Run` 0x531620 (src/freecam/bonemagic.c:101-112):
    // BoneObj_Update at :105, BoneObj_GetEvents at :110. The engine poses the
    // frame that is about to be drawn and only then moves the clock on.
    ++pose_steps_;   // see PoseSteps(): this is the expensive half of a step
    if (!BoneObjUpdate(data_.state, kIdentity3x3, kFreshMatrixCount)) {
        playing_ = false;
        return;
    }
    pose_dirty_ = true;
    events_.clear();
    BoneObjGetEvents(events_, data_.state, data_.clip, kAnimSmoothAnimsDefault);
}

bool AnimPlayer::StepPose() {
    if (!data_.valid()) return false;
    // Human_Wake 0x45F240 (src/humanbase/human.c:289-297) is exactly this: a
    // pose of the current clock with no advance. The clock is NOT moved here.
    ++pose_steps_;   // see PoseSteps()
    if (!BoneObjUpdate(data_.state, kIdentity3x3, kFreshMatrixCount)) return false;
    pose_dirty_ = true;
    return true;
}

void AnimPlayer::StepClock() {
    if (!data_.valid() || !playing_) return;

    // The clock half of Step() alone, i.e. `Human_FireAnimTrigger` 0x4613D0
    // (src/humanbase/human.c:1368-1387) -> `BoneObj_GetEvents` 0x4D3210
    // (src/boneobj/boneobj.c:112-129). No pose: `BoneObj_GetEvents` reads only
    // `bone->blend` and the blend tree, and it is what sets BONE_EVENTS_DONE on
    // the root bone, so `Done()` stays correct.
    events_.clear();
    BoneObjGetEvents(events_, data_.state, data_.clip, kAnimSmoothAnimsDefault);
}

int AnimPlayer::AdvanceSeconds(float seconds) {
    if (!playing_) return 0;
    if (!(seconds > 0.0f)) return 0;
    advance_seconds_ += seconds;
    const float frame_seconds = FrameSeconds(ticks_per_frame_);
    if (!(frame_seconds > 0.0f)) return 0;

    // How many whole engine frames the banked wall clock covers, with the same
    // one-frame-at-a-time subtraction (and the same bound) the per-frame loop
    // used, so the banked remainder stays the float it has always been.
    int due = 0;
    while (advance_seconds_ >= frame_seconds && due < kMaxStepsPerAdvance) {
        advance_seconds_ -= frame_seconds;
        ++due;
    }
    if (due >= kMaxStepsPerAdvance) advance_seconds_ = 0.0f;

    // ONE POSE FOR THE FRAME THAT IS DRAWN, clock-only for the rest - see the
    // header for the engine rule and why this is not a behaviour change. The
    // drawn frame is the LAST one: the editor draws the frame the engine's
    // pose-then-advance step produced (`BoneMagicObj_Run` 0x531620,
    // src/freecam/bonemagic.c:105,110).
    int steps = 0;
    for (int i = 0; i < due; ++i) {
        if (i + 1 < due) StepClock();
        else Step();
        ++steps;
        if (!playing_) break;
    }
    return steps;
}

int AnimPlayer::AdvanceSecondsClockOnly(float seconds) {
    // Same frame arithmetic and the same accumulator as AdvanceSeconds, so a
    // playback that was clock-only for N frames and then starts posing poses at
    // exactly the tick it would have reached had it posed all along.
    if (!playing_) return 0;
    if (!(seconds > 0.0f)) return 0;
    advance_seconds_ += seconds;
    const float frame_seconds = FrameSeconds(ticks_per_frame_);
    if (!(frame_seconds > 0.0f)) return 0;
    int steps = 0;
    while (advance_seconds_ >= frame_seconds && steps < kMaxStepsPerAdvance) {
        StepClock();
        advance_seconds_ -= frame_seconds;
        ++steps;
        if (!playing_) break;
    }
    if (steps >= kMaxStepsPerAdvance) advance_seconds_ = 0.0f;
    return steps;
}

void AnimPlayer::SetSpeed(float multiplier) {
    // Anim_SetSpeed 0x4D6580 (src/boneobj/anim.c:172-179) with
    // BoneObj_UpdateAnimDirection (anim.c:163-169).
    if (source_index_ < 0 || static_cast<std::size_t>(source_index_) >= data_.state.sources.size()) return;
    AnimSetSpeed(data_.state.sources[static_cast<std::size_t>(source_index_)].inst, multiplier,
                 ticks_per_frame_);
}

void AnimPlayer::SetTime(float ticks) {
    // Anim_SetTime 0x4D65B0 (src/boneobj/anim.c:181-185).
    if (source_index_ < 0 || static_cast<std::size_t>(source_index_) >= data_.state.sources.size()) return;
    AnimSetTime(data_.state.sources[static_cast<std::size_t>(source_index_)].inst, ticks);
    pose_dirty_ = true;
}

float AnimPlayer::Time() const {
    // Anim_GetTime 0x4D6500 (src/boneobj/anim.c:149-153).
    if (source_index_ < 0 || static_cast<std::size_t>(source_index_) >= data_.state.sources.size())
        return 0.0f;
    return data_.state.sources[static_cast<std::size_t>(source_index_)].inst.time;
}

float AnimPlayer::Length() const {
    // Anim_GetLength 0x4D64E0 (src/boneobj/anim.c:143-147) reads
    // `Anim_GetInst(o, track)->desc->length`, i.e. the float TICK length.
    return data_.clip.length;
}

float AnimPlayer::Phase() const {
    // Anim_GetPhase 0x4D6520 (src/boneobj/anim.c:155-161).
    return AnimPhase(Time(), Length());
}

bool AnimPlayer::Done() const {
    // HumanPlayer_AnimDone 0x489F50 (src/loadscreen/human.c:196-204) reads
    // BONE_EVENTS_DONE off the root bone's flags.
    if (data_.state.bones.empty()) return true;
    return HumanAnimDone(data_.state.bones[0].flags);
}

const float* AnimPlayer::RootPosition() const {
    // BoneObj_GetRootMotion 0x4D2FB0 (src/lod/boneobj.c:202-211) hands the
    // instance's position to the caller; src/soldier/soldier.c:870 is the
    // shipped consumer.
    static const float kZero[3] = {0.0f, 0.0f, 0.0f};
    if (source_index_ < 0 || static_cast<std::size_t>(source_index_) >= data_.state.sources.size())
        return kZero;
    return data_.state.sources[static_cast<std::size_t>(source_index_)].inst.pos;
}

const float* AnimPlayer::RootDelta() const {
    static const float kZero[3] = {0.0f, 0.0f, 0.0f};
    if (source_index_ < 0 || static_cast<std::size_t>(source_index_) >= data_.state.sources.size())
        return kZero;
    return data_.state.sources[static_cast<std::size_t>(source_index_)].inst.delta;
}

// ---------------------------------------------------------------------------
// The pose
// ---------------------------------------------------------------------------

namespace {

// `BoneMesh_Skin` multiplies `Bone::matrix` (row-major 3x3) by the vertex and
// adds `Bone::pos` (src/dxinit/d3drender.c:259-264). The renderer's
// `boneWorldTransforms` are glm matrices, which are COLUMN-major, so entry
// (row r, column c) has to land at `M[c][r]`. The nine numbers themselves are
// the engine's, not a re-derivation from the quaternion - `glm::mat4_cast` would
// be a different rounding.
void FillPoseFromBones(const std::vector<EngineBone>& bones, std::vector<glm::mat4>& out) {
    out.assign(bones.size(), glm::mat4(1.0f));
    for (std::size_t i = 0; i < bones.size(); ++i) {
        const EngineBone& b = bones[i];
        glm::mat4& m = out[i];
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) m[c][r] = b.matrix[r * 3 + c];
        }
        m[3][0] = b.pos[0] / AnimPlayer::kRawUnitsPerMetre;
        m[3][1] = b.pos[1] / AnimPlayer::kRawUnitsPerMetre;
        m[3][2] = b.pos[2] / AnimPlayer::kRawUnitsPerMetre;
        m[3][3] = 1.0f;
    }
}

}  // namespace

const std::vector<glm::mat4>& AnimPlayer::PoseModelLocal() {
    if (!data_.valid()) {
        pose_.clear();
        return pose_;
    }
    // The engine poses against the OBJECT's world matrix, so a model-local pose
    // is `BoneObj_Update` with the identity: the root quaternion comes out as
    // identity (boneobj.c:382-389, t = 3, s = 2, q = (0,0,0,1)) and every bone is
    // then placed in the model's own frame. This is the same space
    // `AnimationRegistry::EvaluateWorld` emits, which is what the caller's
    // `objMat` expects.
    ++pose_steps_;   // see PoseSteps()
    BoneObjUpdate(data_.state, kIdentity3x3, kFreshMatrixCount);
    CapturePose(pose_);
    return pose_;
}

const std::vector<glm::mat4>& AnimPlayer::PoseWithObjectRotation(const float m[9]) {
    if (!data_.valid()) {
        pose_object_.clear();
        return pose_object_;
    }
    ++pose_steps_;   // see PoseSteps()
    BoneObjUpdate(data_.state, m, kFreshMatrixCount);
    CapturePose(pose_object_);
    return pose_object_;
}

const std::vector<glm::mat4>& AnimPlayer::PoseFromLastStep() {
    if (!data_.valid()) {
        pose_.clear();
        return pose_;
    }
    CapturePose(pose_);
    return pose_;
}

void AnimPlayer::CapturePose(std::vector<glm::mat4>& out) {
    FillPoseFromBones(data_.state.bones, out);
    raw_bones_.assign(data_.state.bones.size(), BoneTransform{});
    for (std::size_t i = 0; i < data_.state.bones.size(); ++i) {
        std::memcpy(raw_bones_[i].matrix, data_.state.bones[i].matrix, sizeof(raw_bones_[i].matrix));
        raw_bones_[i].pos[0] = data_.state.bones[i].pos[0];
        raw_bones_[i].pos[1] = data_.state.bones[i].pos[1];
        raw_bones_[i].pos[2] = data_.state.bones[i].pos[2];
    }
    pose_dirty_ = false;
}

// ---------------------------------------------------------------------------
// The skinning call
// ---------------------------------------------------------------------------

bool AnimPlayer::Skin(const SkinTransformInput* vertices, int base_vertex_count, int extra_influence_count,
                      bool rigid, std::vector<SkinTransformOutput>& out) const {
    // BoneMesh_Skin 0x49B700. The bone records have to be current, so a caller
    // that has only just called SetTime gets them re-derived here; posing does
    // not touch the clock.
    if (!data_.valid()) return false;
    if (raw_bones_.size() != data_.state.bones.size()) {
        const_cast<AnimPlayer*>(this)->PoseModelLocal();
    }
    return BoneMeshSkin(vertices, base_vertex_count, extra_influence_count, raw_bones_.data(),
                        static_cast<int>(raw_bones_.size()), rigid, out);
}

// ---------------------------------------------------------------------------
// The editor's entry point
// ---------------------------------------------------------------------------

// Declared in source/animation.h, next to the editor's own `AnimationClip`.
// `AnimPlayback` (source/runtime/anim_playback.cpp) builds its player through
// `AnimPlayer::Build` directly, so this convenience wrapper has no caller in the
// editor; it is kept for the engine-side tests and for a caller that just wants
// one player for one clip.
AnimPlayer MakeEngineAnimPlayer(const AnimationClip& clip) {
    AnimPlayer player;
    player.Build(clip, clip.animId);
    return player;
}

}  // namespace igi
