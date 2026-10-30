// smoke_sim.h - the retail "Smoke" emitter RUNNING: the per-tick state machine
// of Smoke_Run plus the Particle2D group it drives (issue #43).
//
// smoke.h is the static half (the authored table, the parameter parsing and the
// per-particle derivations). This module is the live half: it owns one emitter
// per authored "Smoke" task and, for each, the fixed-capacity Particle2D pool
// that Smoke_Changed builds. It is pure logic -- no GL, no level, no Windows --
// so the host lane can step it and assert on it.
//
// The two IGI-RE functions it is a transcription of:
//
//   Smoke_Run         HOOK 0x437DB0  src/fxobj/smoke.c:298-339
//   Particle2D_Run    HOOK 0x4FD660  src/graphtask/particle.c:264-305
//
// with Particle2D_New (HOOK 0x4FDF00, particle.c:497-538), Particle2D_Add
// (HOOK 0x4FE120, particle.c:541-566), Particle2D_Emit (HOOK 0x4FE210,
// particle.c:569-634), Particle2D_Remove (HOOK 0x4FDD30, particle.c:422-433)
// and Smoke_Changed (HOOK 0x438000, smoke.c:344-410) supplying the arguments.
//
// THE TICK. The engine steps a task once per engine frame and its loop has no
// elapsed time at all (src/win/appmain.c:224-227), so this class steps exactly
// ONE engine frame per Tick() call. The editor's renderer calls Tick() once per
// rendered frame, which is the same rule the weather particles follow
// (renderer_rain.cpp, RainEffect_Run HOOK 0x436990, fxobj/rain.c:98-105) and the
// one commit f4204d55 pinned for the animation clock: a long editor frame does
// NOT buy extra simulation steps.
//
// EMITTER BEFORE GROUP. Within one tick the engine walks the level's task tree:
// the Smoke task emits from wherever the level placed it, and the Particle2D
// group is a child of the Dynamic root -- Smoke_Changed calls
// Particle2D_New(Level_GetDynamic(), ...) (smoke.c:401-404) and Task_New appends
// with List_AddTail (src/task/task.c:142-151). In the shipped data the Smoke
// tasks are nested inside the Dynamic subtree (LEVEL1/objects.qsc:546 opens
// "Dynamic", the first Smoke is at :903), so the emitter is walked before the
// group that was appended to the Dynamic root while that same Smoke was being
// created. Tick() therefore emits first and then integrates: a particle moves
// on its birth tick. This is an argument from the tree and the shipped file, not
// a runtime trace -- the relative order of two sibling subtrees is a property of
// the level, so it is stated here rather than presented as proven.
//
// NAMED DIVERGENCES (never silently fixed):
//   * The two random streams are per-simulation, not process-global: the engine
//     shares gSmokeRandom across every Smoke task and gParticle2DRandom across
//     every particle group (smoke.c:246, particle.c:99-100). One simulation
//     object with its own pair reproduces a single emitter exactly and keeps two
//     emitters' previews independent; with several emitters the engine's shared
//     stream would interleave their draws.
//   * "Generate factor" (the VarString expression, smoke.c:314-315) is not
//     evaluated: the editor's preview has no script context, so the authored
//     "Initial generate factor value" is used for every tick. Emitters whose
//     expression is non-empty are reported by EmittersWithExpression().
//   * The emitter does not follow a moving ancestor: Smoke_Move (HOOK 0x437AD0,
//     smoke.c:197-209) and Smoke_UpdateLocal (HOOK 0x438240, smoke.c:413-439)
//     need a RigidDynCubeObj parent with a live matrix, which the editor's
//     placement preview does not have. The emitter stays at its authored
//     Position.
//   * `Particle2D_Translate` (HOOK 0x4FE610, smoke.c:309) is applied, because it
//     only needs the emitter's own delta: "Move Particles" moves the already
//     emitted particles with the emitter.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "runtime/smoke.h"
#include "runtime/smoke_sprite.h"

namespace igi {

// The Particle2D group Smoke_Changed builds (smoke.c:353-405): one
// Particle2D_New with the emitter's size, colour, grow, blend flags and count.
struct SmokeParticleGroup {
    bool exists = false;
    float size = 0.0f;                 // Particle2D_New's size[0] == size[1]
    float colour[3] = {0.0f, 0.0f, 0.0f};
    float gravity[3] = {0.0f, 0.0f, 0.0f};  // smoke.c:395 passes `zero`
    float grow = 0.0f;
    float max_depth = kSmokeParticleLife;
    std::uint32_t flags = 0;           // SmokeBlendFlags | 0x40000
    int capacity = 0;                  // == the authored "Number of Particles"
    int frame_delay = 0;               // smoke.c:403 passes 0
    int last_frame = 0;                // Particle2D_Run's `last` == the sprite map's cols - 1
    int sprite_index = 3;              // Smoke.sprite -> which of the four .spr
    // The engine's pool: `list[0..count-1]`, and Particle2D_Remove swaps the
    // last particle into the hole (particle.c:422-433). Same order, same swap.
    std::vector<SmokeParticleState> particles;
};

// One live Smoke task: Smoke's own fields plus its group.
struct SmokeEmitterState {
    SmokeParams params;
    double origin[3] = {0.0, 0.0, 0.0};  // Smoke.origin (ParamTable slot 0)
    double pos[3] = {0.0, 0.0, 0.0};     // Smoke.pos
    double last_pos[3] = {0.0, 0.0, 0.0};// Smoke.lastPos
    bool moved = false;                  // Smoke.moved
    float acc = 0.0f;                    // Smoke.acc, a FLOAT
    double dir[3] = {0.0, 0.0, 1.0};     // Smoke.dir, from Smoke_Changed
    float last_level = 1.0f;             // Smoke.lastLevel
    bool registered = false;             // Smoke.registered (vGenerateFactor)
    SmokeParticleGroup group;
};

// The whole level's smoke: one emitter per authored "Smoke" task.
class SmokeSimulation {
public:
    // A fresh simulation starts from the engine's two seeded streams:
    // Random_Init for gSmokeRandom (smoke.c:246) and Random_Init +
    // Random_Seed(0x12332153) for gParticle2DRandom (particle.c:99-100).
    SmokeSimulation() { ResetRandomStreams(); }

    // Replaces the emitter list, matching by index and applying Smoke_Changed's
    // own rebuild rule (smoke.c:344-409): the group is rebuilt (and its
    // particles dropped) when the intensity is UNCHANGED, and kept when only the
    // intensity changed. An emitter whose "Number of Particles" changed gets a
    // new pool because that is a rebuild.
    void SetEmitters(const std::vector<SmokeParams>& emitters);
    void Clear();

    // The char-map column count of each of the four sprites Smoke_Init interns
    // (smoke.c:189-192), indexed the way Smoke_Changed picks them (smoke.c:
    // 380-393): 0 fireball.spr, 1 blood1.spr, 2 smoke2.spr, 3 smoke1.spr.
    //
    // Particle2D_New sets `animated` when the sprite has a char map at all
    // (particle.c:519-520) and Particle2D_Run takes `last = map->cols - 1`
    // (particle.c:271-272), so the column count is ALL the simulation needs to
    // advance a particle's FRAME exactly as the engine does: one frame per tick
    // for a 7-column map (blood1.spr, frameDelay is 0 for Smoke) and never for a
    // 1-column one (smoke1/smoke2/fireball, measured in the shipped pack).
    // `cols <= 1` (including 0 = no sprite loaded at all) leaves the frame at 0,
    // which is what the engine does in both of those cases. The group stores
    // `cols - 1`, i.e. Particle2D_Run's own `last` (see smoke.h's
    // SmokeParticleTick), so a 7-column sprite's particles reach frame 6.
    // Takes effect immediately: the engine reads the sprite's map every tick.
    void SetSpriteColumns(const int cols[kSmokeSpriteCount]);

    // The column count last set for a slot (0 when none).
    int SpriteColumns(int slot) const;

    // One engine tick for every emitter: Smoke_Run then the group's
    // Particle2D_Run (see "EMITTER BEFORE GROUP" above).
    void Tick();

    // Smoke_Run's first step, exposed for tests and for a paused preview:
    // Particle2D_Translate by the emitter's delta, gated on `moved && enabled`
    // (smoke.c:303-312). Tick() calls it; nothing else does.
    void TranslateParticles(int emitter, const double delta[3]);

    std::size_t EmitterCount() const { return emitters_.size(); }
    const SmokeEmitterState& Emitter(std::size_t index) const { return emitters_[index]; }
    std::vector<SmokeEmitterState>& MutableEmitters() { return emitters_; }

    // Emitters whose authored "Generate factor" expression is non-empty, i.e.
    // the ones whose rate this port cannot follow (see the header comment).
    int EmittersWithExpression() const;

    int ParticleCount(std::size_t emitter) const;
    int TotalParticles() const;

    // How many ticks this simulation has stepped. One per Tick() call: the
    // engine's own clock, not wall time.
    std::uint64_t TickCount() const { return ticks_; }

    // The emitter's random source: Random_Init(gSmokeRandom) at Smoke_Create
    // (smoke.c:246). Reset() re-runs Random_Init; Seed() is Random_Seed.
    SmokeRandom& SmokeRng() { return smoke_rng_; }
    // The particle group's source: Random_Init + Random_Seed(0x12332153) once in
    // Particle2D_Open (particle.c:99-100).
    SmokeRandom& ParticleRng() { return particle_rng_; }

    // Particle2D_Open's seeding, i.e. the state a fresh level starts from.
    void ResetRandomStreams();

private:
    // Smoke_Changed's rebuild (smoke.c:353-406).
    void RebuildGroup(SmokeEmitterState& emitter);

    std::vector<SmokeEmitterState> emitters_;
    SmokeRandom smoke_rng_;
    SmokeRandom particle_rng_;
    int sprite_columns_[kSmokeSpriteCount] = {0, 0, 0, 0};
    std::uint64_t ticks_ = 0;
};

}  // namespace igi
