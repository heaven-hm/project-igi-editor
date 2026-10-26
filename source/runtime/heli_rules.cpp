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

// x87 `fsin`/`fcos` as matrix.c:113-115, :148-150 use them. The argument is a
// float, so it is exact in the 80-bit register; libm is used here (a named
// divergence in the header).
long double X87Sin(long double a) { return std::sin(a); }
long double X87Cos(long double a) { return std::cos(a); }

// Matrix_SetRotY (matrix.c:130-146). Not exposed: only Matrix_SetEuler needs it.
void SetRotY(float m[9], float a) {
    const long double s = X87Sin(a);
    const long double c = X87Cos(a);

    m[1] = 0.0f;
    m[3] = 0.0f;
    m[4] = 1.0f;
    m[5] = 0.0f;
    m[7] = 0.0f;
    m[0] = static_cast<float>(c);
    m[2] = static_cast<float>(s);
    m[6] = static_cast<float>(-s);
    m[8] = static_cast<float>(c);
}

// `!(a >= b) && !(a > b)` in the engine's own terms is `a < b`; the two
// comparisons the engine actually writes are the ones below.
bool IsZero(long double x) { return !(x < 0.0L) && !(x > 0.0L); }

}  // namespace

// ---------------------------------------------------------------------------
// The authored parameter table
// ---------------------------------------------------------------------------

const HeliParamDesc* HeliParamTable() {
    // Index order is the ParamTable_Add order (heli.c:186-200); the authored
    // name and type columns are the retail Task_DeclareParameters line
    // (LEVEL2/objects.qsc:62-63); the range column is the `extra` argument of
    // that ParamTable_Add (heli.c:188-190, :200, with Param_Range02
    // param.c:270-276 and gHeliRange heli.c:184-185).
    //
    // The engine field column is the load-bearing one: the authored "Original
    // Thrust" is bound to `v.start_speed` (heli.c:190), the authored "Speed" to
    // `v.startVel` (heli.c:191), the authored "Model" to `v.name` (heli.c:192),
    // "Collision detection" to `v.collide` (heli.c:193), "Force Z" to
    // `v.airborne` (heli.c:194) and the three VarStrings to `rotor_expr`,
    // `light_expr` and `engine_expr` (heli.c:195-197). The three "ViewCone"
    // slots land in `light_params` (heli.c:198-200) and are the AIDetector
    // cone, which this editor does not model.
    static const HeliParamDesc kTable[kHeliParamCount] = {
        // name                             engine field        type         n  range?  min    max
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
    // `gHeliControls` (heli.c:754), handed out by Heli_GetControlInfo
    // (heli.c:671-681) which rejects any index outside 0..4.
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
    // rotor.h:159-174 `mat_mul`: the original inlines this everywhere and sums
    // the three products in a call-site-dependent order. A local `t` keeps the
    // operation safe when `out` aliases `a` or `b`, exactly like the engine's
    // own `Matrix t` (matrix.c:15).
    float t[9];

    for (int i = 0; i < 3; ++i) {
        const int last = i == 0 ? last0 : (i == 1 ? last1 : last2);
        const int k0 = last == 0 ? 1 : 0;
        const int k1 = last == 2 ? 1 : 2;

        for (int j = 0; j < 3; ++j) {
            t[i * 3 + j] = static_cast<float>(
                static_cast<long double>(a[i * 3 + k0]) * b[k0 * 3 + j] +
                static_cast<long double>(a[i * 3 + k1]) * b[k1 * 3 + j] +
                static_cast<long double>(a[i * 3 + last]) * b[last * 3 + j]);
        }
    }
    for (int i = 0; i < 9; ++i) out[i] = t[i];
}

void HeliSetRotX(float m[9], float a) {
    // matrix.c:113-128.
    const long double s = X87Sin(a);
    const float c = static_cast<float>(X87Cos(a));

    m[0] = 1.0f;
    m[1] = 0.0f;
    m[2] = 0.0f;
    m[3] = 0.0f;
    m[6] = 0.0f;
    m[4] = c;
    m[5] = static_cast<float>(-s);
    m[8] = c;
    m[7] = static_cast<float>(s);
}

void HeliSetRotZ(float m[9], float a) {
    // matrix.c:148-164.
    const long double s = X87Sin(a);
    const float c = static_cast<float>(X87Cos(a));

    m[2] = 0.0f;
    m[5] = 0.0f;
    m[6] = 0.0f;
    m[7] = 0.0f;
    m[8] = 1.0f;
    m[0] = c;
    m[1] = static_cast<float>(-s);
    m[4] = c;
    m[3] = static_cast<float>(s);
}

void HeliSetEuler(float m[9], float ax, float ay, float az) {
    // matrix.c:99-110: Rz(az) * Ry(ay) * Rx(ax), count forced to 0. `Mul`
    // (matrix.c:12-27) sums k = 0, 2, 1, i.e. mat_mul with last = 1 in every row.
    float r[9];

    HeliSetRotZ(m, az);
    SetRotY(r, ay);
    HeliMatrixMul(m, m, r, 1, 1, 1);
    HeliSetRotX(r, ax);
    HeliMatrixMul(m, m, r, 1, 1, 1);
}

bool HeliFne(long double a, long double b) { return a < b || a > b; }

// ---------------------------------------------------------------------------
// The parsed authored form
// ---------------------------------------------------------------------------

bool ParseHeliParams(const std::vector<std::string>& tokens, HeliParams* out) {
    if (out == nullptr) return false;
    const std::size_t base = static_cast<std::size_t>(kHeliTokenPrefix);
    if (tokens.size() < base + static_cast<std::size_t>(kHeliTokenCount)) return false;

    // The engine's ParamTable_Add order (heli.c:186-200), not the authored
    // order: they happen to agree 1:1 here, which the corpus test checks.
    out->position[0] = TokenDouble(tokens, base + 0);
    out->position[1] = TokenDouble(tokens, base + 1);
    out->position[2] = TokenDouble(tokens, base + 2);

    // "Orientation" is Real32x9 but ParamReal32x9_Parse (paramops.c:423-435)
    // reads THREE Euler angles and stores Matrix_SetEuler of them, so three
    // tokens are consumed and nine floats are written.
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

    // "Degrees" is a type whose parent is Real32 (paramops.c:184), so it
    // inherits Real32's parse and stores the authored value verbatim: the
    // engine does NOT convert to radians here.
    out->viewcone_alpha = TokenFloat(tokens, base + 16);
    out->viewcone_gamma = TokenFloat(tokens, base + 17);
    out->viewcone_length = TokenFloat(tokens, base + 18);
    return true;
}

// ---------------------------------------------------------------------------
// Heli_Control (0x431B70, heli.c:359-407)
// ---------------------------------------------------------------------------

bool HeliControlThrustRequested(const float axes_in[5]) {
    // heli.c:363 -- the test is on the raw bits, so a NaN throttle is a request
    // and only the exact pattern 0xBF800000 is the sentinel.
    uint32_t bits = 0;
    std::memcpy(&bits, &axes_in[3], sizeof(bits));
    return bits != kHeliThrustNoneBits;
}

void HeliControlApplyAxes(HeliMoveState* state, const float axes_in[5]) {
    // heli.c:363-366.
    if (state == nullptr || axes_in == nullptr) return;
    if (HeliControlThrustRequested(axes_in)) state->target_speed = axes_in[3];
    state->steer[0] = axes_in[0];
    state->steer[1] = axes_in[1];
    state->steer[2] = axes_in[2];
}

void HeliControlReset(HeliMoveState* state, float axes_out[5], const double start_pos[3],
                      const float start_vel[3], const float start_matrix[9], float start_speed,
                      float out_vel[3]) {
    // heli.c:365-401. DynCube_Move(Game_GetWorld(), h, &h->v.startPos, r) is the
    // world move the editor cannot reproduce; `start_pos` is therefore reported
    // to the caller instead of being applied.
    (void)start_pos;
    if (state == nullptr) return;

    if (axes_out != nullptr) {
        axes_out[3] = kHeliThrustNone;         // heli.c:371
        axes_out[4] = kHeliPilotAnimNone;      // heli.c:372
        axes_out[0] = 0.0f;                    // heli.c:373
        axes_out[1] = 0.0f;                    // heli.c:374
        axes_out[2] = 0.0f;                    // heli.c:375
    }

    // h->v.matrix = h->v.startMatrix (heli.c:376).
    if (start_matrix != nullptr) {
        for (int i = 0; i < 9; ++i) state->matrix[i] = start_matrix[i];
    }
    state->matrix_count = 0;  // ParamReal32x9_Parse leaves count 0 (matrix.c:107)

    // Vehicle_ResetControls(h) (vehicle.c:189-197) zeroes turn, speed, steer,
    // target_speed and brake; Heli_Control then overwrites speed and
    // target_speed with the authored "Original Thrust" and re-zeroes turn/steer
    // (heli.c:379-388).
    state->speed = start_speed;
    state->target_speed = start_speed;
    state->turn[0] = 0.0f;
    state->turn[1] = 0.0f;
    state->turn[2] = 0.0f;
    state->steer[0] = 0.0f;
    state->steer[1] = 0.0f;
    state->steer[2] = 0.0f;
    state->stale = 0.0f;

    // heli.c:392-395: the start velocity is the authored "Speed" turned with
    // the authored "Orientation" matrix, i.e. the matrix's ROWS (row-major).
    if (out_vel != nullptr && start_vel != nullptr && start_matrix != nullptr) {
        const float* m = start_matrix;
        out_vel[0] = m[2] * start_vel[2] + m[1] * start_vel[1] + start_vel[0] * m[0];
        out_vel[1] = m[5] * start_vel[2] + m[4] * start_vel[1] + start_vel[0] * m[3];
        out_vel[2] = m[8] * start_vel[2] + m[7] * start_vel[1] + start_vel[0] * m[6];
    }
}

const float* HeliThrottleSteps() {
    // Heli_ReadKeys (heli.c:689). Note the ladder is NOT 0..1: the lowest
    // digital step is 0.84 and the highest 1.2, while the analogue throttle is
    // a direct value in axes[3].
    static const float kThrottle[kHeliThrottleStepCount] = {0.84f, 0.88f, 0.92f, 0.96f, 1.0f,
                                                            1.04f, 1.08f, 1.12f, 1.16f, 1.2f};
    return kThrottle;
}

float HeliThrottleStep(float target_speed, int delta_steps) {
    // heli.c:706-719: one press moves the target by exactly kSpeedStep (0.01)
    // and clamps to [0.8, 1.25] -- NOT to the throttle ladder's own range.
    long double v = static_cast<long double>(target_speed) +
                    static_cast<long double>(delta_steps) * kHeliSpeedStep;
    if (delta_steps < 0) {
        if (kHeliAccelSpeed > v) v = kHeliAccelSpeed;
    } else if (delta_steps > 0) {
        if (kHeliMaxThrottle < v) v = kHeliMaxThrottle;
    }
    return static_cast<float>(v);
}

// ---------------------------------------------------------------------------
// Heli_Move (0x431E30, heli.c:412-520)
// ---------------------------------------------------------------------------

float HeliAccelRate(float speed, const HeliVehicleConfig& config) {
    // heli.c:416: `!(speed >= 0.8)` picks the slow rate, so a NaN speed also
    // picks accel_slow.
    return !(speed >= kHeliAccelSpeed) ? config.accel_slow : config.accel;
}

long double HeliApproachSpeedWide(float speed, float target, float rate) {
    // heli.c:418-433. `s` stays long double through the ramp and is rounded
    // once on the store into v.speed -- but the SAME wide value is then used for
    // the roll force (heli.c:451), so it is exposed rather than re-read from the
    // narrowed field.
    long double s = speed;

    if (!(s >= target)) {           // s < target, and true for NaN
        s = s + rate;
        if (s > target) s = target;
    } else if (s > target) {
        s = s - rate;
        if (!(s >= target)) s = target;
    }
    // heli.c:431: a speed below 0.001 collapses to exactly 0, but ONLY when the
    // requested throttle is 0 (or NaN) -- a standstill under throttle keeps the
    // sub-milli ramp value.
    if (!(s >= kHeliMilli) && !HeliFne(target, kHeliZero)) s = kHeliZeroD;
    return s;
}

float HeliApproachSpeed(float speed, float target, float rate) {
    return static_cast<float>(HeliApproachSpeedWide(speed, target, rate));
}

void HeliSmoothTurn(const float steer[3], float turn[3], const HeliVehicleConfig& config,
                    float out_turn[3]) {
    // heli.c:434-439.
    for (int i = 0; i < 3; ++i) {
        out_turn[i] = (kHeliOne - config.axis[i].smooth) * steer[i] + config.axis[i].smooth * turn[i];
    }
}

int HeliTurnForces(const float turn[3], const HeliVehicleConfig& config, long double speed,
                   float mass, int touching, float body_speed, HeliForce out_forces[3],
                   float* out_stale) {
    // heli.c:440-456.
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
    // `s` is the long double the ramp produced, so this product chain is one
    // long double rounded once on the store (heli.c:451). `s` is NOT re-read
    // from the narrowed v.speed field.
    f3.force[0] = static_cast<float>(speed * (turn[2] * config.axis[2].rate) * mass *
                                     kHeliRollForce);
    if (out_stale != nullptr) *out_stale = f2.force[0];

    out_forces[0] = f1;
    out_forces[1] = f2;
    out_forces[2] = f3;
    return 3;
}

long double HeliNosePitch(const float matrix[9], float stale, bool* used_stale) {
    // heli.c:457-476. The outer gate (`!touching && body.speed > 2`) is applied
    // by HeliMoveStep, not here.
    if (used_stale != nullptr) *used_stale = false;

    const float* m = matrix;
    if (HeliFne(m[0], kHeliZero) || HeliFne(m[3], kHeliZero)) {
        const long double inv = kHeliOneD / std::sqrt(static_cast<long double>(m[0]) * m[0] +
                                                      static_cast<long double>(m[3]) * m[3]);
        const float cx = m[0] * inv;                 // heli.c:465, rounded to float
        const long double cy = m[3] * inv;           // heli.c:466, kept wide

        long double pitch = kHeliZero * m[8] + cy * m[5] + cx * m[2];
        if (m[8] > kHeliZero)
            pitch = pitch * kHeliPitchDown;          // heli.c:470
        else
            pitch = pitch * kHeliSevenTenths;        // heli.c:472
        return pitch;
    }
    // heli.c:473-476: the original reads the stack slot that held f2[0].
    if (used_stale != nullptr) *used_stale = true;
    return static_cast<long double>(stale);
}

void HeliComposeNoseMatrix(const float matrix[9], int matrix_count,
                           const HeliVehicleConfig& config, long double pitch, float out_matrix[9],
                           int* out_count) {
    // heli.c:477-484. The product is written out inline (not via Mul) and is
    // `nm = m * r`: nm[i][j] = sum_k m[i][k] * r[k][j], so the engine's
    // `r.m[6 + i] * m[2]` term is m[i][2] * r[2][j]. Each row sums its three
    // products in its own order -- rows 0 and 1 as k = 0, 2, 1 and row 2 as
    // k = 2, 0, 1 -- which this port reproduces term for term.
    float r[9];

    HeliSetRotX(r, static_cast<float>(pitch * config.axis[2].rate * kHeliPitchScale));

    const float* m = matrix;
    for (int i = 0; i < 3; ++i) {
        out_matrix[i] = r[i] * m[0] + r[6 + i] * m[2] + r[3 + i] * m[1];
        out_matrix[3 + i] = r[i] * m[3] + r[6 + i] * m[5] + r[3 + i] * m[4];
        out_matrix[6 + i] = r[6 + i] * m[8] + r[i] * m[6] + r[3 + i] * m[7];
    }
    if (out_count != nullptr) *out_count = matrix_count + 0 + 1;  // r.count is 0
}

void HeliRotorSoundParams(float speed, float out_sound_params[3]) {
    // heli.c:508-515. Slot 0 is the count SoundParams_New(2) wrote
    // (soundsys.c:526-541); slot 2 is the `fast` flag.
    if (out_sound_params == nullptr) return;
    int fast = 1;

    out_sound_params[1] = speed;
    if (!(speed > kHeliHundredth)) fast = 0;
    out_sound_params[2] = static_cast<float>(fast);
}

bool HeliIsResting(int touching, bool moved, int force_count, float body_speed, float rest_speed,
                   const float ang_vel[3], float rest_spin) {
    // graphtask/physobj.c:515-527. Every comparison is the engine's own
    // `!(a >= b)` form, so a NaN speed is "resting" and a NaN restSpin is not.
    if (!(touching > 0)) return false;
    if (moved) return false;
    if (force_count != 0) return false;
    if (body_speed >= rest_speed) return false;
    const long double w2 = static_cast<long double>(ang_vel[0]) * ang_vel[0] +
                           static_cast<long double>(ang_vel[1]) * ang_vel[1] +
                           static_cast<long double>(ang_vel[2]) * ang_vel[2];
    if (w2 >= rest_spin) return false;
    return true;
}

void HeliMoveStep(HeliMoveState* state, const HeliVehicleConfig& config, HeliMoveResult* out) {
    if (state == nullptr || out == nullptr) return;

    *out = HeliMoveResult{};
    for (int i = 0; i < 9; ++i) out->matrix[i] = state->matrix[i];
    out->matrix_count = state->matrix_count;
    out->sound_params[0] = 2.0f;  // SoundParams_New(2) stored the count

    // heli.c:416-433 -- the speed ramp. The wide value feeds both the stored
    // field and the roll force.
    const float rate = HeliAccelRate(state->speed, config);
    const long double wide_speed =
        HeliApproachSpeedWide(state->speed, state->target_speed, rate);
    out->speed = static_cast<float>(wide_speed);

    // heli.c:434-439 -- the smoothed turn axes.
    HeliSmoothTurn(state->steer, state->turn, config, out->turn);

    // heli.c:440-456 -- the turning forces.
    HeliForce turn_forces[3];
    float stale = state->stale;
    out->force_count = HeliTurnForces(out->turn, config, wide_speed, state->mass, state->touching,
                                      state->body_speed, turn_forces, &stale);
    for (int i = 0; i < out->force_count; ++i) out->forces[i] = turn_forces[i];
    out->turn_forces_added = out->force_count > 0;

    // heli.c:457-485 -- the nose pitch while airborne and moving.
    if (!state->touching && state->body_speed > kHeliTwo) {
        bool used_stale = false;
        const long double pitch = HeliNosePitch(state->matrix, stale, &used_stale);

        HeliComposeNoseMatrix(state->matrix, state->matrix_count, config, pitch, out->matrix,
                              &out->matrix_count);
        out->nose_pitch_applied = true;
        out->stale_used = used_stale;
    }

    // heli.c:486-501 -- the physics step. `Heli_UpdateWheels` (modelled
    // separately as HeliWheelLift), `Body_AddAeroForces` and
    // `PhysicsObj_Collide` need the retail world; only the resting gate that
    // guards them is reproduced.
    out->resting_gate_open =
        !HeliIsResting(state->touching, state->moved != 0, out->force_count, state->body_speed,
                       state->rest_speed, state->ang_vel, state->rest_spin);

    // heli.c:508-515 -- the rotor sound parameters.
    HeliRotorSoundParams(out->speed, out->sound_params);
    out->sound_updated = true;

    // Write the mutated fields back, in the order the engine wrote them.
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
    // vehicle/parts.c:284-292. The two authored reals are stored SWAPPED:
    // params[1] gets arg 0 and params[0] gets arg 1, both times kAngleUnits
    // (4096, parts.c:61). The corpus test pins this against the shipped
    // HELI.QVM, where the argument order is (top, length).
    HeliWheelParams out{0.0f, 0.0f};
    out.top = TokenFloat(tokens, 0) * kHeliUnit;
    out.len = TokenFloat(tokens, 1) * kHeliUnit;
    return out;
}

HeliWheelProbe HeliBuildWheelProbe(const HeliWheelParams& params, const double wheel_pos[3],
                                   const float offset[3]) {
    // heli.c:542-559. `len` and `top` are DOUBLES in the engine's locals, and
    // the probe starts two metres above the wheel and runs two metres down.
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
    // heli.c:567-609. Coll_Lines (coll.c) produced `found`/`hit_dist`; this is
    // everything Heli_UpdateWheels does with one result.
    HeliWheelLiftResult result{};
    result.landed = false;
    result.max_lift = 0.0;

    if (!found) return result;

    double a = probe.depth;
    if (!(a >= kHeliZeroD)) a = -a;
    if (!(a >= hit_dist)) return result;

    double dist = hit_dist;
    if (!(dist >= kHeliZeroD)) dist = -dist;

    const double pen = static_cast<double>(probe.len) - (dist - kHeliTwoMetersD);
    result.force[0] = 0.0f;
    result.force[1] = 0.0f;

    const double t = static_cast<double>(probe.top) + kHeliTwoMetersD;
    if (t > hit_dist) {
        const float u = probe.top + kHeliTwoMeters;
        const float f = (static_cast<float>(u - dist) / u + kHeliOne) * mass * kHeliLift;

        result.landed = true;
        result.force[2] = f;
        result.max_lift = t - hit_dist;
    } else {
        // heli.c:596 -- `pen / len` divides by the authored length with no
        // guard, so a zero-length wheel produces an infinity or a NaN here.
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
    // heli.c:603-608: the two horizontal axes damp at 0.95 and Z at 0.97.
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
    // physobjtype.c:64-122: `arg` starts at 10, advances 6 per joint, and the
    // task-type parse handler is handed `arg + 2`.
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
    // rotor.c:302-316. `angle` is authored in DEGREES and stored as radians.
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

// editobj/vehconfig.c:16-39. Ten floats, then the part count, then seven tokens
// per part, then three trailing floats. Reports where it stopped so
// Heli_ParseConfig can continue (plane/heli.c:81).
bool ParseVehicleConfigAt(const std::vector<std::string>& args, std::size_t start,
                          HeliVehicleConfig* out, std::size_t* next_out) {
    std::size_t next = start;
    if (args.size() < next + 11) return false;

    *out = HeliVehicleConfig{};
    float f[10];
    for (int i = 0; i < 10; ++i) f[i] = TokenFloat(args, next + static_cast<std::size_t>(i));
    next += 10;

    out->start_speed_scale = f[0];
    out->axis[0].rate = f[1];
    out->axis[0].smooth = f[2];
    out->axis[1].rate = f[3];
    out->axis[1].smooth = f[4];
    out->axis[2].rate = f[5];
    out->axis[2].smooth = f[6];
    out->thrust = f[7];
    out->accel = f[8];
    out->accel_slow = f[9];

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

        // AeroPart_Init (animtrig/body.c:52-94) normalises the normal unless it
        // is exactly the zero vector.
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
    // plane/heli.c:77-88, reached through Heli_ParseArgs 0x432F60.
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
    // once count1 exceeds three (plane/heli.c:71, :87).
    for (int i = 0; i < out->count2; ++i) {
        out->list[3 + i] = TokenInt(args, next);
        ++next;
    }
    return true;
}

float RotorRunValue(float value, const RotorDef& def, float car_speed) {
    // rotor.c:260: `o->value = dir * speed * car->v.speed + o->value`, the
    // whole product chain kept wide and rounded once.
    return static_cast<float>(static_cast<long double>(def.dir) * def.speed * car_speed + value);
}

float RotorSpinAngle(float value) {
    // rotor.c:145: `a = src->value * kSpin`.
    return static_cast<float>(static_cast<long double>(value) * kRotorSpin);
}

float RotorBladeStep(const RotorDef& def, float car_speed) {
    // rotor.c:131: `step = (float)((long double)d->speed * car->v.speed)`.
    return static_cast<float>(static_cast<long double>(def.speed) * car_speed);
}

float RotorBladeTilt(const RotorDef& def, float car_speed, const float owner_matrix[9],
                     const float owner_vel[3], float start_speed_scale) {
    // rotor.c:132-144. `rate` is the owner's forward velocity in its own frame;
    // `k` is a throttle factor clamped to [-0.1, 1.0]; the tilt is then clamped
    // to [-angle, angle]. Note the low clamp is `-maxTilt > tilt` first, so a
    // NaN tilt falls through BOTH branches and is left alone.
    const float maxTilt = def.angle;
    const long double rate = static_cast<long double>(owner_matrix[8]) * owner_vel[2] +
                             static_cast<long double>(owner_matrix[5]) * owner_vel[1] +
                             static_cast<long double>(owner_matrix[2]) * owner_vel[0];
    long double k = static_cast<long double>(kHeliOne) - car_speed;

    if (static_cast<long double>(kRotorMinPitch) > k)
        k = kRotorMinPitch;
    else if (static_cast<long double>(kHeliOne) < k)
        k = kHeliOne;

    float tilt = static_cast<float>(-(k * maxTilt) -
                                    rate * kRotorYawTilt * maxTilt /
                                        (static_cast<long double>(start_speed_scale) * kRotorMassTilt));
    if (-maxTilt > tilt)
        tilt = -maxTilt;
    else if (maxTilt < tilt)
        tilt = maxTilt;
    return tilt;
}

bool RotorLiftForce(const RotorDef& def, float mass, float car_speed, const float owner_matrix[9],
                    HeliForce* out_force) {
    // rotor.c:218-243. The lift is `mass * speed * kLift`; the force is
    // (0, cos(angle)*lift, lift) with the angle between the owner's up column
    // and its forward column, after zeroing up.z.
    if (out_force == nullptr) return false;
    if (!def.lift) return false;

    const long double lift = static_cast<long double>(mass) * car_speed * kRotorLift;
    if (lift == 0.0L) return false;
    const float f = static_cast<float>(lift);

    long double a = kHeliZero;
    float up[3] = {owner_matrix[1], owner_matrix[4], owner_matrix[7]};
    const float fwd[3] = {owner_matrix[2], owner_matrix[5], owner_matrix[8]};

    if (up[0] != 0.0f || up[1] != 0.0f) {
        up[2] = 0.0f;
        // Vec3_Angle (file/vec.c:189-208): the zero-vector guards return 0, the
        // dot over the product of lengths is clamped, and the acos is libm.
        const bool a_zero = IsZero(up[0]) && IsZero(up[1]) && IsZero(up[2]);
        const bool b_zero = IsZero(fwd[0]) && IsZero(fwd[1]) && IsZero(fwd[2]);
        if (a_zero || b_zero) {
            a = 0.0f;
        } else {
            const long double dot = static_cast<long double>(up[2]) * fwd[2] +
                                    static_cast<long double>(up[1]) * fwd[1] +
                                    static_cast<long double>(up[0]) * fwd[0];
            const long double la = std::sqrt(static_cast<long double>(up[0]) * up[0] +
                                             static_cast<long double>(up[1]) * up[1] +
                                             static_cast<long double>(up[2]) * up[2]);
            const long double lb = std::sqrt(static_cast<long double>(fwd[0]) * fwd[0] +
                                             static_cast<long double>(fwd[1]) * fwd[1] +
                                             static_cast<long double>(fwd[2]) * fwd[2]);
            const volatile double c = dot / (la * lb);

            if (c >= 1.0)
                a = 0.0f;
            else if (!(c > -1.0))
                a = 3.14159265f;
            else
                a = std::acos(c);
        }
    }

    out_force->force[0] = 0.0f;
    out_force->force[1] = static_cast<float>(std::cos(a) * f);
    out_force->force[2] = f;
    out_force->point[0] = 0.0f;
    out_force->point[1] = 0.0f;
    out_force->point[2] = 0.0f;
    return true;
}

bool RotorTiltForce(const RotorDef& def, float mass, float car_speed, int touching,
                    HeliForce* out_force) {
    // rotor.c:245-259. `tilt` produces a NEGATIVE X force and `lift` a POSITIVE
    // one; the point is 8192 up the owner's local Y.
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
    // rotor.c:268-277, the same placement part_place performs
    // (vehicle/parts.c:74-86).
    const float* m = owner_matrix;
    const double x = static_cast<long double>(m[2]) * offset[2] +
                     static_cast<long double>(m[1]) * offset[1] +
                     static_cast<long double>(m[0]) * offset[0];
    const double y = static_cast<long double>(m[5]) * offset[2] +
                     static_cast<long double>(m[4]) * offset[1] +
                     static_cast<long double>(m[3]) * offset[0];
    const double z = static_cast<long double>(m[8]) * offset[2] +
                     static_cast<long double>(m[7]) * offset[1] +
                     static_cast<long double>(m[6]) * offset[0];

    out_pos[0] = x + owner_pos[0];
    out_pos[1] = y + owner_pos[1];
    out_pos[2] = z + owner_pos[2];
}

void RotorLocalMatrix(float out[9], const float owner_matrix[9], const float local[9]) {
    // rotor.c:280: `mat_mul(&f, a->matrix, &o->local, 1, 0, 0)`.
    HeliMatrixMul(out, owner_matrix, local, 1, 0, 0);
}

void RotorBladeMatrix(float out[9], const float base[9], float angle, float tilt) {
    // rotor.c:149-154. `copy` starts as `base`, then
    //   copy = base * RotX(a)      mat_mul(&t, &base, &copy.matrix, 0, 0, 0)
    //   copy = copy * RotZ(tilt)   mat_mul(&t, &copy.matrix, &rz, 0, 0, 0)
    // Both multiplies use last = 0 in every row. `a` is NOT advanced here: the
    // caller steps it by RotorBladeStep per blade copy.
    float rx[9];
    float rz[9];
    float t[9];

    HeliSetRotX(rx, angle);
    HeliSetRotZ(rz, tilt);
    HeliMatrixMul(t, base, rx, 0, 0, 0);
    HeliMatrixMul(out, t, rz, 0, 0, 0);
}

}  // namespace igi
