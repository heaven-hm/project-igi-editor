// sound_emitter.h - Sound emitter models (ConditionalSound and AmbientArea),
// parameter schemas, spatial attenuation, orientation transformations, and state machines.
//
// MIT License.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace igi {

// ---------------------------------------------------------------------------
// Engine constants
// ---------------------------------------------------------------------------

constexpr double kSoundDistScale = 4096.0;
constexpr double kAmbientHalfExtentScale = 4096.0;
constexpr double kAmbientFadeStep = 0.05;
constexpr double kAmbientTickRate = 30.0;

constexpr int kConditionalSoundParamCount = 6;
constexpr int kAmbientAreaParamCount = 6;

constexpr int kSoundDefNameCapacity = 0x20;
constexpr int kAmbientSoundListCapacity = 0x100;
constexpr int kAmbientSoundSlots = 8;
constexpr int kAmbientSoundSlotCapacity = 0x20;

constexpr int kConditionalSoundSize = 0x128;
constexpr int kAmbientAreaSize = 0x210;
constexpr int kConditionalSoundIsRunOffset = 0xB0;
constexpr int kConditionalSoundOnceOffset = 0xA9;
constexpr int kConditionalSoundSimpleOffset = 0xA8;
constexpr int kConditionalSoundRelativeOffset = 0xB3;
constexpr int kAmbientAreaVolumeOffset = 0x78;
constexpr int kAmbientAreaDelayOffset = 0x90;
constexpr int kAmbientAreaCountOffset = 0x1B4;

constexpr unsigned int kVoiceFlagPositional = 0x2;
constexpr unsigned int kVoiceFlagGlobal = 0x4;
constexpr unsigned int kVoiceFlagLoop = 0x10;

// ---------------------------------------------------------------------------
// The authored parameter schema
// ---------------------------------------------------------------------------

enum class SoundParamType {
    VarString,
    String32,
    ObjectPos,
    Bool8,
    Real32x9,
    Real64x3,
    Real64,
    String256,
};

// One authored parameter of a task type.
struct SoundParamSpec {
    const char* authored_name;  // retail Task_DeclareParameters name
    const char* engine_type;    // the Param_* type name it is declared with
    SoundParamType type;
    int script_args;  // values it consumes in the level script
    const char* meaning;
};

// Parameter specifications and declared argument types.
const SoundParamSpec* ConditionalSoundParamSpecs(int* count);
const SoundParamSpec* AmbientAreaParamSpecs(int* count);

int SoundParamScriptArgs(SoundParamType type);
const char* SoundParamTypeName(SoundParamType type);

int ConditionalSoundScriptArgs();
int AmbientAreaScriptArgs();

// ---------------------------------------------------------------------------
// Authored emitters
// ---------------------------------------------------------------------------

// A placed ConditionalSound entity.
struct ConditionalSoundEmitter {
    std::string condition;
    std::string sound_def;
    double position[3] = {0.0, 0.0, 0.0};
    bool simple = false;
    bool one_shot = false;
    bool relative_to_microphone = false;
};

// A placed AmbientArea entity.
struct AmbientAreaEmitter {
    double position[3] = {0.0, 0.0, 0.0};
    double orientation[3] = {0.0, 0.0, 0.0};
    double size[3] = {1.0, 1.0, 1.0};
    double min_delay = 0.0;
    double random_wait = 0.0;
    std::string sound_def;
};

// Constructs an orientation matrix from Euler angles (Rz * Ry * Rx).
void MatrixFromEuler(const double euler[3], float matrix[9]);

// The resolved AmbientArea with parsed sound names.
struct AmbientAreaResolved {
    std::string authored_name;
    AmbientAreaEmitter authored;
    float matrix[9] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    std::vector<std::string> sounds;
};

// Splits an authored AmbientArea sound list by delimiter characters.
std::vector<std::string> SplitAmbientSoundList(const std::string& list);

bool ParseConditionalSound(const std::vector<std::string>& arg_tokens,
                           ConditionalSoundEmitter& out);
bool ParseAmbientArea(const std::vector<std::string>& arg_tokens, AmbientAreaResolved& out);

// ---------------------------------------------------------------------------
// Sound definitions
// ---------------------------------------------------------------------------

struct SoundDefinition {
    std::string name;
    std::string sample;
    double offset[3] = {0.0, 0.0, 0.0};
    double volume = 1.0;
    double min_dist = 0.0;
    double max_dist = 1.0;
    int volume_index = -1;
    int pitch_index = -1;
    int sample_index = -1;
    double unk124 = 0.0;
    bool loop = false;
};

bool ParseSoundDefinition(const std::vector<std::string>& tokens, SoundDefinition& out);

class SoundDefinitionRegistry {
public:
    void Add(SoundDefinition definition);
    const SoundDefinition* Find(const std::string& name) const;
    std::size_t size() const { return definitions_.size(); }
    const std::vector<SoundDefinition>& definitions() const { return definitions_; }

private:
    std::vector<SoundDefinition> definitions_;
};

// ---------------------------------------------------------------------------
// Spatial and attenuation arithmetic
// ---------------------------------------------------------------------------

double RoundHalfEven(double value);
bool ConditionHolds(double value);
bool SoundWithinAudibleRadius(double distance_units, double max_dist_m);
double SoundAttenuatedVolume(double distance_units, double min_dist_m, double max_dist_m,
                             double volume);
double SoundDistanceFade(double distance_units, double min_dist_m, double max_dist_m);
double SoundPan(const double delta[3], const double listener_right[3], double distance_units,
                double min_dist_m, double max_dist_m);
void AmbientAreaLocalPoint(const float matrix[9], const double position[3],
                           const double listener[3], double local[3]);
bool AmbientAreaContains(const double size[3], const double local[3]);
bool AmbientAreaContainsListener(const float matrix[9], const double position[3],
                                 const double size[3], const double listener[3]);
int AmbientSoundIndexForRandom(double random_unit, int count);
int AmbientDelayFrames(double random_unit, double min_delay_seconds, double random_wait_seconds);

// ---------------------------------------------------------------------------
// ConditionalSound state machine
// ---------------------------------------------------------------------------

struct ConditionalSoundState {
    std::int8_t is_run = 0;
    std::int8_t is_last_run = 0;
    bool is_playing = false;
    bool played = false;
    bool has_sound = false;
    bool sound_alive = true;
    bool stream_alive = true;
};

struct ConditionalSoundTick {
    bool stop_sound = false;
    bool start_sound = false;
    bool request_stream = false;
    bool is_playing = false;
};

ConditionalSoundTick TickConditionalSound(ConditionalSoundState& state,
                                          const ConditionalSoundEmitter& emitter,
                                          bool has_condition, double condition_value);
void ResetConditionalSound(ConditionalSoundState& state);

// ---------------------------------------------------------------------------
// AmbientArea state machine
// ---------------------------------------------------------------------------

struct AmbientAreaState {
    double volume = 0.0;
    int delay = 0;
    bool has_sound = false;
    bool sound_alive = true;
};

struct AmbientAreaTick {
    double volume = 0.0;
    bool inside = false;
    bool stop_sound = false;
    bool start_sound = false;
    int sound_index = -1;
    bool update_playing = false;
};

AmbientAreaTick TickAmbientArea(AmbientAreaState& state, const AmbientAreaResolved& area,
                                const double listener[3], bool has_condition,
                                double condition_value, double random_unit,
                                double random_unit_for_delay);

// ---------------------------------------------------------------------------
// Authoring validation
// ---------------------------------------------------------------------------

enum class SoundIssueKind {
    // ConditionalSound
    EmptyCondition,        // never runs: isRun stays 0 (condsound.c:123-131)
    EmptySoundDef,         // Sound_Play returns NULL on an empty name (soundsys.c:552)
    NameTooLong,           // Param_String32 truncates at 0x20 (paramtype.c:102-108)
    UnresolvedSoundDef,    // no definition of that name in the registry
    // AmbientArea
    NoSounds,              // count == 0: the pick indexes nothing (ambientarea.c:164)
    TooManySounds,         // more than 8 words overruns names[8] (ambientarea.c:38,212)
    SoundWordTooLong,      // >= 0x20 bytes: the slot copy overruns (ambientarea.c:209)
    SoundListTooLong,      // >= 0x20 bytes overruns sounds[0x20]; >= 0x100 also
                           // gets truncated by Param_String256 (paramtype.c:116)
    NonPositiveSize,       // a zero or negative half extent contains nothing
    NegativeDelay,         // a negative delay or wait makes AmbientDelayFrames negative
};

struct SoundIssue {
    SoundIssueKind kind;
    std::string detail;
};

const char* SoundIssueKindName(SoundIssueKind kind);

// Checks one ConditionalSound against the engine's own limits. `registry` may
// be null, in which case the UnresolvedSoundDef check is skipped.
std::vector<SoundIssue> ValidateConditionalSound(const ConditionalSoundEmitter& emitter,
                                                 const SoundDefinitionRegistry* registry);

// Checks one AmbientArea against the engine's own limits.
std::vector<SoundIssue> ValidateAmbientArea(const AmbientAreaResolved& area,
                                            const SoundDefinitionRegistry* registry);

}  // namespace igi
