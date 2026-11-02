// smoke.cpp - Smoke particle emitter data model and simulation rules.
//
// MIT License.

#include "runtime/smoke.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace igi {
namespace {

// ---------------------------------------------------------------------------
// Parameter token readers
// ---------------------------------------------------------------------------

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

bool TokenBool(const std::vector<std::string>& tokens, std::size_t index, bool fallback = false) {
    if (index >= tokens.size()) return fallback;
    const std::string& token = tokens[index];
    if (token == "TRUE") return true;
    if (token == "FALSE") return false;
    return TokenInt(tokens, index, fallback ? 1 : 0) != 0;
}

constexpr float kPiF = 0x1.921fb6p+1f;

}  // namespace

// ---------------------------------------------------------------------------
// Parameter metadata table
// ---------------------------------------------------------------------------

const SmokeParamDesc* SmokeParamTable() {
    static const SmokeParamDesc kTable[kSmokeParamCount] = {
        {"Position", "origin", "ObjectPos", 3, 0.0f, false, 0.0f, 0.0f},
        {"Alpha", "pitch", "Angle", 1, 0.0f, true, 0.0f, kPiF},
        {"Gamma", "yaw", "Angle", 1, 0.0f, true, -kPiF, kPiF},
        {"Number of Particles", "count", "Int32", 1, 100.0f, true, 1.0f, 255.0f},
        {"Radius", "radius", "Real32", 1, 0.0f, true, 0.0f, 5.0f},
        {"Maximum Random Angle", "spread", "Angle", 1, 0x1.921fb6p-1f, false, 0.0f, 0.0f},
        {"Minimum Velocity", "speedMax", "Real32", 1, 4.5f, true, 0.0f, 14.6484375f},
        {"Maximum Velocity", "speedMin", "Real32", 1, 6.0f, true, 0.0f, 14.6484375f},
        {"Colour", "unk104", "RGB", 3, 2.0f, false, 0.0f, 0.0f},
        {"Life Time", "fadeDelay", "Real32", 1, 0.5f, true, 0.0f, 4.0f},
        {"Fade Time", "fadeTime", "Real32", 1, 1.0f, true, 0.0f, 4.0f},
        {"Fade Mode", "blend", "Int32", 1, 0.0f, true, 0.0f, 2.0f},
        {"Sprite index", "sprite", "Int32", 1, 3.0f, true, 0.0f, 3.0f},
        {"Particle Size", "size", "Real32", 1, 0.5f, true, 0.0f, 5.0f},
        {"Particle Size Delta", "grow", "Real32", 1, 0x1.555556p+1f, true, -150.0f, 150.0f},
        {"Minimum Rotation Speed", "unk120", "Angle", 1, 0.0f, false, 0.0f, 0.0f},
        {"Maximum Rotation Speed", "unk11C", "Angle", 1, 0.0f, false, 0.0f, 0.0f},
        {"Intensity", "level", "Real32", 1, 1.0f, true, 0.0f, 1.0f},
        {"Initial generate factor value", "rate", "Real64", 1, 30.0f, true, 0.0f, 150.0f},
        {"Generate factor", "expr", "VarString", 1, 0.0f, false, 0.0f, 0.0f},
        {"Move Particles", "enabled", "bool8", 1, 0.0f, false, 0.0f, 0.0f},
    };
    return kTable;
}

// ---------------------------------------------------------------------------
// Parameter parsing
// ---------------------------------------------------------------------------

bool ParseSmokeParams(const std::vector<std::string>& arg_tokens, SmokeParams* out) {
    if (out == nullptr) return false;

    if (arg_tokens.size() < kSmokeFirstParam + static_cast<std::size_t>(kSmokeTokenCount)) {
        return false;
    }

    SmokeParams parsed;
    const SmokeParamDesc* table = SmokeParamTable();
    std::size_t cursor = kSmokeFirstParam;

    for (int i = 0; i < kSmokeParamCount; ++i) {
        const std::size_t base = cursor;
        switch (i) {
            case kSmokeOrigin:
                parsed.origin[0] = TokenDouble(arg_tokens, base + 0);
                parsed.origin[1] = TokenDouble(arg_tokens, base + 1);
                parsed.origin[2] = TokenDouble(arg_tokens, base + 2);
                break;
            case kSmokeColour:
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
// Random number generator
// ---------------------------------------------------------------------------

void SmokeRandom::Reset() {
    a_ = 0xC8FB1A07u;
    b_ = 0x39EA7756u;
    c_ = 0x7FF13CCDu;
}

std::uint32_t SmokeRandom::Next() {
    c_ ^= a_ >> 7;
    b_ ^= c_;
    a_ += b_;
    return a_;
}

long double SmokeRandom::Float() {
    return static_cast<long double>(Next() & 0xFFFFu) * (1.0L / 65536.0L);
}

void SmokeRandom::Seed(std::uint32_t seed) {
    Reset();
    a_ ^= seed;
    b_ += seed << 15;
    c_ -= seed << 9;
}

// ---------------------------------------------------------------------------
// Per-tick emission and particle simulation
// ---------------------------------------------------------------------------

// 64-bit integer limits for floating-point rounding guard.
// In C/C++ notation:
// LONG_MIN = -9223372036854775808L
// LONG_MAX =  9223372036854775807L
constexpr long double kLongMin64 = -9223372036854775808.0L;
constexpr long double kLongMax64 =  9223372036854775807.0L;

int SmokeRoundInt(long double value) {
    if (std::isnan(value) || value > kLongMax64 || value < kLongMin64) return 0;
    const long long rounded_value = static_cast<long long>(std::nearbyint(value));
    return static_cast<int>(rounded_value);
}

int SmokeAdvanceAccumulator(double rate, float& acc) {
    long double a = static_cast<long double>(rate + static_cast<double>(acc));
    acc = static_cast<float>(a);

    int emitted = 0;
    while (a > static_cast<long double>(kSmokeTickRate)) {
        emitted++;
        a = static_cast<long double>(acc) - static_cast<long double>(kSmokeTickRate);
        acc = static_cast<float>(a);
    }
    return emitted;
}

int SmokeDelayFrames(const SmokeParams& params) {
    return SmokeRoundInt(static_cast<long double>(params.life_time) *
                         static_cast<long double>(kSmokeTickRate));
}

int SmokeFadeFrames(const SmokeParams& params) {
    const float level = params.intensity;
    return SmokeRoundInt(static_cast<long double>(params.fade_time) *
                         static_cast<long double>(kSmokeTickRate) /
                         static_cast<long double>(level));
}

void SmokeEmitParticles(const SmokeParams& params, const double emitter_pos[3], int count,
                        SmokeRandom& rng, SmokeParticle* out) {
    if (out == nullptr || count <= 0) return;

    for (int i = 0; i < count; ++i) {
        const long double angle =
            static_cast<float>(rng.Float() * static_cast<long double>(kSmokeTwoPi));
        const long double radius = static_cast<float>(rng.Float() *
                                                     static_cast<long double>(params.radius) *
                                                     static_cast<long double>(kSmokeUnit));
        const long double fraction = rng.Float();
        const float rotation_span = params.max_rotation - params.min_rotation;
        const long double rotation =
            static_cast<long double>(rotation_span) * fraction +
            static_cast<long double>(params.min_rotation);
        const float level = params.intensity;

        SmokeParticle& particle = out[i];
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

void SmokeEmitSpeedArguments(const SmokeParams& params, float* out_speed_min, float* out_speed_max) {
    if (out_speed_min) *out_speed_min = params.max_velocity * kSmokeSpeedScale;
    if (out_speed_max) *out_speed_max = params.min_velocity * kSmokeSpeedScale;
}

float SmokeSampleSpeed(float speed_min, float speed_max, long double fraction) {
    return static_cast<float>((static_cast<long double>(speed_max) - static_cast<long double>(speed_min)) * fraction +
                              static_cast<long double>(speed_min));
}

// ---------------------------------------------------------------------------
// Emission cone orientation and kinematics
// ---------------------------------------------------------------------------

namespace {

inline bool NonZero(float x) { return x < 0.0f || x > 0.0f; }

// Normalizes a 3D float vector using high-precision Euclidean norm.
void Normalize3(float vector3[3]) {
    if (!NonZero(vector3[0]) && !NonZero(vector3[1]) && !NonZero(vector3[2])) return;
    const long double inv_norm =
        1.0L / std::sqrt(static_cast<long double>(vector3[0]) * vector3[0] +
                         static_cast<long double>(vector3[1]) * vector3[1] +
                         static_cast<long double>(vector3[2]) * vector3[2]);
    for (int i = 0; i < 3; ++i) vector3[i] = static_cast<float>(vector3[i] * inv_norm);
}

}  // namespace

SmokeConeBasis SmokeConeFrame(const float direction[3]) {
    SmokeConeBasis frame;
    float axis_u[3] = {direction ? direction[0] : 0.0f,
                       direction ? direction[1] : 0.0f,
                       direction ? direction[2] : 1.0f};
    float temp_v[3];
    float weight_a, weight_b;

    Normalize3(axis_u);
    if (std::fabs(static_cast<double>(axis_u[0])) > 0.75) {
        weight_a = 1.0f;
        weight_b = 0.0f;
    } else {
        weight_a = 0.0f;
        weight_b = 1.0f;
    }

    temp_v[0] = weight_a * axis_u[2];
    temp_v[1] = -(weight_b * axis_u[2]);
    temp_v[2] = weight_b * axis_u[1] - weight_a * axis_u[0];
    frame.w[0] = temp_v[0];
    frame.w[1] = temp_v[1];
    frame.w[2] = temp_v[2];
    Normalize3(frame.w);

    frame.c[0] = static_cast<float>(static_cast<long double>(frame.w[1]) * axis_u[2] -
                                    static_cast<long double>(frame.w[2]) * axis_u[1]);
    frame.c[1] = static_cast<float>(-(static_cast<long double>(frame.w[0]) * axis_u[2] -
                                      static_cast<long double>(frame.w[2]) * axis_u[0]));
    frame.c[2] = static_cast<float>(static_cast<long double>(frame.w[0]) * axis_u[1] -
                                    static_cast<long double>(frame.w[1]) * axis_u[0]);
    for (int i = 0; i < 3; ++i) frame.u[i] = axis_u[i];
    return frame;
}

void SmokeEmitDirection(const float direction[3], float spread, long double tilt_fraction,
                        long double azimuth_fraction, float out_direction[3]) {
    if (out_direction == nullptr) return;

    const SmokeConeBasis frame = SmokeConeFrame(direction);
    const float tilt_angle = static_cast<float>(tilt_fraction * static_cast<long double>(spread));
    const float azimuth_angle = static_cast<float>(static_cast<long double>(kPiF) -
                                                  azimuth_fraction * static_cast<long double>(kSmokeTwoPi));
    const float pitch_angle = spread * 0.5f - tilt_angle;

    const float sin_pitch = static_cast<float>(std::sin(static_cast<long double>(pitch_angle)));
    const float cos_pitch = static_cast<float>(std::cos(static_cast<long double>(pitch_angle)));
    const float sin_azimuth = static_cast<float>(std::sin(static_cast<long double>(azimuth_angle)));
    const float cos_azimuth = static_cast<float>(std::cos(static_cast<long double>(azimuth_angle)));
    const float column2[3] = {
        static_cast<float>(static_cast<long double>(sin_azimuth) * sin_pitch),
        static_cast<float>(-(static_cast<long double>(cos_azimuth) * sin_pitch)),
        cos_pitch,
    };

    float perturbed_dir[3];
    for (int i = 0; i < 3; ++i) {
        perturbed_dir[i] = static_cast<float>(static_cast<long double>(column2[0]) * frame.c[i] +
                                             static_cast<long double>(column2[1]) * frame.w[i] +
                                             static_cast<long double>(column2[2]) * frame.u[i]);
    }
    if (NonZero(perturbed_dir[0]) || NonZero(perturbed_dir[1]) || NonZero(perturbed_dir[2])) {
        const long double inv_norm =
            1.0L / std::sqrt(static_cast<long double>(perturbed_dir[0]) * perturbed_dir[0] +
                             static_cast<long double>(perturbed_dir[1]) * perturbed_dir[1] +
                             static_cast<long double>(perturbed_dir[2]) * perturbed_dir[2]);
        for (int i = 0; i < 3; ++i) out_direction[i] = static_cast<float>(perturbed_dir[i] * inv_norm);
    } else {
        for (int i = 0; i < 3; ++i) out_direction[i] = perturbed_dir[i];
    }
}

void SmokeEmitVelocity(const float direction[3], float spread, float speed_min, float speed_max,
                       long double tilt_fraction, long double azimuth_fraction,
                       long double speed_fraction, float out_velocity[3]) {
    if (out_velocity == nullptr) return;
    float unit_dir[3];
    SmokeEmitDirection(direction, spread, tilt_fraction, azimuth_fraction, unit_dir);
    const float speed = SmokeSampleSpeed(speed_min, speed_max, speed_fraction);
    for (int i = 0; i < 3; ++i)
        out_velocity[i] = static_cast<float>(static_cast<long double>(unit_dir[i]) * speed);
}

bool SmokeParticleTick(SmokeParticleState& p, const float gravity[3], float grow, int frame_delay,
                       int last_frame) {
    if (gravity != nullptr) {
        for (int i = 0; i < 3; ++i) p.vel[i] = p.vel[i] + gravity[i];
    }
    for (int i = 0; i < 3; ++i) p.pos[i] = static_cast<double>(p.vel[i]) + p.pos[i];
    p.angle = p.spin + p.angle;
    p.scale = grow + p.scale;
    if (last_frame > 0 && ++p.frame_timer >= frame_delay) {
        p.frame_timer = 0;
        if (p.frame < last_frame) ++p.frame;
    }
    if (p.delay != 0) {
        p.delay = static_cast<short>(p.delay - 1);
        return true;
    }
    const long double level = static_cast<long double>(p.level) - static_cast<long double>(p.fade);
    p.level = static_cast<float>(level);
    return (level > 0.0L) && (p.scale > 0.0f);
}

// ---------------------------------------------------------------------------
// Emitter spatial transform and rendering attributes
// ---------------------------------------------------------------------------

void SmokeDirection(float alpha, float gamma, float out_dir[3]) {
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
    switch (fade_mode) {
        case 0: return 0x1080000u;
        case 1: return 0x1000008u;
        case 2: return 0x1000010u;
        default: return 0u;
    }
}

const char* SmokeSpriteFile(int sprite_index) {
    switch (sprite_index) {
        case 0: return "fireball.spr";
        case 1: return "blood1.spr";
        case 2: return "smoke2.spr";
        default: return "smoke1.spr";
    }
}

float SmokeGroupSize(const SmokeParams& params) {
    return params.particle_size * kSmokeUnit;
}

void SmokeGroupColour(const SmokeParams& params, float out_colour[3]) {
    if (out_colour == nullptr) return;
    for (int i = 0; i < 3; ++i) out_colour[i] = params.colour[i] * kSmokeColourScale;
}

void SmokeParticleColour(const float group_colour[3], std::uint32_t flags, float level,
                         float out_rgba[4]) {
    constexpr float kColorScale = 0.0078125f;
    constexpr std::uint32_t kLevelIsAlpha = 0x80000u;
    if (out_rgba == nullptr) return;

    out_rgba[0] = group_colour[0] * kColorScale;
    out_rgba[1] = group_colour[1] * kColorScale;
    out_rgba[2] = group_colour[2] * kColorScale;
    out_rgba[3] = 0.0f;
    if (flags == 0) return;
    if (flags & kLevelIsAlpha) {
        out_rgba[3] = level;
        return;
    }
    out_rgba[0] *= level;
    out_rgba[1] *= level;
    out_rgba[2] *= level;
    out_rgba[3] = 1.0f;
}

float SmokeGroupGrow(const SmokeParams& params) {
    return params.particle_size_delta * kSmokePerFrame;
}

void SmokeMove(const double anchor_pos[3], const float matrix[9], const double local[3],
               double out_pos[3]) {
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

    out_local[0] = parent_matrix[6] * z + parent_matrix[3] * ly + parent_matrix[0] * lx;
    out_local[1] = parent_matrix[7] * z + parent_matrix[4] * ly + parent_matrix[1] * lx;
    out_local[2] = parent_matrix[8] * z + parent_matrix[5] * ly + parent_matrix[2] * lx;
}

bool SmokeTranslateDelta(bool move_particles, bool moved_once, const double pos[3],
                         const double last_pos[3], double out_delta[3]) {
    if (!(moved_once && move_particles)) return false;
    if (pos == nullptr || last_pos == nullptr || out_delta == nullptr) return false;
    for (int i = 0; i < 3; ++i) out_delta[i] = pos[i] - last_pos[i];
    return true;
}

}  // namespace igi
