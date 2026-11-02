#include "runtime/heli_rules.h"

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

double TokenDouble(const std::vector<std::string>& tokens, std::size_t index,
                   double fallback = 0.0) {
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

std::string TokenString(const std::vector<std::string>& tokens, std::size_t index) {
    if (index >= tokens.size()) return {};
    return tokens[index];
}

long double X87Sin(long double angle) { return std::sin(angle); }
long double X87Cos(long double angle) { return std::cos(angle); }

void SetRotY(float out_matrix[9], float angle) {
    const long double sin_val = X87Sin(angle);
    const long double cos_val = X87Cos(angle);

    out_matrix[1] = 0.0f;
    out_matrix[3] = 0.0f;
    out_matrix[4] = 1.0f;
    out_matrix[5] = 0.0f;
    out_matrix[7] = 0.0f;
    out_matrix[0] = static_cast<float>(cos_val);
    out_matrix[2] = static_cast<float>(sin_val);
    out_matrix[6] = static_cast<float>(-sin_val);
    out_matrix[8] = static_cast<float>(cos_val);
}

bool IsZero(long double value) { return !(value < 0.0L) && !(value > 0.0L); }

}  // namespace

// ---------------------------------------------------------------------------
// The authored parameter table
// ---------------------------------------------------------------------------

const HeliParamDesc* HeliParamTable() {
    static const HeliParamDesc kTable[kHeliParamCount] = {
        {"Position",                        "v.startPos",       "ObjectPos", 3, false,  0.0f,   0.0f},
        {"Orientation",                     "v.startMatrix",    "Real32x9",  3, false,  0.0f,   0.0f},
        {"Original Thrust",                 "v.start_speed",    "Real32",    1, true,   0.0f,   2.0f},
        {"Speed",                           "v.startVel",       "Real32x3",  3, false,  0.0f,   0.0f},
        {"Model",                           "v.name",           "String16",  1, false,  0.0f,   0.0f},
        {"Collision detection",             "v.collide",        "bool8",     1, false,  0.0f,   0.0f},
        {"Force Z",                         "v.airborne",       "bool8",     1, false,  0.0f,   0.0f},
        {"Open door",                       "rotor_expr",       "VarString", 1, false,  0.0f,   0.0f},
        {"Can fire",                        "light_expr",       "VarString", 1, false,  0.0f,   0.0f},
        {"Play sound",                      "engine_expr",      "VarString", 1, false,  0.0f,   0.0f},
        {"ViewCone Alpha (degrees)",        "light_params[0]",  "Degrees",   1, false,  0.0f,   0.0f},
        {"ViewCone Gamma (degrees)",        "light_params[1]",  "Degrees",   1, false,  0.0f,   0.0f},
        {"ViewCone Length (meter)",         "light_params[2]",  "Real32",    1, true,   0.0f, 1000.0f},
    };
    return kTable;
}

const HeliControlInfo* HeliControlTable() {
    static const HeliControlInfo kTable[kHeliControlCount] = {
        {"dAlpha",    -360.0f, 360.0f},
        {"dBeta",     -360.0f, 360.0f},
        {"dGamma",    -360.0f, 360.0f},
        {"Thrust",       0.0f,   1.0f},
        {"PilotAnim",    0.0f,  16.0f},
    };
    return kTable;
}

// ---------------------------------------------------------------------------
// 3x3 rotation helpers
// ---------------------------------------------------------------------------

void HeliMatrixMul(float out[9], const float a[9], const float b[9], int last0, int last1,
                   int last2) {
    float temp_result[9];

    for (int row = 0; row < 3; ++row) {
        const int last = row == 0 ? last0 : (row == 1 ? last1 : last2);
        const int col0 = last == 0 ? 1 : 0;
        const int col1 = last == 2 ? 1 : 2;

        for (int col = 0; col < 3; ++col) {
            temp_result[row * 3 + col] = static_cast<float>(
                static_cast<long double>(a[row * 3 + col0]) * b[col0 * 3 + col] +
                static_cast<long double>(a[row * 3 + col1]) * b[col1 * 3 + col] +
                static_cast<long double>(a[row * 3 + last]) * b[last * 3 + col]);
        }
    }
    for (int idx = 0; idx < 9; ++idx) out[idx] = temp_result[idx];
}

void HeliSetRotX(float m[9], float a) {
    const long double sin_val = X87Sin(a);
    const float cos_val = static_cast<float>(X87Cos(a));

    m[0] = 1.0f;
    m[1] = 0.0f;
    m[2] = 0.0f;
    m[3] = 0.0f;
    m[6] = 0.0f;
    m[4] = cos_val;
    m[5] = static_cast<float>(-sin_val);
    m[8] = cos_val;
    m[7] = static_cast<float>(sin_val);
}

void HeliSetRotZ(float m[9], float a) {
    const long double sin_val = X87Sin(a);
    const float cos_val = static_cast<float>(X87Cos(a));

    m[2] = 0.0f;
    m[5] = 0.0f;
    m[6] = 0.0f;
    m[7] = 0.0f;
    m[8] = 1.0f;
    m[0] = cos_val;
    m[1] = static_cast<float>(-sin_val);
    m[4] = cos_val;
    m[3] = static_cast<float>(sin_val);
}

void HeliSetEuler(float m[9], float ax, float ay, float az) {
    float temp_rot[9];

    HeliSetRotZ(m, az);
    SetRotY(temp_rot, ay);
    HeliMatrixMul(m, m, temp_rot, 1, 1, 1);
    HeliSetRotX(temp_rot, ax);
    HeliMatrixMul(m, m, temp_rot, 1, 1, 1);
}

bool HeliFne(long double a, long double b) { return a < b || a > b; }

// ---------------------------------------------------------------------------
// The parsed authored form
// ---------------------------------------------------------------------------

bool ParseHeliParams(const std::vector<std::string>& tokens, HeliParams* out) {
    if (out == nullptr) return false;
    const std::size_t base = static_cast<std::size_t>(kHeliTokenPrefix);
    if (tokens.size() < base + static_cast<std::size_t>(kHeliTokenCount)) return false;

    // Parameter table order matches authored placement layout.
    out->position[0] = TokenDouble(tokens, base + 0);
    out->position[1] = TokenDouble(tokens, base + 1);
    out->position[2] = TokenDouble(tokens, base + 2);

    // Orientation is specified as three Euler angles which are converted
    // to a 3x3 orientation matrix.
    out->orientation[0] = TokenFloat(tokens, base + 3);
    out->orientation[1] = TokenFloat(tokens, base + 4);
    out->orientation[2] = TokenFloat(tokens, base + 5);
    HeliSetEuler(out->orientation_matrix, out->orientation[0], out->orientation[1],
                 out->orientation[2]);

    out->original_thrust = TokenFloat(tokens, base + 6);

    out->speed[0] = TokenFloat(tokens, base + 7);
    out->speed[1] = TokenFloat(tokens, base + 8);
    out->speed[2] = TokenFloat(tokens, base + 9);

    out->model = TokenString(tokens, base + 10);
    out->collision_detection = TokenBool(tokens, base + 11);
    out->force_z = TokenBool(tokens, base + 12);
    out->open_door = TokenString(tokens, base + 13);
    out->can_fire = TokenString(tokens, base + 14);
    out->play_sound = TokenString(tokens, base + 15);

    // Degrees values are stored verbatim without converting to radians here.
    out->viewcone_alpha = TokenFloat(tokens, base + 16);
    out->viewcone_gamma = TokenFloat(tokens, base + 17);
    out->viewcone_length = TokenFloat(tokens, base + 18);
    return true;
}

// ---------------------------------------------------------------------------
// Flight control operations
// ---------------------------------------------------------------------------

bool HeliControlThrustRequested(const float axes_in[5]) {
    uint32_t bits = 0;
    std::memcpy(&bits, &axes_in[3], sizeof(bits));
    return bits != kHeliThrustNoneBits;
}

void HeliControlApplyAxes(HeliMoveState* state, const float axes_in[5]) {
    if (state == nullptr || axes_in == nullptr) return;
    if (HeliControlThrustRequested(axes_in)) state->target_speed = axes_in[3];
    state->steer[0] = axes_in[0];
    state->steer[1] = axes_in[1];
    state->steer[2] = axes_in[2];
}

void HeliControlReset(HeliMoveState* state, float axes_out[5], const double start_pos[3],
                      const float start_vel[3], const float start_matrix[9], float start_speed,
                      float out_vel[3]) {
    (void)start_pos;
    if (state == nullptr) return;

    if (axes_out != nullptr) {
        axes_out[3] = kHeliThrustNone;
        axes_out[4] = kHeliPilotAnimNone;
        axes_out[0] = 0.0f;
        axes_out[1] = 0.0f;
        axes_out[2] = 0.0f;
    }

    if (start_matrix != nullptr) {
        for (int i = 0; i < 9; ++i) state->matrix[i] = start_matrix[i];
    }
    state->matrix_count = 0;

    state->speed = start_speed;
    state->target_speed = start_speed;
    state->turn[0] = 0.0f;
    state->turn[1] = 0.0f;
    state->turn[2] = 0.0f;
    state->steer[0] = 0.0f;
    state->steer[1] = 0.0f;
    state->steer[2] = 0.0f;
    state->stale = 0.0f;

    if (out_vel != nullptr && start_vel != nullptr && start_matrix != nullptr) {
        const float* matrix_ptr = start_matrix;
        out_vel[0] = matrix_ptr[2] * start_vel[2] + matrix_ptr[1] * start_vel[1] + start_vel[0] * matrix_ptr[0];
        out_vel[1] = matrix_ptr[5] * start_vel[2] + matrix_ptr[4] * start_vel[1] + start_vel[0] * matrix_ptr[3];
        out_vel[2] = matrix_ptr[8] * start_vel[2] + matrix_ptr[7] * start_vel[1] + start_vel[0] * matrix_ptr[6];
    }
}

const float* HeliThrottleSteps() {
    static const float kThrottle[kHeliThrottleStepCount] = {0.84f, 0.88f, 0.92f, 0.96f, 1.0f,
                                                            1.04f, 1.08f, 1.12f, 1.16f, 1.2f};
    return kThrottle;
}

float HeliThrottleStep(float target_speed, int delta_steps) {
    long double stepped_speed = static_cast<long double>(target_speed) +
                                static_cast<long double>(delta_steps) * kHeliSpeedStep;
    if (delta_steps < 0) {
        if (kHeliAccelSpeed > stepped_speed) stepped_speed = kHeliAccelSpeed;
    } else if (delta_steps > 0) {
        if (kHeliMaxThrottle < stepped_speed) stepped_speed = kHeliMaxThrottle;
    }
    return static_cast<float>(stepped_speed);
}

// ---------------------------------------------------------------------------
// Flight simulation operations
// ---------------------------------------------------------------------------

float HeliAccelRate(float speed, const HeliVehicleConfig& config) {
    return !(speed >= kHeliAccelSpeed) ? config.accel_slow : config.accel;
}

long double HeliApproachSpeedWide(float speed, float target, float rate) {
    long double curr_speed = speed;

    if (!(curr_speed >= target)) {
        curr_speed = curr_speed + rate;
        if (curr_speed > target) curr_speed = target;
    } else if (curr_speed > target) {
        curr_speed = curr_speed - rate;
        if (!(curr_speed >= target)) curr_speed = target;
    }
    if (!(curr_speed >= kHeliMilli) && !HeliFne(target, kHeliZero)) curr_speed = kHeliZeroD;
    return curr_speed;
}

float HeliApproachSpeed(float speed, float target, float rate) {
    return static_cast<float>(HeliApproachSpeedWide(speed, target, rate));
}

void HeliSmoothTurn(const float steer[3], float turn[3], const HeliVehicleConfig& config,
                    float out_turn[3]) {
    for (int i = 0; i < 3; ++i) {
        out_turn[i] = (kHeliOne - config.axis[i].smooth) * steer[i] + config.axis[i].smooth * turn[i];
    }
}

int HeliTurnForces(const float turn[3], const HeliVehicleConfig& config, long double speed,
                   float mass, int touching, float body_speed, HeliForce out_forces[3],
                   float* out_stale) {
    const bool any_turn = HeliFne(turn[0], kHeliZero) || HeliFne(turn[1], kHeliZero) ||
                          HeliFne(turn[2], kHeliZero);
    const bool gate = any_turn && (!touching || body_speed > kHeliTwo);
    if (!gate) return 0;

    const float a0 = turn[0] * config.axis[0].rate;
    const float a1 = turn[1] * config.axis[1].rate;
    HeliForce f1 = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 819.2f}};
    HeliForce f2 = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 409.6f}};
    HeliForce f3 = {{0.0f, 0.0f, 0.0f}, {0.0f, 12288.0f, 0.0f}};

    f1.force[1] = a0 * mass * kHeliYawForce;
    f2.force[0] = a1 * mass;
    f3.force[0] = static_cast<float>(speed * (turn[2] * config.axis[2].rate) * mass *
                                     kHeliRollForce);
    if (out_stale != nullptr) *out_stale = f2.force[0];

    out_forces[0] = f1;
    out_forces[1] = f2;
    out_forces[2] = f3;
    return 3;
}

long double HeliNosePitch(const float matrix[9], float stale, bool* used_stale) {
    if (used_stale != nullptr) *used_stale = false;

    const float* mat = matrix;
    if (HeliFne(mat[0], kHeliZero) || HeliFne(mat[3], kHeliZero)) {
        const long double inv = kHeliOneD / std::sqrt(static_cast<long double>(mat[0]) * mat[0] +
                                                      static_cast<long double>(mat[3]) * mat[3]);
        const float cos_heading = mat[0] * inv;
        const long double sin_heading = mat[3] * inv;

        long double pitch = kHeliZero * mat[8] + sin_heading * mat[5] + cos_heading * mat[2];
        if (mat[8] > kHeliZero)
            pitch = pitch * kHeliPitchDown;
        else
            pitch = pitch * kHeliSevenTenths;
        return pitch;
    }
    if (used_stale != nullptr) *used_stale = true;
    return static_cast<long double>(stale);
}

void HeliComposeNoseMatrix(const float matrix[9], int matrix_count,
                           const HeliVehicleConfig& config, long double pitch, float out_matrix[9],
                           int* out_count) {
    float rot_matrix[9];

    HeliSetRotX(rot_matrix, static_cast<float>(pitch * config.axis[2].rate * kHeliPitchScale));

    const float* mat = matrix;
    for (int idx = 0; idx < 3; ++idx) {
        out_matrix[idx] = rot_matrix[idx] * mat[0] + rot_matrix[6 + idx] * mat[2] + rot_matrix[3 + idx] * mat[1];
        out_matrix[3 + idx] = rot_matrix[idx] * mat[3] + rot_matrix[6 + idx] * mat[5] + rot_matrix[3 + idx] * mat[4];
        out_matrix[6 + idx] = rot_matrix[6 + idx] * mat[8] + rot_matrix[idx] * mat[6] + rot_matrix[3 + idx] * mat[7];
    }
    if (out_count != nullptr) *out_count = matrix_count + 0 + 1;
}

void HeliRotorSoundParams(float speed, float out_sound_params[3]) {
    if (out_sound_params == nullptr) return;
    int fast = 1;

    out_sound_params[1] = speed;
    if (!(speed > kHeliHundredth)) fast = 0;
    out_sound_params[2] = static_cast<float>(fast);
}

bool HeliIsResting(int touching, bool moved, int force_count, float body_speed, float rest_speed,
                   const float ang_vel[3], float rest_spin) {
    if (!(touching > 0)) return false;
    if (moved) return false;
    if (force_count != 0) return false;
    if (body_speed >= rest_speed) return false;
    const long double ang_vel_sq = static_cast<long double>(ang_vel[0]) * ang_vel[0] +
                                   static_cast<long double>(ang_vel[1]) * ang_vel[1] +
                                   static_cast<long double>(ang_vel[2]) * ang_vel[2];
    if (ang_vel_sq >= rest_spin) return false;
    return true;
}

void HeliMoveStep(HeliMoveState* state, const HeliVehicleConfig& config, HeliMoveResult* out) {
    if (state == nullptr || out == nullptr) return;

    *out = HeliMoveResult{};
    for (int i = 0; i < 9; ++i) out->matrix[i] = state->matrix[i];
    out->matrix_count = state->matrix_count;
    out->sound_params[0] = 2.0f;

    const float rate = HeliAccelRate(state->speed, config);
    const long double wide_speed =
        HeliApproachSpeedWide(state->speed, state->target_speed, rate);
    out->speed = static_cast<float>(wide_speed);

    HeliSmoothTurn(state->steer, state->turn, config, out->turn);

    HeliForce turn_forces[3];
    float stale = state->stale;
    out->force_count = HeliTurnForces(out->turn, config, wide_speed, state->mass, state->touching,
                                      state->body_speed, turn_forces, &stale);
    for (int i = 0; i < out->force_count; ++i) out->forces[i] = turn_forces[i];
    out->turn_forces_added = out->force_count > 0;

    if (!state->touching && state->body_speed > kHeliTwo) {
        bool used_stale = false;
        const long double pitch = HeliNosePitch(state->matrix, stale, &used_stale);

        HeliComposeNoseMatrix(state->matrix, state->matrix_count, config, pitch, out->matrix,
                              &out->matrix_count);
        out->nose_pitch_applied = true;
        out->stale_used = used_stale;
    }

    out->resting_gate_open =
        !HeliIsResting(state->touching, state->moved != 0, out->force_count, state->body_speed,
                       state->rest_speed, state->ang_vel, state->rest_spin);

    HeliRotorSoundParams(out->speed, out->sound_params);
    out->sound_updated = true;

    state->speed = out->speed;
    for (int i = 0; i < 3; ++i) state->turn[i] = out->turn[i];
    if (out->nose_pitch_applied) {
        for (int i = 0; i < 9; ++i) state->matrix[i] = out->matrix[i];
        state->matrix_count = out->matrix_count;
    }
    state->stale = stale;
    state->force_count = out->force_count;
}

// ---------------------------------------------------------------------------
// HeliWheel
// ---------------------------------------------------------------------------

HeliWheelParams ParseHeliWheelParams(const std::vector<std::string>& tokens) {
    HeliWheelParams out{0.0f, 0.0f};
    out.top = TokenFloat(tokens, 0) * kHeliUnit;
    out.len = TokenFloat(tokens, 1) * kHeliUnit;
    return out;
}

HeliWheelProbe HeliBuildWheelProbe(const HeliWheelParams& params, const double wheel_pos[3],
                                   const float offset[3]) {
    HeliWheelProbe probe{};
    probe.len = params.len;
    probe.top = params.top;
    probe.offset[0] = offset[0];
    probe.offset[1] = offset[1];
    probe.offset[2] = offset[2];
    probe.depth = -(static_cast<double>(params.len) + kHeliTwoMetersD);
    probe.start_z = wheel_pos[2] + kHeliTwoMetersD;
    probe.end_z = probe.depth + probe.start_z;
    return probe;
}

HeliWheelLiftResult HeliWheelLift(const HeliWheelProbe& probe, bool found, double hit_dist,
                                  float mass) {
    HeliWheelLiftResult result{};
    result.landed = false;
    result.max_lift = 0.0;

    if (!found) return result;

    double probe_depth = probe.depth;
    if (!(probe_depth >= kHeliZeroD)) probe_depth = -probe_depth;
    if (!(probe_depth >= hit_dist)) return result;

    double dist = hit_dist;
    if (!(dist >= kHeliZeroD)) dist = -dist;

    const double pen = static_cast<double>(probe.len) - (dist - kHeliTwoMetersD);
    result.force[0] = 0.0f;
    result.force[1] = 0.0f;

    const double top_threshold = static_cast<double>(probe.top) + kHeliTwoMetersD;
    if (top_threshold > hit_dist) {
        const float scaled_top = probe.top + kHeliTwoMeters;
        const float lift_force = (static_cast<float>(scaled_top - dist) / scaled_top + kHeliOne) * mass * kHeliLift;

        result.landed = true;
        result.force[2] = lift_force;
        result.max_lift = top_threshold - hit_dist;
    } else {
        result.landed = false;
        result.force[2] = static_cast<float>(pen / static_cast<double>(probe.len) * mass * kHeliLift);
    }
    result.force[3] = probe.offset[0];
    result.force[4] = probe.offset[1];
    result.force[5] = probe.offset[2];
    result.applied = true;
    return result;
}

void HeliWheelDamp(float vel[3], float ang_mom[3]) {
    if (vel != nullptr) {
        vel[0] = vel[0] * kHeliHeliDamp;
        vel[1] = vel[1] * kHeliHeliDamp;
        vel[2] = vel[2] * kHeliDampZ;
    }
    if (ang_mom != nullptr) {
        ang_mom[0] = ang_mom[0] * kHeliDamp98;
        ang_mom[1] = ang_mom[1] * kHeliDamp98;
        ang_mom[2] = ang_mom[2] * kHeliDamp98;
    }
}

// ---------------------------------------------------------------------------
// Rotor
// ---------------------------------------------------------------------------

int RotorDefArgIndex(int joint_count) {
    return 10 + 6 * joint_count + 2;
}

bool ParsePhysicsDefHeader(const std::vector<std::string>& args, PhysicsDefHeader* out) {
    if (out == nullptr) return false;
    if (args.size() < 10) return false;

    out->task_type = TokenString(args, 0);
    out->type_id = TokenInt(args, 1);
    out->mass = TokenFloat(args, 2);
    out->size[0] = TokenFloat(args, 3);
    out->size[1] = TokenFloat(args, 4);
    out->size[2] = TokenFloat(args, 5);
    out->model = TokenString(args, 8);
    out->joint_count = TokenInt(args, 9);
    out->first_arg = RotorDefArgIndex(out->joint_count);
    return true;
}

bool ParseRotorDef(const std::vector<std::string>& args, int index, RotorDef* out) {
    if (out == nullptr || index < 0) return false;
    const std::size_t base = static_cast<std::size_t>(index);
    if (args.size() < base + static_cast<std::size_t>(kRotorDefArgCount)) return false;

    out->lift = TokenInt(args, base + 0);
    out->tilt = TokenInt(args, base + 1);
    out->dir = TokenInt(args, base + 2);
    out->angle_degrees = TokenFloat(args, base + 3);
    out->angle = static_cast<float>(static_cast<double>(out->angle_degrees) * kRotorDegToRad);
    out->speed = TokenFloat(args, base + 4);
    out->name = TokenString(args, base + 5);
    return true;
}

namespace {

bool ParseVehicleConfigAt(const std::vector<std::string>& args, std::size_t start,
                          HeliVehicleConfig* out, std::size_t* next_out) {
    std::size_t next = start;
    if (args.size() < next + 11) return false;

    *out = HeliVehicleConfig{};
    float config_floats[10];
    for (int i = 0; i < 10; ++i) config_floats[i] = TokenFloat(args, next + static_cast<std::size_t>(i));
    next += 10;

    out->start_speed_scale = config_floats[0];
    out->axis[0].rate = config_floats[1];
    out->axis[0].smooth = config_floats[2];
    out->axis[1].rate = config_floats[3];
    out->axis[1].smooth = config_floats[4];
    out->axis[2].rate = config_floats[5];
    out->axis[2].smooth = config_floats[6];
    out->thrust = config_floats[7];
    out->accel = config_floats[8];
    out->accel_slow = config_floats[9];

    const int part_count = TokenInt(args, next);
    ++next;
    out->part_count = part_count;
    if (part_count < 0 || part_count > 8) return false;
    if (args.size() < next + static_cast<std::size_t>(part_count) * 7 + 3) return false;

    for (int i = 0; i < part_count; ++i) {
        const float area = TokenFloat(args, next + 0);
        const float x = TokenFloat(args, next + 1) * kHeliUnit;
        const float y = TokenFloat(args, next + 2) * kHeliUnit;
        const float z = TokenFloat(args, next + 3) * kHeliUnit;
        const float nx = TokenFloat(args, next + 4);
        const float ny = TokenFloat(args, next + 5);
        const float nz = TokenFloat(args, next + 6);

        float normal[3] = {nx, ny, nz};
        if (nx != 0.0f || ny != 0.0f || nz != 0.0f) {
            const long double inv =
                kHeliOneD / std::sqrt(static_cast<long double>(nx) * nx +
                                      static_cast<long double>(ny) * ny +
                                      static_cast<long double>(nz) * nz);
            normal[0] = static_cast<float>(nx * inv);
            normal[1] = static_cast<float>(ny * inv);
            normal[2] = static_cast<float>(nz * inv);
        }
        out->parts[i][0] = area;
        out->parts[i][1] = normal[0];
        out->parts[i][2] = normal[1];
        out->parts[i][3] = normal[2];
        out->parts[i][4] = x;
        out->parts[i][5] = y;
        out->parts[i][6] = z;
        next += 7;
    }

    out->unk10C[0] = TokenFloat(args, next + 0) * kHeliUnit;
    out->unk10C[1] = TokenFloat(args, next + 1) * 0.5f * kHeliDegToRad;
    out->unk10C[2] = TokenFloat(args, next + 2) * 0.5f * kHeliDegToRad;
    next += 3;
    out->valid = true;
    if (next_out != nullptr) *next_out = next;
    return true;
}

}  // namespace

bool ParseHeliVehicleConfig(const std::vector<std::string>& args, int first_arg,
                            HeliVehicleConfig* out) {
    if (out == nullptr || first_arg < 0) return false;
    return ParseVehicleConfigAt(args, static_cast<std::size_t>(first_arg), out, nullptr);
}

bool ParseHeliConfig(const std::vector<std::string>& args, int first_arg, HeliConfig* out) {
    if (out == nullptr || first_arg < 0) return false;

    std::size_t next = 0;
    if (!ParseVehicleConfigAt(args, static_cast<std::size_t>(first_arg), &out->v, &next)) {
        return false;
    }
    out->count1 = 0;
    out->count2 = 0;
    for (int i = 0; i < 13; ++i) out->list[i] = 0;

    out->count1 = TokenInt(args, next);
    ++next;
    if (out->count1 < 0 || out->count1 > 13) return false;
    if (args.size() < next + static_cast<std::size_t>(out->count1) + 1) return false;
    for (int i = 0; i < out->count1; ++i) {
        out->list[i] = TokenInt(args, next);
        ++next;
    }
    out->count2 = TokenInt(args, next);
    ++next;
    if (out->count2 < 0 || out->count2 > 13 - 3) return false;
    if (args.size() < next + static_cast<std::size_t>(out->count2)) return false;
    // The second list starts at list[3] and therefore overwrites the first list
    // once count1 exceeds three.
    for (int i = 0; i < out->count2; ++i) {
        out->list[3 + i] = TokenInt(args, next);
        ++next;
    }
    return true;
}

// ---------------------------------------------------------------------------
// The decompiled physics-QVM text -> DefinePhysicsObjType argument lists
// ---------------------------------------------------------------------------

namespace {

// The comma-separated arguments of the call whose '(' is at `open`, plus the
// index just past its ')'. The decompiler renders a definition's nested calls
// inside its parent's argument list, so the scan balances parentheses and skips
// over quoted strings (an expression argument may contain both).
std::vector<std::string> PhysicsCallArguments(const std::string& text, std::size_t open,
                                              std::size_t* end) {
    std::vector<std::string> args;
    std::size_t close = std::string::npos;
    int depth = 0;
    bool in_string = false;

    for (std::size_t i = open; i < text.size(); ++i) {
        const char c = text[i];
        if (in_string) {
            if (c == '\\') { ++i; continue; }
            if (c == '"') in_string = false;
            continue;
        }
        if (c == '"') { in_string = true; continue; }
        if (c == '(') { ++depth; continue; }
        if (c == ')') {
            --depth;
            if (depth == 0) { close = i; break; }
        }
    }
    if (close == std::string::npos) {
        if (end != nullptr) *end = text.size();
        return args;
    }

    const std::size_t cursor_start = open + 1;
    std::size_t cursor = cursor_start;
    depth = 0;
    in_string = false;
    for (std::size_t i = cursor; i <= close; ++i) {
        const char c = i < close ? text[i] : ',';
        if (in_string) {
            if (c == '\\') { ++i; continue; }
            if (c == '"') in_string = false;
            continue;
        }
        if (c == '"') { in_string = true; continue; }
        if (c == '(') { ++depth; continue; }
        if (c == ')') { --depth; continue; }
        if (c == ',' && depth == 0) {
            std::string arg = text.substr(cursor, i - cursor);
            const std::size_t first = arg.find_first_not_of(" \t\r\n");
            const std::size_t last = arg.find_last_not_of(" \t\r\n");
            arg = (first == std::string::npos) ? std::string() : arg.substr(first, last - first + 1);
            if (arg.size() >= 2 && arg.front() == '"' && arg.back() == '"') {
                arg = arg.substr(1, arg.size() - 2);
            }
            args.push_back(arg);
            cursor = i + 1;
        }
    }
    if (end != nullptr) *end = close + 1;
    return args;
}

}  // namespace

std::vector<std::vector<std::string>> ParseDefinePhysicsObjTypeCalls(const std::string& text) {
    std::vector<std::vector<std::string>> out;
    const std::string needle = "DefinePhysicsObjType(";
    std::size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        std::size_t end = 0;
        std::vector<std::string> args =
            PhysicsCallArguments(text, pos + needle.size() - 1, &end);
        if (!args.empty()) out.push_back(std::move(args));
        pos = end > pos ? end : pos + needle.size();
    }
    return out;
}

float RotorRunValue(float value, const RotorDef& def, float car_speed) {
    return static_cast<float>(static_cast<long double>(def.dir) * def.speed * car_speed + value);
}

float RotorSpinAngle(float value) {
    return static_cast<float>(static_cast<long double>(value) * kRotorSpin);
}

float RotorBladeStep(const RotorDef& def, float car_speed) {
    return static_cast<float>(static_cast<long double>(def.speed) * car_speed);
}

float RotorBladeTilt(const RotorDef& def, float car_speed, const float owner_matrix[9],
                     const float owner_vel[3], float start_speed_scale) {
    const float max_tilt = def.angle;
    const long double forward_velocity = static_cast<long double>(owner_matrix[8]) * owner_vel[2] +
                                         static_cast<long double>(owner_matrix[5]) * owner_vel[1] +
                                         static_cast<long double>(owner_matrix[2]) * owner_vel[0];
    long double throttle_factor = static_cast<long double>(kHeliOne) - car_speed;

    if (static_cast<long double>(kRotorMinPitch) > throttle_factor)
        throttle_factor = kRotorMinPitch;
    else if (static_cast<long double>(kHeliOne) < throttle_factor)
        throttle_factor = kHeliOne;

    float tilt = static_cast<float>(-(throttle_factor * max_tilt) -
                                    forward_velocity * kRotorYawTilt * max_tilt /
                                        (static_cast<long double>(start_speed_scale) * kRotorMassTilt));
    if (-max_tilt > tilt)
        tilt = -max_tilt;
    else if (max_tilt < tilt)
        tilt = max_tilt;
    return tilt;
}

bool RotorLiftForce(const RotorDef& def, float mass, float car_speed, const float owner_matrix[9],
                    HeliForce* out_force) {
    if (out_force == nullptr) return false;
    if (!def.lift) return false;

    const long double lift = static_cast<long double>(mass) * car_speed * kRotorLift;
    if (lift == 0.0L) return false;
    const float total_lift = static_cast<float>(lift);

    long double angle = kHeliZero;
    float up[3] = {owner_matrix[1], owner_matrix[4], owner_matrix[7]};
    const float fwd[3] = {owner_matrix[2], owner_matrix[5], owner_matrix[8]};

    if (up[0] != 0.0f || up[1] != 0.0f) {
        up[2] = 0.0f;
        const bool a_zero = IsZero(up[0]) && IsZero(up[1]) && IsZero(up[2]);
        const bool b_zero = IsZero(fwd[0]) && IsZero(fwd[1]) && IsZero(fwd[2]);
        if (a_zero || b_zero) {
            angle = 0.0f;
        } else {
            const long double dot = static_cast<long double>(up[2]) * fwd[2] +
                                    static_cast<long double>(up[1]) * fwd[1] +
                                    static_cast<long double>(up[0]) * fwd[0];
            const long double len_up = std::sqrt(static_cast<long double>(up[0]) * up[0] +
                                                 static_cast<long double>(up[1]) * up[1] +
                                                 static_cast<long double>(up[2]) * up[2]);
            const long double len_fwd = std::sqrt(static_cast<long double>(fwd[0]) * fwd[0] +
                                                  static_cast<long double>(fwd[1]) * fwd[1] +
                                                  static_cast<long double>(fwd[2]) * fwd[2]);
            const volatile double dot_normalized = dot / (len_up * len_fwd);

            if (dot_normalized >= 1.0)
                angle = 0.0f;
            else if (!(dot_normalized > -1.0))
                angle = 3.14159265f;
            else
                angle = std::acos(dot_normalized);
        }
    }

    out_force->force[0] = 0.0f;
    out_force->force[1] = static_cast<float>(std::cos(angle) * total_lift);
    out_force->force[2] = total_lift;
    out_force->point[0] = 0.0f;
    out_force->point[1] = 0.0f;
    out_force->point[2] = 0.0f;
    return true;
}

bool RotorTiltForce(const RotorDef& def, float mass, float car_speed, int touching,
                    HeliForce* out_force) {
    if (out_force == nullptr) return false;
    if (car_speed == 0.0f) return false;
    if (!(def.tilt || def.lift)) return false;
    if (touching) return false;

    out_force->force[0] = 0.0f;
    out_force->force[1] = 0.0f;
    out_force->force[2] = 0.0f;
    out_force->point[0] = 0.0f;
    out_force->point[1] = kRotorTiltY;
    out_force->point[2] = 0.0f;

    if (def.tilt)
        out_force->force[0] = -static_cast<float>(static_cast<long double>(mass) * car_speed * kRotorTilt);
    else if (def.lift)
        out_force->force[0] = static_cast<float>(static_cast<long double>(mass) * car_speed * kRotorTilt);
    return true;
}

void RotorOffsetPos(const float offset[3], const float owner_matrix[9], const double owner_pos[3],
                    double out_pos[3]) {
    const float* matrix_ptr = owner_matrix;
    const double x = static_cast<long double>(matrix_ptr[2]) * offset[2] +
                     static_cast<long double>(matrix_ptr[1]) * offset[1] +
                     static_cast<long double>(matrix_ptr[0]) * offset[0];
    const double y = static_cast<long double>(matrix_ptr[5]) * offset[2] +
                     static_cast<long double>(matrix_ptr[4]) * offset[1] +
                     static_cast<long double>(matrix_ptr[3]) * offset[0];
    const double z = static_cast<long double>(matrix_ptr[8]) * offset[2] +
                     static_cast<long double>(matrix_ptr[7]) * offset[1] +
                     static_cast<long double>(matrix_ptr[6]) * offset[0];

    out_pos[0] = x + owner_pos[0];
    out_pos[1] = y + owner_pos[1];
    out_pos[2] = z + owner_pos[2];
}

void RotorLocalMatrix(float out[9], const float owner_matrix[9], const float local[9]) {
    HeliMatrixMul(out, owner_matrix, local, 1, 0, 0);
}

void RotorBladeMatrix(float out[9], const float base[9], float angle, float tilt) {
    float rx[9];
    float rz[9];
    float temp_mat[9];

    HeliSetRotX(rx, angle);
    HeliSetRotZ(rz, tilt);
    HeliMatrixMul(temp_mat, base, rx, 0, 0, 0);
    HeliMatrixMul(out, temp_mat, rz, 0, 0, 0);
}

}  // namespace igi
