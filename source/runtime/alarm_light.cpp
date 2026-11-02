// alarm_light.cpp - AlarmLight task parameter parsing, matrix transformation, and state evaluation.

#include "runtime/alarm_light.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace igi {
namespace {

// ---------------------------------------------------------------------------
// Parameter token parsers
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
// 3x3 Matrix Transformations
// ---------------------------------------------------------------------------

// 3x3 matrix product in row-major layout with ordered component accumulation
void MatrixMul(float* out_matrix, const float* matrix_a, const float* matrix_b) {
    float result[9];
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            const float* row_ptr = &matrix_a[row * 3];
            result[row * 3 + col] = static_cast<float>(
                static_cast<long double>(row_ptr[0]) * matrix_b[col] +
                static_cast<long double>(row_ptr[2]) * matrix_b[6 + col] +
                static_cast<long double>(row_ptr[1]) * matrix_b[3 + col]);
        }
    }
    std::memcpy(out_matrix, result, sizeof(result));
}

// Rotation matrix about X-axis
void MatrixSetRotX(float* out_matrix, float angle) {
    const long double sin_val = std::sin(static_cast<long double>(angle));
    const float cos_val = static_cast<float>(std::cos(static_cast<long double>(angle)));
    out_matrix[0] = 1.0f; out_matrix[1] = 0.0f; out_matrix[2] = 0.0f;
    out_matrix[3] = 0.0f; out_matrix[4] = cos_val; out_matrix[5] = static_cast<float>(-sin_val);
    out_matrix[6] = 0.0f; out_matrix[7] = static_cast<float>(sin_val); out_matrix[8] = cos_val;
}

// Rotation matrix about Y-axis
void MatrixSetRotY(float* out_matrix, float angle) {
    const long double sin_val = std::sin(static_cast<long double>(angle));
    const long double cos_val = std::cos(static_cast<long double>(angle));
    out_matrix[0] = static_cast<float>(cos_val);  out_matrix[1] = 0.0f; out_matrix[2] = static_cast<float>(sin_val);
    out_matrix[3] = 0.0f;                         out_matrix[4] = 1.0f; out_matrix[5] = 0.0f;
    out_matrix[6] = static_cast<float>(-sin_val); out_matrix[7] = 0.0f; out_matrix[8] = static_cast<float>(cos_val);
}

// Rotation matrix about Z-axis
void MatrixSetRotZ(float* out_matrix, float angle) {
    const long double sin_val = std::sin(static_cast<long double>(angle));
    const float cos_val = static_cast<float>(std::cos(static_cast<long double>(angle)));
    out_matrix[0] = cos_val;                     out_matrix[1] = static_cast<float>(-sin_val); out_matrix[2] = 0.0f;
    out_matrix[3] = static_cast<float>(sin_val); out_matrix[4] = cos_val;                     out_matrix[5] = 0.0f;
    out_matrix[6] = 0.0f;                        out_matrix[7] = 0.0f;                        out_matrix[8] = 1.0f;
}

}  // namespace

// ---------------------------------------------------------------------------
// Parameter Schema
// ---------------------------------------------------------------------------

const AlarmLightParamDesc* AlarmLightParamTable() {
    static const AlarmLightParamDesc kTable[kAlarmLightParamCount] = {
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

    // Position coordinates
    parsed.place[0] = TokenDouble(arg_tokens, base + 0);
    parsed.place[1] = TokenDouble(arg_tokens, base + 1);
    parsed.place[2] = TokenDouble(arg_tokens, base + 2);

    // Orientation Euler angles
    parsed.orientation[0] = TokenFloat(arg_tokens, base + 3);
    parsed.orientation[1] = TokenFloat(arg_tokens, base + 4);
    parsed.orientation[2] = TokenFloat(arg_tokens, base + 5);
    AlarmLightEulerMatrix(parsed.orientation, parsed.matrix);

    // Associated models and assets
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
// Tick Update and Evaluation
// ---------------------------------------------------------------------------

// 64-bit integer limits for floating-point rounding guard.
// In C/C++ notation:
// LONG_MIN = -9223372036854775808L
// LONG_MAX =  9223372036854775807L
constexpr long double kAlarmLightLongMin64 = -9223372036854775808.0L;
constexpr long double kAlarmLightLongMax64 =  9223372036854775807.0L;

long long AlarmLightRoundLL(long double value) {
    if (std::isnan(value) || value < kAlarmLightLongMin64 || value > kAlarmLightLongMax64) {
        return std::numeric_limits<long long>::min();
    }
    return static_cast<long long>(std::nearbyint(value));
}

AlarmLightTick AlarmLightRun(AlarmLightState& state, double cond_value, bool has_cond) {
    AlarmLightTick tick;
    const bool was_on = state.is_on;

    const bool want_sound = was_on && !state.is_destroyed;
    tick.sound_changed = (want_sound != state.sound_playing);
    state.sound_playing = want_sound;
    tick.sound_wanted = want_sound;

    if (state.is_destroyed) {
        tick.ran_children = false;
        tick.is_on = state.is_on;
        return tick;
    }

    if (has_cond) {
        const long long rounded = AlarmLightRoundLL(static_cast<long double>(cond_value));
        state.is_on = static_cast<char>(rounded) != 0;
    }

    if (was_on) {
        if (!state.is_on) tick.model_slot = kAlarmLightModelSlotWhenTurningOff;
    } else if (state.is_on) {
        tick.model_slot = kAlarmLightModelSlotWhenTurningOn;
    }

    tick.is_on = state.is_on;
    return tick;
}

// ---------------------------------------------------------------------------
// Flare & Reflector Dynamics
// ---------------------------------------------------------------------------

float AlarmLightReflectorAngle(float angle, bool is_on, bool is_destroyed) {
    if (is_on && !is_destroyed) return angle + kAlarmLightTurn;
    return angle;
}

float AlarmLightFlareRed(float angle) {
    long double normalized_phase =
        std::fmod(static_cast<long double>(angle), static_cast<long double>(kAlarmLightPi)) *
            static_cast<long double>(kAlarmLightInvPi) -
        static_cast<long double>(kAlarmLightHalf);
    if (normalized_phase < static_cast<long double>(0.0f) || std::isnan(normalized_phase)) {
        normalized_phase = -normalized_phase;
    }
    return static_cast<float>((normalized_phase + normalized_phase) *
                              static_cast<long double>(kAlarmLightFlareScale));
}

double AlarmLightFlarePeriodTicks() {
    return static_cast<double>(static_cast<long double>(kAlarmLightPi) /
                               static_cast<long double>(kAlarmLightTurn));
}

float AlarmLightFlareRedAfterTicks(int ticks) {
    float angle = 0.0f;
    for (int i = 0; i < ticks; ++i) angle = AlarmLightReflectorAngle(angle, true, false);
    return AlarmLightFlareRed(angle);
}

bool AlarmLightFlareFacesCamera(const double light_pos[3], const float matrix[9],
                                const double camera_pos[3]) {
    if (light_pos == nullptr || matrix == nullptr || camera_pos == nullptr) return false;
    const float matrix_col1_row0 = matrix[1];
    const float matrix_col1_row1 = matrix[4];
    const float matrix_col1_row2 = matrix[7];
    const long double delta_x = static_cast<long double>(camera_pos[0]) - light_pos[0];
    const long double delta_y = static_cast<long double>(camera_pos[1]) - light_pos[1];
    const long double delta_z = static_cast<long double>(camera_pos[2]) - light_pos[2];
    return delta_z * matrix_col1_row2 + delta_y * matrix_col1_row1 + delta_x * matrix_col1_row0 > 0.0L;
}

// ---------------------------------------------------------------------------
// Orientation Decomposition
// ---------------------------------------------------------------------------

void AlarmLightEulerMatrix(const float euler_angles[3], float out_matrix[9]) {
    if (euler_angles == nullptr || out_matrix == nullptr) return;
    float temp_rotation[9];
    MatrixSetRotZ(out_matrix, euler_angles[2]);
    MatrixSetRotY(temp_rotation, euler_angles[1]);
    MatrixMul(out_matrix, out_matrix, temp_rotation);
    MatrixSetRotX(temp_rotation, euler_angles[0]);
    MatrixMul(out_matrix, out_matrix, temp_rotation);
}

const AlarmLightExplosion& AlarmLightDestroyExplosion() {
    static const AlarmLightExplosion kExplosion;
    return kExplosion;
}

}  // namespace igi
