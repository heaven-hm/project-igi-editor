#include "runtime/smoke_sim.h"

#include <algorithm>
#include <cmath>

namespace igi {
namespace {

// `EQ_OR_NAN(a, b)` (fxobj/fxobj.h:62): true when the two are equal OR either
// is NaN. Used for Smoke_Changed's "did only the intensity change" test
// (smoke.c:347) and for the editor's own "did anything change" gate.
bool EqOrNan(float a, float b) { return !(a < b || a > b); }
bool EqOrNan(double a, double b) { return !(a < b || a > b); }

bool ParamsEqual(const SmokeParams& a, const SmokeParams& b) {
    for (int i = 0; i < 3; ++i) {
        if (!EqOrNan(a.origin[i], b.origin[i])) return false;
        if (!EqOrNan(a.colour[i], b.colour[i])) return false;
    }
    return EqOrNan(a.alpha, b.alpha) && EqOrNan(a.gamma, b.gamma) && a.count == b.count &&
           EqOrNan(a.radius, b.radius) && EqOrNan(a.spread, b.spread) &&
           EqOrNan(a.min_velocity, b.min_velocity) && EqOrNan(a.max_velocity, b.max_velocity) &&
           EqOrNan(a.life_time, b.life_time) && EqOrNan(a.fade_time, b.fade_time) &&
           a.fade_mode == b.fade_mode && a.sprite_index == b.sprite_index &&
           EqOrNan(a.particle_size, b.particle_size) &&
           EqOrNan(a.particle_size_delta, b.particle_size_delta) &&
           EqOrNan(a.min_rotation, b.min_rotation) && EqOrNan(a.max_rotation, b.max_rotation) &&
           EqOrNan(a.intensity, b.intensity) && EqOrNan(a.generate_factor, b.generate_factor) &&
           a.generate_expr == b.generate_expr && a.move_particles == b.move_particles;
}

}  // namespace

void SmokeSimulation::ResetRandomStreams() {
    // Smoke_Create 0x437B90: Random_Init(gSmokeRandom) (smoke.c:246).
    smoke_rng_.Reset();
    // Particle2D_Open 0x4FCF50: Random_Init then Random_Seed(0x12332153)
    // (particle.c:99-100).
    particle_rng_.Reset();
    particle_rng_.Seed(0x12332153u);
}

void SmokeSimulation::Clear() {
    emitters_.clear();
    ticks_ = 0;
    ResetRandomStreams();
}

void SmokeSimulation::RebuildGroup(SmokeEmitterState& e) {
    // Particle2D_New, as Smoke_Changed calls it (smoke.c:401-405):
    //   Particle2D_New(root, world, sprite, &zero, &scale, size, kLife, blend,
    //                  0, grow * kPerFrame, 0, count, 0)
    // The editor's group carries the fields the simulation and the draw need;
    // `root`, `world` and `sprite` have no counterpart here (no task tree, no
    // dyncube world, no .spr loader).
    e.group = SmokeParticleGroup{};
    e.group.exists = true;
    e.group.size = SmokeGroupSize(e.params);          // smoke.c:394
    SmokeGroupColour(e.params, e.group.colour);       // smoke.c:398-400
    e.group.gravity[0] = e.group.gravity[1] = e.group.gravity[2] = 0.0f;  // smoke.c:395
    e.group.grow = SmokeGroupGrow(e.params);          // smoke.c:403
    e.group.max_depth = kSmokeParticleLife;           // smoke.c:403 (kLife)
    e.group.flags = SmokeBlendFlags(e.params.fade_mode) | 0x40000u;  // particle.c:515
    e.group.capacity = std::max(0, e.params.count);
    e.group.frame_delay = 0;                          // smoke.c:403
    // Particle2D_New's `animated` is `sprite->map != NULL` (particle.c:519-520)
    // and Particle2D_Run takes its last frame from `map->cols - 1`
    // (particle.c:271-272). The columns come from the loaded .spr
    // (SetSpriteColumns); with no sprite loaded the map is absent and the frame
    // stays 0, which is the same outcome a 1-column map has.
    e.group.sprite_index = e.params.sprite_index;
    const int slot = (e.params.sprite_index >= 0 && e.params.sprite_index <= 2)
                         ? e.params.sprite_index
                         : 3;  // smoke.c:380-393's default arm
    const int cols = sprite_columns_[slot];
    e.group.last_frame = cols > 1 ? cols - 1 : 0;
    e.group.particles.clear();
    if (e.group.capacity > 0) e.group.particles.reserve(static_cast<std::size_t>(e.group.capacity));
}

void SmokeSimulation::SetEmitters(const std::vector<SmokeParams>& emitters) {
    std::vector<SmokeEmitterState> next(emitters.size());

    for (std::size_t i = 0; i < emitters.size(); ++i) {
        SmokeEmitterState& state = next[i];
        // An emitter that survives keeps its live particles; a NEW one starts
        // from Smoke_Create's defaults (smoke.c:212-247) with no group.
        if (i < emitters_.size()) state = emitters_[i];
        else state.last_level = 1.0f;  // Smoke_Create's default level (smoke.c:241)

        // The engine runs Smoke_Changed when a parameter CHANGES
        // (TaskType_SetHandler(gSmokeType, Param_GetChangedEvent(), Smoke_Changed),
        // smoke.c:161); an unchanged emitter must not be rebuilt, or the pool
        // would be dropped on every frame the editor re-publishes the level.
        if (i < emitters_.size() && ParamsEqual(state.params, emitters[i])) continue;

        state.params = emitters[i];
        state.origin[0] = emitters[i].origin[0];
        state.origin[1] = emitters[i].origin[1];
        state.origin[2] = emitters[i].origin[2];

        // ── Smoke_Changed 0x438000 (smoke.c:344-410) ────────────────────────
        state.pos[0] = state.origin[0];  // smoke.c:346 `t->pos = t->origin`
        state.pos[1] = state.origin[1];
        state.pos[2] = state.origin[2];
        // smoke.c:347 `if (t->ps && EQ_OR_NAN(t->level, t->lastLevel))` -- the
        // group is deleted (and rebuilt below) whenever the intensity did NOT
        // change, and KEPT when only the intensity changed.
        const bool level_unchanged = EqOrNan(state.params.intensity, state.last_level);
        if (state.group.exists && level_unchanged) {
            state.group = SmokeParticleGroup{};  // Task_Delete(t->ps)
        }
        state.moved = false;                     // smoke.c:351
        state.last_level = state.params.intensity;  // smoke.c:352
        if (!state.group.exists) RebuildGroup(state);

        // smoke.c:361-365: dir = column 2 of RotZ(yaw) * RotX(pitch).
        float dir[3];
        SmokeDirection(state.params.alpha, state.params.gamma, dir);
        state.dir[0] = dir[0];
        state.dir[1] = dir[1];
        state.dir[2] = dir[2];

        // smoke.c:407-408: VarString_Bind + Smoke_UpdateLocal. The local offset
        // stays zero because the editor's preview has no moving ancestor (see
        // the header's named divergences).
    }

    emitters_.swap(next);
}

void SmokeSimulation::SetSpriteColumns(const int cols[kSmokeSpriteCount]) {
    for (int i = 0; i < kSmokeSpriteCount; ++i) {
        sprite_columns_[i] = cols ? cols[i] : 0;
    }
    // Re-derive the groups already built so a reload of the pack (or a first
    // load that happened after SetEmitters) takes effect without waiting for a
    // parameter edit: the engine reads `t->sprite->map` every tick, so its
    // frame count is never stale (Particle2D_Run, particle.c:271-272).
    for (SmokeEmitterState& e : emitters_) {
        const int slot = (e.params.sprite_index >= 0 && e.params.sprite_index <= 2)
                             ? e.params.sprite_index
                             : 3;
        const int c = sprite_columns_[slot];
        e.group.last_frame = c > 1 ? c - 1 : 0;
        e.group.sprite_index = e.params.sprite_index;
    }
}

int SmokeSimulation::SpriteColumns(int slot) const {
    if (slot < 0 || slot >= kSmokeSpriteCount) return 0;
    return sprite_columns_[slot];
}

void SmokeSimulation::TranslateParticles(int emitter, const double delta[3]) {
    if (emitter < 0 || static_cast<std::size_t>(emitter) >= emitters_.size()) return;
    if (delta == nullptr) return;
    // Particle2D_Translate HOOK 0x4FE610, as Smoke_Run calls it (smoke.c:303-310).
    for (SmokeParticleState& p : emitters_[static_cast<std::size_t>(emitter)].group.particles) {
        for (int i = 0; i < 3; ++i) p.pos[i] += delta[i];
    }
}

void SmokeSimulation::Tick() {
    for (std::size_t index = 0; index < emitters_.size(); ++index) {
        SmokeEmitterState& e = emitters_[index];

        // ── Smoke_Run HOOK 0x437DB0 (smoke.c:298-339) ────────────────────────
        if (e.moved && e.params.move_particles) {
            // smoke.c:303-310. `enabled` is the authored "Move Particles", and
            // it gates ONLY this translation -- emission is never gated by it.
            const double delta[3] = {e.pos[0] - e.last_pos[0], e.pos[1] - e.last_pos[1],
                                     e.pos[2] - e.last_pos[2]};
            TranslateParticles(static_cast<int>(index), delta);
        }
        e.moved = true;
        e.last_pos[0] = e.pos[0];
        e.last_pos[1] = e.pos[1];
        e.last_pos[2] = e.pos[2];
        // smoke.c:313 CALL_CHILDREN(&t->task, 1): the editor's emitter has no
        // children; the particle group is stepped at the end of this tick (see
        // the header's "EMITTER BEFORE GROUP").
        // smoke.c:314-315: `if (VarString_HasExpr(&t->expr)) t->rate =
        // VarString_EvalReal(&t->expr, t);` -- not evaluated here (named
        // divergence), so `rate` stays at the authored initial value.

        // smoke.c:316-337: the accumulator and the emission loop.
        const int emitted = SmokeAdvanceAccumulator(e.params.generate_factor, e.acc);
        for (int k = 0; k < emitted; ++k) {
            // smoke.c:320-330, in the engine's draw order: angle, radius,
            // fraction from the emitter's stream, then the particle's own three
            // draws inside Particle2D_Emit (tilt, azimuth, speed) from
            // gParticle2DRandom.
            SmokeParticle emitted_particle;
            SmokeEmitParticles(e.params, e.pos, 1, smoke_rng_, &emitted_particle);

            float speed_a = 0.0f;
            float speed_b = 0.0f;
            SmokeEmitSpeedArguments(e.params, &speed_a, &speed_b);  // smoke.c:333
            // Smoke.dir is a Vec3f in the engine (smoke.c:36); the state keeps
            // the same three floats.
            const float axis[3] = {static_cast<float>(e.dir[0]), static_cast<float>(e.dir[1]),
                                   static_cast<float>(e.dir[2])};
            float vel[3];
            // The three draws must be SEQUENCED, not passed as arguments: C++
            // leaves argument evaluation order unspecified, and particle.c
            // consumes them in the order tilt (particle.c:610), azimuth (:611),
            // speed (:616).
            const long double tilt_draw = particle_rng_.Float();
            const long double azimuth_draw = particle_rng_.Float();
            const long double speed_draw = particle_rng_.Float();
            SmokeEmitVelocity(axis, e.params.spread, speed_a, speed_b, tilt_draw, azimuth_draw,
                              speed_draw, vel);

            // Particle2D_Add HOOK 0x4FE120 (particle.c:541-566): a full pool
            // silently drops the emission (`if (t->count >= t->capacity)
            // return;`, particle.c:546).
            if (static_cast<int>(e.group.particles.size()) >= e.group.capacity) continue;
            SmokeParticleState p;
            for (int i = 0; i < 3; ++i) p.pos[i] = emitted_particle.pos[i];
            for (int i = 0; i < 3; ++i) p.vel[i] = vel[i];
            p.spin = emitted_particle.spin;    // the `spin` argument (smoke.c:333)
            p.level = emitted_particle.level;  // the `level` argument, == intensity
            p.scale = emitted_particle.scale;  // the `scale` argument, always kOneF
            p.angle = 0.0f;                    // the `angle` argument is 0 (smoke.c:333)
            p.delay = emitted_particle.delay_frames;
            // particle.c:563 `p->fade = frames ? kOne.f / (long double)frames : kOne.f`,
            // stored into the float field.
            p.fade = emitted_particle.fade_frames
                         ? static_cast<float>(1.0L / static_cast<long double>(emitted_particle.fade_frames))
                         : 1.0f;
            p.frame = 0;
            p.frame_timer = 0;
            e.group.particles.push_back(p);
        }

        // ── Particle2D_Run HOOK 0x4FD660 (particle.c:264-305) ────────────────
        // `t->autoDelete` is off (Particle2D_SetAutoDelete(t->ps, 0),
        // smoke.c:405), so an empty group stays alive.
        SmokeParticleGroup& g = e.group;
        if (!g.exists) continue;
        std::size_t i = 0;
        while (i < g.particles.size()) {
            if (SmokeParticleTick(g.particles[i], g.gravity, g.grow, g.frame_delay, g.last_frame)) {
                ++i;
            } else {
                // Particle2D_Remove (particle.c:422-433): the last particle is
                // swapped into the hole and the count drops; the engine's loop
                // does not advance `i` after a removal, so the swapped-in
                // particle is stepped in the same pass.
                g.particles[i] = g.particles.back();
                g.particles.pop_back();
            }
        }
    }
    ++ticks_;
}

int SmokeSimulation::EmittersWithExpression() const {
    int count = 0;
    for (const SmokeEmitterState& e : emitters_) {
        if (!e.params.generate_expr.empty()) ++count;
    }
    return count;
}

int SmokeSimulation::ParticleCount(std::size_t emitter) const {
    if (emitter >= emitters_.size()) return 0;
    return static_cast<int>(emitters_[emitter].group.particles.size());
}

int SmokeSimulation::TotalParticles() const {
    int total = 0;
    for (const SmokeEmitterState& e : emitters_) total += static_cast<int>(e.group.particles.size());
    return total;
}

}  // namespace igi
