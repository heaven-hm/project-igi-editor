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
#include <cstddef>
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
                // boneobj.c:823 declares `float s2f, s3f, h1, h2, h3, h4, o0,
                // o1, o2, i0, i1;` and only `s2`, `s3`, `h`, `dt` and `in2` stay
                // long double. Every one of h1..h4, o0..o2, i0 and i1 is ROUNDED
                // TO FLOAT by its store (boneobj.c:829-837), and the three
                // component expressions at :839-841 are therefore evaluated
                // entirely in float. Reproduce the rounding, not the wider
                // arithmetic the expressions suggest.
                const float s2f = static_cast<float>(s2);
                const float s3f = static_cast<float>(s3);
                const long double h = (s3 + s3) - s2f * 3.0f;
                const float h1 = static_cast<float>(h + 1.0f);
                const float h2 = static_cast<float>(-h);
                const float h3 = static_cast<float>((s - (s2f + s2f)) + s3f);
                const float h4 = s2f - s3f;  // a FLOAT subtraction
                const float o0 = static_cast<float>(pk->out[0] * dt);
                const float o1 = static_cast<float>(pk->out[1] * dt);
                const float o2 = static_cast<float>(pk->out[2] * dt);
                const float i0 = static_cast<float>(k->in[0] * dt);
                const float i1 = static_cast<float>(k->in[1] * dt);
                const long double i2 = dt * k->in[2];

                // The three components do NOT share a term order in the
                // original (boneobj.c:839-841); floating-point addition is not
                // associative, so each is reproduced as written. Every operand
                // is float, so the whole expression is float.
                p[0] = ((k->pos[0] * h2 + i0 * h4) + pk->pos[0] * h1) + o0 * h3;
                p[1] = ((k->pos[1] * h2 + pk->pos[1] * h1) + i1 * h4) + o1 * h3;
                p[2] = ((k->pos[2] * h2 + pk->pos[2] * h1) + i2 * h4) + o2 * h3;
            } else {
                // boneobj.c:842-855, the linear branch. Note the mixed
                // precision: d0 and e2 stay long double, while e0 and d1 are
                // stored back into a float (boneobj.c:847 `float e0 = d0 * f;`),
                // so the x component and the y component are rounded differently
                // - and the x component is rounded too, which the expression
                // `e0 + k->pos[0]` alone does not show.
                const long double f = (kt - tm) / (kt - pk->time);
                const long double d0 = pk->pos[0] - k->pos[0];
                float d1 = pk->pos[1] - k->pos[1];
                float d2 = pk->pos[2] - k->pos[2];
                const float e0 = static_cast<float>(d0 * f);
                long double e2;

                d1 = static_cast<float>(d1 * f);
                e2 = f * d2;
                p[0] = e0 + k->pos[0];  // float + float
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

// ===========================================================================
// PART TWO - the pose pipeline
// ===========================================================================
//
// The x87 instructions the engine uses (math/x87.h) have no portable
// equivalent, so the port uses the C library's long-double entry points:
//   fsqrt -> std::sqrt(long double)      (both correctly rounded; identical)
//   fsin  -> std::sin(long double)       (may differ by one ulp in the last
//                                         bit for some arguments - NAMED
//                                         DIVERGENCE, see the header)
//   facos -> std::acos(double)           via Math_ACosF's clamp (file/vec.c:36)
// fsin and facos are only reached on a rotation track with > 1 key and a
// non-looping or interpolated sample, i.e. on the great majority of real frames,
// so this is the one place where the port's pose can differ from retail by a
// last-bit rounding. It is reported, not hidden.

namespace {

constexpr float kTwoPiPose = 0x1.921fb6p+2f;   // boneobj.c:32, haze/anim.c:13
constexpr float kSlerpMinPose = 0.001f;        // boneobj.c:33
constexpr float kSlerpMaxPose = 0.999f;        // boneobj.c:34
constexpr float kSmallAnglePose = 0.01f;       // boneobj.c:35

inline long double X87Sqrt(long double x) { return std::sqrt(x); }
inline long double X87Sin(long double x) { return std::sin(x); }

// Math_ACosF HOOK 0x4B2760 (file/vec.c:36-46). Note the guard is `!(d >= -1.0)`,
// which is TRUE for NaN, so a NaN input returns acos(-1) = PI rather than NaN.
// And the clamp is one-sided: a value below -1 returns PI, a value above 1 is
// clamped to 1 and returns 0.
long double MathACosF(float x) {
    double d = x;
    if (!(d >= -1.0)) return std::acos(-1.0);
    if (d > 1.0) d = 1.0;
    return std::acos(d);
}

// Quat_Dot HOOK 0x4B4570 (math/quat.c:33-37). The order is w, z, y, x.
long double QuatDot(const EngineQuat& a, const EngineQuat& b) {
    return static_cast<long double>(a.w) * b.w + static_cast<long double>(a.z) * b.z +
           static_cast<long double>(a.y) * b.y + static_cast<long double>(a.x) * b.x;
}

// inv_len (boneobj.c:72-75).
inline long double InvLen(long double sum) { return 1.0 / X87Sqrt(sum); }

// The float-bit reinterpretation the engine performs with
// `*(int *)&ka->time` (boneobj.c:1146) and with `memcpy` of a whole Quat
// (20 bytes) from a 16-byte `float[4]` field (boneobj.c:1044-1048, 1056).
int FloatBits(float f) {
    int out;
    std::memcpy(&out, &f, sizeof(out));
    return out;
}

// slerp_weights (boneobj.c:1067-1078).
void SlerpWeights(long double angle, float r, float invSin, long double& s0, long double& s1) {
    if (angle < kSmallAnglePose) {
        s0 = 1.0f - r;
        s1 = r;
    } else {
        const float t = static_cast<float>(angle * r);
        s0 = X87Sin(angle - t) * invSin;
        s1 = X87Sin(t) * invSin;
    }
}

// slerp_keys (boneobj.c:1042-1049). `count` is NOT a key count: it is the raw
// INTEGER BITS of a key time, passed in by the caller (boneobj.c:1146,1153).
void SlerpKeys(EngineQuat& out, const float* c0, const EngineQuat& c, long double s0, long double s1,
               int count) {
    out.x = s1 * c.x + s0 * c0[0];
    out.y = s1 * c.y + s0 * c0[1];
    out.z = s1 * c.z + s0 * c0[2];
    out.w = s1 * c.w + s0 * c0[3];
    out.unk10 = count + c.unk10 + 1;
}

// slerp_angle (boneobj.c:1052-1065). The engine's `memcpy(c, key, sizeof(*c))`
// copies 20 bytes out of a 16-byte quaternion field, so `c->unk10` becomes the
// bits of whatever follows that field. Two call sites (boneobj.c:1141,1157) read
// `kb->q`, where the following 4 bytes are `kb->time` - reproduced exactly. The
// third (boneobj.c:1148) reads `kb->b`, the LAST field of RotKey, where the
// following 4 bytes are past the key (the next key's q[0], or past the array on
// the final key); the port reports 0 there instead of reading out of bounds -
// NAMED DIVERGENCE, and it only reaches the >1000 renormalisation counter.
long double SlerpAngle(EngineQuat& c, const float* key, const SlerpInfo& sl, bool readsPastKey) {
    long double angle = sl.angle;
    c.x = key[0];
    c.y = key[1];
    c.z = key[2];
    c.w = key[3];
    c.unk10 = readsPastKey ? 0 : FloatBits(key[4]);
    if (!(sl.angle < kTwoPiPose)) {
        angle = angle - kTwoPiPose;
        c.x = -c.x;
        c.y = -c.y;
        c.z = -c.z;
        c.w = -c.w;
    }
    return angle;
}

}  // namespace

void QuatMul(long double out[4], const float* A, const float* B, int round) {
    long double a = (A[2] - A[1]) * (B[1] - B[2]);
    long double b = (A[3] - A[0]) * (B[2] + B[1]);
    long double c = (B[3] - B[0]) * (A[2] + A[1]);
    long double d = (A[2] + A[0]) * (B[0] + B[1]);
    long double e = (A[3] + A[1]) * (B[3] - B[2]);
    long double f = (A[3] - A[1]) * (B[2] + B[3]);
    long double s;
    float t;

    if (round & kQuatRoundA) a = static_cast<float>(a);
    if (round & kQuatRoundB) b = static_cast<float>(b);
    if (round & kQuatRoundC) c = static_cast<float>(c);
    if (round & kQuatRoundD) d = static_cast<float>(d);
    if (round & kQuatRoundE) e = static_cast<float>(e);
    if (round & kQuatRoundF) f = static_cast<float>(f);
    s = (f + e) + d;
    t = static_cast<float>(((A[2] - A[0]) * (B[0] - B[1]) + s) * 0.5f);
    out[0] = ((A[3] + A[0]) * (B[0] + B[3]) + t) - s;
    out[1] = (b + t) - f;
    out[2] = (t + c) - e;
    out[3] = (a + t) - d;
}

void QuatSlerpPrepare(int& flip, float& angle, float& invSin, const EngineQuat& a, const EngineQuat& b) {
    // math/quat.c:8-31.
    flip = 0;
    const long double dot = QuatDot(a, b);
    float d = static_cast<float>(dot);
    if (dot < 0.0f) {
        if (d < -1e-8f) {
            d = -d;
            flip = 1;
        } else {
            d = 0.0f;
        }
    }
    const long double ang = MathACosF(d);
    angle = static_cast<float>(ang);
    if (ang != 0.0f)
        invSin = static_cast<float>(1.0f / X87Sin(ang));
    else
        invSin = 1.0f;
}

void SlerpPrepare(SlerpInfo& out, const float* a, const float* b) {
    // haze/anim.c:19-37. The two quaternions are built with unk10 = 0, so the
    // angle and invSin do not carry the depth counter.
    EngineQuat qa;
    EngineQuat qb;
    int flip = 0;

    qa.x = a[0];
    qa.y = a[1];
    qa.z = a[2];
    qa.w = a[3];
    qa.unk10 = 0;
    qb.x = b[0];
    qb.y = b[1];
    qb.z = b[2];
    qb.w = b[3];
    qb.unk10 = 0;
    QuatSlerpPrepare(flip, out.angle, out.invSin, qa, qb);
    if (flip) out.angle = out.angle + kTwoPiPose;
}

bool AnimPrepareSlerps(const EngineClip& clip, int bone_count, bool smooth, SlerpInfo* out_slerp,
                       SlerpInfo* out_slerp2, const int* key_offsets, int* key_counts) {
    // haze/anim.c:40-84. `bones <= 0 || set->count <= 0` guards the whole walk;
    // here that is an empty skeleton or a clip with no rotation tracks.
    if (bone_count <= 0 || clip.rot == nullptr) return false;
    if (out_slerp == nullptr || key_offsets == nullptr) return false;

    for (int b = 0; b < bone_count; ++b) {
        const RotTrack& t = clip.rot[b];
        const int keys = t.count;
        if (key_counts != nullptr) key_counts[b] = keys;
        // haze/anim.c:60 `if (keys <= 1 || t->slerp) continue;` - a table is
        // built once and never rebuilt, and a single-key track never gets one
        // (the sampler's n == 1 branch returns before reading it).
        if (keys <= 1) continue;
        SlerpInfo* dst = out_slerp + key_offsets[b];
        SlerpInfo* dst2 = (smooth && out_slerp2 != nullptr) ? out_slerp2 + key_offsets[b] : nullptr;
        for (int k = 0; k < keys; ++k) {
            const int next = (k + 1) % keys;

            // haze/anim.c:68-76: the wrap entry of a NON-looping clip is left
            // {0, 0}, which the sampler reads as "no slerp, use the key"
            // (boneobj.c:1142,1149 `if (angle == 0.0f)`).
            if (next == 0 && !(clip.flags & kAnimDescLoops)) {
                dst[k].angle = 0.0f;
                dst[k].invSin = 0.0f;
                if (dst2 != nullptr) {
                    dst2[k].angle = 0.0f;
                    dst2[k].invSin = 0.0f;
                }
                continue;
            }
            SlerpPrepare(dst[k], t.keys[k].q, t.keys[next].q);
            if (dst2 != nullptr) SlerpPrepare(dst2[k], t.keys[k].a, t.keys[next].b);
        }
    }
    return true;
}

void BoneObjBuild(BonePoseState& state, const int* parents, const float* offsets, int count) {
    // lod/boneobj.c:100-129 (the allocation and its zeroing) plus
    // lod/boneobj.c:153-188 (the tree).
    state.bones.clear();
    if (count <= 0) return;
    state.bones.assign(static_cast<std::size_t>(count), EngineBone{});

    // BoneObj_Build's index scan. `parents[i] == -1` are the roots and are
    // appended to a synthetic parent at depth 0; BoneObj_Create then clears
    // their `parent` to NULL (lod/boneobj.c:126-127), which is what the port's
    // parent == -1 means.
    //
    // Bone_Init PREPENDS (`b->next = parent->child; parent->child = b;`), so
    // each parent's child list is in DESCENDING index order. The scan is done
    // once per parent index, exactly as the engine recurses.
    for (int i = 0; i < count; ++i) {
        EngineBone& b = state.bones[static_cast<std::size_t>(i)];
        b.index = i;
        b.flags = 0;
        b.parent = -1;
        b.child = -1;
        b.next = -1;
        b.blend = -1;
        b.matrix_count = 0;
        b.world = EngineQuat{};
        b.local = EngineQuat{};
        if (offsets != nullptr) {
            b.offset[0] = offsets[i * 3 + 0];
            b.offset[1] = offsets[i * 3 + 1];
            b.offset[2] = offsets[i * 3 + 2];
        }
        // lod/boneobj.c:168-169.
        b.length = static_cast<float>(
            X87Sqrt(static_cast<long double>(b.offset[0]) * b.offset[0] +
                    static_cast<long double>(b.offset[1]) * b.offset[1] +
                    static_cast<long double>(b.offset[2]) * b.offset[2]));
    }

    // Attach. BoneObj_Build recurses depth-first, but every bone is attached to
    // exactly one parent and Bone_Init prepends, so the resulting child lists
    // are the same as this flat scan: for each parent index in ascending order
    // (with -1 first, the engine's synthetic root), prepend every bone whose
    // PLST entry is that index.
    for (int parentIndex = -1; parentIndex < count; ++parentIndex) {
        for (int i = 0; i < count; ++i) {
            if (parents[i] != parentIndex) continue;
            EngineBone& b = state.bones[static_cast<std::size_t>(i)];
            b.parent = parentIndex;
            if (parentIndex >= 0) {
                EngineBone& p = state.bones[static_cast<std::size_t>(parentIndex)];
                b.next = p.child;
                p.child = i;
            }
        }
    }
}

int BoneObjAnimOrientationCB(EngineQuat& out, BonePoseState& state, int boneIndex, AnimInst& inst,
                             const EngineClip& clip) {
    // boneobj.c:1080-1173.
    const int id = state.bones[static_cast<std::size_t>(boneIndex)].index;
    // boneobj.c:1089-1090: the bone that OWNS the instance is marked animated,
    // and that mark is what `BoneObj_UpdateBone` uses to hand the blend node down
    // to its descendants (boneobj.c:520-521). Every other bone in the tree is
    // sampled through the inherited node but stays unmarked, which is why a whole
    // skeleton follows one animation played on track 0.
    if (inst.bone == id) state.bones[static_cast<std::size_t>(boneIndex)].flags |= kBoneAnimated;
    if (clip.rot == nullptr) return 0;
    const RotTrack& tr = clip.rot[id];
    const RotKey* keys = tr.keys;
    const int n = tr.count;

    if (n == 1) {
        out.x = keys[0].q[0];
        out.y = keys[0].q[1];
        out.z = keys[0].q[2];
        out.w = keys[0].q[3];
        out.unk10 = 0;
        return 0;
    }
    if (n <= 0 || keys == nullptr) {
        // The engine reads keys[-1] here; the port refuses.
        out.unk10 = 0;
        return 0;
    }

    const float tm = inst.time;
    int i = 0;
    float kt = 0.0f;
    int a = 0;
    int b = 0;
    const bool has = inst.has_cache;
    // boneobj.c:1102-1103: the cache is only used when the instance's time has
    // not gone BACKWARDS since the cache was written (`!(tm - cache->time < 0)`).
    if (has && !(tm - inst.cache_time < 0.0f)) i = inst.cache_bones[static_cast<std::size_t>(id)];

    for (; i < n; i++) {
        kt = keys[i].time;
        if (!(kt < tm)) break;
    }

    float r;
    if (tm == kt) {
        // boneobj.c:1109-1114: exactly on a key.
        out.x = keys[i].q[0];
        out.y = keys[i].q[1];
        out.z = keys[i].q[2];
        out.w = keys[i].q[3];
        out.unk10 = 0;
        a = i;
        goto done;
    }
    a = i - 1;
    if (i == n) {
        // boneobj.c:1116-1124: past the last key.
        if (clip.flags & kAnimDescLoops) {
            r = (tm - kt) / (clip.length - kt);
            b = 0;
            goto interp;
        }
        out.x = keys[n - 1].q[0];
        out.y = keys[n - 1].q[1];
        out.z = keys[n - 1].q[2];
        out.w = keys[n - 1].q[3];
        goto zero;
    }
    if (a < 0) {
        // boneobj.c:1125-1129: before the first key.
        out.x = keys[0].q[0];
        out.y = keys[0].q[1];
        out.z = keys[0].q[2];
        out.w = keys[0].q[3];
        out.unk10 = 0;
        goto done;
    }
    r = 1.0f - (kt - tm) / (kt - keys[i - 1].time);
    b = i;

interp:
    if (tr.slerp != nullptr) {
        const RotKey& ka = keys[a];
        const RotKey& kb = keys[b];
        long double angle;
        long double s0;
        long double s1;
        EngineQuat c;

        if (inst.smooth_anims) {
            EngineQuat q1;
            EngineQuat q2;

            // boneobj.c:1141-1147.
            angle = SlerpAngle(c, kb.q, tr.slerp[a], false);
            if (angle == 0.0f) {
                q1.x = ka.q[0];
                q1.y = ka.q[1];
                q1.z = ka.q[2];
                q1.w = ka.q[3];
                q1.unk10 = FloatBits(ka.time);   // memcpy of 20 bytes from q
            } else {
                SlerpWeights(angle, r, tr.slerp[a].invSin, s0, s1);
                SlerpKeys(q1, ka.q, c, s0, s1, FloatBits(ka.time));
            }
            // boneobj.c:1148-1154.
            angle = SlerpAngle(c, kb.b, tr.slerp2[a], true);
            if (angle == 0.0f) {
                q2.x = ka.a[0];
                q2.y = ka.a[1];
                q2.z = ka.a[2];
                q2.w = ka.a[3];
                q2.unk10 = FloatBits(ka.b[0]);   // memcpy of 20 bytes from a
            } else {
                SlerpWeights(angle, r, tr.slerp2[a].invSin, s0, s1);
                SlerpKeys(q2, ka.a, c, s0, s1, FloatBits(ka.b[0]));
            }
            // boneobj.c:1155: the Hermite weight `1 - ((1-r)*r + (1-r)*r)`.
            BoneObjSlerp(out, q1, q2, 1.0f - ((1.0f - r) * r + (1.0f - r) * r));
        } else {
            // boneobj.c:1157-1163.
            angle = SlerpAngle(c, kb.q, tr.slerp[a], false);
            if (angle == 0.0f) {
                out.x = ka.q[0];
                out.y = ka.q[1];
                out.z = ka.q[2];
                out.w = ka.q[3];
                out.unk10 = FloatBits(ka.time);
            } else {
                SlerpWeights(angle, r, tr.slerp[a].invSin, s0, s1);
                SlerpKeys(out, ka.q, c, s0, s1, FloatBits(ka.time));
            }
        }
    }
zero:
    out.unk10 = 0;
done:
    if (has) inst.cache_bones[static_cast<std::size_t>(id)] = a;
    return 0;
}

int BoneObjAnimPositionCB(const EngineClip& clip, AnimInst& inst, bool smooth) {
    // boneobj.c:788-868. `SamplePosTrack` is the same arithmetic; this wrapper
    // adds the instance state (the search's `inst->time`, the previous `pos` for
    // the delta, and the copy back into `inst->pos`).
    const PosTrack* track = clip.pos;
    float sampled[3] = {inst.pos[0], inst.pos[1], inst.pos[2]};
    float delta[3] = {0.0f, 0.0f, 0.0f};

    if (track == nullptr) return 0;
    if (!SamplePosTrack(*track, inst.time, smooth, inst.pos, sampled, delta)) return 0;
    inst.pos[0] = sampled[0];
    inst.pos[1] = sampled[1];
    inst.pos[2] = sampled[2];
    inst.delta[0] = delta[0];
    inst.delta[1] = delta[1];
    inst.delta[2] = delta[2];
    return 1;
}

int BoneObjAnimCollectEvents(std::vector<EngineBoneEvent>& out, int n, int boneIndex, const AnimInst& inst,
                             const EngineClip& clip, float from, float to) {
    // boneobj.c:944-980.
    if (clip.events == nullptr) return n;
    const EventTrack& et = *clip.events;
    const EventKey* k = et.keys;
    const int count = et.count;
    const bool back = to < from;
    const float lo = from < to ? from : to;
    const float hi = from > to ? from : to;

    for (int i = 0; i < count; ++i, ++k) {
        int add;

        // boneobj.c:962-963: the keys are time-ordered, so the walk stops at the
        // first key past the window.
        if (k->time > hi) break;
        if (lo == hi && k->time == lo)
            add = 1;
        else if (back)
            add = !(k->time < lo) && k->time < hi;
        else
            add = k->time > lo;
        if (add) {
            EngineBoneEvent ev;
            ev.bone = boneIndex;
            ev.id = k->id;
            ev.anim = inst.id;
            // boneobj.c:974 `memcpy(&ev[n].time, &k->time, 4 * sizeof(float));`
            // - FOUR floats from the key's time, so the event's time AND its
            // three position components all come from the key.
            ev.time = k->time;
            ev.pos[0] = k->pos[0];
            ev.pos[1] = k->pos[1];
            ev.pos[2] = k->pos[2];
            if (static_cast<std::size_t>(n) < out.size())
                out[static_cast<std::size_t>(n)] = ev;
            else
                out.push_back(ev);
            n++;
        }
    }
    return n;
}

int BoneObjAnimEventsCB(std::vector<EngineBoneEvent>& events, int n, BonePoseState& state, int boneIndex,
                        AnimInst& inst, const EngineClip& clip, bool smooth) {
    // boneobj.c:873-941, the full form.
    float old;
    float now;
    bool has = false;

    if (inst.has_cache) inst.cache_time = inst.time;
    inst.flags &= ~kBoneEventsDone;
    state.bones[static_cast<std::size_t>(boneIndex)].flags &= ~kBoneEventsDone;
    old = inst.time;
    now = inst.speed + inst.time;
    inst.time = now;
    if (clip.events != nullptr && clip.events->count > 0) {
        has = true;
        // boneobj.c:888-889: on the FIRST tick the window is empty, so the
        // event exactly at `old` is emitted.
        if ((inst.flags & kAnimFirst) && old != now)
            n = BoneObjAnimCollectEvents(events, n, boneIndex, inst, clip, old, old);
        n = BoneObjAnimCollectEvents(events, n, boneIndex, inst, clip, old, inst.time);
    }
    const float length = clip.length;
    const bool reverse = (inst.flags & kAnimReverse) != 0;

    if ((!(inst.time < length) && !reverse) || (!(inst.time > 0.0f) && reverse)) {
        inst.flags |= kBoneEventsDone;
        state.bones[static_cast<std::size_t>(boneIndex)].flags |= kBoneEventsDone;
        if (inst.flags & kAnimLoop) {
            float from;

            if (reverse) {
                inst.time = length + inst.time;
                from = length + 1.0f;
            } else {
                inst.time = inst.time - length;
                from = -1.0f;
            }
            if (has) n = BoneObjAnimCollectEvents(events, n, boneIndex, inst, clip, from, inst.time);
            if (state.bones[static_cast<std::size_t>(boneIndex)].index == 0 &&
                !(inst.flags & kAnimFirst)) {
                const PosTrack* tr = clip.pos;
                const float saved = inst.time;

                if (tr != nullptr && tr->keys != nullptr && tr->count > 0) {
                    const PosKey* k;
                    if (reverse) {
                        inst.time = 0.0f;
                        BoneObjAnimPositionCB(clip, inst, smooth);
                        k = &tr->keys[tr->count - 1];
                    } else {
                        inst.time = length;
                        BoneObjAnimPositionCB(clip, inst, smooth);
                        k = &tr->keys[0];
                    }
                    inst.pos[0] = k->pos[0] - inst.delta[0];
                    inst.pos[1] = k->pos[1] - inst.delta[1];
                    inst.pos[2] = k->pos[2] - inst.delta[2];
                }
                inst.flags &= ~kAnimFirst;
                inst.time = saved;
                return n;
            }
        } else {
            if (reverse) {
                inst.time = 0.0f;
                inst.flags &= ~kAnimFirst;
                return n;
            }
            inst.time = length;
        }
    }
    inst.flags &= ~kAnimFirst;
    return n;
}

void BoneObjSlerp(EngineQuat& out, const EngineQuat& a, const EngineQuat& b, float t) {
    // boneobj.c:658-702.
    int flip = 0;
    float angle;
    float invSin;
    EngineQuat c;
    long double s0;
    long double s1;

    if (!(t > kSlerpMinPose)) {
        out = b;
        return;
    }
    if (!(t < kSlerpMaxPose)) {
        out = a;
        return;
    }
    QuatSlerpPrepare(flip, angle, invSin, a, b);
    if (flip) {
        c.x = -b.x;
        c.y = -b.y;
        c.z = -b.z;
        c.w = -b.w;
        c.unk10 = b.unk10;
    } else {
        c = b;
    }
    float u = 1.0f - t;
    if (angle == 0.0f) {
        out = a;
        return;
    }
    if (angle < kSmallAnglePose) {
        s0 = 1.0f - u;
        s1 = u;
    } else {
        u = u * angle;
        s0 = X87Sin(angle - u) * invSin;
        s1 = X87Sin(u) * invSin;
    }
    out.x = static_cast<float>(s0 * a.x + s1 * c.x);
    out.y = static_cast<float>(s0 * a.y + s1 * c.y);
    out.z = static_cast<float>(s0 * a.z + s1 * c.z);
    out.w = static_cast<float>(s0 * a.w + s1 * c.w);
    out.unk10 = a.unk10 + c.unk10 + 1;
}

int BoneObjBlendOrientations(EngineQuat& out, int t0, const EngineQuat& q0, int t1, const EngineQuat& q1,
                             float w) {
    // boneobj.c:597-655.
    long double o[4];
    long double inv;

    if (t0 == -1) {
        if (t1 != -1) {
            out = q1;
            return t1;
        }
        return -1;
    }
    if (t1 == -1) {
        out = q0;
        return t0;
    }
    if (t0 == 0) {
        if (t1 == 0) {
            BoneObjSlerp(out, q0, q1, w);
            return 0;
        }
        // boneobj.c:617-631: an ABSOLUTE orientation combined with a relative
        // one. Note the term order and the term order inside inv_len.
        QuatMul(o, &q1.x, &q0.x, kQuatRoundC | kQuatRoundD | kQuatRoundE);
        out.x = static_cast<float>(o[0]);
        out.y = static_cast<float>(o[1]);
        out.z = static_cast<float>(o[2]);
        out.w = static_cast<float>(o[3]);
        out.unk10 = q0.unk10 + q1.unk10 + 1;
        if (out.unk10 > 1000) {
            out.unk10 = 0;
            inv = InvLen(((static_cast<long double>(out.x) * out.x + o[3] * o[3]) +
                          static_cast<long double>(out.z) * out.z) +
                         static_cast<long double>(out.y) * out.y);
            out.x = static_cast<float>(out.x * inv);
            out.y = static_cast<float>(out.y * inv);
            out.z = static_cast<float>(out.z * inv);
            out.w = static_cast<float>(o[3] * inv);
        }
        return 1;
    }
    if (t1 == 1) {
        BoneObjSlerp(out, q0, q1, w);
        return 1;
    }
    // boneobj.c:638-653: two absolute orientations.
    QuatMul(o, &q0.x, &q1.x, kQuatRoundC | kQuatRoundD | kQuatRoundE);
    out.x = static_cast<float>(o[0]);
    out.y = static_cast<float>(o[1]);
    out.z = static_cast<float>(o[2]);
    out.w = static_cast<float>(o[3]);
    out.unk10 = q0.unk10 + q1.unk10 + 1;
    if (out.unk10 > 1000) {
        out.unk10 = 0;
        inv = InvLen(((o[3] * o[3] + static_cast<long double>(out.z) * out.z) +
                      static_cast<long double>(out.y) * out.y) +
                     static_cast<long double>(out.x) * out.x);
        out.x = static_cast<float>(out.x * inv);
        out.y = static_cast<float>(out.y * inv);
        out.z = static_cast<float>(out.z * inv);
        out.w = static_cast<float>(o[3] * inv);
    }
    return 1;
}

int BoneObjGetBranchOrientation(EngineQuat& out, BonePoseState& state, int boneIndex, int type,
                                int sourceIndex, int nodeIndex) {
    // boneobj.c:704-716.
    if (type == kBranchSource) {
        if (sourceIndex < 0 || static_cast<std::size_t>(sourceIndex) >= state.sources.size()) return -1;
        // The engine's animation source always has a getOrientation
        // (BoneObj_NewAnimSource, boneobj.c:782-783), so this is
        // BoneObj_AnimOrientationCB. (The orientation source and the parent
        // source are the two kinds the port does not model; see the header.)
        BlendSourceAnim& source = state.sources[static_cast<std::size_t>(sourceIndex)];
        if (source.clip == nullptr) return -1;
        return BoneObjAnimOrientationCB(out, state, boneIndex, source.inst, *source.clip);
    }
    if (type == kBranchNode) {
        if (nodeIndex < 0) return -1;
        return BoneObjGetNodeOrientation(out, state, boneIndex, nodeIndex);
    }
    return -1;
}

int BoneObjGetNodeOrientation(EngineQuat& out, BonePoseState& state, int boneIndex, int nodeIndex) {
    // boneobj.c:531-585.
    EngineQuat qa;
    EngineQuat qb;
    int r0;
    int r1;
    int r;
    float w;

    if (nodeIndex < 0 || static_cast<std::size_t>(nodeIndex) >= state.nodes.size()) return -1;
    const BlendNodeState& node = state.nodes[static_cast<std::size_t>(nodeIndex)];
    r0 = BoneObjGetBranchOrientation(qa, state, boneIndex, node.branchType[0], node.branchSource[0],
                                     node.branchNode[0]);
    r1 = BoneObjGetBranchOrientation(qb, state, boneIndex, node.branchType[1], node.branchSource[1],
                                     node.branchNode[1]);
    // The engine's node weight/select functions belong to the fade machinery;
    // with the immediate Anim_Play path they are NULL, so w is 0 and the default
    // blend runs (boneobj.c:539-546).
    w = 0.0f;
    r = BoneObjBlendOrientations(out, r0, qa, r1, qb, w);
    if (r == 1) {
        // boneobj.c:547-582: convert an absolute orientation to parent-relative.
        const EngineQuat* p;
        float nq[4];
        long double o[4];

        if (state.bones[static_cast<std::size_t>(boneIndex)].parent >= 0) {
            p = &state.bones[static_cast<std::size_t>(
                     state.bones[static_cast<std::size_t>(boneIndex)].parent)]
                     .world;
        } else {
            if (!state.hasRoot) return -1;
            p = &state.rootQuat;
        }
        nq[0] = -p->x;
        nq[1] = -p->y;
        nq[2] = -p->z;
        nq[3] = p->w;
        QuatMul(o, nq, &out.x, kQuatRoundA | kQuatRoundC | kQuatRoundD | kQuatRoundE);
        out.x = static_cast<float>(o[0]);
        out.y = static_cast<float>(o[1]);
        out.z = static_cast<float>(o[2]);
        out.unk10 = out.unk10 + (p->unk10 + 1);
        out.w = static_cast<float>(o[3]);
        if (out.unk10 > 1000) {
            long double inv;

            out.unk10 = 0;
            inv = InvLen(((o[3] * o[3] + o[2] * o[2]) + static_cast<long double>(out.x) * out.x) +
                         static_cast<long double>(out.y) * out.y);
            out.x = static_cast<float>(out.x * inv);
            out.y = static_cast<float>(out.y * inv);
            out.z = static_cast<float>(o[2] * inv);
            out.w = static_cast<float>(o[3] * inv);
        }
        return 0;
    }
    return r;
}

bool BoneObjUpdateBone(BonePoseState& state, int boneIndex, const float* parentMatrix,
                       const EngineQuat& parentQuat, const float* parentPos, int inherited) {
    // boneobj.c:423-525.
    if (boneIndex < 0 || static_cast<std::size_t>(boneIndex) >= state.bones.size()) return false;
    EngineBone& b = state.bones[static_cast<std::size_t>(boneIndex)];
    const float* e = parentMatrix;
    const int eff = b.blend >= 0 ? b.blend : inherited;
    EngineQuat q;
    int r;

    b.flags &= ~(kBoneAnimated | kBoneOriented);

    // boneobj.c:434-439: the position comes from the REST offset, rotated by the
    // parent's matrix. x stays long double until the store; y and z are float.
    const long double x = (e[2] * b.offset[2] + e[1] * b.offset[1]) + b.offset[0] * e[0];
    const float y = (e[5] * b.offset[2] + e[3] * b.offset[0]) + e[4] * b.offset[1];
    const float z = (e[8] * b.offset[2] + e[6] * b.offset[0]) + e[7] * b.offset[1];
    b.pos[0] = static_cast<float>(x + parentPos[0]);
    b.pos[1] = y + parentPos[1];
    b.pos[2] = z + parentPos[2];

    // boneobj.c:440-446. A NULL node is an access violation in retail; the port
    // reports it.
    if (eff < 0) return false;
    r = BoneObjGetNodeOrientation(q, state, boneIndex, eff);
    if (r == -1) {
        const unsigned int saved = b.flags;

        if (inherited < 0) return false;
        r = BoneObjGetNodeOrientation(q, state, boneIndex, inherited);
        b.flags = saved;
    }

    if (r == 0) {
        // boneobj.c:447-467: the orientation is relative to the parent, so the
        // bone's world is parentWorld * local.
        long double o[4];

        b.local = q;
        QuatMul(o, &parentQuat.x, &q.x, kQuatRoundD);
        q.x = static_cast<float>(o[0]);
        q.y = static_cast<float>(o[1]);
        q.z = static_cast<float>(o[2]);
        q.w = static_cast<float>(o[3]);
        q.unk10 = q.unk10 + parentQuat.unk10 + 1;
        if (q.unk10 > 1000) {
            long double inv;

            q.unk10 = 0;
            inv = InvLen(((static_cast<long double>(q.x) * q.x + static_cast<long double>(q.w) * q.w) +
                          static_cast<long double>(q.z) * q.z) +
                         static_cast<long double>(q.y) * q.y);
            q.x = static_cast<float>(q.x * inv);
            q.y = static_cast<float>(q.y * inv);
            q.z = static_cast<float>(q.z * inv);
            q.w = static_cast<float>(q.w * inv);
        }
    } else {
        // boneobj.c:468-491: the orientation is absolute, so the bone's LOCAL is
        // the inverse of the parent times it, and the world is the value itself.
        const float n[4] = {-parentQuat.x, -parentQuat.y, -parentQuat.z, parentQuat.w};
        long double o[4];
        float r0;
        float r2;

        QuatMul(o, n, &q.x, kQuatRoundD | kQuatRoundE);
        r0 = static_cast<float>(o[0]);
        b.local.x = r0;
        b.local.y = static_cast<float>(o[1]);
        r2 = static_cast<float>(o[2]);
        b.local.z = r2;
        b.local.w = static_cast<float>(o[3]);
        b.local.unk10 = parentQuat.unk10 + q.unk10 + 1;
        if (b.local.unk10 > 1000) {
            long double inv;

            b.local.unk10 = 0;
            inv = InvLen(((o[3] * o[3] + static_cast<long double>(r2) * r2) +
                          static_cast<long double>(o[1]) * o[1]) +
                         static_cast<long double>(r0) * r0);
            b.local.x = static_cast<float>(r0 * inv);
            b.local.y = static_cast<float>(o[1] * inv);
            b.local.z = static_cast<float>(r2 * inv);
            b.local.w = static_cast<float>(o[3] * inv);
        }
    }

    b.matrix_count = q.unk10;
    b.world = q;
    {
        // boneobj.c:494-519. Note the mixed precision: xx, xy, zw and u stay
        // long double while xz, xw, yy, yz, yw and zz are FLOAT, so the nine
        // matrix entries are three different mixes.
        long double xx = static_cast<long double>(q.x) * q.x;
        long double xy = static_cast<long double>(q.x) * q.y;
        long double zw;
        float xz;
        float xw;
        float yy;
        float yz;
        float yw;
        float zz;
        long double u;

        xx = xx + xx;
        xy = xy + xy;
        xz = static_cast<float>(static_cast<long double>(q.x) * q.z * 2);
        xw = static_cast<float>(static_cast<long double>(q.x) * q.w * 2);
        yy = static_cast<float>(static_cast<long double>(q.y) * q.y * 2);
        yz = static_cast<float>(static_cast<long double>(q.z) * q.y * 2);
        yw = static_cast<float>(static_cast<long double>(q.w) * q.y * 2);
        zz = static_cast<float>(static_cast<long double>(q.z) * q.z * 2);
        zw = static_cast<long double>(q.w) * q.z;
        zw = zw + zw;
        b.matrix[0] = (1.0f - yy) - zz;
        b.matrix[1] = static_cast<float>(xy - zw);
        b.matrix[2] = yw + xz;
        b.matrix[3] = zw + xy;
        u = 1.0f - xx;
        b.matrix[4] = static_cast<float>(u - zz);
        b.matrix[5] = yz - xw;
        b.matrix[6] = xz - yw;
        b.matrix[7] = yz + xw;
        b.matrix[8] = static_cast<float>(u - yy);
    }

    // boneobj.c:520-521: only a bone that ended up ANIMATED hands its node down.
    int childInherited = inherited;
    if (b.flags & kBoneAnimated) childInherited = eff;

    for (int c = b.child; c >= 0; c = state.bones[static_cast<std::size_t>(c)].next) {
        if (!BoneObjUpdateBone(state, c, b.matrix, q, b.pos, childInherited)) return false;
    }
    return true;
}

bool BoneObjUpdate(BonePoseState& state, const float m[9], int m_count) {
    // boneobj.c:371-419.
    static const int kNext[3] = {1, 2, 0};
    float mm[9];
    int count = m_count;
    float pos[3] = {0.0f, 0.0f, 0.0f};
    EngineQuat q;
    float* qa = &q.x;

    if (state.bones.empty()) return false;
    for (int i = 0; i < 9; ++i) mm[i] = m[i];
    const float* e = mm;

    // boneobj.c:379-407: the trace-based matrix -> quaternion conversion.
    const long double t = (e[8] + e[4]) + e[0];
    if (t > 0.0f) {
        long double s = X87Sqrt(t + 1.0f);

        q.w = static_cast<float>(s * 0.5f);
        s = 0.5f / s;
        q.x = static_cast<float>(-((e[5] - e[7]) * s));
        q.y = static_cast<float>(-((e[6] - e[2]) * s));
        q.z = static_cast<float>(-((e[1] - e[3]) * s));
    } else {
        int i = 0;
        int j;
        int k;
        long double s;

        if (e[4] > e[0]) i = 1;
        if (e[8] > e[i * 4]) i = 2;
        j = kNext[i];
        k = kNext[j];
        s = X87Sqrt(((e[i * 4] - e[j * 4]) - e[k * 4]) + 1.0f);
        qa[i] = static_cast<float>(s * 0.5f);
        if (s != 0.0f) s = 0.5f / s;
        qa[j] = static_cast<float>((e[j * 3 + i] + e[i * 3 + j]) * s);
        qa[k] = static_cast<float>((e[k * 3 + i] + e[i * 3 + k]) * s);
        q.w = static_cast<float>(-((e[j * 3 + k] - e[k * 3 + j]) * s));
    }
    q.unk10 = count;

    // BoneObj_SetRoot(&mm, &q) - the file-static gRootMatrix/gRootQuat.
    for (int i = 0; i < 9; ++i) state.rootMatrix.m[i] = mm[i];
    state.rootMatrix.count = count;
    state.rootQuat = q;
    state.hasRoot = true;

    const bool ok = BoneObjUpdateBone(state, 0, mm, q, pos, -1);

    // boneobj.c:412-416: the root bone's own local orientation is discarded.
    state.bones[0].local.x = 0.0f;
    state.bones[0].local.y = 0.0f;
    state.bones[0].local.z = 0.0f;
    state.bones[0].local.w = 1.0f;
    state.bones[0].local.unk10 = 0;

    // BoneObj_SetRoot(NULL, NULL).
    state.hasRoot = false;
    return ok;
}

bool BoneObjGetRootMotion(const BonePoseState& state, float outPos[3], float outRot[3]) {
    // lod/boneobj.c:202-211 + BoneBlend_GetMotion 0x4D3020
    // (lod/boneobj.c:220-247). With a single animation source exactly one
    // branch answers, so `a && b` is false and the value is copied, not lerped.
    if (outPos != nullptr) {
        outPos[0] = 0.0f;
        outPos[1] = 0.0f;
        outPos[2] = 0.0f;
    }
    if (outRot != nullptr) {
        outRot[0] = 0.0f;
        outRot[1] = 0.0f;
        outRot[2] = 0.0f;
    }
    if (state.bones.empty()) return false;
    const EngineBone& root = state.bones[0];
    if (root.blend < 0 || static_cast<std::size_t>(root.blend) >= state.nodes.size()) return false;
    const BlendNodeState& node = state.nodes[static_cast<std::size_t>(root.blend)];
    if (node.branchType[0] != kBranchSource || node.branchSource[0] < 0) return false;
    const AnimInst& inst = state.sources[static_cast<std::size_t>(node.branchSource[0])].inst;
    // The source's getPosition writes pos AND rot; the animation position
    // callback only ever writes pos and leaves the rotation untouched, so the
    // rotation half stays as the caller's zeroes (boneobj.c:788-868 writes
    // `pos` and `delta` only).
    if (outPos != nullptr) {
        outPos[0] = inst.pos[0];
        outPos[1] = inst.pos[1];
        outPos[2] = inst.pos[2];
    }
    return true;
}

bool InitAnimInstFromClip(AnimInst& inst, const EngineClip& clip, int bone, int id, uint32_t flags,
                          float ticks_per_frame, int cache_bone_count, bool smooth) {
    // boneobj.c:988-1021. `BoneObj_NewAnimSource` has already dereferenced
    // `gAnimSets[skel].byId[id]` by this point (boneobj.c:778) and
    // `BoneObj_InitAnimInst` dereferences `desc->pos->keys[...]`, so a clip with
    // no position track is an access violation in retail. The port reports it.
    if (clip.pos == nullptr || clip.pos->keys == nullptr || clip.pos->count <= 0) return false;

    // boneobj.c:992-1005: the KeyCache, only when the set has caches.
    if (cache_bone_count > 0) {
        inst.cache_bones.assign(static_cast<std::size_t>(cache_bone_count), 0);
        inst.cache_time = 0.0f;
        inst.has_cache = true;
    } else {
        inst.cache_bones.clear();
        inst.cache_time = 0.0f;
        inst.has_cache = false;
    }
    inst.bone = bone;
    inst.id = id;
    inst.desc_index = id;
    inst.delta[0] = 0.0f;
    inst.delta[1] = 0.0f;
    inst.delta[2] = 0.0f;
    inst.flags = flags;
    inst.smooth_anims = smooth;

    // boneobj.c:1013-1021. Both branches are BIT copies in the original
    // (`memcpy(&inst->time, &desc->length, 4)` and
    // `memcpy(&inst->speed, &gTicksPerFrame, 4)`), so the reverse case starts
    // exactly AT the length and the first advance immediately satisfies
    // `!(time > 0)`.
    if (flags & kAnimReverse) {
        const PosKey& last = clip.pos->keys[clip.pos->count - 1];
        inst.pos[0] = last.pos[0];
        inst.pos[1] = last.pos[1];
        inst.pos[2] = last.pos[2];
        inst.speed = -ticks_per_frame;
        inst.time = clip.length;
    } else {
        const PosKey& first = clip.pos->keys[0];
        inst.pos[0] = first.pos[0];
        inst.pos[1] = first.pos[1];
        inst.pos[2] = first.pos[2];
        inst.time = 0.0f;
        inst.speed = ticks_per_frame;
    }
    return true;
}

int BoneObjGetEvents(std::vector<EngineBoneEvent>& events, BonePoseState& state, const EngineClip& clip,
                     bool smooth) {
    // boneobj.c:112-129. With a single source per bone the node walk is the
    // branch-0 source, so this is one AnimEventsCB call per blended bone.
    int n = 0;
    for (int i = 0; i < state.boneCount(); ++i) {
        EngineBone& b = state.bones[static_cast<std::size_t>(i)];

        if (b.blend < 0) continue;
        b.flags &= ~kBoneFading;
        const BlendNodeState& node = state.nodes[static_cast<std::size_t>(b.blend)];
        for (int br = 0; br < 2; ++br) {
            if (node.branchType[br] != kBranchSource) continue;
            const int si = node.branchSource[br];
            if (si < 0 || static_cast<std::size_t>(si) >= state.sources.size()) continue;
            n = BoneObjAnimEventsCB(events, n, state, i, state.sources[static_cast<std::size_t>(si)].inst,
                                    clip, smooth);
        }
    }
    return n;
}

bool BoneMeshSkin(const SkinTransformInput* vertices, int baseVertexCount, int extraInfluenceCount,
                  const BoneTransform* bones, int boneCount, bool rigid,
                  std::vector<SkinTransformOutput>& out) {
    // d3drender.c:226-313.
    if (baseVertexCount > 4000) return false;   // d3drender.c:234-238
    if (baseVertexCount < 0) return false;
    out.assign(static_cast<std::size_t>(baseVertexCount), SkinTransformOutput{});
    if (baseVertexCount == 0) return true;
    if (vertices == nullptr) return false;

    const auto boneAt = [&](int index) -> BoneTransform {
        if (bones == nullptr || index < 0 || index >= boneCount) return BoneTransform{};
        return bones[index];
    };

    if (!rigid) {
        // d3drender.c:254-274: the base pass. The position is the skinned one
        // TIMES the vertex weight (the engine does not blend it with anything),
        // and the normal is the rotated one.
        for (int i = 0; i < baseVertexCount; ++i) {
            const SkinTransformInput& v = vertices[i];
            const BoneTransform b = boneAt(v.bone);
            out[static_cast<std::size_t>(i)] =
                SkinTransformVertex(v, b, false, nullptr);
        }
        // d3drender.c:275-290: the extra influences, accumulated into
        // `out[v->target]`. The target's normal and UV are left alone.
        for (int i = 0; i < extraInfluenceCount; ++i) {
            const SkinTransformInput& v = vertices[baseVertexCount + i];
            if (v.target < 0 || v.target >= baseVertexCount) continue;
            const BoneTransform b = boneAt(v.bone);
            SkinTransformOutput& base = out[static_cast<std::size_t>(v.target)];
            SkinAccumulatePosition(v, b, base);
        }
        return true;
    }

    // d3drender.c:291-309: the rigid path writes the base vertices with NO
    // weight and never reads the extra influences.
    for (int i = 0; i < baseVertexCount; ++i) {
        const SkinTransformInput& v = vertices[i];
        const BoneTransform b = boneAt(v.bone);
        out[static_cast<std::size_t>(i)] = SkinTransformVertex(v, b, true, nullptr);
    }
    return true;
}

}  // namespace igi
