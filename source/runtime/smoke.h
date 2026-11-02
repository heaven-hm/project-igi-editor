// smoke.h - Smoke particle emitter data model and simulation rules.
//
// MIT License.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace igi {

// ---------------------------------------------------------------------------
// Particle system constants
// ---------------------------------------------------------------------------

constexpr float kSmokeTwoPi = 0x1.921fb6p+2f;
constexpr float kSmokeUnit = 4096.0f;
constexpr float kSmokeTickRate = 30.0f;
constexpr float kSmokePerFrame = 0x1.111112p-5f;
constexpr float kSmokeSpeedScale = 136.0f;
constexpr float kSmokeColourScale = 63.0f;
constexpr float kSmokeOne = 1.0f;
constexpr float kSmokeParticleLife = 409600.0f;

constexpr int kSmokeParamCount = 21;
constexpr int kSmokeTokenCount = 25;
constexpr float kSmokeSteadyStateDivisor = 30.0f;
constexpr std::size_t kSmokeFirstParam = 3;

// The 21 authored parameter slots.
enum SmokeParamIndex {
    kSmokeOrigin = 0,              // "Position"                 ObjectPos
    kSmokeAlpha = 1,               // "Alpha"                    Angle
    kSmokeGamma = 2,               // "Gamma"                    Angle
    kSmokeCount = 3,               // "Number of Particles"      Int32
    kSmokeRadius = 4,              // "Radius"                   Real32
    kSmokeSpread = 5,              // "Maximum Random Angle"     Angle
    kSmokeMinVelocity = 6,         // "Minimum Velocity"         Real32
    kSmokeMaxVelocity = 7,         // "Maximum Velocity"         Real32
    kSmokeColour = 8,              // "Colour"                   RGB
    kSmokeLifeTime = 9,            // "Life Time"                Real32
    kSmokeFadeTime = 10,           // "Fade Time"                Real32
    kSmokeFadeMode = 11,           // "Fade Mode"                Int32
    kSmokeSpriteIndex = 12,        // "Sprite index"             Int32
    kSmokeParticleSize = 13,       // "Particle Size"            Real32
    kSmokeParticleSizeDelta = 14,  // "Particle Size Delta"      Real32
    kSmokeMinRotation = 15,        // "Minimum Rotation Speed"   Angle
    kSmokeMaxRotation = 16,        // "Maximum Rotation Speed"   Angle
    kSmokeIntensity = 17,          // "Intensity"                Real32
    kSmokeGenerateFactor = 18,     // "Initial generate factor value" Real64
    kSmokeGenerateExpr = 19,       // "Generate factor"          VarString
    kSmokeMoveParticles = 20,      // "Move Particles"           bool8
};

// Parameter metadata descriptor for authored smoke emitter attributes.
struct SmokeParamDesc {
    const char* authored_name;  // Declaration string
    const char* engine_field;   // Bound field name
    const char* type_name;      // Script type specification
    int token_count;            // Tokens consumed
    float default_value;        // Default numerical value
    bool has_range;             // Whether a valid range is defined
    float range_min;
    float range_max;
};

const SmokeParamDesc* SmokeParamTable();

// ---------------------------------------------------------------------------
// Authored task representation
// ---------------------------------------------------------------------------

struct SmokeTaskObject {
    std::string type;
    std::vector<std::string> arg_tokens;
};

struct SmokeParams {
    double origin[3] = {0.0, 0.0, 0.0};
    float alpha = 0.0f;                   // pitch, radians
    float gamma = 0.0f;                   // yaw, radians
    int count = 100;
    float radius = 0.0f;
    float spread = 0x1.921fb6p-1f;        // pi/4, radians
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
};

bool ParseSmokeParams(const std::vector<std::string>& arg_tokens, SmokeParams* out);
std::vector<SmokeParams> ResolveSmokeTasks(const std::vector<SmokeTaskObject>& objects);

// ---------------------------------------------------------------------------
// Particle system pseudo-random number generator
// ---------------------------------------------------------------------------

class SmokeRandom {
public:
    SmokeRandom() { Reset(); }

    void Reset();
    std::uint32_t Next();
    long double Float();
    void Seed(std::uint32_t seed);

private:
    std::uint32_t a_ = 0;
    std::uint32_t b_ = 0;
    std::uint32_t c_ = 0;
};

// ---------------------------------------------------------------------------
// Per-tick emission and particle simulation
// ---------------------------------------------------------------------------

int SmokeRoundInt(long double value);
int SmokeAdvanceAccumulator(double rate, float& acc);

struct SmokeParticle {
    double pos[3] = {0.0, 0.0, 0.0};
    float spin = 0.0f;
    float level = 0.0f;
    float scale = 1.0f;
    int delay_frames = 0;
    int fade_frames = 0;
};

void SmokeEmitParticles(const SmokeParams& params, const double emitter_pos[3], int count,
                        SmokeRandom& rng, SmokeParticle* out);

void SmokeEmitSpeedArguments(const SmokeParams& params, float* a, float* b);
float SmokeSampleSpeed(float a, float b, long double fraction);

// ---------------------------------------------------------------------------
// Emission cone orientation and kinematics
// ---------------------------------------------------------------------------

struct SmokeConeBasis {
    float u[3] = {0.0f, 0.0f, 1.0f};  // Normalized axis
    float w[3] = {0.0f, -1.0f, 0.0f};
    float c[3] = {-1.0f, 0.0f, 0.0f}; // Cross product w x u
};

// Computes an orthonormal basis frame around the emission axis.
SmokeConeBasis SmokeConeFrame(const float dir[3]);

// Evaluates the unit direction vector perturbed within the emission cone.
void SmokeEmitDirection(const float dir[3], float spread, long double tilt_fraction,
                        long double azimuth_fraction, float out_dir[3]);

// Evaluates initial particle velocity vector.
void SmokeEmitVelocity(const float dir[3], float spread, float speed_a, float speed_b,
                       long double tilt_fraction, long double azimuth_fraction,
                       long double speed_fraction, float out_vel[3]);

struct SmokeParticleState {
    double pos[3] = {0.0, 0.0, 0.0};
    float vel[3] = {0.0f, 0.0f, 0.0f};
    float spin = 0.0f;
    float level = 0.0f;
    float fade = 0.0f;
    float angle = 0.0f;
    float scale = 1.0f;
    int delay = 0;
    int frame = 0;
    int frame_timer = 0;
};

bool SmokeParticleTick(SmokeParticleState& p, const float gravity[3], float grow, int frame_delay,
                       int last_frame);

int SmokeDelayFrames(const SmokeParams& params);
int SmokeFadeFrames(const SmokeParams& params);

// ---------------------------------------------------------------------------
// Emitter spatial transform and rendering attributes
// ---------------------------------------------------------------------------

void SmokeDirection(float alpha, float gamma, float out_dir[3]);
std::uint32_t SmokeBlendFlags(int fade_mode);
const char* SmokeSpriteFile(int sprite_index);

float SmokeGroupSize(const SmokeParams& params);
void SmokeGroupColour(const SmokeParams& params, float out_colour[3]);
void SmokeParticleColour(const float group_colour[3], std::uint32_t flags, float level,
                         float out_rgba[4]);
float SmokeGroupGrow(const SmokeParams& params);

void SmokeMove(const double anchor_pos[3], const float matrix[9], const double local[3],
               double out_pos[3]);
void SmokeUpdateLocal(const double origin[3], const double parent_pos[3], const float parent_matrix[9],
                      bool has_parent, double out_local[3]);
bool SmokeTranslateDelta(bool move_particles, bool moved_once, const double pos[3],
                         const double last_pos[3], double out_delta[3]);

}  // namespace igi
