#include "runtime/smoke.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace igi {
namespace {

// ---------------------------------------------------------------------------
// Authored token readers
// ---------------------------------------------------------------------------

// Token layout: [0]=task id, [1]=type, [2]=name, [3]=declared parameter 0.
// So declared parameter k lives at token index k + 3, exactly as
// mission_fog.cpp documents for GlobalLightKeyframe.
float TokenFloat(const std::vector<std::string>& tokens, std::size_t index, float fallback = 0.0f) {
    if (index >= tokens.size()) return fallback;
    const std::string& token = tokens[index];
    if (token.empty()) return fallback;
    char* end = nullptr;
    const float value = std::strtof(token.c_str(), &end);
    if (end == token.c_str()) return fallback;
    return value;
}

double TokenDouble(const std::vector<std::string>& tokens, std::size_t index, double fallback = 0.0) {
    if (index >= tokens.size()) return fallback;
    const std::string& token = tokens[index];
    if (token.empty()) return fallback;
    char* end = nullptr;
    const double value = std::strtod(token.c_str(), &end);
    if (end == token.c_str()) return fallback;
    return value;
}

int TokenInt(const std::vector<std::string>& tokens, std::size_t index, int fallback = 0) {
    if (index >= tokens.size()) return fallback;
    const std::string& token = tokens[index];
    if (token.empty()) return fallback;
    char* end = nullptr;
    const long value = std::strtol(token.c_str(), &end, 10);
    if (end == token.c_str()) return fallback;
    return static_cast<int>(value);
}

// A bool8 authored value: the retail scripts write TRUE/FALSE, and the QVM
// identifier table holds exactly those two (mission_fog's corpus test relies on
// the same encoding). Anything else falls back to a number.
bool TokenBool(const std::vector<std::string>& tokens, std::size_t index, bool fallback = false) {
    if (index >= tokens.size()) return fallback;
    const std::string& token = tokens[index];
    if (token == "TRUE") return true;
    if (token == "FALSE") return false;
    return TokenInt(tokens, index, fallback ? 1 : 0) != 0;
}

// pi, as the float the engine's own ranges use (smoke.c:150-151).
constexpr float kPiF = 0x1.921fb6p+1f;

}  // namespace

// ---------------------------------------------------------------------------
// The authored parameter table
// ---------------------------------------------------------------------------

const SmokeParamDesc* SmokeParamTable() {
    // Index order is the ParamTable_Add order (smoke.c:167-187); the authored
    // name and type columns are the retail Task_DeclareParameters line
    // (LEVEL2/objects.qsc:59-61); the default column is Smoke_Create
    // (smoke.c:212-247); the range column is Smoke_Init (smoke.c:129-154).
    // `default_value` is the Smoke_Create value for the numeric slots and the
    // first component for the two three-token slots; the three non-numeric
    // slots have no numeric default and carry 0. "Radius" is the one row
    // Smoke_Create never assigns (smoke.c:212-247 sets origin but not radius),
    // so its 0.0 here is this port's choice and not an engine value -- the
    // ParamTable's own default template is an uninitialised stack `Smoke s`
    // (smoke.c:127, :171).
    static const SmokeParamDesc kTable[kSmokeParamCount] = {
        // name                        engine field   type         n  default            range?  min          max
        {"Position",                   "origin",      "ObjectPos", 3, 0.0f,               false, 0.0f,         0.0f},
        {"Alpha",                      "pitch",       "Angle",     1, 0.0f,               true,  0.0f,         kPiF},
        {"Gamma",                      "yaw",         "Angle",     1, 0.0f,               true,  -kPiF,        kPiF},
        {"Number of Particles",        "count",       "Int32",     1, 100.0f,             true,  1.0f,         255.0f},
        {"Radius",                     "radius",      "Real32",    1, 0.0f,               true,  0.0f,         5.0f},
        {"Maximum Random Angle",       "spread",      "Angle",     1, 0x1.921fb6p-1f,    false, 0.0f,         0.0f},
        {"Minimum Velocity",           "speedMax",    "Real32",    1, 4.5f,               true,  0.0f,         14.6484375f},
        {"Maximum Velocity",           "speedMin",    "Real32",    1, 6.0f,               true,  0.0f,         14.6484375f},
        {"Colour",                     "unk104",      "RGB",       3, 2.0f,               false, 0.0f,         0.0f},
        {"Life Time",                  "fadeDelay",   "Real32",    1, 0.5f,               true,  0.0f,         4.0f},
        {"Fade Time",                  "fadeTime",    "Real32",    1, 1.0f,               true,  0.0f,         4.0f},
        {"Fade Mode",                  "blend",       "Int32",     1, 0.0f,               true,  0.0f,         2.0f},
        {"Sprite index",               "sprite",      "Int32",     1, 3.0f,               true,  0.0f,         3.0f},
        {"Particle Size",              "size",        "Real32",    1, 0.5f,               true,  0.0f,         5.0f},
        {"Particle Size Delta",        "grow",        "Real32",    1, 0x1.555556p+1f,    true,  -150.0f,      150.0f},
        {"Minimum Rotation Speed",     "unk120",      "Angle",     1, 0.0f,               false, 0.0f,         0.0f},
        {"Maximum Rotation Speed",     "unk11C",      "Angle",     1, 0.0f,               false, 0.0f,         0.0f},
        {"Intensity",                  "level",       "Real32",    1, 1.0f,               true,  0.0f,         1.0f},
        {"Initial generate factor value", "rate",     "Real64",    1, 30.0f,              true,  0.0f,         150.0f},
        {"Generate factor",            "expr",        "VarString", 1, 0.0f,               false, 0.0f,         0.0f},
        {"Move Particles",             "enabled",     "bool8",     1, 0.0f,               false, 0.0f,         0.0f},
    };
    return kTable;
}

// ---------------------------------------------------------------------------
// Parsing one authored call
// ---------------------------------------------------------------------------

bool ParseSmokeParams(const std::vector<std::string>& arg_tokens, SmokeParams* out) {
    if (out == nullptr) return false;

    // The cursor advance is the engine's: ParamCursor walks the declared
    // parameters and each one consumes its type's argument count
    // (paramops.c:139-171), which is 3 for ObjectPos (via Real64x3, alias table
    // paramops.c:182) and 3 for RGB (via Real32x3, paramops.c:180).
    if (arg_tokens.size() < kSmokeFirstParam + static_cast<std::size_t>(kSmokeTokenCount)) {
        return false;
    }

    SmokeParams parsed;  // starts at the Smoke_Create defaults
    const SmokeParamDesc* table = SmokeParamTable();
    std::size_t cursor = kSmokeFirstParam;

    for (int i = 0; i < kSmokeParamCount; ++i) {
        const std::size_t base = cursor;
        switch (i) {
            case kSmokeOrigin:
                // ObjectPos is Real64x3: three doubles (paramops.c:182).
                parsed.origin[0] = TokenDouble(arg_tokens, base + 0);
                parsed.origin[1] = TokenDouble(arg_tokens, base + 1);
                parsed.origin[2] = TokenDouble(arg_tokens, base + 2);
                break;
            case kSmokeColour:
                // RGB is Real32x3: three floats (paramops.c:180).
                parsed.colour[0] = TokenFloat(arg_tokens, base + 0);
                parsed.colour[1] = TokenFloat(arg_tokens, base + 1);
                parsed.colour[2] = TokenFloat(arg_tokens, base + 2);
                break;
            case kSmokeAlpha:            parsed.alpha = TokenFloat(arg_tokens, base); break;
            case kSmokeGamma:            parsed.gamma = TokenFloat(arg_tokens, base); break;
            case kSmokeCount:            parsed.count = TokenInt(arg_tokens, base); break;
            case kSmokeRadius:           parsed.radius = TokenFloat(arg_tokens, base); break;
            case kSmokeSpread:           parsed.spread = TokenFloat(arg_tokens, base); break;
            case kSmokeMinVelocity:      parsed.min_velocity = TokenFloat(arg_tokens, base); break;
            case kSmokeMaxVelocity:      parsed.max_velocity = TokenFloat(arg_tokens, base); break;
            case kSmokeLifeTime:         parsed.life_time = TokenFloat(arg_tokens, base); break;
            case kSmokeFadeTime:         parsed.fade_time = TokenFloat(arg_tokens, base); break;
            case kSmokeFadeMode:         parsed.fade_mode = TokenInt(arg_tokens, base); break;
            case kSmokeSpriteIndex:      parsed.sprite_index = TokenInt(arg_tokens, base); break;
            case kSmokeParticleSize:     parsed.particle_size = TokenFloat(arg_tokens, base); break;
            case kSmokeParticleSizeDelta: parsed.particle_size_delta = TokenFloat(arg_tokens, base); break;
            case kSmokeMinRotation:      parsed.min_rotation = TokenFloat(arg_tokens, base); break;
            case kSmokeMaxRotation:      parsed.max_rotation = TokenFloat(arg_tokens, base); break;
            case kSmokeIntensity:        parsed.intensity = TokenFloat(arg_tokens, base); break;
            case kSmokeGenerateFactor:   parsed.generate_factor = TokenDouble(arg_tokens, base); break;
            case kSmokeGenerateExpr:
                parsed.generate_expr = arg_tokens[base];
                break;
            case kSmokeMoveParticles:    parsed.move_particles = TokenBool(arg_tokens, base); break;
            default: break;
        }
        cursor += static_cast<std::size_t>(table[i].token_count);
    }

    *out = parsed;
    return true;
}

std::vector<SmokeParams> ResolveSmokeTasks(const std::vector<SmokeTaskObject>& objects) {
    std::vector<SmokeParams> out;
    for (const SmokeTaskObject& object : objects) {
        if (object.type != "Smoke") continue;
        SmokeParams params;
        if (ParseSmokeParams(object.arg_tokens, &params)) out.push_back(params);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Random (src/math/random.c)
// ---------------------------------------------------------------------------

void SmokeRandom::Reset() {
    // Random_Init 0x4B4720 (random.c:5-9).
    a_ = 0xC8FB1A07u;
    b_ = 0x39EA7756u;
    c_ = 0x7FF13CCDu;
}

std::uint32_t SmokeRandom::Next() {
    // Random_Next 0x4B4740 (random.c:12-18). Unsigned throughout: `a >> 7` is a
    // logical shift and `a += b` wraps modulo 2^32.
    c_ ^= a_ >> 7;
    b_ ^= c_;
    a_ += b_;
    return a_;
}

long double SmokeRandom::Float() {
    // Random_Float 0x4B4770 (random.c:21-24). The low 16 bits scaled by 1/65536,
    // so the result is uniform on {0, 1/65536, ..., 65535/65536} -- never 1.
    return static_cast<long double>(Next() & 0xFFFFu) * (1.0L / 65536.0L);
}

void SmokeRandom::Seed(std::uint32_t seed) {
    // Random_Seed 0x4B47E0 (random.c:34-40). The shifts are on the unsigned
    // seed, so `seed << 15` and `seed << 9` wrap modulo 2^32 before the
    // add/subtract -- reproduced exactly.
    Reset();
    a_ ^= seed;
    b_ += seed << 15;
    c_ -= seed << 9;
}

// ---------------------------------------------------------------------------
// The per-tick rule
// ---------------------------------------------------------------------------

int SmokeRoundInt(long double value) {
    // fxobj.h:53-58 is `fistpll` with the x87 control word's rounding mode, which
    // the engine never changes from the default round-to-nearest-even. The
    // "integer indefinite" the x87 produces for NaN or out-of-range input is
    // 0x8000000000000000; the engine's `return (int)r` truncates that to 0.
    if (std::isnan(value)) return 0;
    if (!(value > -9.223372036854775808e18L && value < 9.223372036854775808e18L)) return 0;
    const long long rounded = static_cast<long long>(std::nearbyint(value));
    // The engine's cast to int truncates the low 32 bits, which for in-range
    // values is the identity and for huge ones wraps the same way.
    return static_cast<int>(rounded);
}

int SmokeAdvanceAccumulator(double rate, float& acc) {
    // smoke.c:316-318. `acc = t->rate + t->acc` is a double addition
    // (double + float -> double), widened to the long double local; the store
    // back into the float field truncates.
    long double a = static_cast<long double>(rate + static_cast<double>(acc));
    acc = static_cast<float>(a);

    int emitted = 0;
    // smoke.c:319 `while (acc > kRate)`: the comparison is long double against
    // the float 30.0f, and it is strictly greater, so a tick whose running total
    // is exactly 30.0 emits nothing -- which is why the first tick of a rate of
    // 30 is empty and every later one emits the carried 30 plus 30 again.
    while (a > static_cast<long double>(kSmokeTickRate)) {
        ++emitted;
        // smoke.c:335-336: `acc = t->acc - kRate` is FLOAT arithmetic, so the
        // running remainder is re-derived from the truncated stored value.
        a = static_cast<long double>(static_cast<float>(acc - kSmokeTickRate));
        acc = static_cast<float>(a);
    }
    return emitted;
}

int SmokeDelayFrames(const SmokeParams& params) {
    // smoke.c:332: `round_int(t->fadeDelay * kRate)`. fadeDelay is the authored
    // "Life Time" (smoke.c:176).
    return SmokeRoundInt(static_cast<long double>(params.life_time * kSmokeTickRate));
}

int SmokeFadeFrames(const SmokeParams& params) {
    // smoke.c:331: `round_int(t->fadeTime * kRate / c)` with c = t->level. Both
    // the multiply and the divide are float (smoke.c is compiled with
    // excess-precision=standard, smoke.c:1). fadeTime is the authored "Fade
    // Time" (smoke.c:177).
    const float scaled = params.fade_time * kSmokeTickRate;
    return SmokeRoundInt(static_cast<long double>(scaled / params.intensity));
}

void SmokeEmitParticles(const SmokeParams& params, const double emitter_pos[3], int count,
                        SmokeRandom& rng, SmokeParticle* out) {
    if (out == nullptr || count <= 0) return;

    for (int i = 0; i < count; ++i) {
        // smoke.c:320-330, in the engine's draw order: angle, radius, fraction.
        // Each is a long double expression stored into a float local, so the
        // float rounding happens before the trigonometry.
        const long double angle =
            static_cast<float>(rng.Float() * static_cast<long double>(kSmokeTwoPi));
        const long double radius = static_cast<float>(rng.Float() *
                                                     static_cast<long double>(params.radius) *
                                                     static_cast<long double>(kSmokeUnit));
        const long double fraction = rng.Float();
        // smoke.c:323: (unk11C - unk120) is a FLOAT subtraction, then the
        // interpolation is long double.
        const float rotation_span = params.max_rotation - params.min_rotation;
        const long double rotation =
            static_cast<long double>(rotation_span) * fraction +
            static_cast<long double>(params.min_rotation);
        const float level = params.intensity;

        SmokeParticle& particle = out[i];
        // smoke.c:328-330: the disc is in the emitter's XY plane; Z is copied
        // unmodified. x87 fcos/fsin are approximated here by libm (named
        // divergence, smoke.h header comment).
        particle.pos[2] = emitter_pos[2];
        particle.pos[0] = static_cast<double>(std::cos(angle) * radius +
                                              static_cast<long double>(emitter_pos[0]));
        particle.pos[1] = static_cast<double>(std::sin(angle) * radius +
                                              static_cast<long double>(emitter_pos[1]));
        particle.delay_frames = SmokeDelayFrames(params);
        particle.fade_frames = SmokeFadeFrames(params);
        particle.level = level;
        particle.spin = static_cast<float>(rotation * static_cast<long double>(kSmokePerFrame));
        particle.scale = kSmokeOne;
    }
}

void SmokeEmitSpeedArguments(const SmokeParams& params, float* a, float* b) {
    // smoke.c:333: `t->speedMin * kSpeed, t->speedMax * kSpeed`. Slot 7 writes
    // speedMin (smoke.c:174) and holds the authored "Maximum Velocity"; slot 6
    // writes speedMax (smoke.c:173) and holds the authored "Minimum Velocity".
    if (a != nullptr) *a = params.max_velocity * kSmokeSpeedScale;
    if (b != nullptr) *b = params.min_velocity * kSmokeSpeedScale;
}

float SmokeSampleSpeed(float a, float b, long double fraction) {
    // particle.c:616: `speed = (speedMax - speedMin) * Random_Float + speedMin`,
    // i.e. (b - a) * fraction + a in the callee's own parameter names.
    return static_cast<float>((static_cast<long double>(b) - static_cast<long double>(a)) * fraction +
                              static_cast<long double>(a));
}

// ---------------------------------------------------------------------------
// Particle2D_Emit's direction (particle.c:569-633)
// ---------------------------------------------------------------------------

namespace {

// particle.c:580 `NONZERO(x)` is `x < 0 || x > 0` (graphtask.h:34), i.e. false
// for zero AND for NaN.
bool NonZero(float x) { return x < 0.0f || x > 0.0f; }

// `inv = 1 / x87_sqrt(...)` with the products in long double, stored back into
// the float components one at a time (particle.c:581-585, :600-606, :617-623).
// x87_sqrt is the hardware square root; std::sqrt of a long double is the same
// value for every input this path sees (named divergence, smoke.h header).
void Normalize3(float v[3]) {
    if (!NonZero(v[0]) && !NonZero(v[1]) && !NonZero(v[2])) return;
    const long double inv =
        1.0L / std::sqrt(static_cast<long double>(v[0]) * v[0] + static_cast<long double>(v[1]) * v[1] +
                         static_cast<long double>(v[2]) * v[2]);
    for (int i = 0; i < 3; ++i) v[i] = static_cast<float>(v[i] * inv);
}

}  // namespace

SmokeConeBasis SmokeConeFrame(const float dir[3]) {
    SmokeConeBasis frame;
    float u[3] = {dir ? dir[0] : 0.0f, dir ? dir[1] : 0.0f, dir ? dir[2] : 1.0f};
    float v[3];
    float a, b;

    // particle.c:577-586.
    Normalize3(u);
    // particle.c:587-593: the branch tests the FIRST component against
    // kAxisLimit = 0.75 (particle.c:81).
    if (std::fabs(static_cast<double>(u[0])) > 0.75) {
        a = 1.0f;
        b = 0.0f;
    } else {
        a = 0.0f;
        b = 1.0f;
    }
    // particle.c:594-599.
    v[0] = a * u[2];
    v[1] = -(b * u[2]);
    v[2] = b * u[1] - a * u[0];
    frame.w[0] = v[0];
    frame.w[1] = v[1];
    frame.w[2] = v[2];
    Normalize3(frame.w);  // particle.c:600-606
    // particle.c:607-609: `c = w x u`.
    frame.c[0] = static_cast<float>(static_cast<long double>(frame.w[1]) * u[2] -
                                    static_cast<long double>(frame.w[2]) * u[1]);
    frame.c[1] = static_cast<float>(-(static_cast<long double>(frame.w[0]) * u[2] -
                                      static_cast<long double>(frame.w[2]) * u[0]));
    frame.c[2] = static_cast<float>(static_cast<long double>(frame.w[0]) * u[1] -
                                    static_cast<long double>(frame.w[1]) * u[0]);
    for (int i = 0; i < 3; ++i) frame.u[i] = u[i];
    return frame;
}

void SmokeEmitDirection(const float dir[3], float spread, long double tilt_fraction,
                        long double azimuth_fraction, float out_dir[3]) {
    if (out_dir == nullptr) return;

    const SmokeConeBasis frame = SmokeConeFrame(dir);
    // particle.c:610-612. `tilt = Random_Float * spread` is a long double
    // expression stored into the float `tilt`; `ax = spread * kHalf - tilt` is
    // float arithmetic.
    const float tilt = static_cast<float>(tilt_fraction * static_cast<long double>(spread));
    // particle.c:611 `az = kPi.f - Random_Float * kTwoPi.f`; kPiF here is the
    // file's own float pi (smoke.c:150-151 defines the same value for the
    // ranges) and kSmokeTwoPi is smoke.c:94's kTwoPi.
    const float az = static_cast<float>(static_cast<long double>(kPiF) -
                                        azimuth_fraction * static_cast<long double>(kSmokeTwoPi));
    const float ax = spread * 0.5f - tilt;

    // particle.c:613 `Matrix_SetEuler(&m, ax, 0, az)` then particle.c:614-615
    // `d[i] = m.m[2]*c[i] + m.m[5]*w[i] + m.m[8]*u[i]`. Column 2 of
    // RotZ(az)*RotX(ax) is (sin az sin ax, -cos az sin ax, cos ax) with the
    // FLOAT-rounded sin/cos the matrix builders store (matrix.c:112-127,
    // :148-164).
    const float sx = static_cast<float>(std::sin(static_cast<long double>(ax)));
    const float cx = static_cast<float>(std::cos(static_cast<long double>(ax)));
    const float sz = static_cast<float>(std::sin(static_cast<long double>(az)));
    const float cz = static_cast<float>(std::cos(static_cast<long double>(az)));
    const float col2[3] = {
        static_cast<float>(static_cast<long double>(sz) * sx),
        static_cast<float>(-(static_cast<long double>(cz) * sx)),
        cx,
    };

    float d[3];
    for (int i = 0; i < 3; ++i) {
        d[i] = static_cast<float>(static_cast<long double>(col2[0]) * frame.c[i] +
                                  static_cast<long double>(col2[1]) * frame.w[i] +
                                  static_cast<long double>(col2[2]) * frame.u[i]);
    }
    // particle.c:617-623: `e = d * inv`, component by component into a float
    // array (the same shape as Normalize3, but it is spelled out in the engine
    // because `ez` is kept separately).
    if (NonZero(d[0]) || NonZero(d[1]) || NonZero(d[2])) {
        const long double inv =
            1.0L / std::sqrt(static_cast<long double>(d[0]) * d[0] + static_cast<long double>(d[1]) * d[1] +
                             static_cast<long double>(d[2]) * d[2]);
        for (int i = 0; i < 3; ++i) out_dir[i] = static_cast<float>(d[i] * inv);
    } else {
        for (int i = 0; i < 3; ++i) out_dir[i] = d[i];
    }
}

void SmokeEmitVelocity(const float dir[3], float spread, float speed_a, float speed_b,
                       long double tilt_fraction, long double azimuth_fraction,
                       long double speed_fraction, float out_vel[3]) {
    if (out_vel == nullptr) return;
    float unit[3];
    SmokeEmitDirection(dir, spread, tilt_fraction, azimuth_fraction, unit);
    // particle.c:616 then :629-631: `speed = (b - a)*Random + a`, `vel = e*speed`.
    const float speed = SmokeSampleSpeed(speed_a, speed_b, speed_fraction);
    for (int i = 0; i < 3; ++i)
        out_vel[i] = static_cast<float>(static_cast<long double>(unit[i]) * speed);
}

bool SmokeParticleTick(SmokeParticleState& p, const float gravity[3], float grow, int frame_delay,
                       int animated_frames) {
    // particle.c:280-285.
    if (gravity != nullptr) {
        for (int i = 0; i < 3; ++i) p.vel[i] = p.vel[i] + gravity[i];
    }
    for (int i = 0; i < 3; ++i) p.pos[i] = static_cast<double>(p.vel[i]) + p.pos[i];
    // particle.c:286-287.
    p.angle = p.spin + p.angle;
    p.scale = grow + p.scale;
    // particle.c:288-292: only an animated sprite advances its frame.
    if (animated_frames > 0 && ++p.frame_timer >= frame_delay) {
        p.frame_timer = 0;
        if (p.frame < animated_frames - 1) ++p.frame;
    }
    // particle.c:293-304.
    if (p.delay != 0) {
        p.delay = static_cast<short>(p.delay - 1);  // Particle2D.delay is int16
        return true;
    }
    const long double level = static_cast<long double>(p.level) - static_cast<long double>(p.fade);
    p.level = static_cast<float>(level);
    // particle.c:300 `if (level > kZero.f && p->scale > kZero.f)`: both
    // comparisons are false for NaN, so a NaN level removes the particle.
    return (level > 0.0L) && (p.scale > 0.0f);
}

// ---------------------------------------------------------------------------
// The emitter's transform
// ---------------------------------------------------------------------------

void SmokeDirection(float alpha, float gamma, float out_dir[3]) {
    // Smoke_Changed: rz = RotZ(gamma); rx = rz * RotX(alpha); dir = column 2 of
    // rx (smoke.c:361-365). Column 2 of RotZ*RotX is RotZ applied to
    // (0, -sin(alpha), cos(alpha)), which expands to
    //   ( sin(gamma)sin(alpha), -cos(gamma)sin(alpha), cos(alpha) ).
    // At alpha == gamma == 0 that is (0, 0, 1): straight up.
    if (out_dir == nullptr) return;
    const long double sin_gamma = std::sin(static_cast<long double>(gamma));
    const long double cos_gamma = std::cos(static_cast<long double>(gamma));
    const long double sin_alpha = std::sin(static_cast<long double>(alpha));
    const long double cos_alpha = std::cos(static_cast<long double>(alpha));

    out_dir[0] = static_cast<float>(sin_gamma * sin_alpha);
    out_dir[1] = static_cast<float>(-cos_gamma * sin_alpha);
    out_dir[2] = static_cast<float>(cos_alpha);
}

std::uint32_t SmokeBlendFlags(int fade_mode) {
    // smoke.c:366-379. The `default` arm passes whatever the stack held; this
    // port returns 0 for it (named divergence). Particle2D_New ORs 0x40000 into
    // the value before storing it (particle.c:515), so the stored flags are
    // 0x10C0000 / 0x1040008 / 0x1040010 / 0x40000.
    switch (fade_mode) {
        case 0: return 0x1080000u;
        case 1: return 0x1000008u;
        case 2: return 0x1000010u;
        default: return 0u;
    }
}

const char* SmokeSpriteFile(int sprite_index) {
    // smoke.c:189-192 load the four sprites; smoke.c:380-393 select them.
    switch (sprite_index) {
        case 0: return "fireball.spr";
        case 1: return "blood1.spr";
        case 2: return "smoke2.spr";
        default: return "smoke1.spr";
    }
}

float SmokeGroupSize(const SmokeParams& params) {
    // smoke.c:394: `size[0] = size[1] = t->size * kUnit`.
    return params.particle_size * kSmokeUnit;
}

void SmokeGroupColour(const SmokeParams& params, float out_colour[3]) {
    // smoke.c:398-400: `scale = unk104 * kScale`.
    if (out_colour == nullptr) return;
    for (int i = 0; i < 3; ++i) out_colour[i] = params.colour[i] * kSmokeColourScale;
}

float SmokeGroupGrow(const SmokeParams& params) {
    // smoke.c:403: Particle2D_New's `grow` argument is `t->grow * kPerFrame`.
    return params.particle_size_delta * kSmokePerFrame;
}

void SmokeMove(const double anchor_pos[3], const float matrix[9], const double local[3],
               double out_pos[3]) {
    // smoke.c:202-208: v = M * local, then pos = anchor_pos + v. The component
    // order is the engine's (z, y, x), which is what makes the multiply
    // row-major against the engine's matrix layout.
    if (anchor_pos == nullptr || matrix == nullptr || local == nullptr || out_pos == nullptr) return;
    double v[3];
    v[0] = matrix[2] * local[2] + matrix[1] * local[1] + matrix[0] * local[0];
    v[1] = matrix[5] * local[2] + matrix[4] * local[1] + matrix[3] * local[0];
    v[2] = matrix[8] * local[2] + matrix[7] * local[1] + matrix[6] * local[0];
    out_pos[0] = anchor_pos[0] + v[0];
    out_pos[1] = anchor_pos[1] + v[1];
    out_pos[2] = anchor_pos[2] + v[2];
}

void SmokeUpdateLocal(const double origin[3], const double parent_pos[3], const float parent_matrix[9],
                      bool has_parent, double out_local[3]) {
    // smoke.c:413-439. With no moving ancestor the offset is zero and the local
    // matrix is the identity (smoke.c:420-428); this port reports the zero
    // offset, which is the part the emitter rule consumes.
    if (origin == nullptr || out_local == nullptr) return;
    if (!has_parent || parent_pos == nullptr || parent_matrix == nullptr) {
        out_local[0] = 0.0;
        out_local[1] = 0.0;
        out_local[2] = 0.0;
        return;
    }

    const long double z = static_cast<long double>(origin[2]) -
                          static_cast<long double>(parent_pos[2]);
    const double lx = origin[0] - parent_pos[0];
    const double ly = origin[1] - parent_pos[1];
    // The rotation reads the UNROTATED x/y components (smoke.c:434-436), which
    // is what the original does: `local.y` and `local.x` are assigned first and
    // only then overwritten by v.
    out_local[0] = parent_matrix[6] * z + parent_matrix[3] * ly + parent_matrix[0] * lx;
    out_local[1] = parent_matrix[7] * z + parent_matrix[4] * ly + parent_matrix[1] * lx;
    out_local[2] = parent_matrix[8] * z + parent_matrix[5] * ly + parent_matrix[2] * lx;
}

bool SmokeTranslateDelta(bool move_particles, bool moved_once, const double pos[3],
                         const double last_pos[3], double out_delta[3]) {
    // smoke.c:303-312. `t->moved && t->enabled` gates only Particle2D_Translate;
    // the emission loop below it is not gated by `enabled` at all.
    if (!(moved_once && move_particles)) return false;
    if (pos == nullptr || last_pos == nullptr || out_delta == nullptr) return false;
    for (int i = 0; i < 3; ++i) out_delta[i] = pos[i] - last_pos[i];
    return true;
}

}  // namespace igi
