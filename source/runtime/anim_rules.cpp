// anim_rules.cpp - see anim_rules.h for the full citation map.
//
// The engine files behind these rules all begin with
// `#pragma GCC optimize("excess-precision=standard")` - anim.c:2,
// boneobj.c:2, haze/anim.c, lod/boneobj.c, dxinit/d3drender.c, pistol/soldier.c,
// soldier/soldier.c, glass/tba.c, loadscreen/human.c - which is why the
// intermediate types below are spelled out rather than left to the compiler.
// The two exceptions are named where they matter: humanstate.c and level.c do
// NOT set it.
#include "runtime/anim_rules.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace igi {

// ---------------------------------------------------------------------------
// Local helpers
// ---------------------------------------------------------------------------

namespace {

// `fistpll` with the x87 control word's rounding mode, low 32 bits. The engine
// never changes the mode from round-to-nearest-even (humanbase/human.c:207-213
// is the same helper). The "integer indefinite" the x87 produces for NaN or an
// out-of-range value is 0x8000000000000000, and the engine's `return (int)r`
// truncates that to 0.
int RoundInt(long double value) {
    if (std::isnan(value)) return 0;
    if (!(value > -9.223372036854775808e18L && value < 9.223372036854775808e18L)) return 0;
    const long long rounded = static_cast<long long>(std::nearbyint(value));
    return static_cast<int>(rounded);
}

// Authored token readers. Layout: [0]=task id, [1]=type, [2]=name, [3]=declared
// parameter 0, fixed by Level_NewTask 0x4F0780 (level.c:180-241), which starts
// at `cur.index = 3` and reads one table entry per parameter in order.
// Same shape as source/runtime/cutscene_rules.cpp's readers.
float TokenFloat(const std::vector<std::string>& tokens, std::size_t index, float fallback) {
    if (index >= tokens.size()) return fallback;
    const std::string& token = tokens[index];
    if (token.empty()) return fallback;
    char* end = nullptr;
    const float value = std::strtof(token.c_str(), &end);
    if (end == token.c_str()) return fallback;
    return value;
}

int TokenInt(const std::vector<std::string>& tokens, std::size_t index, int fallback) {
    if (index >= tokens.size()) return fallback;
    const std::string& token = tokens[index];
    if (token.empty()) return fallback;
    char* end = nullptr;
    const long value = std::strtol(token.c_str(), &end, 10);
    if (end == token.c_str()) return fallback;
    return static_cast<int>(value);
}

}  // namespace

const char* const kAnimNotFoundWarning = "Animation #%d not found";

// ---------------------------------------------------------------------------
// The animation table
// ---------------------------------------------------------------------------

AnimSetTable BuildAnimSetTable(int skel, int bone_count, int anim_count, int capacity,
                               const AnimRecord* records, int record_count, bool* out_of_range) {
    if (out_of_range) *out_of_range = false;

    AnimSetTable table;
    table.skel = skel;
    table.bone_count = bone_count;
    table.capacity = capacity;

    // boneobj.c:1583-1588. `if (!set->count) return;` guards the whole build, so
    // a capacity of 0 leaves descs/byId NULL and every lookup missing.
    if (capacity <= 0) return table;

    // boneobj.c:1586-1588: `set->descs = Mem_Alloc(total * 0x14, 4);
    // set->byId = Mem_Alloc(total * 4, 4); memset(set->byId, 0, total * 4);`
    // Only `anims` of those slots are ever written, which is why an in-range id
    // can still be NULL.
    table.descs.assign(static_cast<std::size_t>(capacity), AnimDesc{});
    table.by_id.assign(static_cast<std::size_t>(capacity), -1);

    // boneobj.c:1590-1597: `for (k = anims; k > 0; k--)` walking one BOAN form
    // per iteration, in file order. `records` is that same order.
    int count = anim_count;
    if (count > record_count) count = record_count;
    for (int k = 0; k < count; ++k) {
        const AnimRecord& record = records[k];
        // DIVERGENCE, named: the engine writes `set->byId[id]` and
        // `set->descs[id]` with no check that `id < total`, and no check that
        // `id >= 0`. A record outside the table is a heap overflow in retail;
        // here it is dropped and reported.
        if (record.id < 0 || record.id >= capacity) {
            if (out_of_range) *out_of_range = true;
            continue;
        }
        // boneobj.c:1594-1597: `id = ah[2]; memcpy(&d->length, &ah[0], 4);
        // d->flags = ah[1]; set->byId[id] = d;`
        table.descs[static_cast<std::size_t>(record.id)].length = record.length_ticks;
        table.descs[static_cast<std::size_t>(record.id)].flags = record.flags;
        table.by_id[static_cast<std::size_t>(record.id)] = record.id;
        ++table.clip_count;
    }
    return table;
}

// ---------------------------------------------------------------------------
// Lookup and the failure mode
// ---------------------------------------------------------------------------

AnimResolution ResolveAnim(const AnimSetTable& table, int bone, int id) {
    AnimResolution out;
    out.id = id;

    // haze/anim.c:246-247:
    //   `if (!obj || !obj->bones || bone < 0 ||
    //     bone >= gSkeletons[obj->skel].count) return 0;`
    // A skeleton with count == 0 fails every bone index, which is exactly what
    // BoneObj_Create does with "no hierarchy loaded" (lod/boneobj.c:111-116).
    if (bone < 0 || bone >= table.bone_count) {
        out.lookup = kAnimBoneOutOfRange;
        return out;
    }

    // haze/anim.c:249-250: `if (!set->count || id >= set->count) return 0;`
    // Note the ORDER the engine uses: the capacity test comes before the read,
    // and the id is compared as an int against the capacity, so a negative id
    // satisfies neither half of it.
    if (table.capacity <= 0) {
        out.lookup = kAnimTableEmpty;
        return out;
    }
    if (id >= table.capacity) {
        out.lookup = kAnimIdAtOrAboveCapacity;
        return out;
    }
    if (id < 0) {
        // THE HOLE. haze/anim.c:255 is `return set->byId[id] != NULL;` with no
        // `id < 0` test, so a negative id reaches `byId[-1]` and reads the four
        // bytes in front of the allocation. Nothing in the shipped data does it
        // (see the gAnimSlots table, all ids positive), but the guard does not
        // stop it. Reported, not read.
        out.lookup = kAnimIdNegative;
        return out;
    }

    if (table.by_id[static_cast<std::size_t>(id)] < 0) {
        // In range but never loaded: `set->count` is the CAPACITY, and only
        // `anims` of those slots were filled (boneobj.c:1583,1590).
        out.lookup = kAnimSlotEmpty;
        return out;
    }

    out.lookup = kAnimFound;
    out.desc_index = id;
    out.length_ticks = table.descs[static_cast<std::size_t>(id)].length;
    return out;
}

double AnimLengthSeconds(const AnimSetTable& table, int id) {
    const AnimResolution r = ResolveAnim(table, 0, id);
    if (!r.found()) return -1.0;
    // haze/anim.c:238-242:
    //   `return (long double)gAnimSets[skel].byId[id]->length * kSecondsPerTick;`
    // The cast is on the FLOAT length, so this is a long double multiply.
    return static_cast<double>(static_cast<long double>(r.length_ticks) * kAnimSecondsPerTick);
}

int AnimLengthTicks(const AnimSetTable& table, int id) {
    const AnimResolution r = ResolveAnim(table, 0, id);
    if (!r.found()) return -1;
    // haze/anim.c:110-115:
    //   `fistpll((long double)byId[id]->length); return (int)r;`
    // A ROUND, not a truncation - so a 3360.0 tick clip is 3360 and a clip of
    // length 3359.6 would be 3360 as well.
    return RoundInt(static_cast<long double>(r.length_ticks));
}

float AnimPhase(float time_ticks, float length_ticks) {
    // anim.c:155-161:
    //   `float t = Anim_GetTime(o, track);`   <- long double narrowed to float
    //   `return t / Anim_GetLength(o, track);` <- float promoted back to long double
    const float narrowed = time_ticks;
    const long double ratio = static_cast<long double>(narrowed) /
                              static_cast<long double>(length_ticks);
    return static_cast<float>(ratio);
}

// ---------------------------------------------------------------------------
// Frame / tick timing
// ---------------------------------------------------------------------------

bool InitAnimInst(AnimInst& inst, const AnimSetTable& table, int bone, int id, uint32_t flags,
                  float ticks_per_frame, const float* first_pos, const float* last_pos) {
    const AnimResolution r = ResolveAnim(table, bone, id);
    if (!r.found()) {
        // The engine has no guard here. BoneObj_NewAnimSource (boneobj.c:775-786)
        // returns `gAnimSets[skel].byId[id]` unchecked and BoneObj_InitAnimInst
        // (boneobj.c:1014,1018) dereferences `desc->pos->keys[...]`, so a missing
        // id is an access violation, not a fallback.
        return false;
    }

    inst.bone = bone;
    inst.id = id;
    inst.flags = flags;
    inst.desc_index = r.desc_index;
    inst.delta[0] = 0.0f;
    inst.delta[1] = 0.0f;
    inst.delta[2] = 0.0f;

    // boneobj.c:1013-1021.
    if (flags & kAnimReverse) {
        // `memcpy(inst->pos, desc->pos->keys[desc->pos->count - 1].pos, 12)`
        if (last_pos != nullptr) {
            inst.pos[0] = last_pos[0];
            inst.pos[1] = last_pos[1];
            inst.pos[2] = last_pos[2];
        }
        // `inst->speed = -gTicksPerFrame;`
        inst.speed = -ticks_per_frame;
        // `memcpy(&inst->time, &desc->length, 4);` - a bit copy, so the instance
        // starts exactly at the clip length and the first tick immediately
        // satisfies `!(time > 0)`.
        inst.time = r.length_ticks;
    } else {
        // `memcpy(inst->pos, desc->pos->keys[0].pos, 12)`
        if (first_pos != nullptr) {
            inst.pos[0] = first_pos[0];
            inst.pos[1] = first_pos[1];
            inst.pos[2] = first_pos[2];
        }
        // `*(int *)&inst->time = 0;` and
        // `memcpy(&inst->speed, &gTicksPerFrame, 4);` - both bit copies.
        inst.time = 0.0f;
        inst.speed = ticks_per_frame;
    }
    return true;
}

bool SamplePosTrack(const PosTrack& track, float time_ticks, bool smooth,
                    const float previous_pos[3], float out_pos[3], float out_delta[3]) {
    if (track.keys == nullptr || track.count <= 0) {
        // See the header: the engine reads keys[-1] here.
        return false;
    }

    const PosKey* keys = track.keys;
    const int n = track.count;
    float p[3];

    if (n == 1) {
        // boneobj.c:794-798.
        p[0] = keys[0].pos[0];
        p[1] = keys[0].pos[1];
        p[2] = keys[0].pos[2];
    } else {
        // boneobj.c:800-811. `kt` is initialised from the low 32 bits of the
        // `AnimInst *` in the original - the decompiler models it as a union
        // (boneobj.c:800-804) because the source left it uninitialised. The loop
        // overwrites it on the first iteration for every n >= 1, so the value is
        // only observable when n == 0, which the guard above rejects.
        const float tm = time_ticks;
        float kt = 0.0f;
        int i = 0;

        for (; i < n; i++) {
            kt = keys[i].time;
            if (!(kt < tm)) break;
        }

        if (tm == kt) {
            // boneobj.c:812-813: exactly on a key.
            p[0] = keys[i].pos[0];
            p[1] = keys[i].pos[1];
            p[2] = keys[i].pos[2];
        } else if (i == n) {
            // boneobj.c:814-815: past the last key.
            p[0] = keys[n - 1].pos[0];
            p[1] = keys[n - 1].pos[1];
            p[2] = keys[n - 1].pos[2];
        } else {
            const PosKey* k = &keys[i];
            const PosKey* pk = &keys[i - 1];

            if (smooth) {
                // boneobj.c:819-841, the gBoneSmoothAnims branch, which is the
                // default (lod/boneobj.c:65 sets it to 1). `dt` is a float
                // subtraction widened to long double; `s` runs 0 at the previous
                // key to 1 at the next one.
                const long double dt = kt - pk->time;
                const long double s = 1.0f - (kt - tm) / dt;
                const long double s2 = s * s;
                const long double s3 = s2 * s;
                // s2f/s3f are the float truncations the engine takes before
                // mixing them back into long double expressions.
                const float s2f = static_cast<float>(s2);
                const float s3f = static_cast<float>(s3);
                const long double h = (s3 + s3) - s2f * 3.0f;
                const long double h1 = h + 1.0f;
                const long double h2 = -h;
                const long double h3 = (s - (s2f + s2f)) + s3f;
                const float h4 = s2f - s3f;  // a FLOAT subtraction
                const long double o0 = pk->out[0] * dt;
                const long double o1 = pk->out[1] * dt;
                const long double o2 = pk->out[2] * dt;
                const long double i0 = k->in[0] * dt;
                const long double i1 = k->in[1] * dt;
                const long double i2 = dt * k->in[2];

                // The three components do NOT share a term order in the
                // original (boneobj.c:839-841); floating-point addition is not
                // associative, so each is reproduced as written.
                p[0] = static_cast<float>(((k->pos[0] * h2 + i0 * h4) + pk->pos[0] * h1) + o0 * h3);
                p[1] = static_cast<float>(((k->pos[1] * h2 + pk->pos[1] * h1) + i1 * h4) + o1 * h3);
                p[2] = static_cast<float>(((k->pos[2] * h2 + pk->pos[2] * h1) + i2 * h4) + o2 * h3);
            } else {
                // boneobj.c:842-855, the linear branch. Note the mixed
                // precision: d0 and e0/e2 stay long double while d1 is stored
                // back into a float, so the x component and the y component are
                // rounded differently.
                const long double f = (kt - tm) / (kt - pk->time);
                const long double d0 = pk->pos[0] - k->pos[0];
                float d1 = pk->pos[1] - k->pos[1];
                float d2 = pk->pos[2] - k->pos[2];
                const long double e0 = d0 * f;
                long double e2;

                d1 = static_cast<float>(d1 * f);
                e2 = f * d2;
                p[0] = static_cast<float>(e0 + k->pos[0]);
                p[1] = d1 + k->pos[1];  // float + float
                p[2] = static_cast<float>(e2 + k->pos[2]);
            }
        }
    }

    // boneobj.c:857-865:
    //   `inst->delta[i] = p[i] - inst->pos[i]; memcpy(inst->pos, p, 12);`
    // The delta is against the position the instance held BEFORE this sample,
    // which is why the caller passes it in.
    if (out_delta != nullptr) {
        out_delta[0] = p[0] - previous_pos[0];
        out_delta[1] = p[1] - previous_pos[1];
        out_delta[2] = p[2] - previous_pos[2];
    }
    if (out_pos != nullptr) {
        out_pos[0] = p[0];
        out_pos[1] = p[1];
        out_pos[2] = p[2];
    }
    return true;
}

AnimAdvance AdvanceAnimInst(AnimInst& inst, float length_ticks, const PosTrack* pos_track,
                            bool smooth) {
    AnimAdvance out;

    // boneobj.c:881-885: the event bit is cleared on both the instance and the
    // bone, then the playhead moves by exactly one `speed` per call.
    //   `old = inst->time; now = inst->speed + inst->time; inst->time = now;`
    // `inst->flags &= ~BONE_EVENTS_DONE; bone->flags &= ~BONE_EVENTS_DONE;`
    inst.flags &= ~kBoneEventsDone;
    out.old_time = inst.time;
    const float now = inst.speed + inst.time;
    inst.time = now;
    out.time = now;

    const bool reverse = (inst.flags & kAnimReverse) != 0;

    // boneobj.c:892-894:
    //   `if ((!(inst->time < d->length) && !(inst->flags & ANIM_REVERSE)) ||
    //     (!(inst->time > 0.0f)   &&  (inst->flags & ANIM_REVERSE)))`
    // Both halves are negated comparisons, so the forward end test is
    // `time >= length` and a tick landing exactly on the length counts as past
    // the end. NaN fails both `!(NaN < x)` ... no: `!(NaN < x)` is TRUE, so a
    // NaN playhead is treated as past the end, not as still running.
    if ((!(inst.time < length_ticks) && !reverse) || (!(inst.time > 0.0f) && reverse)) {
        out.past_end = true;
        // boneobj.c:895-896.
        inst.flags |= kBoneEventsDone;
        out.events_done = true;

        if (inst.flags & kAnimLoop) {
            out.wrapped = true;
            float from;
            if (reverse) {
                // boneobj.c:901-902.
                inst.time = length_ticks + inst.time;
                from = length_ticks + 1.0f;
            } else {
                // boneobj.c:904-905. ONE subtraction, not a while loop: a speed
                // larger than the clip length leaves the playhead past the end
                // after the wrap.
                inst.time = inst.time - length_ticks;
                from = -1.0f;
            }
            (void)from;  // the event-collection window; see the header note

            // boneobj.c:909-929. Root motion correction, and ONLY on the wrap
            // tick: the position track is re-sampled at the far end of the clip
            // and the root is placed relative to the first (forward) or last
            // (reverse) key.
            if (inst.bone == 0 && !(inst.flags & kAnimFirst) && pos_track != nullptr) {
                const float saved = inst.time;
                float sampled[3] = {0.0f, 0.0f, 0.0f};
                float delta[3] = {0.0f, 0.0f, 0.0f};
                // `inst->time = 0` (reverse) or `= desc->length` (forward), then
                // BoneObj_AnimPositionCB(NULL, NULL, inst).
                const float probe = reverse ? 0.0f : length_ticks;
                if (SamplePosTrack(*pos_track, probe, smooth, inst.pos, sampled, delta)) {
                    const PosKey* key = reverse ? &pos_track->keys[pos_track->count - 1]
                                                : &pos_track->keys[0];
                    // `inst->pos[i] = k->pos[i] - inst->delta[i]`, with the delta
                    // the probe sample just produced against the instance's
                    // pre-probe position.
                    inst.pos[0] = key->pos[0] - delta[0];
                    inst.pos[1] = key->pos[1] - delta[1];
                    inst.pos[2] = key->pos[2] - delta[2];
                    inst.delta[0] = delta[0];
                    inst.delta[1] = delta[1];
                    inst.delta[2] = delta[2];
                    out.root_fixup = true;
                    out.root_pos[0] = inst.pos[0];
                    out.root_pos[1] = inst.pos[1];
                    out.root_pos[2] = inst.pos[2];
                }
                // boneobj.c:926-928: `inst->flags &= ~ANIM_FIRST;
                // inst->time = saved; return n;`
                inst.flags &= ~kAnimFirst;
                inst.time = saved;
                out.time = saved;
                return out;
            }
        } else {
            // boneobj.c:930-937: a one-shot CLAMPS at the far end and keeps
            // running. It does not stop, and BONE_EVENTS_DONE is the only signal
            // the caller gets.
            out.clamped = true;
            if (reverse) {
                inst.time = 0.0f;
                inst.flags &= ~kAnimFirst;
                out.time = inst.time;
                return out;
            }
            inst.time = length_ticks;
        }
    }

    // boneobj.c:939: `inst->flags &= ~ANIM_FIRST;`
    inst.flags &= ~kAnimFirst;
    out.time = inst.time;
    return out;
}

float AnimBlendFrames(float blend_seconds, float ticks_per_frame) {
    // anim.c:40: `float t = blend * kTicksPerSecond / gTicksPerFrame;`
    // Every operand is float and the file is excess-precision=standard, so the
    // product rounds before the divide. With ticks_per_frame == 0 the result is
    // +inf, or NaN when blend_seconds is also 0 (the pre-game state: BoneObj_Init
    // zeroes gTicksPerFrame at lod/boneobj.c:71 and only game.c:449 sets it).
    const float scaled = blend_seconds * kAnimTicksPerSecond;
    return scaled / ticks_per_frame;
}

AnimPlayMode AnimPlayModeFor(float frames, bool has_old_blend, bool has_parent) {
    // anim.c:44-52: `if (old || b->parent) { if (!(t < 1.0f)) { fade; return; }
    // if (old) delete; }`
    if (!has_old_blend && !has_parent) return kAnimPlayImmediate;
    if (!(frames < 1.0f)) {
        // NaN lands here: `!(NaN < 1.0f)` is true.
        return std::isnan(frames) ? kAnimPlayFadeNaN : kAnimPlayFade;
    }
    return kAnimPlayImmediate;
}

float AnimFadeOutFrames(float time_seconds, float ticks_per_frame) {
    // anim.c:74-75:
    //   `long double tt = time * kTicksPerSecond / gTicksPerFrame;`
    //   `float t = tt;`
    // The multiply and the divide are float (excess-precision=standard), then
    // the result is widened; `t` is the float truncation of that.
    const float scaled = time_seconds * kAnimTicksPerSecond;
    const long double tt = static_cast<long double>(scaled / ticks_per_frame);
    // anim.c:81: `if (tt < 1.0f)` - the test is on the LONG DOUBLE, so a value
    // that rounds up to 1.0f as a float but is below 1.0 as a long double takes
    // the "delete now" path.
    if (tt < 1.0f) return 0.0f;
    return static_cast<float>(tt);
}

void AnimSetSpeed(AnimInst& inst, float speed_multiplier, float ticks_per_frame) {
    // anim.c:172-179: `inst->speed = gTicksPerFrame * speed;` then
    // BoneObj_UpdateAnimDirection (anim.c:163-169), which sets ANIM_REVERSE iff
    // the speed is < 0 and CLEARS it otherwise. A speed of exactly 0 therefore
    // clears the reverse bit rather than preserving it.
    inst.speed = ticks_per_frame * speed_multiplier;
    if (inst.speed < 0.0f)
        inst.flags |= kAnimReverse;
    else
        inst.flags &= ~kAnimReverse;
}

void AnimSetTime(AnimInst& inst, float time_ticks) {
    // anim.c:181-185: `Anim_GetInst(o, track)->time = time;` - a raw store.
    // No clamp, no wrap, and no key resample: the next position callback
    // interpolates from wherever this lands.
    inst.time = time_ticks;
}

// ---------------------------------------------------------------------------
// The authored form
// ---------------------------------------------------------------------------

GenericTbaAnim ParseGenericTbaAnim(const std::vector<std::string>& tokens) {
    GenericTbaAnim out;
    // The defaults are the ones GenericTBA_Create writes at tba.c:105-106,
    // `t->use_time = 4.0f; t->anim = -1;`. They are applied AFTER the base
    // ExplodeObject create handler runs (tba.c:102-103), and the authored values
    // are written over them afterwards by Level_NewTask's param loop
    // (level.c:206-221), so an unauthored or unparseable token really does leave
    // these defaults in place.
    out.anim = TokenInt(tokens, kGenericTbaAnimToken, -1);
    out.use_time = TokenFloat(tokens, kGenericTbaUseTimeToken, 4.0f);
    return out;
}

SoldierAnim ParseSoldierAnim(const std::vector<std::string>& tokens) {
    SoldierAnim out;
    // HumanSoldier's Task_DeclareParameters ends with ("Bone Heirachy","Int32",
    // "Stand Animation","Int32") after Position(3), Gamma, Model, Team
    // (retail LEVEL2/objects.qsc:47), so the two tokens sit at 9 and 10 in the
    // editor's argTokens layout. Soldier_Open registers them at
    // soldier/soldier.c:717-734 as `Param_Int32()` on `s->s.model` and
    // `s->s.idleAnim`.
    out.bone_hierarchy = TokenInt(tokens, kSoldierBoneHierarchyToken, -1);
    out.stand_animation = TokenInt(tokens, kSoldierStandAnimationToken, -1);
    return out;
}

int SoldierModelOrDefault(int authored_model) {
    // soldier.c:751 `s->s.model = 1;` - the default is 1, NOT -1 and not 0.
    // Skeleton 1 is therefore what a HumanSoldier with no authored
    // "Bone Heirachy" uses.
    return authored_model >= 0 ? authored_model : 1;
}

// ---------------------------------------------------------------------------
// How a caller plays an authored animation
// ---------------------------------------------------------------------------

int AnimUseTimeFrames(float use_time_seconds) {
    // humanstate.c:520 `#define FRAMES(seconds) x87_fistp((seconds) * FPS)` with
    // FPS 30.0f (humanstate.c:91), and x87_fistp is `fistpll` with the current
    // rounding mode (humanstate.c:123-128) - round to nearest even.
    //
    // DIVERGENCE, named: humanstate.c is one of the two cited files that does
    // NOT set excess-precision=standard (the other is level.c), so in the
    // original build the `seconds * 30.0f` product may have been carried in
    // extended precision into the `long double` parameter. This computes the
    // float product first. The two differ only when `use_time * 30.0f` lands
    // within one ulp of a .5 boundary.
    const float product = use_time_seconds * kAnimHumanStateFps;
    return RoundInt(static_cast<long double>(product));
}

int GenericTbaGetAnim(const GenericTbaAnim& authored) {
    // tba.c:280-284: `int16_t GenericTBA_GetAnim(GenericTBA *t) { return t->anim; }`
    // A bare field read of the Int16 at 0x24A (tba.c:29-51). The two extra
    // arguments IGI-RE's ORIG declares at humanstate.c:35 are a
    // caller-cleans-stack artifact of the call site, not parameters.
    return authored.anim;
}

float GenericTbaGetUseTime(const GenericTbaAnim& authored) {
    // tba.c:274-278: `float GenericTBA_GetUseTime(GenericTBA *t) { return t->use_time; }`
    return authored.use_time;
}

bool HumanAnimDone(uint32_t bone_flags) {
    // loadscreen/human.c:200-203: `return (h->view->motion->flags & 0x100) != 0;`
    // 0x100 is BONE_EVENTS_DONE, the bit AdvanceAnimInst reports as events_done.
    return (bone_flags & kBoneEventsDone) != 0;
}

// ---------------------------------------------------------------------------
// The player's default slot table
// ---------------------------------------------------------------------------

// humanplayer.c:1563, `char store_538870[392]` = 49 * sizeof(AnimSlot). The
// literal lists 389 bytes, so the final three are the C initialiser's implicit
// zeros; slot 48 is therefore { 2, 1 }.
const AnimSlot kAnimSlots[kAnimSlotCount] = {
    {2, 1},   {4, 1},   {37, 1},  {35, 1},  {64, 1},  {70, 1},  {68, 1},  {11, 1},  {10, 1},
    {57, 1},  {74, 1},  {72, 1},  {75, 1},  {77, 1},  {76, 1},  {81, 1},  {80, 1},  {14, 0},
    {15, 0},  {19, 1},  {17, 1},  {41, 1},  {39, 1},  {27, 1},  {49, 1},  {47, 1},  {29, 1},
    {28, 1},  {25, 1},  {2, 1},   {0, 0},   {0, 0},   {0, 0},   {0, 0},   {0, 0},   {0, 0},
    {0, 0},   {168, 1}, {170, 2}, {170, 0}, {169, 1}, {171, 0}, {172, 1}, {173, 1}, {2, 1},
    {19, 1},  {0, 0},   {0, 0},   {2, 1},
};

// humanplayer.c:1554-1556, `store_5385B4[49]` aliased to gBodyStates, the state
// table StateMachine_Init installs at humanplayer.c:1225. Slot i of kAnimSlots
// is the animation state i enters with.
const char* const kBodyStateNames[kAnimSlotCount] = {
    "HumanState_Enter00",
    "HumanState_Enter01",
    "HumanState_Enter02",
    "HumanState_Enter03",
    "HumanState_Enter04",
    "HumanState_Enter05",
    "HumanState_Enter06",
    "HumanState_Enter07",
    "HumanState_Enter08",
    "HumanState_Enter09",
    "HumanState_Enter10",
    "HumanState_Enter11",
    "HumanState_Enter12",
    "HumanState_Enter13",
    "HumanState_Enter14",
    "HumanState_Enter15",
    "HumanState_Enter16",
    "HumanState_Enter17",
    "HumanState_Enter18",
    "HumanState_Enter19",
    "HumanState_Enter20",
    "HumanState_Enter21",
    "HumanPlayer_RunForwardRightEnter",
    "HumanPlayer_RunBackEnter",
    "HumanPlayer_RunBackLeftEnter",
    "HumanPlayer_RunBackRightEnter",
    "HumanPlayer_RunLeftEnter",
    "HumanPlayer_RunRightEnter",
    "HumanPlayer_JumpEnter",
    "HumanPlayer_FallEnter",
    "HumanPlayer_PicklockEnter",
    "HumanPlayer_Use31Enter",
    "HumanPlayer_TypeComputerEnter",
    "HumanPlayer_Use33Enter",
    "HumanPlayer_Use34Enter",
    "HumanPlayer_Use35Enter",
    "HumanPlayer_Use36Enter",
    "HumanPlayer_LadderEnter",
    "HumanPlayer_LadderOnEnter",
    "HumanPlayer_LadderOffTopEnter",
    "HumanPlayer_LadderSlideEnter",
    "HumanPlayer_State41Enter",
    "HumanPlayer_WireDragEnter",
    "HumanPlayer_ZiplineEnter",
    "HumanPlayer_BodyLeanEnter",
    "HumanPlayer_BodyComputerEnter",
    "HumanPlayer_BodyHitEnter",
    "HumanPlayer_BodyDeathEnter",
    "HumanPlayer_BodyFlyEnter",
};

// ---------------------------------------------------------------------------
// The bone / skin side
// ---------------------------------------------------------------------------

namespace {

// d3drender.c:259-261 / :278-280 / :296-298. The three call sites spell the same
// row-by-row product with DIFFERENT term orders, so each is a separate function
// rather than one shared helper - floating-point addition is not associative and
// the shipped code does not agree with itself.
inline void RotateBasePass(const float p[3], const float m[9], float out[3]) {
    // d3drender.c:259-261.
    out[0] = p[0] * m[0] + p[2] * m[2] + p[1] * m[1];
    out[1] = p[0] * m[3] + p[2] * m[5] + p[1] * m[4];
    out[2] = p[2] * m[8] + m[7] * p[1] + m[6] * p[0];
}

inline void RotateExtraPass(const float p[3], const float m[9], float out[3]) {
    // d3drender.c:278-280. Same product, reversed term order.
    out[0] = m[2] * p[2] + m[1] * p[1] + m[0] * p[0];
    out[1] = m[5] * p[2] + m[4] * p[1] + m[3] * p[0];
    out[2] = m[8] * p[2] + m[7] * p[1] + m[6] * p[0];
}

inline void RotateNormal(const float n[3], const float m[9], float out[3]) {
    // d3drender.c:271-273, identical in both non-rigid and rigid paths
    // (d3drender.c:303-305).
    out[0] = m[1] * n[1] + m[2] * n[2] + n[0] * m[0];
    out[1] = n[2] * m[5] + n[1] * m[4] + n[0] * m[3];
    out[2] = m[6] * n[0] + m[8] * n[2] + m[7] * n[1];
}

}  // namespace

SkinTransformOutput SkinTransformVertex(const SkinTransformInput& in, const BoneTransform& bone,
                                        bool rigid, const SkinTransformOutput* base) {
    SkinTransformOutput out;

    if (rigid) {
        // d3drender.c:294-308. `long double x = <float expression>` - the
        // rotation is computed as FLOAT and then widened, and only the
        // `x + b->pos.x` add is done in long double.
        float rotated[3];
        RotateBasePass(in.p, bone.matrix, rotated);

        out.position[0] = static_cast<float>(static_cast<long double>(rotated[0]) +
                                             static_cast<long double>(bone.pos[0]));
        out.position[1] = static_cast<float>(static_cast<long double>(rotated[1]) +
                                             static_cast<long double>(bone.pos[1]));
        out.position[2] = static_cast<float>(static_cast<long double>(rotated[2]) +
                                             static_cast<long double>(bone.pos[2]));

        RotateNormal(in.n, bone.matrix, out.normal);
        out.uv[0] = in.uv[0];
        out.uv[1] = in.uv[1];
        return out;
    }

    if (base != nullptr) {
        // d3drender.c:276-290, the extra-influence loop. `t->x = w * X + t->x`
        // ACCUMULATES into an already-written base vertex; the normal and uv of
        // the base vertex are left alone.
        float rotated[3];
        RotateExtraPass(in.p, bone.matrix, rotated);

        // `long double X = x + b->pos.x;` - the sum is a FLOAT add, widened.
        const long double x = static_cast<long double>(rotated[0] + bone.pos[0]);
        const long double y = static_cast<long double>(rotated[1] + bone.pos[1]);
        const long double z = static_cast<long double>(rotated[2] + bone.pos[2]);
        const long double w = static_cast<long double>(in.w);

        out.position[0] = static_cast<float>(w * x + static_cast<long double>(base->position[0]));
        out.position[1] = static_cast<float>(w * y + static_cast<long double>(base->position[1]));
        out.position[2] = static_cast<float>(w * z + static_cast<long double>(base->position[2]));
        out.normal[0] = base->normal[0];
        out.normal[1] = base->normal[1];
        out.normal[2] = base->normal[2];
        out.uv[0] = base->uv[0];
        out.uv[1] = base->uv[1];
        return out;
    }

    // d3drender.c:257-274, the base pass. `long double X = x + b->pos.x` is a
    // float sum widened, and `o->x = X * v->w` is a long double multiply stored
    // back into a float - so the sum is rounded TWICE, once as a float and once
    // on the way out.
    float rotated[3];
    RotateBasePass(in.p, bone.matrix, rotated);

    const long double x = static_cast<long double>(rotated[0] + bone.pos[0]);
    const long double y = static_cast<long double>(rotated[1] + bone.pos[1]);
    const long double z = static_cast<long double>(rotated[2] + bone.pos[2]);
    const long double w = static_cast<long double>(in.w);

    out.position[0] = static_cast<float>(x * w);
    out.position[1] = static_cast<float>(y * w);
    out.position[2] = static_cast<float>(z * w);
    RotateNormal(in.n, bone.matrix, out.normal);
    out.uv[0] = in.uv[0];
    out.uv[1] = in.uv[1];
    return out;
}

void SkinAccumulatePosition(const SkinTransformInput& in, const BoneTransform& bone,
                            SkinTransformOutput& base) {
    const SkinTransformOutput accumulated = SkinTransformVertex(in, bone, false, &base);
    base.position[0] = accumulated.position[0];
    base.position[1] = accumulated.position[1];
    base.position[2] = accumulated.position[2];
}

}  // namespace igi
