#include "runtime/alarm_light.h"

#include <cmath>
#include <cstdlib>
#include <limits>

namespace igi {
namespace {

// ---------------------------------------------------------------------------
// Authored token readers (same conventions as mission_fog.cpp / smoke.cpp)
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

std::string TokenString(const std::vector<std::string>& tokens, std::size_t index) {
    if (index >= tokens.size()) return std::string();
    return tokens[index];
}

// ---------------------------------------------------------------------------
// The engine's 3x3 helpers (src/math/matrix.c)
// ---------------------------------------------------------------------------

// Mul 0x4B3400-ish (matrix.c:10-25): out = a * b in the engine's row-major
// layout. The summation order is the engine's -- r[0]*b[j], then r[2]*b[6+j],
// then r[1]*b[3+j] -- which is not the textbook order and therefore not the
// same floating-point result.
void MatrixMul(float* out, const float* a, const float* b) {
    float t[9];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            const float* r = &a[i * 3];
            t[i * 3 + j] = static_cast<float>(static_cast<long double>(r[0]) * b[j] +
                                              static_cast<long double>(r[2]) * b[6 + j] +
                                              static_cast<long double>(r[1]) * b[3 + j]);
        }
    }
    for (int i = 0; i < 9; ++i) out[i] = t[i];
}

// Matrix_SetRotX 0x4B3B60 (matrix.c:112-128). `s` stays long double and `c` is
// rounded to float, exactly as the original's declarations do.
void MatrixSetRotX(float* m, float a) {
    const long double s = std::sin(static_cast<long double>(a));
    const float c = static_cast<float>(std::cos(static_cast<long double>(a)));
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

// Matrix_SetRotY 0x4B3BA0 (matrix.c:130-146).
void MatrixSetRotY(float* m, float a) {
    const long double s = std::sin(static_cast<long double>(a));
    const long double c = std::cos(static_cast<long double>(a));
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

// Matrix_SetRotZ 0x4B3BE0 (matrix.c:148-164).
void MatrixSetRotZ(float* m, float a) {
    const long double s = std::sin(static_cast<long double>(a));
    const float c = static_cast<float>(std::cos(static_cast<long double>(a)));
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

}  // namespace

// ---------------------------------------------------------------------------
// The authored parameter table
// ---------------------------------------------------------------------------

const AlarmLightParamDesc* AlarmLightParamTable() {
    // Order is the ParamTable_Add order (alarm.c:109-115). The authored name
    // and type columns are the retail Task_DeclareParameters call as it appears
    // in the QVM bytecode (LEVEL9/OBJECTS.QVM code 0x7070-0x7127), verbatim --
    // including the retail typo "Destroyde model".
    static const AlarmLightParamDesc kTable[kAlarmLightParamCount] = {
        // name                  engine field     type          n  model slot
        {"Position",             "place",         "ObjectPos",  3, -1},
        {"Orientation",          "placeMatrix",   "Real32x9",   3, -1},
        {"On expression",        "cond",          "VarString",  1, -1},
        {"On model",             "offModel",      "String16",   1, kAlarmLightModelSlotA},
        {"Off model",            "onModel",       "String16",   1, kAlarmLightModelSlotB},
        {"Destroyde model",      "deadModel",     "String16",   1, kAlarmLightModelSlotC},
        {"On sound",             "onSound",       "String16",   1, -1},
        {"Explode sound",        "deadSound",     "String16",   1, -1},
    };
    return kTable;
}

const std::string& AlarmLightParams::ModelForSlot(int slot) const {
    static const std::string kEmpty;
    switch (slot) {
        case kAlarmLightModelSlotA: return on_model;
        case kAlarmLightModelSlotB: return off_model;
        case kAlarmLightModelSlotC: return destroyed_model;
        default: return kEmpty;
    }
}

bool ParseAlarmLightParams(const std::vector<std::string>& arg_tokens, AlarmLightParams* out) {
    if (out == nullptr) return false;
    if (arg_tokens.size() < kAlarmLightFirstParam + static_cast<std::size_t>(kAlarmLightTokenCount)) {
        return false;
    }

    AlarmLightParams parsed;
    const std::size_t base = kAlarmLightFirstParam;

    // Slot 0: "Position", ObjectPos, three doubles (paramops.c:182 alias chain
    // ObjectPos -> Real64x3).
    parsed.place[0] = TokenDouble(arg_tokens, base + 0);
    parsed.place[1] = TokenDouble(arg_tokens, base + 1);
    parsed.place[2] = TokenDouble(arg_tokens, base + 2);

    // Slot 1: "Orientation", declared Real32x9 but authored as THREE Euler
    // angles; ParamReal32x9_Parse 0x4EFBA0 reads x, y, z and calls
    // Matrix_SetEuler (paramops.c:423-435).
    parsed.orientation[0] = TokenFloat(arg_tokens, base + 3);
    parsed.orientation[1] = TokenFloat(arg_tokens, base + 4);
    parsed.orientation[2] = TokenFloat(arg_tokens, base + 5);
    AlarmLightEulerMatrix(parsed.orientation, parsed.matrix);

    // Slots 2..7: one token each.
    parsed.on_expression = TokenString(arg_tokens, base + 6);
    parsed.on_model = TokenString(arg_tokens, base + 7);
    parsed.off_model = TokenString(arg_tokens, base + 8);
    parsed.destroyed_model = TokenString(arg_tokens, base + 9);
    parsed.on_sound = TokenString(arg_tokens, base + 10);
    parsed.explode_sound = TokenString(arg_tokens, base + 11);

    *out = parsed;
    return true;
}

std::vector<AlarmLightParams> ResolveAlarmLightTasks(const std::vector<AlarmLightTaskObject>& objects) {
    std::vector<AlarmLightParams> out;
    for (const AlarmLightTaskObject& object : objects) {
        if (object.type != "AlarmLight") continue;
        AlarmLightParams params;
        if (ParseAlarmLightParams(object.arg_tokens, &params)) out.push_back(params);
    }
    return out;
}

// ---------------------------------------------------------------------------
// The per-tick state machine
// ---------------------------------------------------------------------------

long long AlarmLightRoundLL(long double value) {
    // animcam.h:67-73 is `fistpll` with the x87 control word's rounding mode:
    // round-to-nearest-even, matching std::nearbyint under the default mode. NaN
    // and out-of-range leave the x87 "integer indefinite" 0x8000000000000000.
    if (std::isnan(value)) return std::numeric_limits<long long>::min();
    if (!(value > -9.223372036854775808e18L && value < 9.223372036854775808e18L)) {
        return std::numeric_limits<long long>::min();
    }
    return static_cast<long long>(std::nearbyint(value));
}

AlarmLightTick AlarmLightRun(AlarmLightState& state, double cond_value, bool has_cond) {
    AlarmLightTick tick;
    const bool was_on = state.is_on;

    // elevator/alarmlight.c:48-54. The decision reads the state at ENTRY, so the
    // sound task lags the lamp by one tick in both directions.
    const bool want_sound = was_on && !state.is_destroyed;
    tick.sound_changed = (want_sound != state.sound_playing);
    state.sound_playing = want_sound;
    tick.sound_wanted = want_sound;

    // elevator/alarmlight.c:55-56: a destroyed lamp returns before the
    // expression, the model swap, and its children.
    if (state.is_destroyed) {
        tick.ran_children = false;
        tick.is_on = state.is_on;
        return tick;
    }

    // elevator/alarmlight.c:57-58: `al->isOn = (char)round_ll(VarString_EvalReal(...))`.
    // The char cast truncates to eight bits, so a value of 256 switches the lamp
    // off again; the port keeps that.
    if (has_cond) {
        const long long rounded = AlarmLightRoundLL(static_cast<long double>(cond_value));
        state.is_on = static_cast<char>(rounded) != 0;
    }

    // elevator/alarmlight.c:59-64: the model is swapped only on a transition,
    // and the two slots are the ones documented in the header.
    if (was_on) {
        if (!state.is_on) tick.model_slot = kAlarmLightModelSlotWhenTurningOff;
    } else if (state.is_on) {
        tick.model_slot = kAlarmLightModelSlotWhenTurningOn;
    }

    tick.is_on = state.is_on;
    return tick;
}

// ---------------------------------------------------------------------------
// The reflector
// ---------------------------------------------------------------------------

float AlarmLightReflectorAngle(float angle, bool is_on, bool is_destroyed) {
    // alarm.c:158-166: the whole body, including `t->angle = t->angle + kTurn`,
    // is inside `if (l->isOn && !l->isDestroyed)`.
    if (is_on && !is_destroyed) return angle + kAlarmLightTurn;
    return angle;
}

float AlarmLightFlareRed(float angle) {
    // alarm.c:235-242. `crt_fmod(t->angle, kPi)` is the CRT fmod in long double;
    // `* kInvPi - kHalfF` mixes the float constants in. `k < kZeroF || k != k`
    // is the original's "negative or NaN" test, and negating a NaN leaves it
    // NaN -- reproduced rather than clamped.
    long double k = std::fmod(static_cast<long double>(angle),
                              static_cast<long double>(kAlarmLightPi)) *
                        static_cast<long double>(kAlarmLightInvPi) -
                    static_cast<long double>(kAlarmLightHalf);
    if (k < static_cast<long double>(0.0f) || std::isnan(k)) k = -k;
    return static_cast<float>((k + k) * static_cast<long double>(kAlarmLightFlareScale));
}

double AlarmLightFlarePeriodTicks() {
    // fmod(angle, pi) drives the wave, so one period of the red channel is a
    // sweep of pi in `angle`, i.e. pi / kTurn Run calls. kTurn is a float
    // approximation of pi/30, so the exact answer is 29.999999165173982; the
    // engine never computes this, it is derived from the constants at alarm.c:71
    // and alarm.c:74.
    return static_cast<double>(static_cast<long double>(kAlarmLightPi) /
                               static_cast<long double>(kAlarmLightTurn));
}

float AlarmLightFlareRedAfterTicks(int ticks) {
    // The engine accumulates the angle in a float field (alarm.c:162, :166), so
    // the port accumulates too instead of computing ticks * kTurn at once.
    float angle = 0.0f;
    for (int i = 0; i < ticks; ++i) angle = AlarmLightReflectorAngle(angle, true, false);
    return AlarmLightFlareRed(angle);
}

bool AlarmLightFlareFacesCamera(const double light_pos[3], const float matrix[9],
                                const double camera_pos[3]) {
    // alarm.c:220-227. Note the axis is the light's AUTHORED placeMatrix row 1
    // (m[1], m[4], m[7]), not the turned reflector matrix.
    if (light_pos == nullptr || matrix == nullptr || camera_pos == nullptr) return false;
    const float m1 = matrix[1];
    const float m4 = matrix[4];
    const float m7 = matrix[7];
    const long double dx = static_cast<long double>(camera_pos[0]) - light_pos[0];
    const long double dy = static_cast<long double>(camera_pos[1]) - light_pos[1];
    const long double dz = static_cast<long double>(camera_pos[2]) - light_pos[2];
    return dz * m7 + dy * m4 + dx * m1 > 0.0L;
}

// ---------------------------------------------------------------------------
// Matrix_SetEuler
// ---------------------------------------------------------------------------

void AlarmLightEulerMatrix(const float euler[3], float out_matrix[9]) {
    // matrix.c:99-110: SetRotZ(az), then Mul(m, m, RotY(ay)), then
    // Mul(m, m, RotX(ax)). Mul is left-multiplying, so the product is
    // RotZ * RotY * RotX.
    if (euler == nullptr || out_matrix == nullptr) return;
    float r[9];
    MatrixSetRotZ(out_matrix, euler[2]);
    MatrixSetRotY(r, euler[1]);
    MatrixMul(out_matrix, out_matrix, r);
    MatrixSetRotX(r, euler[0]);
    MatrixMul(out_matrix, out_matrix, r);
}

const AlarmLightExplosion& AlarmLightDestroyExplosion() {
    // elevator/alarmlight.c:126-127, argument for argument.
    static const AlarmLightExplosion kExplosion;
    return kExplosion;
}

}  // namespace igi
