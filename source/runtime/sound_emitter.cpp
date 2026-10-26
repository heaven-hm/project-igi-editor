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

// src/vehicle/condsound.c:86-92 (ParamTable_New(6) + six ParamTable_Add), with
// the authored names from LEVEL1/objects.qsc:78.
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

// src/aiaction/ambientarea.c:73-79 (ParamTable_New(6) + six ParamTable_Add),
// with the authored names from LEVEL1/objects.qsc:55.
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
// Matrix helpers, ported operation for operation from src/math/matrix.c
// ---------------------------------------------------------------------------

// x87_sin/x87_cos are fsin/fcos on the x87 FPU (src/math/x87.h:19-29). The host
// has no x87 transcendental unit, so std::sin/std::cos on long double stand in;
// the results can differ in the last bits, which the tests allow for.
long double SinLD(long double a) { return std::sin(a); }
long double CosLD(long double a) { return std::cos(a); }

// src/math/matrix.c:112-128, HOOK 0x4B3B60. `s` stays long double, `c` is
// narrowed to float, exactly as the reconstruction does.
void SetRotX(float m[9], float a) {
    const long double s = SinLD(a);
    const float c = (float)CosLD(a);
    m[0] = 1.0f;
    m[1] = 0.0f;
    m[2] = 0.0f;
    m[3] = 0.0f;
    m[6] = 0.0f;
    m[4] = c;
    m[5] = (float)(-s);
    m[8] = c;
    m[7] = (float)s;
}

// src/math/matrix.c:130-146, HOOK 0x4B3BA0. Both stay long double here.
void SetRotY(float m[9], float a) {
    const long double s = SinLD(a);
    const long double c = CosLD(a);
    m[1] = 0.0f;
    m[3] = 0.0f;
    m[4] = 1.0f;
    m[5] = 0.0f;
    m[7] = 0.0f;
    m[0] = (float)c;
    m[2] = (float)s;
    m[6] = (float)(-s);
    m[8] = (float)c;
}

// src/math/matrix.c:148-164, HOOK 0x4B3BE0.
void SetRotZ(float m[9], float a) {
    const long double s = SinLD(a);
    const float c = (float)CosLD(a);
    m[2] = 0.0f;
    m[5] = 0.0f;
    m[6] = 0.0f;
    m[7] = 0.0f;
    m[8] = 1.0f;
    m[0] = c;
    m[1] = (float)(-s);
    m[4] = c;
    m[3] = (float)s;
}

// src/math/matrix.c:9-21: out = a * b, accumulated in long double and summed in
// the original's order k = 0, 2, 1 (which is why the terms are not written in
// index order below), then narrowed to float.
void Mul(const float a[9], const float b[9], float out[9]) {
    float t[9];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            const float* r = &a[i * 3];
            t[i * 3 + j] = (float)((long double)r[0] * b[j] + (long double)r[2] * b[6 + j] +
                                   (long double)r[1] * b[3 + j]);
        }
    }
    std::memcpy(out, t, sizeof(t));
}

// `(char)round_int(v)` (src/vehicle/condsound.c:125, src/aiaction/ambientarea.c:131).
// round_int is an x87 fistpll (src/missile/missile.h:111-117), so the value is
// rounded with the current FP mode and then stored to an integer; the `(char)`
// cast keeps the low byte. Values outside the 64-bit integer range are
// undefined in the engine (fistpll would raise); this returns 0 for them.
std::int8_t CharFromRounded(double value) {
    const double rounded = RoundHalfEven(value);
    if (!(rounded == rounded)) return 0;  // NaN
    if (std::fabs(rounded) >= 9.2233720368547758e18) return 0;
    // fmod is exact, so this reproduces the low byte of the integer value.
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

// A bool8 script token. The retail scripts spell these TRUE/FALSE
// (LEVEL1/objects.qsc:562); ParamBool8_Parse is Script_GetArgInt, so any
// non-zero integer is true (src/level/paramtype.c:151-157).
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
    // kParamTypes maps these three to Param_Three, which sets *n = 3
    // (src/physobj/paramops.c:143,146,152 and :398-402); ObjectPos is an alias
    // of Real64x3 (paramops.c:183). Every other type is Param_One, *n = 1
    // (paramops.c:355-359).
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
    // ParamReal32x9_Parse takes float angles (src/physobj/paramops.c:423-435:
    // `float x, y, z; ... Matrix_SetEuler(m, x, y, z)`), so narrow first.
    const float ax = static_cast<float>(euler[0]);
    const float ay = static_cast<float>(euler[1]);
    const float az = static_cast<float>(euler[2]);
    float r[9];
    // Matrix_SetEuler (src/math/matrix.c:99-110): Rz, then * Ry, then * Rx.
    SetRotZ(matrix, az);
    SetRotY(r, ay);
    Mul(matrix, r, matrix);
    SetRotX(r, ax);
    Mul(matrix, r, matrix);
}

std::vector<std::string> SplitAmbientSoundList(const std::string& list) {
    // AmbientArea_Changed (src/aiaction/ambientarea.c:198-221) walks the string
    // and cuts on ' ', ';' and ','; empty words are skipped because `inWord`
    // gates the copy. The engine stops at the NUL it wrote itself, so an
    // embedded NUL cannot occur here.
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
    // Order is the ParamTable order (src/vehicle/condsound.c:87-92).
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
    // Order is the ParamTable order (src/aiaction/ambientarea.c:74-79).
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
    // The argument list SoundDefSound_Write prints
    // (src/define/sounddef.c:206-218).
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
    // SoundDefSound_Find only accepts a match while nothing has matched yet
    // (src/define/sounddef.c:221-226), so the first definition of a name wins -
    // which is why COMMON's two machinery_01 definitions resolve to the first.
    for (const SoundDefinition& definition : definitions_) {
        if (definition.name == name) return &definition;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Engine arithmetic
// ---------------------------------------------------------------------------

double RoundHalfEven(double value) {
    // round_int is `fistpll`, a store with the current FP rounding mode
    // (src/missile/missile.h:111-117). std::nearbyint follows the same mode and
    // does not raise inexact, so it is the exact analogue; the default mode is
    // round-to-nearest with ties to even, which is what the retail code sees.
    return std::nearbyint(value);
}

bool ConditionHolds(double value) { return CharFromRounded(value) != 0; }

bool SoundWithinAudibleRadius(double distance_units, double max_dist_m) {
    // SoundDefSound_Play, HOOK 0x4E8520 (src/define/sounddef.c:65-71): only for
    // positional sounds (flags & 2, set at sounddef.c:107), with
    // r = maxDist * kDistScale, and it returns unless r*r >= DistanceSq. The
    // comparison is therefore inclusive of the boundary. s->range and
    // kDistScale are both float (src/define/define.h:82), so the product is a
    // float multiply; rd() then widens it to double (define.h:92-97).
    const float range = static_cast<float>(max_dist_m);
    const double r = static_cast<double>(static_cast<float>(range * 4096.0f));
    return r * r >= distance_units * distance_units;
}

double SoundAttenuatedVolume(double distance_units, double min_dist_m, double max_dist_m,
                             double volume) {
    // Voice_SetParams, HOOK 0x496E60 (src/transctx/voice.c:584-600).
    const double min_dist = min_dist_m * kSoundDistScale;
    const double max_dist = max_dist_m * kSoundDistScale;
    double result = volume;
    if (distance_units > max_dist) {
        result = 0.0;
    } else if (distance_units >= min_dist) {
        // kOneD - (dist - minDist) / (maxDist - minDist), times the volume.
        const double k = (1.0 - (distance_units - min_dist) / (max_dist - min_dist)) * volume;
        result = k;
        // `if (!(k >= kZero.f))` - this also catches NaN, which is what a
        // zero-width min/max pair produces at exactly minDist.
        if (!(k >= 0.0)) {
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
    // src/transctx/voice.c:601-611. `r` is a volatile double in the engine, i.e.
    // the product is rounded to double before use.
    const double min_dist = min_dist_m * kSoundDistScale;
    const double max_dist = max_dist_m * kSoundDistScale;
    if (!(distance_units > 0.0)) return 0.0;
    const volatile double r = listener_right[0] * delta[0] + listener_right[1] * delta[1] +
                              listener_right[2] * delta[2];
    const double k = distance_units / (max_dist + min_dist);
    double pan = r / distance_units * k;
    if (!(pan >= -1.0)) {
        pan = -1.0;
    } else if (pan > 1.0) {
        pan = 1.0;
    }
    return pan;
}

void AmbientAreaLocalPoint(const float matrix[9], const double position[3],
                           const double listener[3], double local[3]) {
    // src/aiaction/ambientarea.c:134-140. The delta is world -> box, so the
    // authored local->world matrix is applied transposed: the rows read are
    // m[0],m[3],m[6] / m[1],m[4],m[7] / m[2],m[5],m[8].
    const long double dx = (long double)listener[0] - (long double)position[0];
    const long double dy = (long double)listener[1] - (long double)position[1];
    const long double dz = (long double)listener[2] - (long double)position[2];
    local[0] = (double)((matrix[6] * dz + matrix[3] * dy) + matrix[0] * dx);
    local[1] = (double)((matrix[7] * dz + matrix[4] * dy) + matrix[1] * dx);
    // The engine keeps lz in long double; this narrowing is the one place the
    // decomposed API loses precision. AmbientAreaContainsListener does not.
    local[2] = (double)((matrix[8] * dz + matrix[5] * dy) + matrix[2] * dx);
}

bool AmbientAreaContains(const double size[3], const double local[3]) {
    // src/aiaction/ambientarea.c:142-143, with kHalfSize = 4096.0 and
    // kNegHalfSize = -4096.0 (ambientarea.c:51-52). Every comparison is strict,
    // so a listener exactly on a face is OUTSIDE.
    return size[0] * -kAmbientHalfExtentScale < local[0] &&
           size[0] * kAmbientHalfExtentScale > local[0] &&
           size[1] * -kAmbientHalfExtentScale < local[1] &&
           size[1] * kAmbientHalfExtentScale > local[1] &&
           size[2] * -kAmbientHalfExtentScale < local[2] &&
           size[2] * kAmbientHalfExtentScale > local[2];
}

bool AmbientAreaContainsListener(const float matrix[9], const double position[3],
                                 const double size[3], const double listener[3]) {
    const long double dx = (long double)listener[0] - (long double)position[0];
    const long double dy = (long double)listener[1] - (long double)position[1];
    const long double dz = (long double)listener[2] - (long double)position[2];
    const double lx = (double)((matrix[6] * dz + matrix[3] * dy) + matrix[0] * dx);
    const double ly = (double)((matrix[7] * dz + matrix[4] * dy) + matrix[1] * dx);
    const long double lz = (matrix[8] * dz + matrix[5] * dy) + matrix[2] * dx;
    return size[0] * -kAmbientHalfExtentScale < lx && size[0] * kAmbientHalfExtentScale > lx &&
           size[1] * -kAmbientHalfExtentScale < ly && size[1] * kAmbientHalfExtentScale > ly &&
           lz > size[2] * -kAmbientHalfExtentScale && lz < size[2] * kAmbientHalfExtentScale;
}

int AmbientSoundIndexForRandom(double random_unit, int count) {
    // src/aiaction/ambientarea.c:164. Random_Float returns
    // (Random_Next & 0xFFFF) / 65536, i.e. [0, 1) (src/math/random.c:22-26).
    // round_int, not a floor: for count == 3 this returns 3 for the top sixth of
    // the range, which is one past the last valid name. The engine then calls
    // Sound_Play on an empty name and gets NULL (src/sound/soundsys.c:552).
    return static_cast<int>(RoundHalfEven(random_unit * count));
}

int AmbientDelayFrames(double random_unit, double min_delay_seconds, double random_wait_seconds) {
    // src/aiaction/ambientarea.c:120-123: round_int((r * randDelay + minDelay) * 30).
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

    // src/vehicle/condsound.c:123-126. Without an expression the condition is
    // not evaluated at all, so isRun keeps its previous value.
    if (has_condition) {
        state.is_last_run = state.is_run;
        state.is_run = CharFromRounded(condition_value);
    }

    // ConditionalSound_UpdatePlaying (condsound.c:179-189). For the Simple
    // branch the engine asks DirectSound whether the streamed voice is still
    // running; the host supplies that answer through `stream_alive`.
    if (emitter.simple) {
        state.is_playing = state.stream_alive;
        if (state.is_playing) tick.request_stream = true;
    } else if (state.has_sound) {
        state.is_playing = state.sound_alive;
    }

    // `if (cs->once && cs->played) return;` (condsound.c:128-129). Note this is
    // AFTER UpdatePlaying, so a one-shot still re-requests its stream.
    if (emitter.one_shot && state.played) {
        tick.is_playing = state.is_playing;
        return tick;
    }

    if (!state.is_run) {
        // ConditionalSound_Stop returns immediately when there is no sound task
        // (condsound.c:140-141), and isPlaying is cleared only on the path that
        // deletes one. A Simple emitter therefore keeps isPlaying == 1 and keeps
        // being re-requested even after its condition drops: UpdatePlaying runs
        // before the condition test (condsound.c:126-127), and nothing else ever
        // clears the flag. The sound outlives its condition until the streamed
        // voice itself ends, which the host reports through `stream_alive`.
        if (state.has_sound) {
            tick.stop_sound = true;
            state.has_sound = false;
            state.is_playing = false;
        }
    } else if (!state.is_last_run) {
        // Rising edge only. ConditionalSound_Play (condsound.c:150-177) stops
        // first, then either starts a stream or a positioned sound task.
        tick.start_sound = true;
        if (!emitter.simple) state.has_sound = true;
        state.played = true;
        state.is_playing = true;
    }

    tick.is_playing = state.is_playing;
    return tick;
}

void ResetConditionalSound(ConditionalSoundState& state) {
    // ConditionalSound_Reset, HOOK 0x425560 (src/vehicle/condsound.c:257-262):
    // Stop, isRun = 0, isLastRun = 0. isPlaying is cleared by Stop.
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
    long double v = 0.0L;
    bool inside = false;

    // src/aiaction/ambientarea.c:131-149.
    if (has_condition && !ConditionHolds(condition_value)) {
        // The engine's cond field is not in the ParamTable and no retail
        // instance authors it, so VarString_HasExpr is always false and this
        // branch is dead in the retail data. It is reproduced because it is
        // what the code does; see docs/igi-re-parity/SOUND.md.
        v = (long double)state.volume - kAmbientFadeStep;
    } else {
        // AmbientAreaContainsListener, not the decomposed LocalPoint+Contains
        // pair: the engine keeps the z axis in long double (ambientarea.c:140).
        if (AmbientAreaContainsListener(area.matrix, area.authored.position, area.authored.size,
                                        listener)) {
            v = (long double)state.volume + kAmbientFadeStep;
            inside = true;
        } else {
            v = (long double)state.volume - kAmbientFadeStep;
        }
    }

    // `a->volume = v;` then the clamps on `v`, not on a->volume
    // (ambientarea.c:150-154). `!(v >= 0.0)` also catches NaN.
    state.volume = (double)v;
    if (!(v >= 0.0L)) {
        state.volume = 0.0;
    } else if (v > 1.0L) {
        state.volume = 1.0;
    }

    // src/aiaction/ambientarea.c:156-177.
    const bool sound_died = state.has_sound && !state.sound_alive;
    if (state.volume == 0.0 || sound_died) {
        // The delay is re-rolled on every frame the area is silent, so an area
        // the listener is not inside keeps drawing randoms.
        state.delay = AmbientDelayFrames(random_unit_for_delay, area.authored.min_delay,
                                         area.authored.random_wait);
        if (state.has_sound) {
            tick.stop_sound = true;
            state.has_sound = false;
        }
    } else if (state.has_sound) {
        // `if (inside) Sound_SetPos(*a->sound, ear);` then
        // `Sound_SetVolume(*a->sound, -1.0f, a->volume);`. The position is the
        // LISTENER, so the definition's min/max distance never attenuates it:
        // the authored box, not the sound's radius, decides what is heard.
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
        // VarString_HasExpr is false, so isRun stays 0 and Run stops the sound
        // every tick (src/vehicle/condsound.c:123-131): it can never play.
        issues.push_back({SoundIssueKind::EmptyCondition,
                          "no Condition expression: this emitter can never run"});
    }
    if (emitter.sound_def.empty()) {
        // Sound_Play returns NULL for an empty name (src/sound/soundsys.c:552).
        issues.push_back({SoundIssueKind::EmptySoundDef, "no SoundDef: nothing to play"});
    } else if ((int)emitter.sound_def.size() >= kSoundDefNameCapacity) {
        // Param_String32 reads at most 0x20 bytes including the terminator
        // (src/level/paramtype.c:102-108) into cs->name[0x20] (condsound.c:37).
        issues.push_back({SoundIssueKind::NameTooLong,
                          "SoundDef is at least 32 bytes; Param_String32 truncates it"});
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
        // Param_String256 reads at most 0x100 bytes including the terminator
        // (src/level/paramtype.c:116-122), so anything longer is silently cut.
        issues.push_back({SoundIssueKind::SoundListTooLong,
                          "SoundDef list is at least 0x100 bytes; Param_String256 truncates it"});
    }
    if (list.size() >= (std::size_t)kAmbientSoundSlotCapacity) {
        // `sounds` is only 0x20 bytes (ambientarea.c:37) while the parameter
        // writes up to 0x100 into it: a genuine 256-into-32 overrun. It also
        // makes the split read its own output, because names[0] begins exactly
        // 0x20 bytes after sounds (0x94 -> 0xB4, ambientarea.c:37-38), so from
        // 32 bytes on the word copies overwrite the bytes still being scanned.
        // Every retail list is at most 31 bytes, which is why the corpus is
        // unaffected.
        issues.push_back({SoundIssueKind::SoundListTooLong,
                          "SoundDef list is at least 32 bytes; it overruns sounds[0x20] and the "
                          "split then reads what it writes"});
    }

    if (area.sounds.empty()) {
        // count == 0: the pick is round(r * 0) == 0 and names[0] is empty, so
        // Sound_Play gets an empty name (src/aiaction/ambientarea.c:164-167).
        issues.push_back({SoundIssueKind::NoSounds, "no sound names in the SoundDef list"});
    }
    if ((int)area.sounds.size() > kAmbientSoundSlots) {
        // AmbientArea_Changed writes names[a->count++] with no bound
        // (ambientarea.c:212) into names[8][0x20] (ambientarea.c:38); the ninth
        // word lands on `count` itself at 0x1B4.
        issues.push_back({SoundIssueKind::TooManySounds,
                          "more than 8 sound names; the ninth word overruns names[8]"});
    }
    for (const std::string& word : area.sounds) {
        if ((int)word.size() >= kAmbientSoundSlotCapacity) {
            // crt_strncpy(a->names[count], start, p - start) is not clamped
            // (ambientarea.c:209-210), so a word of 0x20 bytes or more runs into
            // the next slot - and because `names` begins only 0x20 bytes after
            // `sounds`, a long word can also overwrite the source it is copying
            // from while the scan is still reading it.
            issues.push_back({SoundIssueKind::SoundWordTooLong,
                              "'" + word + "' is at least 32 bytes; the slot copy overruns"});
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
