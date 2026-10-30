// smoke.h - the retail "Smoke" particle emitter, resolved the way IGI-RE
// resolves it.
//
// Issue #43 asked for "create/edit lights and smoke FX". The editor already
// parses level task trees, but it knows nothing about the two retail effect
// task types that a level actually authors: "Smoke" (a particle emitter) and
// "AlarmLight" (the only light-with-runtime-state task type in retail). This
// module is the pure-logic half for Smoke; alarm_light.h is the other half.
//
// Everything here is cited to IGI-RE (`/Users/haseeb-mir/Documents/Code/IGI-RE`),
// which is the only evidence source used. The authored parameter names and
// types come from the retail `Task_DeclareParameters("Smoke", ...)` line, which
// ships inside every OBJECTS.QVM that authors a smoke task and is also present
// as decompiled text in `location0/LEVEL2/objects.qsc:59-61`.
//
// The five IGI-RE files this module is built from:
//
//   src/fxobj/smoke.c        Smoke_Init 0x437510, Smoke_Create 0x437B90,
//                            Smoke_Run 0x437DB0, Smoke_Changed 0x438000,
//                            Smoke_Move 0x437AD0, Smoke_UpdateLocal 0x438240
//   src/graphtask/particle.c Particle2D_New 0x4FDF00, Particle2D_Add 0x4FE120,
//                            Particle2D_Emit 0x4FE210
//   src/math/random.c        Random_Init 0x4B4720, Random_Next 0x4B4740,
//                            Random_Float 0x4B4770
//   src/math/matrix.c        Matrix_RotateX 0x4B34F0, Matrix_RotateZ 0x4B3790,
//                            Matrix_SetEuler 0x4B38E0
//   src/physobj/paramops.c   the script-argument reader: ParamReal32x9_Parse
//                            0x4EFBA0 reads THREE Euler angles, and the type
//                            aliases give ObjectPos 3 args and RGB 3 args
//
// A "Smoke" task consumes exactly 25 argument tokens (3 for ObjectPos, 3 for
// RGB, 19 scalars), which every one of the 71 retail Smoke instances in the
// shipped QVMs satisfies. See docs/igi-re-parity/SMOKE_LIGHTS.md.
//
// Named divergences (never silently fixed):
//   * x87 `fsin`/`fcos` (smoke.c:104-114) are not bit-identical to libm
//     `sinl`/`cosl`; this port uses libm and is exact to ~1 ulp of the 64-bit
//     x87 mantissa, not bit-exact.
//   * `fistpll` (fxobj.h:53-58) rounds with the current x87 rounding mode,
//     round-to-nearest-even. SmokeRoundInt reproduces that, including the
//     "integer indefinite" result on overflow.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace igi {

// ---------------------------------------------------------------------------
// Engine constants. Every one is IGI-RE DATA (src/fxobj/smoke.c).
// ---------------------------------------------------------------------------

constexpr float kSmokeTwoPi = 0x1.921fb6p+2f;      // smoke.c:94  kTwoPi
constexpr float kSmokeUnit = 4096.0f;              // smoke.c:95  kUnit
constexpr float kSmokeTickRate = 30.0f;            // smoke.c:96  kRate
constexpr float kSmokePerFrame = 0x1.111112p-5f;   // smoke.c:97  kPerFrame
constexpr float kSmokeSpeedScale = 136.0f;         // smoke.c:98  kSpeed
constexpr float kSmokeColourScale = 63.0f;         // smoke.c:99  kScale
constexpr float kSmokeOne = 1.0f;                  // smoke.c:100 kOneF
constexpr float kSmokeParticleLife = 409600.0f;    // smoke.c:102 kLife

// ParamTable_New(21) (smoke.c:166) and the 21 ParamTable_Add calls that follow
// (smoke.c:167-187). The authored argument list is 25 tokens long because
// ObjectPos and RGB each consume three script arguments (paramops.c:182, :180).
constexpr int kSmokeParamCount = 21;
constexpr int kSmokeTokenCount = 25;

// The engine's own comment for Smoke_Run is "moves the emitter along and emits
// rate / 30 particles a frame" (smoke.c:297). The loop at smoke.c:319-337 tests
// `acc > kRate` STRICTLY and emits before subtracting, and Smoke_Create starts
// the accumulator at zero (smoke.c:228). The steady state is therefore exactly
// rate / 30 particles per tick, but the FIRST tick of an exact multiple of 30
// emits one fewer, and a rate below 30 emits nothing until the carried
// remainder crosses the threshold:
//
//     rate  30 -> 0, 1, 1, 1, ...     rate  60 -> 1, 2, 2, 2, ...
//     rate 150 -> 4, 5, 5, 5, ...     rate  45 -> 1, 1, 2, 1, 2, ...
//     rate   1 -> 0 for the first 29 ticks, then 1 every 30 ticks
//
// Reproduced, not corrected; docs/igi-re-parity/SMOKE_LIGHTS.md records it.
constexpr float kSmokeSteadyStateDivisor = 30.0f;  // == kSmokeTickRate

// Token layout of an editor LevelObject::argTokens vector:
//   [0] = task id, [1] = type, [2] = name, [3] = declared parameter 0.
// Identical to mission_fog.h's FogTaskObject convention.
constexpr std::size_t kSmokeFirstParam = 3;

// The 21 authored parameters, in the order ParamTable_Add registers them
// (smoke.c:167-187) and in the order the retail declaration line lists them
// (LEVEL2/objects.qsc:59-61). The engine field is the IGI-RE struct field the
// slot writes; where the two disagree the disagreement is called out, because
// it is load-bearing for the runtime rule.
enum SmokeParamIndex {
    kSmokeOrigin = 0,              // "Position"                 ObjectPos -> Smoke.origin
    kSmokeAlpha = 1,               // "Alpha"                    Angle     -> Smoke.pitch
    kSmokeGamma = 2,               // "Gamma"                    Angle     -> Smoke.yaw
    kSmokeCount = 3,               // "Number of Particles"      Int32     -> Smoke.count
    kSmokeRadius = 4,              // "Radius"                   Real32    -> Smoke.radius
    kSmokeSpread = 5,              // "Maximum Random Angle"     Angle     -> Smoke.spread
    kSmokeMinVelocity = 6,         // "Minimum Velocity"         Real32    -> Smoke.speedMax (!)
    kSmokeMaxVelocity = 7,         // "Maximum Velocity"         Real32    -> Smoke.speedMin (!)
    kSmokeColour = 8,              // "Colour"                   RGB       -> Smoke.unk104
    kSmokeLifeTime = 9,            // "Life Time"                Real32    -> Smoke.fadeDelay
    kSmokeFadeTime = 10,           // "Fade Time"                Real32    -> Smoke.fadeTime
    kSmokeFadeMode = 11,           // "Fade Mode"                Int32     -> Smoke.blend
    kSmokeSpriteIndex = 12,        // "Sprite index"             Int32     -> Smoke.sprite
    kSmokeParticleSize = 13,       // "Particle Size"            Real32    -> Smoke.size
    kSmokeParticleSizeDelta = 14,  // "Particle Size Delta"      Real32    -> Smoke.grow
    kSmokeMinRotation = 15,        // "Minimum Rotation Speed"   Angle     -> Smoke.unk120
    kSmokeMaxRotation = 16,        // "Maximum Rotation Speed"   Angle     -> Smoke.unk11C
    kSmokeIntensity = 17,          // "Intensity"                Real32    -> Smoke.level
    kSmokeGenerateFactor = 18,     // "Initial generate factor value" Real64 -> Smoke.rate
    kSmokeGenerateExpr = 19,       // "Generate factor"          VarString -> Smoke.expr
    kSmokeMoveParticles = 20,      // "Move Particles"           bool8     -> Smoke.enabled
};

// One row of the authored parameter table.
struct SmokeParamDesc {
    const char* authored_name;  // the retail declaration string
    const char* engine_field;   // the IGI-RE struct field the slot writes
    const char* type_name;      // the retail declared type
    int token_count;            // script arguments consumed (paramops.c arg count)
    float default_value;        // Smoke_Create (smoke.c:212-247)
    bool has_range;             // whether Smoke_Init registers a RangeF/RangeI
    float range_min;            // smoke.c:129-154
    float range_max;
};

// The 21 rows, indexed by SmokeParamIndex. Never null.
const SmokeParamDesc* SmokeParamTable();

// ---------------------------------------------------------------------------
// The authored task
// ---------------------------------------------------------------------------

// One task as the editor's loader hands it over -- the same shape as
// igi::FogTaskObject / LevelObject::argTokens.
struct SmokeTaskObject {
    std::string type;
    std::vector<std::string> arg_tokens;
};

// The resolved parameters of one "Smoke" instance. Field names follow the
// AUTHORED names (what the level author sees), not the IGI-RE struct field
// names, because two of the retail slots write a field whose IGI-RE name means
// the opposite thing:
//
//   authored "Minimum Velocity" (slot 6) -> Smoke.speedMax, default 4.5
//   authored "Maximum Velocity" (slot 7) -> Smoke.speedMin, default 6.0
//
// (smoke.c:173-174 vs smoke.c:224-225). The emission interval is unaffected:
// Particle2D_Emit's interpolation is uniform in its random argument, so a
// reversed pair still yields a uniform sample on [min, max] (particle.c:616).
//
// The two fade slots are NOT interchangeable:
//   authored "Life Time" (slot 9) -> Smoke.fadeDelay -> the delay argument
//   authored "Fade Time" (slot 10) -> Smoke.fadeTime -> the frames argument
struct SmokeParams {
    double origin[3] = {0.0, 0.0, 0.0};
    float alpha = 0.0f;                   // pitch, radians
    float gamma = 0.0f;                   // yaw, radians
    int count = 100;
    float radius = 0.0f;
    float spread = 0x1.921fb6p-1f;        // pi/4, radians (Smoke_Create, smoke.c:226)
    float min_velocity = 4.5f;
    float max_velocity = 6.0f;
    float colour[3] = {2.0f, 2.0f, 2.0f};
    float life_time = 0.5f;
    float fade_time = 1.0f;
    int fade_mode = 0;
    int sprite_index = 3;
    float particle_size = 0.5f;
    float particle_size_delta = 0x1.555556p+1f;  // 2.6666667
    float min_rotation = 0.0f;
    float max_rotation = 0.0f;
    float intensity = 1.0f;
    double generate_factor = 30.0;
    std::string generate_expr;
    bool move_particles = false;

    // Smoke_Activate re-registers "<name>.vGenerateFactor" as a live double
    // pointer at Smoke.rate (smoke.c:259-260), so the script can retune the
    // emission rate while the level runs. `generate_expr` is the separate
    // "Generate factor" expression, evaluated every tick (smoke.c:314-315).
};

// Parses one authored "Smoke" call. `arg_tokens` is the LevelObject::argTokens
// layout documented above. Returns false when the call is shorter than the
// 25 tokens the 21 parameters require, leaving `out` untouched -- a truncated
// task is reported rather than silently filled with defaults.
bool ParseSmokeParams(const std::vector<std::string>& arg_tokens, SmokeParams* out);

// Every "Smoke" task in the list, in file order. Tasks of other types are
// ignored, and a Smoke task that fails to parse is skipped.
std::vector<SmokeParams> ResolveSmokeTasks(const std::vector<SmokeTaskObject>& objects);

// ---------------------------------------------------------------------------
// The emitter's random source (src/math/random.c)
// ---------------------------------------------------------------------------
//
// Smoke_Create calls Random_Init(gSmokeRandom) (smoke.c:246), so the emitter's
// sequence is fixed at task creation and is therefore fully reproducible. The
// state is 12 bytes (smoke.c:463-464, gSmokeRandom aliases a 16-byte store).
class SmokeRandom {
public:
    SmokeRandom() { Reset(); }

    // Random_Init 0x4B4720 (random.c:5-9).
    void Reset();
    // Random_Next 0x4B4740 (random.c:12-18). `c ^= a >> 7; b ^= c; a += b;`.
    std::uint32_t Next();
    // Random_Float 0x4B4770 (random.c:21-24): `(Next() & 0xFFFF) / 65536`,
    // i.e. 16 bits of mantissa, uniform on {0, 1/65536, ..., 65535/65536}.
    long double Float();
    // Random_Seed 0x4B47E0 (random.c:34-40): Random_Init then
    // `a ^= seed; b += seed << 15; c -= seed << 9`. Particle2D_Open seeds
    // gParticle2DRandom with 0x12332153 (particle.c:99-100).
    void Seed(std::uint32_t seed);

private:
    std::uint32_t a_ = 0;
    std::uint32_t b_ = 0;
    std::uint32_t c_ = 0;
};

// ---------------------------------------------------------------------------
// The per-tick rule (Smoke_Run 0x437DB0, smoke.c:298-339)
// ---------------------------------------------------------------------------

// `fistpll` with the x87 default rounding mode: round-to-nearest-even
// (fxobj.h:53-58). On overflow or NaN the x87 leaves the "integer indefinite"
// 0x8000000000000000, whose low 32 bits -- and therefore the C cast back to
// int -- are zero.
int SmokeRoundInt(long double value);

// The emission accumulator, exactly:
//
//   acc = t->rate + t->acc;      // double + float -> double -> long double
//   t->acc = acc;                // Smoke.acc is a FLOAT: this truncates
//   while (acc > kRate) { emit(); acc = t->acc - kRate; t->acc = acc; }
//
// (smoke.c:316-337). `rate` is Smoke.rate, a double (smoke.c:44); `acc` is
// Smoke.acc, a float (smoke.c:45). The first comparison therefore uses the
// untruncated long double while every later one reads the truncated float.
// Returns how many particles the tick emits and updates `acc`.
int SmokeAdvanceAccumulator(double rate, float& acc);

// One emitted particle, as Smoke_Run derives it before handing the values to
// Particle2D_Emit (smoke.c:319-334).
struct SmokeParticle {
    double pos[3] = {0.0, 0.0, 0.0};  // on a disc of `radius` in the emitter's XY plane
    float spin = 0.0f;                // Particle2D_Emit's `spin` argument
    float level = 0.0f;               // Particle2D_Emit's `level` argument
    float scale = 1.0f;               // Particle2D_Emit's `scale` argument, always kOneF
    int delay_frames = 0;             // Particle2D_Add's `delay`
    int fade_frames = 0;              // Particle2D_Add's `frames`
};

// The per-particle derivation (smoke.c:319-334). `count` is normally
// SmokeAdvanceAccumulator's result. Draws three Random_Float values per
// particle, in the engine's order: angle, radius, rotation-speed fraction.
void SmokeEmitParticles(const SmokeParams& params, const double emitter_pos[3], int count,
                        SmokeRandom& rng, SmokeParticle* out);

// The (a, b) pair Smoke_Run passes to Particle2D_Emit for the speed range
// (smoke.c:333): a = authored "Maximum Velocity" * kSpeed, b = authored
// "Minimum Velocity" * kSpeed. Named for the ENGINE call arguments so the
// reversal is visible instead of hidden.
void SmokeEmitSpeedArguments(const SmokeParams& params, float* a, float* b);

// Particle2D_Emit's speed draw (particle.c:616):
//   speed = (b - a) * Random_Float + a
// With a > b (which is what the retail call passes) the interpolation runs
// backwards; because the fraction is uniform on [0,1) the sample is still
// uniform on [a, b], so the reversal is distribution-preserving.
float SmokeSampleSpeed(float a, float b, long double fraction);

// ---------------------------------------------------------------------------
// Particle2D_Emit's direction: the emission cone (particle.c:569-633)
// ---------------------------------------------------------------------------
//
// Particle2D_Emit turns the emitter's axis into ONE particle direction:
//
//   u = normalize(dir)                                    particle.c:577-586
//   |u.x| > 0.75 ? (a, b) = (1, 0) : (0, 1)               particle.c:587-593
//   v = (a*u.z, -b*u.z, b*u.y - a*u.x);  w = normalize(v)  particle.c:594-606
//   c = w x u                                             particle.c:607-609
//   tilt = Random_Float * spread                          particle.c:610
//   az   = PI - Random_Float * 2*PI                       particle.c:611
//   ax   = spread * 0.5 - tilt                            particle.c:612
//   d    = column2(RotZ(az) * RotX(ax)) * (c, w, u)       particle.c:613-615
//   e    = normalize(d);  vel = e * speed                 particle.c:617-631
//
// Column 2 of RotZ(az)*RotX(ax) is (sin az sin ax, -cos az sin ax, cos ax)
// (matrix.c:148-164 Matrix_SetRotZ 0x4B3BE0, matrix.c:112-127 Matrix_SetRotX
// 0x4B3B60, matrix.c:99-110 Matrix_SetEuler 0x4B38E0, matrix.c:6-21 Mul), so
// `d` is the axis `u` tilted by |ax| inside the (c, w) plane. `ax` runs over
// (spread/2 - spread, spread/2] -- i.e. **the cone's half-angle is spread/2,
// not spread**, even though the authored parameter is named "Maximum Random
// Angle" (smoke.c:172). That is the engine's arithmetic, reproduced here.
//
// The two draws are consumed in the order tilt, azimuth (particle.c:610-611);
// the speed draw comes last (particle.c:616). Particle2D_Emit's random source
// is gParticle2DRandom -- a DIFFERENT stream from the emitter's gSmokeRandom,
// seeded once in Particle2D_Open (particle.c:99-100).
struct SmokeConeBasis {
    float u[3] = {0.0f, 0.0f, 1.0f};  // the normalised axis
    float w[3] = {0.0f, -1.0f, 0.0f};
    float c[3] = {-1.0f, 0.0f, 0.0f}; // w x u
};

// The (u, w, c) frame Particle2D_Emit builds from the emitter axis
// (particle.c:577-609). `dir` is Smoke_Changed's axis (SmokeDirection).
SmokeConeBasis SmokeConeFrame(const float dir[3]);

// The unit direction of one particle (particle.c:610-628), given the two
// Random_Float draws the caller has already taken, in the engine's order.
void SmokeEmitDirection(const float dir[3], float spread, long double tilt_fraction,
                        long double azimuth_fraction, float out_dir[3]);

// The full velocity Particle2D_Add stores (particle.c:616-632):
// `e * speed`, with `speed = SmokeSampleSpeed(a, b, speed_fraction)`.
void SmokeEmitVelocity(const float dir[3], float spread, float speed_a, float speed_b,
                       long double tilt_fraction, long double azimuth_fraction,
                       long double speed_fraction, float out_vel[3]);

// ---------------------------------------------------------------------------
// Particle2D_Run's per-particle step (particle.c:264-305)
// ---------------------------------------------------------------------------
//
// One particle per engine tick, in the engine's order:
//
//   vel += gravity                  (zero for Smoke, smoke.c:395)
//   pos += vel
//   angle += spin
//   scale += grow
//   if (delay) delay--;             -- and the level is NOT faded this tick
//   else { level -= fade; the particle dies when level <= 0 or scale <= 0 }
//
// (particle.c:280-304). `delay` and `fade` are the two arguments Smoke_Run
// derives (smoke.c:331-332, SmokeDelayFrames/SmokeFadeFrames) and `fade` is
// `frames ? 1/frames : 1` (particle.c:563), so the fade is a per-tick
// SUBTRACTION of a float -- the lifetime is `delay + frames` ticks plus
// whatever the float recurrence leaves behind (1 - 30*(1/30) rounds to exactly
// 0, 1 - 60*(1/60) does not).
//
// `frame`/`frameTimer` advance only for an animated sprite (`t->animated` is
// `sprite->map != NULL`, particle.c:519-520). `last_frame` is that map's
// `cols - 1` -- exactly Particle2D_Run's local `last` (:271-272), NOT a count:
// the engine advances while `p->frame < last` (:291-292), so a 7-column map
// (blood1.spr) tops out at frame 6 and a 1-column map at frame 0. The editor
// takes the columns from the retail .spr itself (runtime/smoke_sprite.h,
// SmokeSimulation::SetSpriteColumns).
struct SmokeParticleState {
    double pos[3] = {0.0, 0.0, 0.0};
    float vel[3] = {0.0f, 0.0f, 0.0f};
    float spin = 0.0f;   // radians per tick (Smoke_Run's `spin` argument)
    float level = 0.0f;  // fades to 0; also the alpha for the default fade mode
    float fade = 0.0f;   // per-tick subtraction
    float angle = 0.0f;  // accumulated spin, the quad's screen rotation
    float scale = 1.0f;  // grows by the group's `grow` per tick
    int delay = 0;       // ticks before the fade starts
    int frame = 0;
    int frame_timer = 0;
};

// One tick of Particle2D_Run for one particle (particle.c:280-304). Returns
// false when the particle is removed this tick (the engine's
// Particle2D_Remove, particle.c:422-433). `gravity`, `grow` and `frame_delay`
// are the group's (Particle2D_New) fields; Smoke's gravity is (0,0,0) and its
// frameDelay is 0 (smoke.c:403). `last_frame` is Particle2D_Run's `last`, i.e.
// the sprite map's `cols - 1` (0 when the sprite has no char map or one column).
bool SmokeParticleTick(SmokeParticleState& p, const float gravity[3], float grow, int frame_delay,
                       int last_frame);

// The `frames`/`delay` arguments (smoke.c:331-332):
//   delay = round_int(fadeDelay * kRate)          -- authored "Life Time"
//   frames = round_int(fadeTime * kRate / level)  -- authored "Fade Time"
// Particle2D_Add documents `frames` as "how many frames the level takes to fade
// from 1 to 0 (0 for one)" (particle.c:540, :563). Dividing by `intensity`
// makes a brighter emitter fade FASTER; a level of 0 divides by zero and the
// x87 round yields 0 frames, i.e. a one-frame fade.
int SmokeDelayFrames(const SmokeParams& params);
int SmokeFadeFrames(const SmokeParams& params);

// ---------------------------------------------------------------------------
// The emitter's transform (Smoke_Changed 0x438000, Smoke_Move 0x437AD0,
// Smoke_UpdateLocal 0x438240)
// ---------------------------------------------------------------------------

// Smoke_Changed: `dir` is column 2 of RotZ(yaw) * RotX(pitch) (smoke.c:361-365
// with matrix.c:72-97). Expanded: (sin(yaw)sin(pitch), -cos(yaw)sin(pitch),
// cos(pitch)) -- straight up at pitch == yaw == 0.
void SmokeDirection(float alpha, float gamma, float out_dir[3]);

// The QVertex blend flags Smoke_Changed passes to Particle2D_New
// (smoke.c:366-379): 0 -> 0x1080000, 1 -> 0x1000008, 2 -> 0x1000010, and any
// other value passes whatever the stack held, i.e. undefined; this port
// returns 0 for that case. Particle2D_New then ORs in 0x40000 (particle.c:515).
std::uint32_t SmokeBlendFlags(int fade_mode);

// The sprite slot Smoke_Changed picks (smoke.c:380-393), and the retail sprite
// file each slot loads (smoke.c:189-192): 0 fireball.spr, 1 blood1.spr,
// 2 smoke2.spr, anything else smoke1.spr.
const char* SmokeSpriteFile(int sprite_index);

// Particle2D_New's derived arguments (smoke.c:394-404):
//   size[0] = size[1] = particle_size * kUnit
//   colour           = authored "Colour" * kScale
//   grow             = particle_size_delta * kPerFrame
//   maxDepth         = kLife
//   capacity         = count
//   gravity          = (0, 0, 0), frameDelay = 0, light = 0
float SmokeGroupSize(const SmokeParams& params);
void SmokeGroupColour(const SmokeParams& params, float out_colour[3]);

// The quad's vertex colour and alpha, i.e. what Particle2D_Draw hands
// QVertex_DrawQuadEx for one particle (particle.c:194-203), already scaled the
// way the engine scales it: `gColorScale = 1/128` (qvertex.c:189), NOT 1/255.
// `group_colour` is SmokeGroupColour's output (the authored "Colour" * 63).
//
//   flags == 0            -> rgb = colour/128, a = 0   (drawn invisible: the
//                            engine leaves `alpha` at 0 -- particle.c:110)
//   flags & 0x80000       -> rgb = colour/128, a = level   (fade mode 0)
//   otherwise             -> rgb = colour/128 * level, a = 1 (fade modes 1, 2)
//
// The values are left UNCLAMPED. D3DQVertex_Emit packs `v->r * -255` into a
// byte (d3d.c:415-421), so the engine truncates and, above 1.0, wraps rather
// than clamps; this port keeps the float and lets the framebuffer clamp, which
// differs only for an authored "Colour" above 128/63 (D3D would show a wrapped
// channel where GL shows white).
void SmokeParticleColour(const float group_colour[3], std::uint32_t flags, float level,
                         float out_rgba[4]);
float SmokeGroupGrow(const SmokeParams& params);

// Smoke_Move: pos = anchor_pos + anchor_matrix * local (smoke.c:197-209).
// `matrix` is the anchor's nine floats in the engine's row-major layout.
void SmokeMove(const double anchor_pos[3], const float matrix[9], const double local[3],
               double out_pos[3]);

// Smoke_UpdateLocal: the offset of `origin` from the nearest moving ancestor,
// expressed in that ancestor's frame (smoke.c:413-439). `has_parent` false
// zeroes the offset and returns the identity, matching smoke.c:420-428.
void SmokeUpdateLocal(const double origin[3], const double parent_pos[3], const float parent_matrix[9],
                      bool has_parent, double out_local[3]);

// Smoke_Run's first step: while the emitter has moved at least once and
// "Move Particles" is set, the already-emitted particles are translated by the
// emitter's delta (smoke.c:303-312). Returns true when the group must be
// translated, and writes the delta. Note that `move_particles` gates only this
// translation -- emission itself is NOT gated by it.
bool SmokeTranslateDelta(bool move_particles, bool moved_once, const double pos[3],
                         const double last_pos[3], double out_delta[3]);

}  // namespace igi
