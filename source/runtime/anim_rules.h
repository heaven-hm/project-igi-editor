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

// anim.c:57 `b->flags = (b->flags & 0xFFFAFEFF) | BONE_ANIMATED`. The mask
// clears BONE_EVENTS_DONE (0x100), BONE_FADING (0x10000) and BONE_ANIMATED
// (0x20000) - note it clears the event bit as well, which is not obvious from
// the fade/animate intent.
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
//   which sets ANIM_REVERSE iff the speed is < 0 (anim.c:163-169). A speed of
//   exactly 0 keeps whatever direction flag was already there.
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

}  // namespace igi
