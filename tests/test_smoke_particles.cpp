// test_smoke_particles.cpp - the retail "Smoke" emitter RUNNING (issue #43).
//
// test_smoke_rules.cpp pins the static half of the emitter: the authored
// parameter table, the parsing, the accumulator and the per-particle
// derivations. This suite pins the LIVE half: what the emitter actually does
// once per engine tick, i.e. Smoke_Run 0x437DB0 plus the Particle2D group it
// drives (Particle2D_New 0x4FDF00, Particle2D_Add 0x4FE120, Particle2D_Emit
// 0x4FE210, Particle2D_Run 0x4FD660, Particle2D_Remove 0x4FDD30).
//
// Oracle (IGI-RE, read-only) for every number here:
//   src/fxobj/smoke.c:298-339       Smoke_Run: the accumulator and the emission
//   src/fxobj/smoke.c:344-410       Smoke_Changed: dir, the group's arguments,
//                                   the rebuild-unless-only-level-changed rule
//   src/graphtask/particle.c:541-566  Particle2D_Add: pos/vel/level/fade/delay
//   src/graphtask/particle.c:569-634  Particle2D_Emit: the cone, the speed
//   src/graphtask/particle.c:264-305  Particle2D_Run: move, spin, grow, fade
//   src/graphtask/particle.c:422-433  Particle2D_Remove: swap with the last
//   src/math/random.c:5-40          Random_Init/Next/Float/Seed
//
// The golden velocity below was derived INDEPENDENTLY of this port: a small
// numpy script re-implemented the oracle's arithmetic (float32 stores at the
// same points, long double intermediates) straight from particle.c and
// smoke.c, and produced the numbers quoted at their use sites. It is not a
// recording of this implementation's output.

#include "../source/runtime/smoke.h"
#include "../source/runtime/smoke_sim.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

using igi::SmokeEmitterState;
using igi::SmokeParams;
using igi::SmokeSimulation;
using igi::SmokeParticleState;

constexpr double kPi = 3.14159265358979323846;

// The authored parameters of one emitter, at Smoke_Create's defaults
// (smoke.c:212-247) with the fields a test cares about overridden.
SmokeParams Params() {
    SmokeParams params;
    params.origin[0] = 0.0;
    params.origin[1] = 0.0;
    params.origin[2] = 0.0;
    return params;
}

// A long-lived emitter: "Life Time" 4 s and "Fade Time" 4 s are the top of both
// ranges (gSmokeFadeRange, smoke.c:137-138), so nothing dies inside the short
// runs the spawn-rate tests use.
SmokeParams LongLivedParams(double rate, int count) {
    SmokeParams params = Params();
    params.count = count;
    params.life_time = 4.0f;   // -> delay 120 ticks (SmokeDelayFrames)
    params.fade_time = 4.0f;   // -> 120 fade ticks at intensity 1
    params.intensity = 1.0f;
    params.generate_factor = rate;
    params.radius = 0.0f;
    params.spread = 0.0f;
    return params;
}

int TickN(SmokeSimulation& sim, int ticks) {
    for (int i = 0; i < ticks; ++i) sim.Tick();
    return sim.ParticleCount(0);
}

// ---------------------------------------------------------------------------
// The spawn rate: Smoke_Run's accumulator, at the simulation level
// ---------------------------------------------------------------------------

// smoke.c:316-337. `acc = rate + acc; while (acc > kRate) { emit; acc -= kRate }`
// with kRate == 30 (smoke.c:96) and `acc` a FLOAT field that Smoke_Create starts
// at 0 (smoke.c:228). The first tick of a rate of exactly 30 therefore emits
// NOTHING (30 is not > 30) and every later tick emits the carried 30 plus 30.
TEST(SmokeParticles, SpawnRateIsRateOverThirtyWithTheEnginesFirstTickGap) {
    SmokeSimulation sim;
    sim.SetEmitters({LongLivedParams(30.0, 255)});
    // Tick 0: acc = 30 + 0 = 30, and 30 > 30 is false.
    sim.Tick();
    EXPECT_EQ(sim.ParticleCount(0), 0) << "rate 30, tick 0: 30 > 30 is false (smoke.c:319)";
    // Tick 1: acc = 30 + 30 = 60 -> one particle, acc = 30.
    sim.Tick();
    EXPECT_EQ(sim.ParticleCount(0), 1);
    EXPECT_EQ(TickN(sim, 8), 9) << "one particle per tick from tick 1 on";
    EXPECT_EQ(TickN(sim, 90), 99) << "100 ticks, 99 emissions";
}

// rate 150 (the top of gSmokeRateRange, smoke.c:134): 150/30 = 5 per tick, but
// the carried remainder makes the first tick emit 4 (acc = 150 > 30 -> 120, 90,
// 60, 30; the fifth subtraction would leave 0 which is not > 30).
TEST(SmokeParticles, SpawnRateOneHundredFiftyEmitsFourThenFive) {
    SmokeSimulation sim;
    sim.SetEmitters({LongLivedParams(150.0, 255)});
    sim.Tick();
    EXPECT_EQ(sim.ParticleCount(0), 4);
    sim.Tick();
    EXPECT_EQ(sim.ParticleCount(0), 9);
    sim.Tick();
    EXPECT_EQ(sim.ParticleCount(0), 14);
    // 29 more ticks: 5 each.
    EXPECT_EQ(TickN(sim, 29), 14 + 29 * 5);
}

// A rate below 30 emits nothing until the carried remainder crosses 30: 29
// ticks of nothing, then one particle every 30 ticks (smoke.c:319-336).
TEST(SmokeParticles, SpawnRateOneWaitsForTheCarry) {
    SmokeSimulation sim;
    sim.SetEmitters({LongLivedParams(1.0, 255)});
    EXPECT_EQ(TickN(sim, 29), 0) << "29 ticks of rate 1 leave acc at 29";
    sim.Tick();  // acc = 30, still not > 30
    EXPECT_EQ(sim.ParticleCount(0), 0) << "the 30th tick lands exactly on 30";
    sim.Tick();  // acc = 31 -> one particle, acc = 1
    EXPECT_EQ(sim.ParticleCount(0), 1);
}

// The pool is `count` (Particle2D_New's capacity, smoke.c:404) and
// Particle2D_Add drops an emission when it is full (particle.c:546). Nothing
// ever exceeds it.
TEST(SmokeParticles, PoolIsCappedAtTheAuthoredParticleCount) {
    SmokeSimulation sim;
    sim.SetEmitters({LongLivedParams(150.0, 10)});
    EXPECT_EQ(TickN(sim, 5), 10) << "10 emissions in 2 ticks, then the pool is full";
    EXPECT_EQ(TickN(sim, 50), 10) << "55 ticks of 5/tick cannot grow a 10-slot pool";
    EXPECT_EQ(sim.Emitter(0).group.capacity, 10);
}

// ---------------------------------------------------------------------------
// Lifetime: Particle2D_Run's delay/fade pair
// ---------------------------------------------------------------------------

// smoke.c:331-332 -> particle.c:541-566. "Life Time" 0.5 s is 15 delay ticks,
// "Fade Time" 1.0 s at intensity 1 is 30 fade ticks, so a particle is stepped
// 45 times and removed on the 45th (particle.c:293-304). One particle is born
// per tick, so the steady-state pool is 44: the particle born on tick b is
// stepped on ticks b..b+44 and gone at the end of b+44.
TEST(SmokeParticles, LifetimeIsDelayPlusFadeTicksAndThePoolSettlesAtOneLess) {
    SmokeSimulation sim;
    SmokeParams params = Params();
    params.count = 255;
    params.life_time = 0.5f;   // delay = round(0.5 * 30) = 15
    params.fade_time = 1.0f;   // frames = round(1.0 * 30 / 1) = 30
    params.intensity = 1.0f;
    params.generate_factor = 30.0;
    params.radius = 0.0f;
    params.spread = 0.0f;
    sim.SetEmitters({params});

    EXPECT_EQ(TickN(sim, 45), 44) << "the first particle is removed during tick 45";
    EXPECT_EQ(TickN(sim, 5), 44) << "steady state";
    EXPECT_EQ(TickN(sim, 200), 44) << "still steady: 45 ticks of life, 1 born per tick";
    EXPECT_EQ(igi::SmokeDelayFrames(params), 15);
    EXPECT_EQ(igi::SmokeFadeFrames(params), 30);
}

// The fade is a per-tick SUBTRACTION of a float (particle.c:297, :563), so the
// lifetime is not always `frames`: 1 - 30*(1/30) rounds to exactly 0 and the
// particle dies on the 30th fade tick, while 1 - 60*(1/60) leaves a positive
// residue and it survives a 61st. Both numbers are the engine's arithmetic.
TEST(SmokeParticles, FadeIsAFloatSubtractionSoSixtyFramesLastSixtyOneTicks) {
    SmokeSimulation sim;
    SmokeParams params = Params();
    params.count = 255;
    params.life_time = 0.0f;   // no delay: the fade starts on the birth tick
    params.fade_time = 2.0f;   // frames = round(2.0 * 30 / 1) = 60
    params.intensity = 1.0f;
    params.generate_factor = 30.0;
    params.radius = 0.0f;
    params.spread = 0.0f;
    sim.SetEmitters({params});
    ASSERT_EQ(igi::SmokeFadeFrames(params), 60);

    // One particle per tick, 61 ticks of life -> 60 alive at the end of a tick.
    EXPECT_EQ(TickN(sim, 61), 60) << "the particle born on tick 1 dies during tick 61";
    EXPECT_EQ(TickN(sim, 200), 60) << "steady state is 60, not 59";
}

// ---------------------------------------------------------------------------
// Velocity and the emission cone (Particle2D_Emit)
// ---------------------------------------------------------------------------

// The first particle of a fresh simulation, derived independently from
// particle.c + smoke.c (see the file header): emitter at the origin, axis +Z
// (pitch == yaw == 0), "Radius" 2 m, "Maximum Random Angle" 0x1.921fb6p-1
// (pi/4 -- Smoke_Create's default, smoke.c:226), "Minimum Velocity" 4.5,
// "Maximum Velocity" 6.0, "Number of Particles" 100.
//
//   smoke stream draws : angle 5.29434299, radius 2709.875 units, fraction .451980591
//   particle stream    : tilt 0.138837248, az -0.0564696677, speed fraction -> 759.253784
//   => birth offset     (1489.50269, -2263.80298), velocity (10.762025, 190.378006, 734.919495)
//   => after one tick   (1500.264715, -2073.424974)
//
// The tolerance is for libm vs the x87 fsin/fcos the engine uses (the named
// divergence in smoke.h); the values agree to ~1e-7 relative.
TEST(SmokeParticles, FirstParticleMatchesAnIndependentDerivationOfTheOracle) {
    SmokeSimulation sim;
    SmokeParams params = Params();
    params.count = 100;
    params.radius = 2.0f;
    params.spread = 0x1.921fb6p-1f;  // pi/4, the authored "Maximum Random Angle"
    params.min_velocity = 4.5f;      // -> Particle2D_Emit's `a` (smoke.c:333)
    params.max_velocity = 6.0f;      // -> Particle2D_Emit's `b`
    params.life_time = 4.0f;
    params.fade_time = 4.0f;
    params.intensity = 1.0f;
    params.generate_factor = 30.0;
    sim.SetEmitters({params});

    sim.Tick();  // tick 0: rate 30 emits nothing
    ASSERT_EQ(sim.ParticleCount(0), 0);
    sim.Tick();  // tick 1: the first particle, emitted and then stepped once
    ASSERT_EQ(sim.ParticleCount(0), 1);

    const SmokeParticleState& p = sim.Emitter(0).group.particles[0];
    EXPECT_NEAR(p.vel[0], 10.762024879455566, 0.001);
    EXPECT_NEAR(p.vel[1], 190.3780059814453, 0.001);
    EXPECT_NEAR(p.vel[2], 734.9194946289062, 0.001);
    // The particle has already been stepped once this tick (the group runs
    // after the emitter: smoke_sim.h, "EMITTER BEFORE GROUP").
    EXPECT_NEAR(p.pos[0], 1489.50269 + 10.762024879455566, 0.01);
    EXPECT_NEAR(p.pos[1], -2263.80298 + 190.3780059814453, 0.01);
    EXPECT_NEAR(p.pos[2], 0.0 + 734.9194946289062, 0.01)
        << "the emission disc is in the emitter's XY plane";
    // The birth tick's own Particle2D_Run step has already run: the delay is
    // decremented before it is tested (particle.c:293-295) and the scale has
    // already grown by the group's `grow` (particle.c:287).
    EXPECT_EQ(p.delay, 119) << "round(4.0 * 30) = 120, minus the birth tick's step";
    EXPECT_NEAR(p.scale, 1.0f + igi::SmokeGroupGrow(params), 1e-6f)
        << "Particle2D_Emit's `scale` argument is kOneF (smoke.c:333) plus one grow step";
}

// The speed is drawn uniformly between the two authored velocities, each scaled
// by kSpeed == 136 (smoke.c:98, :333), so every particle's speed lies inside
// [min * 136, max * 136] -- and the reversal of the two authored names
// ("Minimum Velocity" -> speedMax -> the callee's `a`) does not change that.
TEST(SmokeParticles, EverySpeedIsInsideTheAuthoredRange) {
    SmokeSimulation sim;
    SmokeParams params = LongLivedParams(150.0, 255);
    params.min_velocity = 1.0f;
    params.max_velocity = 3.0f;
    params.spread = 0.0f;
    params.radius = 0.0f;
    sim.SetEmitters({params});
    TickN(sim, 40);

    ASSERT_GT(sim.ParticleCount(0), 100);
    const float low = 1.0f * igi::kSmokeSpeedScale;
    const float high = 3.0f * igi::kSmokeSpeedScale;
    for (const SmokeParticleState& p : sim.Emitter(0).group.particles) {
        const double speed = std::sqrt(double(p.vel[0]) * p.vel[0] + double(p.vel[1]) * p.vel[1] +
                                       double(p.vel[2]) * p.vel[2]);
        EXPECT_GE(speed, low - 1e-3);
        EXPECT_LE(speed, high + 1e-3);
    }
}

// The cone's half-angle is spread/2, not spread: `ax = spread * 0.5 - tilt`
// with `tilt` in [0, spread) (particle.c:610-612), and `ax` is the angle
// between the emitted direction and the axis. Over many particles the maximum
// deviation must approach spread/2 and never exceed it.
//
// The authored "Maximum Random Angle" of 0x1.921fb6p-1 is pi/4 (Smoke_Create's
// default, smoke.c:226 -- NOT pi/2), so the cone opens to pi/8.
TEST(SmokeParticles, ConeHalfAngleIsHalfTheAuthoredMaximumRandomAngle) {
    SmokeSimulation sim;
    SmokeParams params = LongLivedParams(150.0, 255);
    params.spread = 0x1.921fb6p-1f;  // pi/4 -> a pi/8 half-angle
    params.radius = 0.0f;
    sim.SetEmitters({params});
    TickN(sim, 60);
    ASSERT_GT(sim.ParticleCount(0), 200);

    double worst = 0.0;
    for (const SmokeParticleState& p : sim.Emitter(0).group.particles) {
        const double n = std::sqrt(double(p.vel[0]) * p.vel[0] + double(p.vel[1]) * p.vel[1] +
                                   double(p.vel[2]) * p.vel[2]);
        ASSERT_GT(n, 0.0);
        // The axis is +Z here, so the deviation is acos(vel.z / |vel|).
        const double deviation = std::acos(std::max(-1.0, std::min(1.0, double(p.vel[2]) / n)));
        EXPECT_LE(deviation, kPi / 8.0 + 1e-6) << "never past spread/2";
        worst = std::max(worst, deviation);
    }
    EXPECT_GT(worst, kPi / 8.0 - 0.01) << "and the cone is actually filled to spread/2";
}

// spread == 0 collapses the cone onto the axis exactly (ax == 0 -> cos(ax) == 1
// -> d == u, particle.c:612-615).
TEST(SmokeParticles, ZeroSpreadEmitssAlongTheAxis) {
    SmokeSimulation sim;
    SmokeParams params = LongLivedParams(150.0, 255);
    params.spread = 0.0f;
    params.radius = 0.0f;
    sim.SetEmitters({params});
    TickN(sim, 10);
    ASSERT_GT(sim.ParticleCount(0), 10);
    for (const SmokeParticleState& p : sim.Emitter(0).group.particles) {
        EXPECT_NEAR(p.vel[0], 0.0f, 1e-4f);
        EXPECT_NEAR(p.vel[1], 0.0f, 1e-4f);
        EXPECT_GT(p.vel[2], 0.0f) << "the default axis is straight up (smoke.c:363-365)";
    }
}

// The emitted position is on a disc of `radius` metres (radius * kUnit units)
// in the emitter's XY plane, and Z is copied from the emitter unchanged
// (smoke.c:328-330). Both velocities are 0 here, so the particle's position is
// still exactly its birth position and the check needs no age arithmetic
// (speed = (b - a) * Random + a with a == b == 0, particle.c:616).
TEST(SmokeParticles, EmissionDiscIsRadiusMetresWideInTheEmittersXyPlane) {
    SmokeSimulation sim;
    SmokeParams params = LongLivedParams(30.0, 255);
    params.origin[0] = 100.0;
    params.origin[1] = -50.0;
    params.origin[2] = 25.0;
    params.radius = 2.0f;
    params.spread = 0.0f;
    params.min_velocity = 0.0f;
    params.max_velocity = 0.0f;
    sim.SetEmitters({params});
    sim.Tick();  // tick 0 of a rate of 30: nothing yet
    ASSERT_EQ(sim.ParticleCount(0), 0);
    sim.Tick();  // tick 1: one particle
    ASSERT_EQ(sim.ParticleCount(0), 1);

    for (const SmokeParticleState& p : sim.Emitter(0).group.particles) {
        EXPECT_FLOAT_EQ(p.vel[0], 0.0f);
        EXPECT_FLOAT_EQ(p.vel[1], 0.0f);
        EXPECT_FLOAT_EQ(p.vel[2], 0.0f);
        const double dx = p.pos[0] - 100.0;
        const double dy = p.pos[1] + 50.0;
        EXPECT_LE(std::sqrt(dx * dx + dy * dy), 2.0 * igi::kSmokeUnit + 1e-6)
            << "radius * kUnit = 8192 units from the emitter's axis";
        EXPECT_DOUBLE_EQ(p.pos[2], 25.0) << "Z is the emitter's Z (smoke.c:328)";
    }
    // The disc is filled, not a ring: with radius 2 m the samples must not all
    // sit at one distance (Random_Float is uniform on [0,1), smoke.c:321).
    sim.Tick();
    ASSERT_EQ(sim.ParticleCount(0), 2);
    const double d0 = std::hypot(sim.Emitter(0).group.particles[0].pos[0] - 100.0,
                                 sim.Emitter(0).group.particles[0].pos[1] + 50.0);
    const double d1 = std::hypot(sim.Emitter(0).group.particles[1].pos[0] - 100.0,
                                 sim.Emitter(0).group.particles[1].pos[1] + 50.0);
    EXPECT_NE(d0, d1);
}

// Smoke_Changed builds the axis as column 2 of RotZ(yaw) * RotX(pitch)
// (smoke.c:361-365): (sin yaw sin pitch, -cos yaw sin pitch, cos pitch).
TEST(SmokeParticles, EmissionAxisIsTheEmittersOwnRotation) {
    float dir[3];

    igi::SmokeDirection(0.0f, 0.0f, dir);
    EXPECT_NEAR(dir[0], 0.0f, 1e-6f);
    EXPECT_NEAR(dir[1], 0.0f, 1e-6f);
    EXPECT_NEAR(dir[2], 1.0f, 1e-6f) << "pitch == yaw == 0 points straight up";

    igi::SmokeDirection(static_cast<float>(kPi / 2.0), 0.0f, dir);
    EXPECT_NEAR(dir[0], 0.0f, 1e-6f);
    EXPECT_NEAR(dir[1], -1.0f, 1e-6f) << "a positive pitch tips towards -Y";
    EXPECT_NEAR(dir[2], 0.0f, 1e-6f);

    igi::SmokeDirection(static_cast<float>(kPi / 2.0), static_cast<float>(kPi / 2.0), dir);
    EXPECT_NEAR(dir[0], 1.0f, 1e-6f);
    EXPECT_NEAR(dir[1], 0.0f, 1e-6f);
    EXPECT_NEAR(dir[2], 0.0f, 1e-6f);

    // The simulation carries the same axis, and the particles leave along it.
    SmokeSimulation sim;
    SmokeParams params = LongLivedParams(150.0, 255);
    params.alpha = static_cast<float>(kPi / 2.0);  // "Alpha" is the pitch
    params.gamma = 0.0f;                           // "Gamma" is the yaw
    params.spread = 0.0f;
    params.radius = 0.0f;
    sim.SetEmitters({params});
    EXPECT_NEAR(sim.Emitter(0).dir[1], -1.0, 1e-6);
    sim.Tick();
    sim.Tick();
    ASSERT_GT(sim.ParticleCount(0), 0);
    for (const SmokeParticleState& p : sim.Emitter(0).group.particles)
        EXPECT_LT(p.vel[1], 0.0f) << "every particle leaves downwards";
}

// Particle2D_Emit's basis (particle.c:577-609) is orthonormal for any axis, and
// it switches construction on |u.x| > 0.75 (particle.c:587-593). Both branches
// must give a right-handed frame with c == w x u.
TEST(SmokeParticles, ConeFrameIsOrthonormalInBothAxisBranches) {
    const float axes[5][3] = {
        {0.0f, 0.0f, 1.0f},      // |u.x| == 0: the b branch
        {0.5f, 0.5f, 0.7071f},   // |u.x| == 0.5: still the b branch
        {0.9f, 0.1f, 0.42f},     // |u.x| > 0.75: the a branch
        {-0.99f, 0.0f, 0.14f},   // |u.x| > 0.75, negative
        {0.75f, 0.0f, 0.6614f},  // exactly at the limit: NOT > 0.75
    };
    for (const auto& axis : axes) {
        const igi::SmokeConeBasis frame = igi::SmokeConeFrame(axis);
        const double nu = std::sqrt(double(frame.u[0]) * frame.u[0] + double(frame.u[1]) * frame.u[1] +
                                    double(frame.u[2]) * frame.u[2]);
        const double nw = std::sqrt(double(frame.w[0]) * frame.w[0] + double(frame.w[1]) * frame.w[1] +
                                    double(frame.w[2]) * frame.w[2]);
        EXPECT_NEAR(nu, 1.0, 1e-6);
        EXPECT_NEAR(nw, 1.0, 1e-6);
        const double dot = double(frame.u[0]) * frame.w[0] + double(frame.u[1]) * frame.w[1] +
                           double(frame.u[2]) * frame.w[2];
        EXPECT_NEAR(dot, 0.0, 1e-6);
        const double nc = std::sqrt(double(frame.c[0]) * frame.c[0] + double(frame.c[1]) * frame.c[1] +
                                    double(frame.c[2]) * frame.c[2]);
        EXPECT_NEAR(nc, 1.0, 1e-6);
        // c == w x u (particle.c:607-609).
        EXPECT_NEAR(frame.c[0], frame.w[1] * frame.u[2] - frame.w[2] * frame.u[1], 1e-6f);
        EXPECT_NEAR(frame.c[1], -(frame.w[0] * frame.u[2] - frame.w[2] * frame.u[0]), 1e-6f);
        EXPECT_NEAR(frame.c[2], frame.w[0] * frame.u[1] - frame.w[1] * frame.u[0], 1e-6f);
    }
}

// ---------------------------------------------------------------------------
// Motion: Particle2D_Run's integration
// ---------------------------------------------------------------------------

// Smoke's group gravity is (0,0,0) (smoke.c:395) and Particle2D_Run adds it to
// the velocity every tick (particle.c:280-282), so the velocity never changes
// and pos advances by exactly `vel` per tick (particle.c:283-285). `pos` is a
// double and `vel` a float, so the accumulation is exact.
TEST(SmokeParticles, ZeroGravityMeansStraightLineMotionAtOneVelocityPerTick) {
    SmokeSimulation sim;
    SmokeParams params = LongLivedParams(30.0, 255);
    params.spread = 0.0f;
    params.radius = 0.0f;
    params.particle_size_delta = 0.0f;
    sim.SetEmitters({params});
    sim.Tick();
    sim.Tick();
    ASSERT_EQ(sim.ParticleCount(0), 1);

    const SmokeParticleState first = sim.Emitter(0).group.particles[0];
    for (int i = 0; i < 30; ++i) sim.Tick();
    const SmokeParticleState& now = sim.Emitter(0).group.particles[0];
    ASSERT_EQ(now.vel[0], first.vel[0]);
    ASSERT_EQ(now.vel[1], first.vel[1]);
    ASSERT_EQ(now.vel[2], first.vel[2]);
    EXPECT_DOUBLE_EQ(now.pos[0], first.pos[0] + 30.0 * double(first.vel[0]));
    EXPECT_DOUBLE_EQ(now.pos[1], first.pos[1] + 30.0 * double(first.vel[1]));
    EXPECT_DOUBLE_EQ(now.pos[2], first.pos[2] + 30.0 * double(first.vel[2]));
}

// "Particle Size Delta" is the group's `grow` argument, scaled by kPerFrame
// (smoke.c:403), and Particle2D_Run adds it to the particle's scale every tick
// (particle.c:287). "Minimum/Maximum Rotation Speed" is the particle's spin per
// tick (smoke.c:333) and `angle` accumulates it (particle.c:286).
TEST(SmokeParticles, ScaleGrowsAndAngleSpinsEveryTick) {
    SmokeSimulation sim;
    SmokeParams params = LongLivedParams(30.0, 255);
    params.spread = 0.0f;
    params.radius = 0.0f;
    params.particle_size_delta = 3.0f;  // grow = 3 * kPerFrame = 0.1 per tick
    params.min_rotation = 0.3f;
    params.max_rotation = 0.6f;
    sim.SetEmitters({params});
    sim.Tick();
    sim.Tick();
    ASSERT_EQ(sim.ParticleCount(0), 1);

    const SmokeParticleState first = sim.Emitter(0).group.particles[0];
    EXPECT_FLOAT_EQ(igi::SmokeGroupGrow(params), 0.1f);
    EXPECT_NEAR(first.scale, 1.1f, 1e-6f) << "one step of grow on the birth tick";
    EXPECT_NEAR(first.spin, first.angle, 1e-6f) << "angle += spin on the birth tick";
    EXPECT_GE(first.spin, 0.3f / 30.0f - 1e-6f);
    EXPECT_LE(first.spin, 0.6f / 30.0f + 1e-6f);

    for (int i = 0; i < 9; ++i) sim.Tick();
    const SmokeParticleState& now = sim.Emitter(0).group.particles[0];
    EXPECT_NEAR(now.scale, 1.0f + 10.0f * 0.1f, 1e-5f);
    EXPECT_NEAR(now.angle, 10.0f * first.spin, 1e-5f);
}

// A zero "Particle Size Delta" leaves the scale at Particle2D_Emit's `scale`
// argument (kOneF) and the engine then removes the particle when the scale
// reaches 0 (particle.c:300) -- which it never does here.
TEST(SmokeParticles, ZeroGrowKeepsTheScaleAtOne) {
    SmokeSimulation sim;
    SmokeParams params = LongLivedParams(30.0, 8);
    params.spread = 0.0f;
    params.radius = 0.0f;
    params.particle_size_delta = 0.0f;
    sim.SetEmitters({params});
    TickN(sim, 10);
    ASSERT_EQ(sim.ParticleCount(0), 8);
    for (const SmokeParticleState& p : sim.Emitter(0).group.particles) EXPECT_FLOAT_EQ(p.scale, 1.0f);
}

// ---------------------------------------------------------------------------
// The tick rule and the emitter's own state machine
// ---------------------------------------------------------------------------

// One Tick() is one engine frame: the engine's loop has no elapsed time
// (src/win/appmain.c:224-227) and the editor steps the same way the weather
// particles do (renderer_rain.cpp, RainEffect_Run 0x436990). A long frame buys
// no extra steps -- there is no dt argument to pass.
TEST(SmokeParticles, OneTickIsOneEngineFrameAndTheCounterProvesIt) {
    SmokeSimulation sim;
    sim.SetEmitters({LongLivedParams(30.0, 255)});
    EXPECT_EQ(sim.TickCount(), 0u);
    TickN(sim, 7);
    EXPECT_EQ(sim.TickCount(), 7u);
    EXPECT_EQ(sim.ParticleCount(0), 6) << "tick 0 is empty, ticks 1..6 emit one each";
}

// Smoke_Changed rebuilds the group when anything but the intensity changed, and
// KEEPS it when only the intensity changed (smoke.c:347-350: the group is
// deleted exactly when EQ_OR_NAN(level, lastLevel)). Re-publishing the same
// parameters must not touch the live particles -- that is what makes the
// editor's per-frame re-publish safe.
TEST(SmokeParticles, RepublishKeepsParticlesAndOnlyAnIntensityEditKeepsThem) {
    SmokeSimulation sim;
    SmokeParams params = LongLivedParams(30.0, 255);
    sim.SetEmitters({params});
    TickN(sim, 10);
    const int before = sim.ParticleCount(0);
    ASSERT_GT(before, 0);

    sim.SetEmitters({params});  // identical: nothing changes
    EXPECT_EQ(sim.ParticleCount(0), before) << "an unchanged emitter is not rebuilt";

    SmokeParams intensity_only = params;
    intensity_only.intensity = 0.5f;
    sim.SetEmitters({intensity_only});
    EXPECT_EQ(sim.ParticleCount(0), before)
        << "only the intensity changed -> EQ_OR_NAN false -> the group survives (smoke.c:347)";

    SmokeParams radius_changed = intensity_only;
    radius_changed.radius = 1.0f;
    sim.SetEmitters({radius_changed});
    EXPECT_EQ(sim.ParticleCount(0), 0) << "any other parameter change drops the group";

    // A new emitter starts empty and emits on the engine's schedule.
    sim.SetEmitters({radius_changed, params});
    EXPECT_EQ(sim.EmitterCount(), 2u);
    EXPECT_EQ(sim.ParticleCount(0), 0);
    EXPECT_EQ(sim.ParticleCount(1), 0);
    sim.Tick();
    EXPECT_EQ(sim.ParticleCount(1), 0) << "tick 0 of a rate of 30 is empty";
    sim.Tick();
    EXPECT_EQ(sim.ParticleCount(1), 1);
}

// Smoke_Run's first step: the already emitted particles are translated by the
// emitter's delta, but only while `moved` is set and "Move Particles" is on
// (smoke.c:303-312). Emission itself is never gated by it.
TEST(SmokeParticles, MoveParticlesTranslatesTheLivePoolOnlyWhenEnabled) {
    for (int enabled = 0; enabled < 2; ++enabled) {
        SmokeSimulation sim;
        SmokeParams params = LongLivedParams(30.0, 255);
        params.spread = 0.0f;
        params.radius = 0.0f;
        params.move_particles = enabled != 0;
        sim.SetEmitters({params});
        TickN(sim, 3);
        ASSERT_EQ(sim.ParticleCount(0), 2);

        const double before_x = sim.Emitter(0).group.particles[0].pos[0];
        sim.MutableEmitters()[0].pos[0] += 1000.0;  // as Smoke_Move would move it
        sim.Tick();
        const double after_x = sim.Emitter(0).group.particles[0].pos[0];
        const double expected = before_x + double(sim.Emitter(0).group.particles[0].vel[0]) +
                                (enabled ? 1000.0 : 0.0);
        EXPECT_NEAR(after_x, expected, 1e-6)
            << (enabled ? "Move Particles on: the pool follows the emitter"
                        : "Move Particles off: the pool stays where it was");
        EXPECT_GT(sim.ParticleCount(0), 2) << "emission is not gated by Move Particles";
    }
}

// The emitter's own position is its authored "Position" (ParamTable slot 0,
// smoke.c:167) and it does not follow a moving ancestor in the editor
// (smoke_sim.h, named divergences).
TEST(SmokeParticles, EmitterPositionIsTheAuthoredPosition) {
    SmokeSimulation sim;
    SmokeParams params = LongLivedParams(30.0, 255);
    params.origin[0] = 24614592.0;
    params.origin[1] = -55795096.0;
    params.origin[2] = 174417712.0;
    sim.SetEmitters({params});
    EXPECT_DOUBLE_EQ(sim.Emitter(0).pos[0], 24614592.0);
    EXPECT_DOUBLE_EQ(sim.Emitter(0).pos[1], -55795096.0);
    EXPECT_DOUBLE_EQ(sim.Emitter(0).pos[2], 174417712.0);
}

// The two random streams are the engine's two: gSmokeRandom (Random_Init at
// Smoke_Create, smoke.c:246) and gParticle2DRandom (Random_Init +
// Random_Seed(0x12332153) in Particle2D_Open, particle.c:99-100). Resetting the
// simulation must reproduce the same particle exactly, which is what makes the
// preview and the tests deterministic.
TEST(SmokeParticles, TwoIdenticalRunsProduceIdenticalParticles) {
    auto run = [](std::vector<SmokeParticleState>& out) {
        SmokeSimulation sim;
        SmokeParams params = Params();
        params.count = 40;
        params.radius = 1.5f;
        params.spread = 0.7f;
        params.min_velocity = 2.0f;
        params.max_velocity = 5.0f;
        params.life_time = 2.0f;
        params.fade_time = 2.0f;
        params.generate_factor = 45.0;
        sim.SetEmitters({params});
        for (int i = 0; i < 20; ++i) sim.Tick();
        out = sim.Emitter(0).group.particles;
    };
    std::vector<SmokeParticleState> a, b;
    run(a);
    run(b);
    ASSERT_EQ(a.size(), b.size());
    ASSERT_GT(a.size(), 10u);
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_EQ(a[i].pos[0], b[i].pos[0]);
        EXPECT_EQ(a[i].pos[1], b[i].pos[1]);
        EXPECT_EQ(a[i].pos[2], b[i].pos[2]);
        EXPECT_EQ(a[i].vel[0], b[i].vel[0]);
        EXPECT_EQ(a[i].vel[1], b[i].vel[1]);
        EXPECT_EQ(a[i].vel[2], b[i].vel[2]);
        EXPECT_EQ(a[i].level, b[i].level);
        EXPECT_EQ(a[i].scale, b[i].scale);
        EXPECT_EQ(a[i].angle, b[i].angle);
        EXPECT_EQ(a[i].delay, b[i].delay);
    }
}

// The two streams are seeded differently: gParticle2DRandom gets
// Random_Seed(0x12332153) on top of Random_Init (particle.c:100), so a
// simulation's particle stream must not equal a bare Random_Init stream.
TEST(SmokeParticles, ParticleStreamIsSeededWithTheEnginesSeed) {
    SmokeSimulation sim;
    sim.ResetRandomStreams();
    igi::SmokeRandom plain;
    plain.Reset();
    bool differs = false;
    for (int i = 0; i < 8; ++i) {
        if (sim.ParticleRng().Float() != plain.Float()) differs = true;
    }
    EXPECT_TRUE(differs) << "Particle2D_Open's Random_Seed(0x12332153) must shift the stream";
}

// Random_Seed 0x4B47E0 (random.c:34-40): Random_Init, then a ^= seed,
// b += seed << 15, c -= seed << 9. The first three values of the seeded stream
// are pinned here so a change to the seeding shows up as a number, not a
// mystery.
TEST(SmokeParticles, SeededStreamMatchesRandomSeed) {
    igi::SmokeRandom r;
    r.Seed(0x12332153u);
    const long double a = r.Float();
    const long double b = r.Float();
    const long double c = r.Float();
    // Derived from random.c:12-24 by hand: c ^= a >> 7; b ^= c; a += b, then the
    // low 16 bits over 65536.
    const std::uint32_t s = 0x12332153u;
    std::uint32_t ra = 0xC8FB1A07u ^ s;
    std::uint32_t rb = 0x39EA7756u + (s << 15);
    std::uint32_t rc = 0x7FF13CCDu - (s << 9);
    auto next = [&]() {
        rc ^= ra >> 7;
        rb ^= rc;
        ra += rb;
        return ra;
    };
    EXPECT_EQ(static_cast<std::uint32_t>(a * 65536.0L), next() & 0xFFFFu);
    EXPECT_EQ(static_cast<std::uint32_t>(b * 65536.0L), next() & 0xFFFFu);
    EXPECT_EQ(static_cast<std::uint32_t>(c * 65536.0L), next() & 0xFFFFu);
}

// ---------------------------------------------------------------------------
// The blend flags the draw path reads (Particle2D_Draw, particle.c:108-208)
// ---------------------------------------------------------------------------

// Smoke_Changed's "Fade Mode" picks the QVertex flags (smoke.c:366-379) and
// Particle2D_New ORs in 0x40000 (particle.c:515). Particle2D_Draw then treats
// 0x80000 as "the level is the alpha" and its absence as "the level scales the
// colour" (particle.c:194-203) -- the rule the viewport preview has to follow.
TEST(SmokeParticles, FadeModeSelectsTheAlphaOrColourRule) {
    EXPECT_EQ(igi::SmokeBlendFlags(0) | 0x40000u, 0x10C0000u);
    EXPECT_EQ(igi::SmokeBlendFlags(1) | 0x40000u, 0x1040008u);
    EXPECT_EQ(igi::SmokeBlendFlags(2) | 0x40000u, 0x1040010u);
    EXPECT_NE(igi::SmokeBlendFlags(0) & 0x80000u, 0u) << "mode 0: alpha = level";
    EXPECT_EQ(igi::SmokeBlendFlags(1) & 0x80000u, 0u) << "mode 1: colour *= level";
    EXPECT_EQ(igi::SmokeBlendFlags(2) & 0x80000u, 0u) << "mode 2: colour *= level";
}

}  // namespace
