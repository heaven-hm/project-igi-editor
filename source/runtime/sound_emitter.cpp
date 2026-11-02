#include "runtime/sound_emitter.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace igi {
namespace {

// ---------------------------------------------------------------------------
// The parameter schema
// ---------------------------------------------------------------------------

// Parameter schemas with authored names and types.
const SoundParamSpec kConditionalSoundParams[kConditionalSoundParamCount] = {
    {"Condition", "VarString", SoundParamType::VarString, 1,
     "script expression; non-zero runs the sound"},
    {"SoundDef", "String32", SoundParamType::String32, 1,
     "sound definition name, at most 0x20 bytes"},
    {"Position", "ObjectPos", SoundParamType::ObjectPos, 3,
     "world position, used only when not relative to the microphone"},
    {"Simple", "bool8", SoundParamType::Bool8, 1,
     "play through a one-frame sound handle instead of a positioned task"},
    {"OneShot", "bool8", SoundParamType::Bool8, 1,
     "after the first play, never stop and never replay"},
    {"Relative to microphone", "bool8", SoundParamType::Bool8, 1,
     "global: heard at any distance, no position, no pan"},
};

const SoundParamSpec kAmbientAreaParams[kAmbientAreaParamCount] = {
    {"Position", "ObjectPos", SoundParamType::ObjectPos, 3, "box centre"},
    {"Orientation", "Real32x9", SoundParamType::Real32x9, 3,
     "Euler x,y,z; expanded to a 9-float matrix by Matrix_SetEuler"},
    {"Size", "Real64x3", SoundParamType::Real64x3, 3,
     "half extents in metres; the box is size * 4096 units from the centre"},
    {"Min Delay", "Real64", SoundParamType::Real64, 1,
     "seconds; floor of the delay between two sounds"},
    {"Random wait", "Real64", SoundParamType::Real64, 1,
     "seconds; random part of that delay"},
    {"SoundDef", "String256", SoundParamType::String256, 1,
     "space, ';' or ',' separated list of definition names"},
};

// ---------------------------------------------------------------------------
// Matrix transformations
// ---------------------------------------------------------------------------

long double SinLD(long double angle) { return std::sin(angle); }
long double CosLD(long double angle) { return std::cos(angle); }

void MatrixSetRotX(float out_matrix[9], float angle) {
    const long double sin_val = SinLD(angle);
    const float cos_val = static_cast<float>(CosLD(angle));
    out_matrix[0] = 1.0f;
    out_matrix[1] = 0.0f;
    out_matrix[2] = 0.0f;
    out_matrix[3] = 0.0f;
    out_matrix[6] = 0.0f;
    out_matrix[4] = cos_val;
    out_matrix[5] = static_cast<float>(-sin_val);
    out_matrix[8] = cos_val;
    out_matrix[7] = static_cast<float>(sin_val);
}

void MatrixSetRotY(float out_matrix[9], float angle) {
    const long double sin_val = SinLD(angle);
    const long double cos_val = CosLD(angle);
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

void MatrixSetRotZ(float out_matrix[9], float angle) {
    const long double sin_val = SinLD(angle);
    const float cos_val = static_cast<float>(CosLD(angle));
    out_matrix[2] = 0.0f;
    out_matrix[5] = 0.0f;
    out_matrix[6] = 0.0f;
    out_matrix[7] = 0.0f;
    out_matrix[8] = 1.0f;
    out_matrix[0] = cos_val;
    out_matrix[1] = static_cast<float>(-sin_val);
    out_matrix[4] = cos_val;
    out_matrix[3] = static_cast<float>(sin_val);
}

void MatrixMul(float out_matrix[9], const float matrix_a[9], const float matrix_b[9]) {
    float temp_result[9];
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            const float* row_ptr = &matrix_a[row * 3];
            temp_result[row * 3 + col] = static_cast<float>(
                static_cast<long double>(row_ptr[0]) * matrix_b[col] +
                static_cast<long double>(row_ptr[2]) * matrix_b[6 + col] +
                static_cast<long double>(row_ptr[1]) * matrix_b[3 + col]);
        }
    }
    std::memcpy(out_matrix, temp_result, sizeof(temp_result));
}

// 64-bit integer limits for floating-point rounding guard.
// In C/C++ notation:
// LONG_MIN = -9223372036854775808L
// LONG_MAX =  9223372036854775807L
constexpr double kSoundRoundLimitMin = -9223372036854775808.0;
constexpr double kSoundRoundLimitMax =  9223372036854775807.0;

std::int8_t CharFromRounded(double value) {
    const double rounded = RoundHalfEven(value);
    if (!std::isfinite(rounded) || rounded < kSoundRoundLimitMin || rounded > kSoundRoundLimitMax) {
        return 0;
    }
    double reduced = std::fmod(rounded, 256.0);
    if (reduced < -128.0) {
        reduced += 256.0;
    } else if (reduced >= 128.0) {
        reduced -= 256.0;
    }
    return static_cast<std::int8_t>(reduced);
}

// Parses one authored token, mirroring the level loader's tolerance: a missing
// or unparsable token falls back rather than failing the whole instance.
double TokenDouble(const std::vector<std::string>& tokens, std::size_t index, double fallback) {
    if (index >= tokens.size()) return fallback;
    const std::string& token = tokens[index];
    if (token.empty()) return fallback;
    char* end = nullptr;
    const double value = std::strtod(token.c_str(), &end);
    if (end == token.c_str()) return fallback;
    return value;
}

int TokenInt(const std::vector<std::string>& tokens, std::size_t index, int fallback) {
    if (index >= tokens.size()) return fallback;
    const std::string& token = tokens[index];
    if (token.empty()) return fallback;
    char* end = nullptr;
    const long value = std::strtol(token.c_str(), &end, 10);
    if (end == token.c_str()) return fallback;
    return static_cast<int>(value);
}

// A bool8 script token. Any non-zero integer evaluates to true.
bool TokenBool(const std::vector<std::string>& tokens, std::size_t index, bool fallback) {
    if (index >= tokens.size()) return fallback;
    const std::string& token = tokens[index];
    if (token.empty()) return fallback;
    if (token == "TRUE" || token == "true") return true;
    if (token == "FALSE" || token == "false") return false;
    char* end = nullptr;
    const long value = std::strtol(token.c_str(), &end, 10);
    if (end == token.c_str()) return fallback;
    return value != 0;
}

// The level loader's argument layout: [0] task id, [1] task type, [2] name,
// [3] first declared parameter (source/level/level_objects.h argTokens).
constexpr std::size_t kFirstParam = 3;

}  // namespace

// ---------------------------------------------------------------------------
// Schema
// ---------------------------------------------------------------------------

const SoundParamSpec* ConditionalSoundParamSpecs(int* count) {
    if (count) *count = kConditionalSoundParamCount;
    return kConditionalSoundParams;
}

const SoundParamSpec* AmbientAreaParamSpecs(int* count) {
    if (count) *count = kAmbientAreaParamCount;
    return kAmbientAreaParams;
}

int SoundParamScriptArgs(SoundParamType type) {
    switch (type) {
        case SoundParamType::ObjectPos:
        case SoundParamType::Real64x3:
        case SoundParamType::Real32x9:
            return 3;
        default:
            return 1;
    }
}

const char* SoundParamTypeName(SoundParamType type) {
    switch (type) {
        case SoundParamType::VarString:
            return "VarString";
        case SoundParamType::String32:
            return "String32";
        case SoundParamType::ObjectPos:
            return "ObjectPos";
        case SoundParamType::Bool8:
            return "bool8";
        case SoundParamType::Real32x9:
            return "Real32x9";
        case SoundParamType::Real64x3:
            return "Real64x3";
        case SoundParamType::Real64:
            return "Real64";
        case SoundParamType::String256:
            return "String256";
    }
    return "";
}

int ConditionalSoundScriptArgs() {
    int total = 0;
    for (int i = 0; i < kConditionalSoundParamCount; ++i) {
        total += kConditionalSoundParams[i].script_args;
    }
    return total;
}

int AmbientAreaScriptArgs() {
    int total = 0;
    for (int i = 0; i < kAmbientAreaParamCount; ++i) {
        total += kAmbientAreaParams[i].script_args;
    }
    return total;
}

// ---------------------------------------------------------------------------
// Authored emitters
// ---------------------------------------------------------------------------

void MatrixFromEuler(const double euler[3], float matrix[9]) {
    const float angle_x = static_cast<float>(euler[0]);
    const float angle_y = static_cast<float>(euler[1]);
    const float angle_z = static_cast<float>(euler[2]);
    float temp_rot[9];
    MatrixSetRotZ(matrix, angle_z);
    MatrixSetRotY(temp_rot, angle_y);
    MatrixMul(matrix, matrix, temp_rot);
    MatrixSetRotX(temp_rot, angle_x);
    MatrixMul(matrix, matrix, temp_rot);
}

std::vector<std::string> SplitAmbientSoundList(const std::string& list) {
    std::vector<std::string> words;
    std::size_t start = 0;
    bool in_word = false;
    for (std::size_t i = 0; i <= list.size(); ++i) {
        const char c = (i == list.size()) ? '\0' : list[i];
        if (c == ' ' || c == ';' || c == ',' || c == '\0') {
            if (in_word) {
                words.push_back(list.substr(start, i - start));
                in_word = false;
            }
        } else if (!in_word) {
            start = i;
            in_word = true;
        }
    }
    return words;
}

bool ParseConditionalSound(const std::vector<std::string>& arg_tokens,
                           ConditionalSoundEmitter& out) {
    if (arg_tokens.size() < kFirstParam + static_cast<std::size_t>(ConditionalSoundScriptArgs())) {
        return false;
    }
    ConditionalSoundEmitter parsed;
    parsed.condition = arg_tokens[kFirstParam + 0];
    parsed.sound_def = arg_tokens[kFirstParam + 1];
    parsed.position[0] = TokenDouble(arg_tokens, kFirstParam + 2, 0.0);
    parsed.position[1] = TokenDouble(arg_tokens, kFirstParam + 3, 0.0);
    parsed.position[2] = TokenDouble(arg_tokens, kFirstParam + 4, 0.0);
    parsed.simple = TokenBool(arg_tokens, kFirstParam + 5, false);
    parsed.one_shot = TokenBool(arg_tokens, kFirstParam + 6, false);
    parsed.relative_to_microphone = TokenBool(arg_tokens, kFirstParam + 7, false);
    out = parsed;
    return true;
}

bool ParseAmbientArea(const std::vector<std::string>& arg_tokens, AmbientAreaResolved& out) {
    if (arg_tokens.size() < kFirstParam + static_cast<std::size_t>(AmbientAreaScriptArgs())) {
        return false;
    }
    AmbientAreaResolved parsed;
    parsed.authored_name = arg_tokens[2];
    parsed.authored.position[0] = TokenDouble(arg_tokens, kFirstParam + 0, 0.0);
    parsed.authored.position[1] = TokenDouble(arg_tokens, kFirstParam + 1, 0.0);
    parsed.authored.position[2] = TokenDouble(arg_tokens, kFirstParam + 2, 0.0);
    parsed.authored.orientation[0] = TokenDouble(arg_tokens, kFirstParam + 3, 0.0);
    parsed.authored.orientation[1] = TokenDouble(arg_tokens, kFirstParam + 4, 0.0);
    parsed.authored.orientation[2] = TokenDouble(arg_tokens, kFirstParam + 5, 0.0);
    parsed.authored.size[0] = TokenDouble(arg_tokens, kFirstParam + 6, 1.0);
    parsed.authored.size[1] = TokenDouble(arg_tokens, kFirstParam + 7, 1.0);
    parsed.authored.size[2] = TokenDouble(arg_tokens, kFirstParam + 8, 1.0);
    parsed.authored.min_delay = TokenDouble(arg_tokens, kFirstParam + 9, 0.0);
    parsed.authored.random_wait = TokenDouble(arg_tokens, kFirstParam + 10, 0.0);
    parsed.authored.sound_def = arg_tokens[kFirstParam + 11];
    MatrixFromEuler(parsed.authored.orientation, parsed.matrix);
    parsed.sounds = SplitAmbientSoundList(parsed.authored.sound_def);
    out = parsed;
    return true;
}

// ---------------------------------------------------------------------------
// Sound definitions
// ---------------------------------------------------------------------------

bool ParseSoundDefinition(const std::vector<std::string>& tokens, SoundDefinition& out) {
    if (tokens.size() != 13) return false;
    SoundDefinition parsed;
    parsed.name = tokens[0];
    parsed.sample = tokens[1];
    parsed.offset[0] = TokenDouble(tokens, 2, 0.0);
    parsed.offset[1] = TokenDouble(tokens, 3, 0.0);
    parsed.offset[2] = TokenDouble(tokens, 4, 0.0);
    parsed.volume = TokenDouble(tokens, 5, 1.0);
    parsed.min_dist = TokenDouble(tokens, 6, 0.0);
    parsed.max_dist = TokenDouble(tokens, 7, 1.0);
    parsed.volume_index = TokenInt(tokens, 8, -1);
    parsed.pitch_index = TokenInt(tokens, 9, -1);
    parsed.sample_index = TokenInt(tokens, 10, -1);
    parsed.unk124 = TokenDouble(tokens, 11, 0.0);
    parsed.loop = TokenBool(tokens, 12, false);
    out = parsed;
    return true;
}

void SoundDefinitionRegistry::Add(SoundDefinition definition) {
    definitions_.push_back(std::move(definition));
}

const SoundDefinition* SoundDefinitionRegistry::Find(const std::string& name) const {
    for (const SoundDefinition& definition : definitions_) {
        if (definition.name == name) return &definition;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Engine arithmetic
// ---------------------------------------------------------------------------

double RoundHalfEven(double value) {
    return std::nearbyint(value);
}

bool ConditionHolds(double value) { return CharFromRounded(value) != 0; }

bool SoundWithinAudibleRadius(double distance_units, double max_dist_m) {
    const float range = static_cast<float>(max_dist_m);
    const double scaled_range = static_cast<double>(static_cast<float>(range * 4096.0f));
    return scaled_range * scaled_range >= distance_units * distance_units;
}

double SoundAttenuatedVolume(double distance_units, double min_dist_m, double max_dist_m,
                             double volume) {
    const double min_dist = min_dist_m * kSoundDistScale;
    const double max_dist = max_dist_m * kSoundDistScale;
    double result = volume;
    if (distance_units > max_dist) {
        result = 0.0;
    } else if (distance_units >= min_dist) {
        const double factor = (1.0 - (distance_units - min_dist) / (max_dist - min_dist)) * volume;
        result = factor;
        if (!(factor >= 0.0)) {
            result = 0.0;
        } else if (result > 1.0) {
            result = 1.0;
        }
    }
    return result;
}

double SoundDistanceFade(double distance_units, double min_dist_m, double max_dist_m) {
    return SoundAttenuatedVolume(distance_units, min_dist_m, max_dist_m, 1.0);
}

double SoundPan(const double delta[3], const double listener_right[3], double distance_units,
                double min_dist_m, double max_dist_m) {
    const double min_dist = min_dist_m * kSoundDistScale;
    const double max_dist = max_dist_m * kSoundDistScale;
    if (!(distance_units > 0.0)) return 0.0;
    const volatile double right_projection = listener_right[0] * delta[0] + listener_right[1] * delta[1] +
                                             listener_right[2] * delta[2];
    const double distance_factor = distance_units / (max_dist + min_dist);
    double pan = right_projection / distance_units * distance_factor;
    if (!(pan >= -1.0)) {
        pan = -1.0;
    } else if (pan > 1.0) {
        pan = 1.0;
    }
    return pan;
}

void AmbientAreaLocalPoint(const float matrix[9], const double position[3],
                           const double listener[3], double local[3]) {
    const long double delta_x = static_cast<long double>(listener[0]) - static_cast<long double>(position[0]);
    const long double delta_y = static_cast<long double>(listener[1]) - static_cast<long double>(position[1]);
    const long double delta_z = static_cast<long double>(listener[2]) - static_cast<long double>(position[2]);
    local[0] = static_cast<double>((matrix[6] * delta_z + matrix[3] * delta_y) + matrix[0] * delta_x);
    local[1] = static_cast<double>((matrix[7] * delta_z + matrix[4] * delta_y) + matrix[1] * delta_x);
    local[2] = static_cast<double>((matrix[8] * delta_z + matrix[5] * delta_y) + matrix[2] * delta_x);
}

bool AmbientAreaContains(const double size[3], const double local[3]) {
    return size[0] * -kAmbientHalfExtentScale < local[0] &&
           size[0] * kAmbientHalfExtentScale > local[0] &&
           size[1] * -kAmbientHalfExtentScale < local[1] &&
           size[1] * kAmbientHalfExtentScale > local[1] &&
           size[2] * -kAmbientHalfExtentScale < local[2] &&
           size[2] * kAmbientHalfExtentScale > local[2];
}

bool AmbientAreaContainsListener(const float matrix[9], const double position[3],
                                 const double size[3], const double listener[3]) {
    const long double delta_x = static_cast<long double>(listener[0]) - static_cast<long double>(position[0]);
    const long double delta_y = static_cast<long double>(listener[1]) - static_cast<long double>(position[1]);
    const long double delta_z = static_cast<long double>(listener[2]) - static_cast<long double>(position[2]);
    const double local_x = static_cast<double>((matrix[6] * delta_z + matrix[3] * delta_y) + matrix[0] * delta_x);
    const double local_y = static_cast<double>((matrix[7] * delta_z + matrix[4] * delta_y) + matrix[1] * delta_x);
    const long double local_z = (matrix[8] * delta_z + matrix[5] * delta_y) + matrix[2] * delta_x;
    return size[0] * -kAmbientHalfExtentScale < local_x && size[0] * kAmbientHalfExtentScale > local_x &&
           size[1] * -kAmbientHalfExtentScale < local_y && size[1] * kAmbientHalfExtentScale > local_y &&
           local_z > size[2] * -kAmbientHalfExtentScale && local_z < size[2] * kAmbientHalfExtentScale;
}

int AmbientSoundIndexForRandom(double random_unit, int count) {
    return static_cast<int>(RoundHalfEven(random_unit * count));
}

int AmbientDelayFrames(double random_unit, double min_delay_seconds, double random_wait_seconds) {
    return static_cast<int>(
        RoundHalfEven((random_unit * random_wait_seconds + min_delay_seconds) * kAmbientTickRate));
}

// ---------------------------------------------------------------------------
// ConditionalSound
// ---------------------------------------------------------------------------

ConditionalSoundTick TickConditionalSound(ConditionalSoundState& state,
                                          const ConditionalSoundEmitter& emitter,
                                          bool has_condition, double condition_value) {
    ConditionalSoundTick tick;

    if (has_condition) {
        state.is_last_run = state.is_run;
        state.is_run = CharFromRounded(condition_value);
    }

    if (emitter.simple) {
        state.is_playing = state.stream_alive;
        if (state.is_playing) tick.request_stream = true;
    } else if (state.has_sound) {
        state.is_playing = state.sound_alive;
    }

    if (emitter.one_shot && state.played) {
        tick.is_playing = state.is_playing;
        return tick;
    }

    if (!state.is_run) {
        if (state.has_sound) {
            tick.stop_sound = true;
            state.has_sound = false;
            state.is_playing = false;
        }
    } else if (!state.is_last_run) {
        tick.start_sound = true;
        if (!emitter.simple) state.has_sound = true;
        state.played = true;
        state.is_playing = true;
    }

    tick.is_playing = state.is_playing;
    return tick;
}

void ResetConditionalSound(ConditionalSoundState& state) {
    state.has_sound = false;
    state.is_playing = false;
    state.is_run = 0;
    state.is_last_run = 0;
}

// ---------------------------------------------------------------------------
// AmbientArea
// ---------------------------------------------------------------------------

AmbientAreaTick TickAmbientArea(AmbientAreaState& state, const AmbientAreaResolved& area,
                                const double listener[3], bool has_condition,
                                double condition_value, double random_unit,
                                double random_unit_for_delay) {
    AmbientAreaTick tick;
    long double volume_level = 0.0L;
    bool inside = false;

    if (has_condition && !ConditionHolds(condition_value)) {
        volume_level = static_cast<long double>(state.volume) - kAmbientFadeStep;
    } else {
        if (AmbientAreaContainsListener(area.matrix, area.authored.position, area.authored.size,
                                        listener)) {
            volume_level = static_cast<long double>(state.volume) + kAmbientFadeStep;
            inside = true;
        } else {
            volume_level = static_cast<long double>(state.volume) - kAmbientFadeStep;
        }
    }

    state.volume = static_cast<double>(volume_level);
    if (!(volume_level >= 0.0L)) {
        state.volume = 0.0;
    } else if (volume_level > 1.0L) {
        state.volume = 1.0;
    }

    const bool sound_died = state.has_sound && !state.sound_alive;
    if (state.volume == 0.0 || sound_died) {
        state.delay = AmbientDelayFrames(random_unit_for_delay, area.authored.min_delay,
                                         area.authored.random_wait);
        if (state.has_sound) {
            tick.stop_sound = true;
            state.has_sound = false;
        }
    } else if (state.has_sound) {
        tick.update_playing = true;
    } else if (inside) {
        const int index = AmbientSoundIndexForRandom(random_unit, (int)area.sounds.size());
        if (state.delay == 0) {
            tick.start_sound = true;
            tick.sound_index = index;
        }
    }

    if (state.delay) state.delay--;

    tick.volume = state.volume;
    tick.inside = inside;
    return tick;
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

const char* SoundIssueKindName(SoundIssueKind kind) {
    switch (kind) {
        case SoundIssueKind::EmptyCondition:
            return "empty-condition";
        case SoundIssueKind::EmptySoundDef:
            return "empty-sound-def";
        case SoundIssueKind::NameTooLong:
            return "name-too-long";
        case SoundIssueKind::UnresolvedSoundDef:
            return "unresolved-sound-def";
        case SoundIssueKind::NoSounds:
            return "no-sounds";
        case SoundIssueKind::TooManySounds:
            return "too-many-sounds";
        case SoundIssueKind::SoundWordTooLong:
            return "sound-word-too-long";
        case SoundIssueKind::SoundListTooLong:
            return "sound-list-too-long";
        case SoundIssueKind::NonPositiveSize:
            return "non-positive-size";
        case SoundIssueKind::NegativeDelay:
            return "negative-delay";
    }
    return "";
}

std::vector<SoundIssue> ValidateConditionalSound(const ConditionalSoundEmitter& emitter,
                                                 const SoundDefinitionRegistry* registry) {
    std::vector<SoundIssue> issues;
    if (emitter.condition.empty()) {
        issues.push_back({SoundIssueKind::EmptyCondition,
                          "no Condition expression: this emitter can never run"});
    }
    if (emitter.sound_def.empty()) {
        issues.push_back({SoundIssueKind::EmptySoundDef, "no SoundDef: nothing to play"});
    } else if ((int)emitter.sound_def.size() >= kSoundDefNameCapacity) {
        issues.push_back({SoundIssueKind::NameTooLong,
                          "SoundDef is at least 32 bytes; string buffer truncates it"});
    } else if (registry && !registry->Find(emitter.sound_def)) {
        issues.push_back({SoundIssueKind::UnresolvedSoundDef,
                          "no DefineSound for '" + emitter.sound_def + "'"});
    }
    return issues;
}

std::vector<SoundIssue> ValidateAmbientArea(const AmbientAreaResolved& area,
                                            const SoundDefinitionRegistry* registry) {
    std::vector<SoundIssue> issues;
    const std::string& list = area.authored.sound_def;

    if (list.size() >= (std::size_t)kAmbientSoundListCapacity) {
        issues.push_back({SoundIssueKind::SoundListTooLong,
                          "SoundDef list is at least 0x100 bytes; parameter truncates it"});
    }
    if (list.size() >= (std::size_t)kAmbientSoundSlotCapacity) {
        issues.push_back({SoundIssueKind::SoundListTooLong,
                          "SoundDef list is at least 32 bytes; exceeds maximum slot capacity"});
    }

    if (area.sounds.empty()) {
        issues.push_back({SoundIssueKind::NoSounds, "no sound names in the SoundDef list"});
    }
    if ((int)area.sounds.size() > kAmbientSoundSlots) {
        issues.push_back({SoundIssueKind::TooManySounds,
                          "more than 8 sound names; exceeding available slots"});
    }
    for (const std::string& word : area.sounds) {
        if ((int)word.size() >= kAmbientSoundSlotCapacity) {
            issues.push_back({SoundIssueKind::SoundWordTooLong,
                              "'" + word + "' is at least 32 bytes; exceeds slot capacity"});
        }
    }

    for (int axis = 0; axis < 3; ++axis) {
        if (!(area.authored.size[axis] > 0.0)) {
            // size * +/-4096 collapses to an empty interval, so the strict
            // comparisons can never both hold (ambientarea.c:142-143).
            issues.push_back({SoundIssueKind::NonPositiveSize,
                              "Size axis " + std::to_string(axis) +
                                  " is not positive: the box contains nothing"});
        }
    }
    if (area.authored.min_delay < 0.0 || area.authored.random_wait < 0.0) {
        // AmbientDelayFrames would be negative, and `if (a->delay) a->delay--`
        // then counts up from a negative value, so the sound can never start
        // (ambientarea.c:120-123,166,176-177).
        issues.push_back({SoundIssueKind::NegativeDelay,
                          "Min Delay or Random wait is negative: the delay counts up"});
    }

    if (registry) {
        for (const std::string& word : area.sounds) {
            if (!registry->Find(word)) {
                issues.push_back({SoundIssueKind::UnresolvedSoundDef,
                                  "no DefineSound for '" + word + "'"});
            }
        }
    }
    return issues;
}

}  // namespace igi
