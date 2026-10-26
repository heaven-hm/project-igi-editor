// anim_rules.h - the retail animation player's rules, resolved the way IGI-RE
// resolves them.
//
// Issue #73 is titled "Animation playback + skinning". The word "skinning" is
// open-igi's class name: IGI-RE has NO symbol called "skinning". It has
// BoneMesh_Skin 0x49B700 (src/dxinit/d3drender.c:226), and that is what this
// module ports on the skin side. Say that plainly: this is a port of the
// retail rules, not of open-igi's structure.
//
// What a level/object EDITOR actually needs from the retail animation system is
// the narrow slice this module covers:
//
//   * how an animation is NAMED in authored data and resolved to an engine
//     animation id (the animation table and its lookup);
//   * the failure mode when the id does not resolve (there is exactly one
//     guard, Anim_Exists, and exactly one warning string);
//   * the frame/tick timing rules: the engine's tick rate, the per-frame
//     advance, loop-vs-one-shot and the wrap/clamp arithmetic;
//   * what GenericTBA_GetAnim 0x4485D0 returns and what its callers do with it;
//   * the bone/skin data layout and the skinning transform, as far as IGI-RE
//     actually exposes them.
//
// It deliberately does NOT cover the AI action system, combat animation
// selection or the network path: those are out of scope for an editor. Where
// the AI path is mentioned it is only as the provenance of a number.
//
// Everything here is cited to IGI-RE (`/Users/haseeb-mir/Documents/Code/IGI-RE`),
// the verified C decompilation of retail `IGI.exe`. The IGI-RE files this
// module is built from:
//
//   src/boneobj/anim.c        Anim_Play 0x4D61D0, Anim_PlayNow 0x4D6110,
//                             Anim_PlayOriented 0x4D6130, Anim_FadeOut 0x4D62B0,
//                             Anim_GetInst 0x4D6460, Anim_GetId 0x4D64C0,
//                             Anim_GetLength 0x4D64E0, Anim_GetTime 0x4D6500,
//                             Anim_GetPhase 0x4D6520, Anim_SetSpeed 0x4D6580,
//                             Anim_SetTime 0x4D65B0, kTicksPerSecond
//   src/boneobj/boneobj.c     BoneObj_NewAnimSource 0x4D47D0,
//                             BoneObj_AllocAnimInst 0x4D4F20 (the 0x34-byte pool
//                             the instance comes from; not ported - the editor
//                             owns the instance),
//                             BoneObj_InitAnimInst 0x4D4F60,
//                             BoneObj_AnimEventsCB 0x4D4B60 (the frame advance),
//                             BoneObj_AnimPositionCB 0x4D4840,
//                             BoneObj_AnimOrientationCB 0x4D5180,
//                             BoneObj_Load 0x4D67A0 (the table builder)
//   src/haze/anim.c           Anim_PrepareSlerps 0x4D6D40, Anim_GetPosKey 0x4D6FC0,
//                             Anim_GetPosKeyCount 0x4D7020,
//                             Anim_GetLengthTicks 0x4D7040,
//                             Anim_GetLengthSeconds 0x4D74D0,
//                             Anim_Exists 0x4D74F0 (the only missing-anim guard)
//   src/lod/boneobj.c         BoneObj_Init 0x4D2C30, BoneObj_SetFrameRate 0x4D2D30,
//                             BoneObj_GetTicksPerFrame 0x4D2D50,
//                             BoneObj_Create 0x4D2D60
//   src/glass/tba.c           GenericTBA_Open 0x447DB0 (the param table),
//                             GenericTBA_Create 0x447FF0 (the defaults),
//                             GenericTBA_GetAnim 0x4485D0, GenericTBA_GetUseTime 0x4485C0
//   src/humanstate/humanstate.c  the GenericTBA callers: TypeComputerEnter
//                             0x40C670, Use35Enter 0x40CF70, Use36Enter 0x40D180,
//                             and FRAMES at :520
//   src/loadscreen/human.c    Human_PlayAnimSlot 0x489E70, Human_PlayAnim 0x489EA0,
//                             Human_PlayWeaponAnim 0x489ED0, Human_StopWeaponAnim 0x489F00,
//                             HumanPlayer_PlayAnim 0x489F20, HumanPlayer_AnimDone 0x489F50
//   src/weapon/humanplayer.c  HumanPlayer_SetWeaponAnim 0x4137E0,
//                             HumanPlayer_RequestWeaponAnim 0x413A60
//   src/pistol/soldier.c      Soldier_StandEnter 0x45A930 (the "Stand Animation" rule)
//   src/soldier/soldier.c     Soldier_Open 0x45CAB0 (the HumanSoldier param table),
//                             Soldier_AnimEnter 0x45C750, Soldier_UpdateInput 0x45CCF0,
//                             Soldier_GetDeathVelocity 0x45D5B0 (kFps)
//   src/humanplayer/humanplayer.c HumanPlayer_CreateHandler 0x410370 (skeleton 0)
//   src/game/game.c           BoneObj_SetFrameRate(30.0f) at :449
//   src/dxinit/d3drender.c    BoneMesh_Skin 0x49B700, SkinVertex, NormalVertex
//   src/level/level.c         Level_NewTask 0x4F0780 (how authored args reach fields)
//   src/physobj/paramops.c    ParamReal32x9_Parse 0x4EFBA0 (3 tokens, not 9)
//
// Named divergences (never silently fixed):
//   * `BoneObj_NewAnimSource` (boneobj.c:778) indexes `gAnimSets[skel].byId[id]`
//     with NO bounds check and NO NULL check, and `BoneObj_InitAnimInst`
//     (boneobj.c:1014,1018) dereferences the result. A missing or negative id is
//     an access violation in retail. This port returns an explicit error instead
//     and reports it; the rule (no guard on the play path) is preserved.
//   * `Anim_Exists` (haze/anim.c:244-255) checks `id >= set->count` but NOT
//     `id < 0`, so a negative id passes the guard and is read out of bounds.
//     Reproduced as a distinguishable result, not "fixed".
//   * `fistpll` in `Anim_GetLengthTicks` (haze/anim.c:112) rounds with the x87
//     rounding mode, round-to-nearest-even; the port reproduces that, including
//     the "integer indefinite" result on overflow.
//   * `BoneMesh_Skin`'s position sum is rounded to FLOAT before the weight
//     multiply (d3drender.c:266-267: `long double X = x + b->pos.x` where both
//     are float, then `o->x = X * v->w`). The port reproduces that intermediate
//     rounding rather than computing in double throughout.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace igi {

// ---------------------------------------------------------------------------
// Engine constants. Every one is IGI-RE DATA.
// ---------------------------------------------------------------------------

// anim.c:9 (and the same literal at lod/boneobj.c:39) - the header comment of
// anim.c states it outright: "Times are in ticks, 4800 a second."
constexpr float kAnimTicksPerSecond = 4800.0f;

// haze/anim.c:12 `kSecondsPerTick = 0x1.b4e81cp-13f`. The nearest float to
// 1/4800 (0.00020833333837799728f); it is NOT exactly 1/4800.
constexpr float kAnimSecondsPerTick = 0x1.b4e81cp-13f;

// The frame rate the game installs once, at game.c:449:
// `BoneObj_SetFrameRate(30.0f)`. lod/boneobj.c:88 then computes
// `gTicksPerFrame = kTicksPerSecond / fps` = 160.0f exactly.
constexpr float kAnimDefaultFps = 30.0f;
constexpr float kAnimDefaultTicksPerFrame = kAnimTicksPerSecond / kAnimDefaultFps;  // 160.0f

// The three copies of the same frame rate the engine keeps in other modules:
// humanstate.c:91 `#define FPS 30.0f`, soldier/soldier.c:145 `kFps = 30.0f`,
// aiaction/aiaction.c:159 `kFpsLocal = 30.0`. They are separate literals, not
// one shared constant, and none of them is derived from gTicksPerFrame.
constexpr float kAnimHumanStateFps = 30.0f;
constexpr float kAnimSoldierFps = 30.0f;
constexpr double kAnimAiFps = 30.0;

// AnimInst::flags bits (boneobj.h:75-77).
constexpr uint32_t kAnimLoop = 1;         // ANIM_LOOP
constexpr uint32_t kAnimReverse = 2;      // ANIM_REVERSE
constexpr uint32_t kAnimFirst = 0x8000;   // ANIM_FIRST

// AnimDesc::flags bit (boneobj.h:65). Note the plural: this is the CLIP's own
// flag, read from the BOBJ BOAH chunk, and it is a different thing from the
// per-instance ANIM_LOOP that a caller passes to Anim_Play.
constexpr uint32_t kAnimDescLoops = 0x80000000u;  // ANIM_LOOPS

// Bone::flags bits (boneobj.h:48-51).
constexpr uint32_t kBoneEventsDone = 0x100;
constexpr uint32_t kBoneFading = 0x10000;
constexpr uint32_t kBoneAnimated = 0x20000;
constexpr uint32_t kBoneOriented = 0x40000;

// anim.c:57 `b->flags = (b->flags & 0xFFFAFEFF) | BONE_ANIMATED`. The mask is
// ~0x00050100, so it clears BONE_EVENTS_DONE (0x100), BONE_FADING (0x10000) and
// BONE_ORIENTED (0x40000). It does NOT clear BONE_ANIMATED (0x20000) - that is
// the bit the expression then SETS. Note it clears the event bit as well, which
// is not obvious from the fade/animate intent. BoneObj_BlendTo (boneobj.c:1413)
// uses the same mask but ORs BONE_FADING instead.
constexpr uint32_t kAnimPlayFlagsMask = 0xFFFAFEFFu;

// The authored parameter name of the animation a GenericTBA object plays, and
// of the time it plays it for. Retail LEVEL2/objects.qsc:53-54 declares them;
// tba.c:90-95 registers the matching fields.
constexpr const char* kGenericTbaAnimName = "Activate Anim";          // Int16
constexpr const char* kGenericTbaUseTimeName = "Activate Time (s)";   // Real32

// GenericTBA's own five extended parameters, in ParamTable_Add order
// (tba.c:91-95). "Activate Anim" is slot 2 and "Activate Time (s)" slot 3.
constexpr int kGenericTbaExtraParams = 5;
constexpr int kGenericTbaParamCount = 13 + kGenericTbaExtraParams;  // 18

// Token index of the two anim-bearing values in the editor's argTokens layout
// ([0]=task id, [1]=type, [2]=name, [3]=first declared parameter). Position and
// Orientation each consume THREE tokens (paramops.c:423-435), then fourteen
// single-token parameters precede "Activate Anim". See ParseGenericTbaAnim.
constexpr int kGenericTbaAnimToken = 22;
constexpr int kGenericTbaUseTimeToken = 23;

// The authored name of a HumanSoldier-family soldier's stand animation, and of
// its bone hierarchy (retail LEVEL2/objects.qsc:47). In the editor's argTokens
// layout these are indices 9 and 10.
constexpr const char* kSoldierBoneHierarchyName = "Bone Heirachy";  // sic, retail typo
constexpr const char* kSoldierStandAnimationName = "Stand Animation";
constexpr int kSoldierBoneHierarchyToken = 9;
constexpr int kSoldierStandAnimationToken = 10;

// HumanPlayer has no "Bone Heirachy" parameter at all: the engine hardcodes
// skeleton 0 for it (humanplayer.c:1212) and its initial animation id 2
// (humanplayer.c:1214). A soldier's equivalents are its authored
// "Bone Heirachy" and "Stand Animation".
constexpr int kHumanPlayerSkeleton = 0;
constexpr int kHumanPlayerStandAnim = 2;

// The one warning string the engine prints for an unresolved authored
// animation (pistol/soldier.c:126, rpg/rpg.c:167, soldier/soldier.c:625, ...).
// It is literally "Animation #%d not found".
extern const char* const kAnimNotFoundWarning;

// ---------------------------------------------------------------------------
// The animation table (AnimSet / AnimDesc), and the BOBJ loader rule
// ---------------------------------------------------------------------------

// AnimDesc (boneobjtypes.h:44-50). 20 bytes; the port only needs the two
// fields the playback rules read.
struct AnimDesc {
    float length = 0.0f;   // in ticks, 4800 per second
    uint32_t flags = 0;    // kAnimDescLoops is the only bit the engine reads
};

// One clip as the BOBJ loader reads it out of a BOAN/BOAH chunk
// (boneobj.c:1592-1597): `id = ah[2]; memcpy(&d->length, &ah[0], 4);
// d->flags = ah[1];`.
struct AnimRecord {
    int id = 0;
    float length_ticks = 0.0f;
    uint32_t flags = 0;
};

// The per-skeleton animation table the loader builds. In the engine this is
// `AnimSet { AnimDesc *descs; AnimDesc **byId; Pool caches; int hasCaches;
// int count; }` (boneobjtypes.h:52-58) inside the 64-entry `gAnimSets`
// (haze/anim.c:284-285). `count` is the CAPACITY, not the number of clips.
struct AnimSetTable {
    int skel = 0;              // the skeleton index this table belongs to
    int bone_count = 0;        // gSkeletons[skel].count
    int capacity = 0;          // set->count, from the BOAL/BALH second word
    int clip_count = 0;        // how many records the loader actually filled
    std::vector<AnimDesc> descs;      // capacity entries
    std::vector<int> by_id;           // capacity entries; -1 where byId is NULL

    bool valid() const { return capacity > 0 && static_cast<int>(by_id.size()) == capacity; }
};

// BoneObj_Load's table construction, boneobj.c:1583-1630:
//   set->count = total;  set->descs = alloc(total);  set->byId = alloc(total);
//   memset(set->byId, 0, total * 4);
//   for (k = anims; k > 0; k--) { ... d = set->byId[id] = &set->descs[id]; ... }
// `anims` is the FIRST word of the BALH chunk and `total` the second
// (boneobj.c:1581-1583). Only `anims` records are filled, so the rest of the
// table stays NULL - that is why a syntactically valid id can still miss.
//
// DIVERGENCE, named: the engine writes `set->byId[id]` with no check that
// `id < total`. A record whose id is >= total is a heap overflow in retail;
// here it is dropped and `out_of_range` is set.
AnimSetTable BuildAnimSetTable(int skel, int bone_count, int anim_count, int capacity,
                               const AnimRecord* records, int record_count,
                               bool* out_of_range = nullptr);

// ---------------------------------------------------------------------------
// Lookup, and the failure mode
// ---------------------------------------------------------------------------

// Anim_Exists HOOK 0x4D74F0 (haze/anim.c:244-255), reproduced exactly:
//
//   if (!obj || !obj->bones || bone < 0 || bone >= gSkeletons[obj->skel].count)
//       return 0;
//   set = &gAnimSets[obj->skel];
//   if (!set->count || id >= set->count) return 0;
//   return set->byId[id] != NULL;
//
// Note what is MISSING: there is no `id < 0` test. A negative id satisfies
// `id >= set->count` being false, so the engine reads `byId[-1]` - out of
// bounds. This port returns kAnimIdNegative so the caller can tell the hole
// apart from an ordinary miss instead of reading memory.
enum AnimLookup {
    kAnimFound = 0,
    kAnimIdNegative = 1,   // passes the engine's guard, then reads out of bounds
    kAnimIdAtOrAboveCapacity = 2,
    kAnimTableEmpty = 3,
    kAnimSlotEmpty = 4,    // in range, but byId[id] is NULL (not loaded)
    kAnimBoneOutOfRange = 5,
};

// The verdict plus the two facts the engine's caller wants next.
struct AnimResolution {
    AnimLookup lookup = kAnimSlotEmpty;
    int id = -1;
    int desc_index = -1;          // -1 unless lookup == kAnimFound
    float length_ticks = 0.0f;    // valid when desc_index >= 0

    bool found() const { return lookup == kAnimFound; }
};

// Anim_Exists as a verdict. `bone` is the track index the engine passes (always
// 0 for the human body track in the shipped callers).
AnimResolution ResolveAnim(const AnimSetTable& table, int bone, int id);

// Anim_GetLengthSeconds HOOK 0x4D74D0 (haze/anim.c:238-242):
//   `(long double)byId[id]->length * kSecondsPerTick`.
// Returns -1 when the id does not resolve (the engine would fault).
double AnimLengthSeconds(const AnimSetTable& table, int id);

// Anim_GetLengthTicks HOOK 0x4D7040 (haze/anim.c:110-115):
//   `fistpll((long double)byId[id]->length)` then `(int)`.
// This ROUNDS (round-to-nearest-even), it does not truncate. Returns -1 when
// the id does not resolve.
int AnimLengthTicks(const AnimSetTable& table, int id);

// Anim_GetPhase HOOK 0x4D6520 (anim.c:155-161): `time / length`. Note that
// `Anim_GetTime` returns a long double but the local is a FLOAT (anim.c:157),
// so the ratio is computed from the float-rounded time.
float AnimPhase(float time_ticks, float length_ticks);

// ---------------------------------------------------------------------------
// Frame / tick timing
// ---------------------------------------------------------------------------

// gTicksPerFrame, BSS 0xA54658 (anim.c:188, lod/lodvars.h:28). BoneObj_Init
// sets it to 0 (lod/boneobj.c:71) and only game.c:449 ever sets it to a real
// value, so it is 0 for any code that runs before the game module loads.
constexpr float kAnimTicksPerFrameUninitialised = 0.0f;

// gBoneSmoothAnims, DATA 0xA54678. BoneObj_Init sets it to 1 (lod/boneobj.c:65)
// and nothing in the reconstructed engine ever changes it, so the Hermite
// branch of the position track is the DEFAULT - not the linear one.
constexpr bool kAnimSmoothAnimsDefault = true;

// PosKey, boneobjtypes.h:32-36. 40 bytes: pos, time, out tangent, in tangent.
// The loader allocates `count * sizeof(PosKey)` for count > 1 but only
// `count * 16` for count == 1 (boneobj.c:1606), so a single-key track carries
// no tangents at all - and never needs them, because the count == 1 branch of
// the sampler returns keys[0].pos directly.
struct PosKey {
    float pos[3] = {0.0f, 0.0f, 0.0f};
    float time = 0.0f;
    float out[3] = {0.0f, 0.0f, 0.0f};
    float in[3] = {0.0f, 0.0f, 0.0f};
};

// The position track of one clip. Retail: every one of the 376 shipped clips
// has at least one key and every first key sits at time 0.
struct PosTrack {
    const PosKey* keys = nullptr;
    int count = 0;
};

// AnimInst (boneobj.h:79-89), the per-track playback state. 0x34 bytes - the
// instance pool stride at lod/boneobj.c:77.
struct AnimInst {
    int bone = 0;
    int id = -1;
    uint32_t flags = 0;
    int desc_index = -1;
    float pos[3] = {0.0f, 0.0f, 0.0f};
    float delta[3] = {0.0f, 0.0f, 0.0f};
    float speed = 0.0f;
    float time = 0.0f;
    bool has_cache = false;   // the engine's KeyCache*, which the port does not model

    // --- the pose pipeline's half of the same record -------------------------
    // `cache` is a KeyCache (boneobj.h:68-73): one int per skeleton bone holding
    // the index of the last key the rotation sampler used for that bone, so a
    // monotonic playhead does not restart its search at key 0. `cache_time` is
    // the cache's `time` field, written by the clock callback
    // (boneobj.c:879-880) and compared by the sampler (boneobj.c:1102).
    // Allocated only when the animation set has caches
    // (`BoneObj_InitAnimInst`, boneobj.c:992-1005).
    std::vector<int> cache_bones;
    float cache_time = 0.0f;

    // The engine reads the file-static `gBoneSmoothAnims` (boneobj.c:1138) for
    // the two-quaternion Hermite rotation form. It defaults to 1
    // (lod/boneobj.c:65) and nothing in the reconstructed engine changes it.
    bool smooth_anims = kAnimSmoothAnimsDefault;
};

// BoneObj_InitAnimInst HOOK 0x4D4F60 (boneobj.c:988-1021), the part that sets
// the timing state:
//
//   inst->flags = flags;
//   if (flags & ANIM_REVERSE) {
//       inst->pos  = desc->pos->keys[desc->pos->count - 1].pos;
//       inst->speed = -gTicksPerFrame;
//       inst->time  = desc->length;
//   } else {
//       inst->pos  = desc->pos->keys[0].pos;
//       inst->time  = 0;
//       inst->speed = gTicksPerFrame;
//   }
//
// `first_pos` / `last_pos` are the first and last PosKey positions; pass
// nullptr for either and the port leaves that pos component untouched (the
// engine would read through a NULL `desc->pos` and fault).
// Returns false when the id does not resolve - the engine has no guard here and
// would dereference NULL (see the header's divergence note).
bool InitAnimInst(AnimInst& inst, const AnimSetTable& table, int bone, int id, uint32_t flags,
                  float ticks_per_frame, const float* first_pos = nullptr,
                  const float* last_pos = nullptr);

// One tick of BoneObj_AnimEventsCB HOOK 0x4D4B60 (boneobj.c:873-941). This is
// the ONLY place the playhead moves:
//
//   old = inst->time;  now = inst->speed + inst->time;  inst->time = now;
//   if ((!(inst->time < d->length) && !(inst->flags & ANIM_REVERSE)) ||
//       (!(inst->time > 0.0f)   &&  (inst->flags & ANIM_REVERSE))) {
//       inst->flags |= BONE_EVENTS_DONE;
//       bone->flags |= BONE_EVENTS_DONE;
//       if (inst->flags & ANIM_LOOP) {
//           if (ANIM_REVERSE) { inst->time = d->length + inst->time; }
//           else              { inst->time = inst->time - d->length; }
//           /* then, for bone->index == 0 and !(flags & ANIM_FIRST), the root
//              position is re-derived: the pos track is evaluated at the far end
//              and `inst->pos = key->pos - inst->delta` (boneobj.c:909-929) */
//       } else {
//           if (ANIM_REVERSE) { inst->time = 0; }
//           else              { inst->time = d->length; }
//       }
//   }
//   inst->flags &= ~ANIM_FIRST;
//
// Three quirks are reproduced rather than corrected:
//   * the wrap subtracts `length` ONCE. A speed greater than the clip length
//     leaves `time` still past the end after the wrap.
//   * the end test is `!(time < length)`, i.e. `time >= length`, so a tick that
//     lands exactly on the length is treated as past the end.
//   * the one-shot case CLAMPS (`time = length`, or 0 in reverse); it does not
//     stop the instance, and it is the caller that reads BONE_EVENTS_DONE.
struct AnimAdvance {
    float old_time = 0.0f;
    float time = 0.0f;
    bool past_end = false;         // the `!(time < length)` test fired
    bool events_done = false;      // BONE_EVENTS_DONE was set on the instance
    bool wrapped = false;          // ANIM_LOOP and the end was crossed
    bool clamped = false;          // one-shot: time was forced to 0 or length
    bool root_fixup = false;       // the bone->index == 0 pos-track correction ran
    float root_pos[3] = {0.0f, 0.0f, 0.0f};  // valid when root_fixup
};

// The fixup needs the clip's position track. Pass nullptr (or a count of 0) to
// skip it, which is what the engine does for every bone whose `index != 0`.
AnimAdvance AdvanceAnimInst(AnimInst& inst, float length_ticks, const PosTrack* pos_track,
                            bool smooth = kAnimSmoothAnimsDefault);

// BoneObj_AnimPositionCB HOOK 0x4D4840 (boneobj.c:788-868), the position-track
// sampler the wrap fixup and the pose both go through. Writes `out_pos` (the
// sampled position) and `out_delta` (`out_pos - previous_pos`, the same delta
// the callback stores in the instance).
//
//   n == 1                      -> pos = keys[0].pos, delta = 0
//   time exactly on a key time  -> pos = that key's pos
//   time past the last key      -> pos = keys[n-1].pos
//   otherwise                   -> between keys[i-1] and keys[i]
//
// The between-keys case has two forms. `smooth` (the engine's default,
// gBoneSmoothAnims = 1) is a Hermite blend using the previous key's OUT tangent
// and the next key's IN tangent; otherwise it is a straight lerp. The engine
// keeps both, and the two do NOT agree at the endpoints.
//
// Returns false, without touching the outputs, when count <= 0. The engine does
// NOT guard that: with n == 0 its search loop leaves `kt` holding the bits of
// the `AnimInst *` (the decompiler models it as a union, boneobj.c:800-804),
// the `tm == kt` test fails, `i == n` then takes `keys[n - 1].pos` = keys[-1],
// and `tr->keys` was never even assigned (`if (keys > 0)` guards the alloc,
// boneobj.c:1604-1609). Shipped data never reaches it - all 376 clips have
// >= 1 key - so the port refuses instead of reading out of bounds.
bool SamplePosTrack(const PosTrack& track, float time_ticks, bool smooth,
                    const float previous_pos[3], float out_pos[3], float out_delta[3]);

// Anim_Play HOOK 0x4D61D0 (anim.c:38-58) and Anim_PlayOriented 0x4D6130:
//   `float t = blend * kTicksPerSecond / gTicksPerFrame;`
// The blend argument is in SECONDS and `t` comes out in FRAMES. Returns t as
// the engine computes it, i.e. with the division by gTicksPerFrame unguarded:
// a zero gTicksPerFrame (the pre-game state) yields +inf, or NaN when blend is
// also 0. The caller's test is `!(t < 1.0f)`, which is TRUE for NaN - so with
// an uninitialised gTicksPerFrame even a zero blend takes the FADE path.
float AnimBlendFrames(float blend_seconds, float ticks_per_frame);

// Anim_Play's branch, anim.c:45-57, as a verdict:
//   if (old || b->parent) {
//       if (!(t < 1.0f)) { BoneObj_FadeIn(...); return; }   // FADE
//       if (old) BoneObj_DeleteBlendNode(old);
//   }
//   /* immediate */
enum AnimPlayMode {
    kAnimPlayImmediate = 0,  // the blend node is replaced at once
    kAnimPlayFade = 1,       // BoneObj_FadeIn over `frames`
    kAnimPlayFadeNaN = 2,    // NaN blend: !(NaN < 1) is true, so it fades
};
AnimPlayMode AnimPlayModeFor(float frames, bool has_old_blend, bool has_parent);

// Anim_FadeOut HOOK 0x4D62B0 (anim.c:72-110):
//   `long double tt = time * kTicksPerSecond / gTicksPerFrame; float t = tt;`
//   `if (tt < 1.0f) { delete the blend; return; }`  -- note the test is on the
//   long double, the fade duration is the float truncation.
// Returns the fade duration in frames; 0 means "delete immediately".
float AnimFadeOutFrames(float time_seconds, float ticks_per_frame);

// Anim_SetSpeed HOOK 0x4D6580 (anim.c:172-179):
//   `inst->speed = gTicksPerFrame * speed;` then BoneObj_UpdateAnimDirection,
//   which sets ANIM_REVERSE iff the speed is < 0 and CLEARS it otherwise
//   (anim.c:163-169). A speed of exactly 0 therefore CLEARS the reverse bit
//   rather than preserving it.
void AnimSetSpeed(AnimInst& inst, float speed_multiplier, float ticks_per_frame);

// Anim_SetTime HOOK 0x4D65B0 (anim.c:181-185): a raw store, no clamp and no
// wrap. Retail uses it to carry a step phase across a state change
// (pistol/soldier.c:113 `Anim_SetTime(anim, 0, Anim_GetLength(anim, 0) * phase)`).
void AnimSetTime(AnimInst& inst, float time_ticks);

// ---------------------------------------------------------------------------
// The authored form
// ---------------------------------------------------------------------------

// A GenericTBA's two anim-bearing authored values. tba.c:44,47 and :105-106.
struct GenericTbaAnim {
    int anim = -1;         // "Activate Anim" (Int16) -> GenericTBA::anim @0x24A
    float use_time = 4.0f; // "Activate Time (s)" (Real32) -> ::use_time @0x24C
};

// Reads "Activate Anim" and "Activate Time (s)" out of an editor argTokens
// vector. Token indices 22 and 23 (see the constants above); a short vector or
// a non-numeric token keeps the engine default from GenericTBA_Create
// (tba.c:105-106: use_time = 4.0f, anim = -1).
//
// The token layout is fixed by Level_NewTask 0x4F0780 (level.c:180-241): the
// authored arguments are read in ParamTable order and written straight into
// `(char *)task + entry.offset`, starting at `cur.index = 3`.
GenericTbaAnim ParseGenericTbaAnim(const std::vector<std::string>& tokens);

// A HumanSoldier-family soldier's two anim-bearing authored values
// (retail LEVEL2/objects.qsc:47). HumanPlayer has neither: the engine hardcodes
// skeleton 0 and stand animation 2 for it (humanplayer.c:1212,1214).
struct SoldierAnim {
    int bone_hierarchy = -1;   // "Bone Heirachy" (Int32) -> Soldier::model
    int stand_animation = -1;  // "Stand Animation" (Int32) -> Soldier::idleAnim
};

// Reads argTokens indices 9 and 10. A short vector or a non-numeric token
// yields -1, which is the engine default for both fields
// (soldier.c:746 `s->s.idleAnim = -1;`, :751 `s->s.model = 1;` - note the
// defaults DIFFER: the engine's model default is 1, not -1. This function
// reports -1 to mean "not authored"; use SoldierModelOrDefault for the
// engine's fallback).
SoldierAnim ParseSoldierAnim(const std::vector<std::string>& tokens);

// Soldier_CreateHandler's default, soldier.c:751: `s->s.model = 1;`.
int SoldierModelOrDefault(int authored_model);

// ---------------------------------------------------------------------------
// The player's default slot table
// ---------------------------------------------------------------------------

// AnimSlot, humanbase/humanbasetypes.h:15-18: { int anim; int loop; }.
struct AnimSlot {
    int anim = 0;
    int loop = 0;
};

// gAnimSlots, DATA 0x538870 (addr.c:365), reconstructed at
// humanplayer.c:1563 as `char store_538870[392]` - 49 slots, 8 bytes each.
// HumanPlayer_CreateHandler points the human at it (humanplayer.c:1222) and
// Human_PlayAnimSlot (loadscreen/human.c:168-173) reads one entry:
//
//   AnimSlot *s = &h->animSlots[slot];
//   Human_PlayAnim(h, s->anim, s->loop, blend);
//
// so BOTH the clip id and the loop/flags word come straight from this table -
// `loop` is Anim_Play's `flags`, so the one entry with loop == 2 is a REVERSE
// playback, not "loop twice".
//
// The 49 entries line up one-to-one with gBodyStates (humanplayer.c:1554-1556),
// the state table StateMachine_Init installs at humanplayer.c:1225, so
// kBodyStateNames below names the state each slot belongs to.
//
// NOTE: this is the retail table, not authored data. Nothing in a level file
// can change it.
constexpr int kAnimSlotCount = 49;
extern const AnimSlot kAnimSlots[kAnimSlotCount];
extern const char* const kBodyStateNames[kAnimSlotCount];

// ---------------------------------------------------------------------------
// How a caller plays an authored animation
// ---------------------------------------------------------------------------

// The engine's three resolution paths, in the order the shipped code tests
// them. Every human anim caller is one of these shapes:
//
//   * kAnimPlayAuthoredOnly - humanstate.c:996 and :1051 (the GenericTBA Use35
//     and Use36 states): `Human_PlayAnim(h, GenericTBA_GetAnim(obj, 1, 0), 1,
//     0.0f)` with NO Anim_Exists guard at all. A GenericTBA whose authored anim
//     does not resolve faults in BoneObj_InitAnimInst.
//     The THIRD GenericTBA use state does not read the authored value at all:
//     HumanPlayer_TypeComputerEnter (humanstate.c:775) hardcodes
//     `Human_PlayAnim(h, 0x5A, 1, 0.0f)` - animation 90, loop 1 - and only the
//     use TIME comes from the object (humanstate.c:789).
//   * kAnimPlayGuarded - pistol/soldier.c:121-127, rpg/rpg.c:163-169,
//     soldier/soldier.c:620-628, patrol/female.c:567-573 (the soldier stand /
//     script animation): `-1` means "use the state's own slot", a resolvable id
//     is played, anything else warns and falls back to the slot.
//   * kAnimPlayHardcoded - humanplayer.c:1214 `Human_PlayAnim(h, 2, 0, 0)`.
enum AnimPlayPath {
    kAnimPlayAuthoredOnly = 0,
    kAnimPlayGuarded = 1,
    kAnimPlayHardcoded = 2,
};

// The loop/one-shot argument each shipped caller passes. It is passed straight
// through as Anim_Play's `flags`, so `loop` is really a flags WORD: 1 sets
// ANIM_LOOP, 2 would set ANIM_REVERSE, and 0 is a one-shot.
//
//   GenericTBA use states      loop = 1   (humanstate.c:996,1051 and :1117)
//   Soldier "Stand Animation"  loop = 0   (pistol/soldier.c:124)
//   Soldier script animation   loop = 0   (soldier/soldier.c:623)
//   SoldierFemale script anim  loop = 1   (patrol/female.c:570)
//   HumanPlayer create         loop = 0   (humanplayer.c:1214)
//   GenericTBA use-time frames loop = 0   (the FRAMES window itself)
constexpr uint32_t kAnimLoopGenericTbaUse = 1;
constexpr uint32_t kAnimLoopSoldierStand = 0;
constexpr uint32_t kAnimLoopSoldierScript = 0;
constexpr uint32_t kAnimLoopSoldierFemaleScript = 1;
constexpr uint32_t kAnimLoopHumanPlayerCreate = 0;

// The engine's `FRAMES(seconds)` macro, humanstate.c:520:
//   `#define FRAMES(seconds) x87_fistp((seconds) * FPS)` with FPS 30.0f
// (humanstate.c:91). `fistp` rounds to nearest even. GenericTBA's use window
// is `FRAMES(GenericTBA_GetUseTime(obj))` (humanstate.c:789,1010,1117), i.e.
// the authored "Activate Time (s)" in frames.
int AnimUseTimeFrames(float use_time_seconds);

// GenericTBA_GetAnim HOOK 0x4485D0 (tba.c:280-284) is a bare field read:
//   `int16_t GenericTBA_GetAnim(GenericTBA *t) { return t->anim; }`
// The two extra parameters in the humanstate.c:35 declaration
// (`(void *obj, int a, int b)`) are not in the signature - IGI-RE declares the
// ORIG with them to match the call site, and the callee ignores them (a
// `__cdecl` caller-cleans-stack artifact, not an argument).
// So there is nothing to compute: the return is the authored Int16 at 0x24A.
int GenericTbaGetAnim(const GenericTbaAnim& authored);

// The same for GenericTBA_GetUseTime 0x4485C0 (tba.c:274-278).
float GenericTbaGetUseTime(const GenericTbaAnim& authored);

// HumanPlayer_AnimDone HOOK 0x489F50 (loadscreen/human.c:196-204):
//   `return (h->view->motion->flags & 0x100) != 0;`
// 0x100 is BONE_EVENTS_DONE, the bit AdvanceAnimInst reports as `events_done`.
bool HumanAnimDone(uint32_t bone_flags);

// HumanPlayer_SetWeaponAnim 0x4137E0 and RequestWeaponAnim 0x413A60 are
// declared here only as provenance for the flags above: the first maps an
// authored "weapon action" ordinal to (body, weapon) clip ids and loop flags,
// and the second is a one-line store (`h->weaponAnimReq = anim`,
// weapon/humanplayer.c:1130-1134). Neither is ported: weapon animation
// selection is combat behaviour and is out of scope for an editor.

// ---------------------------------------------------------------------------
// The bone / skin side
// ---------------------------------------------------------------------------

// SkinVertex, src/dxinit/d3drender.c:37-44. 0x28 bytes
// (d3drender.c:52 `_Static_assert(sizeof(SkinVertex) == 0x28)`).
// This is the engine's skinned-vertex record. The editor's own MEF reader calls
// the same four payload fields (p, n, uv, w, target, bone) RenderVertex::
// {rawPos, normal, uv, weight, localVertexId, boneIndex}; the offsets below are
// the engine's, and a conversion is a field-by-field copy.
struct SkinVertex {
    float p[3];       // 0x00
    float n[3];       // 0x0C
    float uv[2];      // 0x18
    float w;          // 0x20
    uint16_t target;  // 0x24 - the base vertex an extra influence accumulates into
    uint16_t bone;    // 0x26 - index into the Bone array
};

// NormalVertex, src/dxinit/d3drender.c:46-50. 0x20 bytes.
struct NormalVertex {
    float x, y, z;     // 0x00
    float nx, ny, nz;  // 0x0C
    float u, v;        // 0x18
};

// The offsets BoneMesh_Skin reads out of the mesh header (d3drender.c:230-253).
// These are raw byte offsets into the RD3D-backed mesh record; the editor's own
// loader already exposes the same two counts as
// ParsedGeometry::{baseVertexCount, extraInfluenceCount}.
constexpr int kMeshSkinExtraCountOffset = 0x0C;  // d3drender.c:275
constexpr int kMeshSkinVertexCountOffset = 0x10;  // d3drender.c:230,253
constexpr int kMeshSkinVertexArrayOffset = 0x18;  // d3drender.c:253
// The Bone array is reached through `*(Bone **)(bones + 8)` (d3drender.c:258),
// i.e. the bone-object's `bones` pointer at +8 (boneobjtypes.h:76-80).
constexpr int kBoneObjBonesPointerOffset = 8;

// Bone, boneobjtypes.h:60-74. 0x84 bytes.
//   index 0x00  flags 0x04  offset 0x08  length 0x14  pos 0x18
//   matrix 0x24 (Matrix, 0x28)  world 0x4C (Quat, 0x14)  local 0x60 (Quat, 0x14)
//   parent 0x74  child 0x78  next 0x7C  blend 0x80
// BoneMesh_Skin reads only `matrix` (a row-major 3x3, types.h:13-16) and `pos`.
constexpr int kBoneMatrixOffset = 0x24;
constexpr int kBonePosOffset = 0x18;
constexpr int kBoneSize = 0x84;

// The rigid flag selects the second of BoneMesh_Skin's two vertex paths.
// `rigid` is the caller's `int rigid` (d3drender.c:226) and it is NOT a
// boolean test of the mesh: BoneMesh_Skin branches on it directly.
//
// Non-rigid path (d3drender.c:254-289): the first `count` vertices are written
// outright, then `mesh[0x0C]` extra vertices are transformed and their weighted
// share is ACCUMULATED into `out[v->target]`.
// Rigid path (d3drender.c:290-309): the first `count` vertices are written
// outright and the extra vertices are ignored entirely.
struct SkinTransformInput {
    float p[3] = {0.0f, 0.0f, 0.0f};
    float n[3] = {0.0f, 0.0f, 0.0f};
    float uv[2] = {0.0f, 0.0f};
    float w = 0.0f;
    int bone = 0;
    int target = 0;
};

// The bone the vertex is bound to: `matrix` row-major 3x3 and `pos`.
struct BoneTransform {
    float matrix[9] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    float pos[3] = {0.0f, 0.0f, 0.0f};
};

// BoneMesh_Skin's per-vertex maths, reproduced. `accumulate` is the extra-
// influence form (`t->x = w * X + t->x`, d3drender.c:287-289) instead of the
// overwrite form (d3drender.c:262-264).
//
// DIVERGENCE, named: the engine computes `x` in float (its operands are all
// float and the file is built with -fexcess-precision=standard,
// d3drender.c:1), rounds the `x + b->pos.x` sum to float, and only then
// multiplies by `w` in long double before storing. The port does the same
// rounding, so `position` matches bit for bit; the intermediate `long double`
// is not observable after the store.
struct SkinTransformOutput {
    float position[3] = {0.0f, 0.0f, 0.0f};
    float normal[3] = {0.0f, 0.0f, 0.0f};
    float uv[2] = {0.0f, 0.0f};
};

SkinTransformOutput SkinTransformVertex(const SkinTransformInput& in, const BoneTransform& bone,
                                        bool rigid, const SkinTransformOutput* base = nullptr);

// The same, restricted to the position, for callers that only need the
// accumulate step. Equivalent to SkinTransformVertex with the normal/uv fields
// dropped.
void SkinAccumulatePosition(const SkinTransformInput& in, const BoneTransform& bone,
                            SkinTransformOutput& base);

// ===========================================================================
// PART TWO - the pose pipeline
// ===========================================================================
//
// Everything above this line ports the ANIMATION TABLE, the CLOCK and the
// per-vertex SKIN arithmetic. Everything below ports the three stages between
// them, which is what actually turns a playing clip into a posed skeleton:
//
//   * the ROTATION TRACK SAMPLER, `BoneObj_AnimOrientationCB` 0x4D5180
//     (boneobj.c:1080-1173) and the slerp tables `Anim_PrepareSlerps` 0x4D6D40
//     (haze/anim.c:40-84) feed it;
//   * the BONE TRANSFORMS, `BoneObj_UpdateBone` 0x4D3A90 (boneobj.c:423-525)
//     and `BoneObj_Update` 0x4D38C0 (boneobj.c:371-419);
//   * the EVENT/CLOCK callback `BoneObj_AnimEventsCB` 0x4D4B60
//     (boneobj.c:873-941) in its full form, including the event window.
//
// The one structural fact that makes this tractable, and that the port must NOT
// "improve": a bone's POSITION in the pose never comes from the clip's position
// track. `BoneObj_UpdateBone` (boneobj.c:434-439) places every bone from its
// REST offset - `b->pos = M * b->offset + parent->pos` - and only its
// ORIENTATION comes from the rotation track. The position track is consumed
// only by `BoneObj_GetRootMotion` 0x4D2FB0 (lod/boneobj.c:202-211) and by the
// wrap-tick root fixup inside the clock callback. A port that lerps bone
// positions from the translation keys produces a different skeleton.

// Quat, types.h:20-23. 0x14 bytes. `unk10` is the engine's multiply-depth
// counter: it is incremented on every quaternion multiply and, when it exceeds
// 1000, the quaternion is renormalised and the counter reset
// (boneobj.c:457-467). It is not a rounding artefact - it changes the RESULT.
struct EngineQuat {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
    int unk10 = 0;
};

// Matrix, types.h:13-17. 0x28 bytes: a ROW-MAJOR 3x3 and the same depth
// counter. `count` is `q.unk10` copied through (boneobj.c:492).
struct EngineMatrix {
    float m[9] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    int count = 0;
};

// BlendBranch, boneobj.h:22-25.
enum { kBranchNone = -1, kBranchNode = 0, kBranchSource = 1 };

// SlerpInfo, boneobjtypes.h:25-28. One entry per rotation key.
struct SlerpInfo {
    float angle = 0.0f;
    float invSin = 0.0f;
};

// RotKey, boneobjtypes.h:30-35. 0x34 bytes. `q` is the key, `a` is the key's
// OUT tangent quaternion and `b` its IN tangent - the loader copies the BORD
// chunk straight in (boneobj.c:1616-1621), and igi1conv's BEF writes the same
// three quaternions as `RotationKeyFrameData` q0/q1/q2. Verified against
// 000.IFF BOAN id 2: the BEF triple equals the raw RotKey triple for all 33
// bones.
struct RotKey {
    float q[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float time = 0.0f;
    float a[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float b[4] = {0.0f, 0.0f, 0.0f, 1.0f};
};

// RotTrack, boneobjtypes.h:37-42. `slerp`/`slerp2` are built by
// Anim_PrepareSlerps and are NULL until it runs; the sampler branches on
// `slerp` being non-NULL (boneobj.c:1133).
struct RotTrack {
    int count = 0;
    const RotKey* keys = nullptr;
    const SlerpInfo* slerp = nullptr;
    const SlerpInfo* slerp2 = nullptr;
};

// EventKey / EventTrack, boneobj.h:53-63. 0x18 and 8 bytes.
struct EventKey {
    int id = 0;
    int unk4 = 0;
    float time = 0.0f;
    float pos[3] = {0.0f, 0.0f, 0.0f};
};
struct EventTrack {
    int count = 0;
    const EventKey* keys = nullptr;
};

// The engine's AnimDesc (boneobjtypes.h:44-50), completed. The port's earlier
// `AnimDesc` keeps only `length` and `flags` because the table rules need only
// those two; this is the same record with the three track pointers the pose
// pipeline dereferences.
struct EngineClip {
    float length = 0.0f;   // ticks, 4800 per second
    uint32_t flags = 0;    // kAnimDescLoops is the only bit read
    const PosTrack* pos = nullptr;
    const RotTrack* rot = nullptr;   // one per BONE, indexed by bone->index
    const EventTrack* events = nullptr;
};

// BoneEvent, boneobj.h:12-18. What the clock callback hands back to the game.
struct EngineBoneEvent {
    int bone = 0;
    int id = 0;
    int anim = 0;
    float time = 0.0f;
    float pos[3] = {0.0f, 0.0f, 0.0f};
};

// Bone, boneobjtypes.h:60-74. 0x84 bytes. The engine links bones with POINTERS
// (`parent`, `child`, `next`, `blend`); the port uses indices into
// `BonePoseState::bones` so the structure stays copyable and has no ownership.
// `blend` is an index into `BonePoseState::nodes`, or -1.
struct EngineBone {
    int index = -1;
    uint32_t flags = 0;
    float offset[3] = {0.0f, 0.0f, 0.0f};   // parent-relative rest offset (TLST)
    float length = 0.0f;                    // |offset|, set by BoneObj_Build
    float pos[3] = {0.0f, 0.0f, 0.0f};      // world position, raw units
    float matrix[9] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    int matrix_count = 0;
    EngineQuat world;
    EngineQuat local;
    int parent = -1;
    int child = -1;   // head of the child list, built by BoneObj_Build
    int next = -1;    // the parent's next child
    int blend = -1;   // node index, or -1 for "no blend of its own"
};

// BlendSource, boneobj.h:29-35, reduced to the ONE source kind the editor
// needs: the animation instance. The engine also has an orientation source
// (`BoneObj_NewOrientationSource` 0x4D5700, used by Anim_PlayOriented) and a
// parent source (`BoneObj_NewParentSource` 0x4D46E0, used by the fade paths);
// neither is reachable from `Anim_Play` with blend 0, which is what every
// shipped caller passes, so neither is modelled. `AnimPlayModeFor` above still
// reports which of the two blend paths a non-zero blend would take.
struct BlendSourceAnim {
    AnimInst inst;
    // The clip the instance plays. In the engine this is `inst->desc`, reached
    // through the BlendSource's `data` pointer; the port keeps it here because
    // the rotation and event callbacks take the clip explicitly.
    const EngineClip* clip = nullptr;
};

// BlendNode, boneobj.h:37-46, reduced to the branch structure. `weight`,
// `free`, `select` and `data` belong to the fade machinery, which only exists
// when a non-zero blend is passed to Anim_Play; with the immediate path they
// are all NULL, which is exactly the case modelled here.
struct BlendNodeState {
    int branchType[2] = {kBranchNone, kBranchNone};
    int branchSource[2] = {-1, -1};   // index into BonePoseState::sources
    int branchNode[2] = {-1, -1};     // index into BonePoseState::nodes
};

// The whole runtime object: the engine's `BoneObj` (boneobjtypes.h:76-80) plus
// the blend tree the bones hang off, plus the two file-static root globals
// `gRootMatrix`/`gRootQuat` (boneobj.c:1639-1640) that BoneObj_SetRoot 0x4D38A0
// writes and BoneObj_GetRoot 0x4D4190 reads.
struct BonePoseState {
    std::vector<EngineBone> bones;
    std::vector<BlendNodeState> nodes;
    std::vector<BlendSourceAnim> sources;
    EngineMatrix rootMatrix;
    EngineQuat rootQuat;
    bool hasRoot = false;

    // The engine's BoneObj::bones is a pointer that may be NULL; the port
    // reports that as an empty vector. `boneCount` is
    // `gSkeletons[skel].count`.
    int boneCount() const { return static_cast<int>(bones.size()); }
};

// BoneObj_Build 0x4D2EC0 / Bone_Init 0x4D2F80 (lod/boneobj.c:153-188), plus the
// `o->bones` allocation and `Mem_Alloc` zeroing of BoneObj_Create 0x4D2D60
// (lod/boneobj.c:100-129). `parents[i]` is PLST[i] and `offsets[i*3..]` is
// TLST[i], both straight out of the BOBJ BOBH form (boneobj.c:1570-1576).
//
// BoneObj_Build recurses by INDEX SCAN: for each bone i in ascending order it
// appends i to the child list of `parents[i]`. Bone_Init PREPENDS, so a parent's
// child list ends up in DESCENDING index order. Reproduced, because the
// recursion order decides the order the bones are written.
//
// `length` is `sqrt(offset.x^2 + offset.y^2 + offset.z^2)` in long double with
// an x87 fsqrt (lod/boneobj.c:168-169), not hypotf.
void BoneObjBuild(BonePoseState& state, const int* parents, const float* offsets, int count);

// Quat_SlerpPrepare HOOK 0x4B44D0 (math/quat.c:8-31). `flip` is written 1 when
// the dot product is negative by more than 1e-8; between -1e-8 and 0 the dot is
// forced to 0 instead, which makes the angle PI/2 rather than 0. `invSin` is
// 1/sin(angle) through an x87 fsin, or exactly 1.0f when the angle is 0.
void QuatSlerpPrepare(int& flip, float& angle, float& invSin, const EngineQuat& a, const EngineQuat& b);

// Anim_PrepareSlerps HOOK 0x4D6D40 (haze/anim.c:40-84). Fills `slerp` for every
// rotation track of one clip, and `slerp2` as well when gBoneSmoothAnims is set
// (the default). The tables are per KEY, and the last key wraps to key 0 only
// when the CLIP has ANIM_LOOPS; otherwise its entry is {0, 0}, which the sampler
// reads as "use the key itself" (boneobj.c:1142).
//
// The engine allocates each bone's table separately
// (haze/anim.c:62 `Mem_Alloc(keys * sizeof(SlerpInfo), 4)`), so the caller's
// flat buffers need a per-bone start index: `key_offsets[b]` is the index of
// bone b's first entry, and `key_offsets[bone_count]` must be the total. The
// caller fills `key_offsets` from the tracks (bone b owns `rot[b].count`
// entries); `key_counts`, when non-null, receives the same per-bone counts.
// Returns false when the clip has no rotation tracks.
bool AnimPrepareSlerps(const EngineClip& clip, int bone_count, bool smooth, SlerpInfo* out_slerp,
                       SlerpInfo* out_slerp2, const int* key_offsets, int* key_counts = nullptr);

// The engine's `slerp_prepare` (haze/anim.c:19-37), which is QuatSlerpPrepare
// plus the flip: a flipped pair gets its angle INCREASED by 2*PI rather than
// negating the second quaternion, and the sampler's `slerp_angle` then undoes
// that at sample time (boneobj.c:1052-1065).
void SlerpPrepare(SlerpInfo& out, const float* a, const float* b);

// BoneObj_AnimOrientationCB HOOK 0x4D5180 (boneobj.c:1080-1173). The rotation
// track sampler. Returns the engine's verdict: 0 for "orientation relative to
// the parent bone", which is the only value this callback ever returns
// (boneobj.c:1171). Writes `out` and, when `inst.has_cache` is set, updates the
// per-bone key cache.
//
// Reproduced quirks, each of which changes a pose:
//   * `n == 1` returns the single key with `unk10 = 0` (boneobj.c:1094-1098) and
//     does NOT consult the slerp table.
//   * the search loop stops at the first key whose time is NOT `< tm`, so a
//     sample exactly on a key takes the key; a sample past the LAST key wraps to
//     key 0 only when the CLIP has ANIM_LOOPS, and the blend factor is then
//     measured against `length - kt`, not `kt - previous` (boneobj.c:1116-1124).
//   * `a < 0` (a sample before the first key) returns keys[0] (boneobj.c:1125-1129).
//   * the smooth path builds TWO quaternions - from the key's `a`/`b` tangents
//     through `slerp2` - and slerps between them with
//     `1 - ((1-r)*r + (1-r)*r)` (boneobj.c:1155), which is the only place the
//     Hermite rotation form is used.
//   * `out->unk10` is set to 0 on the early-exit paths and to
//     `key_time_bits + c->unk10 + 1` from `slerp_keys` on the interpolated one
//     (boneobj.c:1048) - the raw INTEGER BITS of the key time, not the time.
int BoneObjAnimOrientationCB(EngineQuat& out, BonePoseState& state, int boneIndex, AnimInst& inst,
                             const EngineClip& clip);

// BoneObj_AnimPositionCB HOOK 0x4D4840 (boneobj.c:788-868) in its instance form:
// samples the clip's position track at `inst.time`, writes `inst.pos`, sets
// `inst.delta` to the change against the instance's previous position and
// returns 1. `SamplePosTrack` above is the same arithmetic as a pure function.
int BoneObjAnimPositionCB(const EngineClip& clip, AnimInst& inst, bool smooth);

// BoneObj_AnimCollectEvents HOOK 0x4D4DC0 (boneobj.c:944-980). Appends the
// events in the window (`from`, `to`] - or the single event exactly at `from`
// when the window is empty - to `out` starting at index `n`. The window is
// HALF-OPEN and direction-aware: when `to < from` (a reverse step) the test is
// `!(k->time < lo) && k->time < hi`.
int BoneObjAnimCollectEvents(std::vector<EngineBoneEvent>& out, int n, int boneIndex, const AnimInst& inst,
                             const EngineClip& clip, float from, float to);

// BoneObj_AnimEventsCB HOOK 0x4D4B60 (boneobj.c:873-941), the ONLY place the
// playhead moves. This is the full form: it also emits events and it sets
// BONE_EVENTS_DONE on BOTH the instance and the bone.
//
// `AdvanceAnimInst` above is the pure-clock core of the same function and is
// kept for the callers that have no bone and no event sink; the two must agree
// on `time`/`flags`/`pos`, and the tests pin that they do.
//
// Returns the number of events appended (starting from `n`).
int BoneObjAnimEventsCB(std::vector<EngineBoneEvent>& events, int n, BonePoseState& state, int boneIndex,
                        AnimInst& inst, const EngineClip& clip, bool smooth);

// BoneObj_GetNodeOrientation HOOK 0x4D3F50 (boneobj.c:531-585). Blends the
// node's two branches, and when the blend reports an ABSOLUTE orientation (1)
// converts it to parent-relative by multiplying by the inverse of the parent's
// world quaternion. The `r == 1` branch is reachable from an animation source
// only through `BoneObj_BlendOrientations`'s t0==0/t1==1 case, i.e. when a bone
// carries both an animation source and an orientation source - the fade and
// Anim_PlayOriented paths. It is implemented anyway so the blend tree is not
// silently special-cased.
int BoneObjGetNodeOrientation(EngineQuat& out, BonePoseState& state, int boneIndex, int nodeIndex);

// BoneObj_GetBranchOrientation HOOK 0x4D4690 (boneobj.c:704-716).
int BoneObjGetBranchOrientation(EngineQuat& out, BonePoseState& state, int boneIndex, int type, int sourceIndex,
                                int nodeIndex);

// BoneObj_BlendOrientations HOOK 0x4D41C0 (boneobj.c:597-655). The default node
// blend. Note the asymmetry: two RELATIVE orientations (0, 0) slerp; a relative
// and an absolute (0, 1) MULTIPLY and return 1; two absolute ones (1, 1) also
// multiply and return 1; and a `-1` operand is simply the other one.
int BoneObjBlendOrientations(EngineQuat& out, int t0, const EngineQuat& q0, int t1, const EngineQuat& q1,
                             float w);

// BoneObj_Slerp HOOK 0x4D4500 (boneobj.c:658-702). `t` is CLAMPED at both ends
// by the negated comparisons: `!(t > 0.001f)` copies `b` and `!(t < 0.999f)`
// copies `a`, so the function's argument order is the reverse of what the
// names suggest at t = 0. A zero angle also copies `a`. `kSmallAngle` (0.01) is
// the threshold below which the weights are the linear pair instead of the sine
// pair.
void BoneObjSlerp(EngineQuat& out, const EngineQuat& a, const EngineQuat& b, float t);

// quat_mul (boneobj.c:41-70), the 8-multiply form. `round` is the bit mask of
// which partial products the CALLER wants rounded to float - the engine has six
// call sites with four different masks, so this is a parameter and not a
// constant. `out[0]` is left unrounded for the caller to store, exactly as the
// engine leaves it (`o[0] still has to be rounded by the caller`).
enum {
    kQuatRoundA = 1,
    kQuatRoundB = 2,
    kQuatRoundC = 4,
    kQuatRoundD = 8,
    kQuatRoundE = 16,
    kQuatRoundF = 32,
};
void QuatMul(long double out[4], const float* A, const float* B, int round);

// BoneObj_UpdateBone HOOK 0x4D3A90 (boneobj.c:423-525). Poses one bone and,
// recursively, its child list.
//
// The two facts that make the pose:
//   * the bone's POSITION is its REST offset rotated by the parent's matrix and
//     added to the parent's position (boneobj.c:434-439) - never the clip's
//     translation track;
//   * the bone's ORIENTATION comes from `b->blend` if it has one, otherwise
//     from the `inherited` node, which is the nearest ancestor that ended up
//     with BONE_ANIMATED set (boneobj.c:520-521).
//
// `inherited` is a node index or -1. The engine would dereference a NULL node
// here; the port reports `false` instead (named divergence).
bool BoneObjUpdateBone(BonePoseState& state, int boneIndex, const float* parentMatrix,
                       const EngineQuat& parentQuat, const float* parentPos, int inherited);

// BoneObj_Update HOOK 0x4D38C0 (boneobj.c:371-419). Extracts the object's world
// orientation from `m` as a quaternion, installs it as the root, poses the whole
// tree and then forces the ROOT bone's `local` to identity (boneobj.c:412-416) -
// the root's own local orientation is discarded even though its world is kept.
//
// `m` is the engine's row-major 3x3. `m_count` is `Matrix::count`, which lands
// in the depth counters; the engine's object matrices carry whatever the task
// set, and the only consequence of a different value is when the >1000
// renormalisation fires.
//
// The engine reads `o->bones` unconditionally; the port returns false for an
// empty state.
bool BoneObjUpdate(BonePoseState& state, const float m[9], int m_count);

// BoneObj_GetRootMotion HOOK 0x4D2FB0 (lod/boneobj.c:202-211) with a
// single-source blend, i.e. `BoneBlend_GetMotion` 0x4D3020
// (lod/boneobj.c:220-247) where exactly one branch answers: the root bone's
// animation position track, and the root ROTATION half is cleared to zero
// because an animation source has no `getPosition` rotation output. Returns
// false when the root bone has no blend, which is when the engine zeroes both.
bool BoneObjGetRootMotion(const BonePoseState& state, float outPos[3], float outRot[3]);

// BoneObj_InitAnimInst HOOK 0x4D4F60 (boneobj.c:988-1021) for a clip the caller
// already holds, without the `AnimSetTable` indirection `InitAnimInst` above
// needs. Same body: the cache decision, `bone`/`id`/`desc`/`delta`/`flags`, and
// the two timing branches.
//
// `cache_bone_count` is `gSkeletons[skel].count` when the set has caches and 0
// otherwise (boneobj.c:992 `if (set->hasCaches)`). The editor's set has no
// pooled caches, so pass 0: `inst.cache` stays NULL, which is BEHAVIOURALLY
// IDENTICAL to having one, because the cache is only read while the playhead has
// not gone backwards (boneobj.c:1102) and the index it stores is always the key
// the search would have found anyway (boneobj.c:1169-1170 stores the `a` the
// search just computed). It is an optimisation, not a rule.
bool InitAnimInstFromClip(AnimInst& inst, const EngineClip& clip, int bone, int id, uint32_t flags,
                          float ticks_per_frame, int cache_bone_count = 0,
                          bool smooth = kAnimSmoothAnimsDefault);

// The engine's per-frame driver loop, `BoneObj_GetEvents` HOOK 0x4D3210
// (boneobj.c:112-129): for every bone with a blend, clear BONE_FADING, run the
// node's event callback (which advances that instance's clock) and then prune.
// The pruning and the `b->flags &= 0xFFF87EFF` reset belong to the fade tree;
// with a single source they are no-ops, and the port keeps only the clock
// advance plus the event sink. Returns the number of events collected.
//
// ORDER MATTERS: retail poses FIRST and advances the clock SECOND - see
// `BoneMagicObj_Run` (freecam/bonemagic.c:101-112), which calls
// BoneObj_Update at :105 and BoneObj_GetEvents at :110. Advancing before posing
// shifts every frame by one tick.
int BoneObjGetEvents(std::vector<EngineBoneEvent>& events, BonePoseState& state, const EngineClip& clip,
                     bool smooth);

// BoneMesh_Skin HOOK 0x49B700 (dxinit/d3drender.c:226-313), the whole vertex
// loop rather than the per-vertex maths above. The three paths are selected by
// `rigid` and by whether the vertex index is past `baseVertexCount`:
//
//   * non-rigid, base: `out[i] = skin(v[i]) * v[i].w` for the first
//     `baseVertexCount` vertices;
//   * non-rigid, extra: `out[v[i].target] += skin(v[i]) * v[i].w` for the next
//     `extraInfluenceCount` vertices - the NORMAL and UV of the target are left
//     as the base pass wrote them;
//   * rigid: the base pass only, with no weight, and the extra vertices are not
//     read at all.
//
// `vertices` is the engine's SkinVertex array, `baseVertexCount` is
// `mesh[0x10]` and `extraInfluenceCount` is `mesh[0x0C]`. The engine refuses
// more than 4000 base vertices by printing and spinning forever
// (d3drender.c:234-238); the port returns false.
//
// The output is NormalVertex order, i.e. `{x,y,z, nx,ny,nz, u,v}`, written into
// `out` which is resized to `baseVertexCount`.
bool BoneMeshSkin(const SkinTransformInput* vertices, int baseVertexCount, int extraInfluenceCount,
                  const BoneTransform* bones, int boneCount, bool rigid,
                  std::vector<SkinTransformOutput>& out);

}  // namespace igi
