// sound_emitter.h - the authored sound-emitter model of Project IGI, resolved the
// way IGI-RE resolves it (issue #70).
//
// IGI has exactly two task types that a LEVEL AUTHOR places to emit sound:
//
//   * "ConditionalSound" (0x424E80, src/vehicle/condsound.c) - a sound that
//     plays while a script expression is non-zero. Its parameters are
//     Condition / SoundDef / Position / Simple / OneShot / Relative to
//     microphone (condsound.c:87-92). It has NO radius of its own: the audible
//     radius comes from the referenced sound DEFINITION's min/max distance
//     (src/transctx/voice.c:584-600).
//
//   * "AmbientArea" (0x44C130, src/aiaction/ambientarea.c) - an oriented box
//     that picks one of up to eight named sounds at random and fades it in and
//     out while the listener is inside. Its parameters are Position /
//     Orientation / Size / Min Delay / Random wait / SoundDef
//     (ambientarea.c:73-79). Its "radius" is the BOX, not a distance falloff:
//     it re-positions the playing sound onto the listener every frame
//     (ambientarea.c:161,170) so the definition's min/max distance never
//     attenuates it, and drives the level with its own +/-0.05 per frame
//     integrator (ambientarea.c:53,132,144,147).
//
// Both live in the level's OBJECTS.QVM/objects.qsc as ordinary Task_New calls.
// The parameter NAMES are not in the binary: ParamTable_Add stores
// {offset, type, flags, range} only (src/physobj/paramops.c, ParamTable_Add
// ORIG 0x4F1A80). The names come from the retail level script's own
// Task_DeclareParameters line, which repeats the ParamTable order verbatim:
//
//   LEVEL1/objects.qsc:55
//     Task_DeclareParameters("AmbientArea", "Position", "ObjectPos",
//       "Orientation", "Real32x9", "Size", "Real64x3", "Min Delay", "Real64",
//       "Random wait", "Real64", "SoundDef", "String256");
//   LEVEL1/objects.qsc:78
//     Task_DeclareParameters("ConditionalSound", "Condition", "VarString",
//       "SoundDef", "String32", "Position", "ObjectPos", "Simple", "bool8",
//       "OneShot", "bool8", "Relative to microphone", "bool8");
//
// The number of script arguments each type consumes is engine DATA, not the
// type's name: kParamTypes maps Real32x9 and Real64x3 to Param_Three
// (paramops.c:143,146,152) and ObjectPos aliases Real64x3 (paramops.c:183), so
// the authored "Orientation" is THREE Euler angles that ParamReal32x9_Parse
// turns into a 9-float matrix with Matrix_SetEuler (paramops.c:423-435,
// src/math/matrix.c:99-110). This is confirmed by every retail instance: a
// Building's or an AmbientArea's Orientation occupies three script tokens.
//
// This module ports the AUTHORING MODEL and the checkable LOGIC - the schema,
// the containment/falloff arithmetic, the fade integrator, the condition
// coercion and the play/stop state machines. It deliberately does NOT port
// anything that needs the audio device (Voice_*, DirectSoundBuffer) and it
// does not evaluate script expressions: VarString_EvalReal is the game's own
// script VM (src/param/varstring.c:160-168), so a condition enters this module
// as a resolved number, exactly as it enters ConditionalSound_Run.
//
// Everything here is cited to IGI-RE. Where the reconstructed engine is
// self-inconsistent or leaves a value undefined, the divergence is named in a
// comment and in docs/igi-re-parity/SOUND.md rather than silently fixed.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace igi {

// ---------------------------------------------------------------------------
// Engine constants. Every one is IGI-RE DATA.
// ---------------------------------------------------------------------------

// Game units per metre. Voice_SetParams multiplies a sound's authored min/max
// distance by this before comparing it with a Euclidean distance in world
// units (src/transctx/voice.c:79,584-585); SoundDefSound_Play culls with the
// same factor (src/define/sounddef.c:67).
constexpr double kSoundDistScale = 4096.0;

// AmbientArea's box scale and volume integrator (src/aiaction/ambientarea.c:51-54).
// kAmbientHalfExtentScale is the SAME 4096 as kSoundDistScale, but AmbientArea
// names it separately; the two are kept separate here so a change in one cannot
// silently move the other.
constexpr double kAmbientHalfExtentScale = 4096.0;
constexpr double kAmbientFadeStep = 0.05;
constexpr double kAmbientTickRate = 30.0;

// ParamTable sizes (src/vehicle/condsound.c:86, src/aiaction/ambientarea.c:73).
constexpr int kConditionalSoundParamCount = 6;
constexpr int kAmbientAreaParamCount = 6;

// Storage capacities the parameter parsers enforce, so the editor can warn
// before writing something the engine would truncate or overrun:
//   ConditionalSound.name[0x20] with Param_String32 ->
//     Script_GetArgString(..., 0x20)  (condsound.c:37, src/level/paramtype.c:102-108)
//   AmbientArea.sounds[0x20] with Param_String256 ->
//     Script_GetArgString(..., 0x100) (ambientarea.c:37, paramtype.c:116-122)
//   AmbientArea.names[8][0x20]        (ambientarea.c:38)
constexpr int kSoundDefNameCapacity = 0x20;
constexpr int kAmbientSoundListCapacity = 0x100;
constexpr int kAmbientSoundSlots = 8;
constexpr int kAmbientSoundSlotCapacity = 0x20;

// Struct sizes pinned by IGI-RE's own _Static_asserts, and the offsets of the
// fields the editor inspects (condsound.c:58-59, ambientarea.c:45;
// tests/test_vehicle_condsound.py and tests/test_aiaction_area.py pin the same
// offsets against the retail binary).
constexpr int kConditionalSoundSize = 0x128;
constexpr int kAmbientAreaSize = 0x210;
constexpr int kConditionalSoundIsRunOffset = 0xB0;
constexpr int kConditionalSoundOnceOffset = 0xA9;
constexpr int kConditionalSoundSimpleOffset = 0xA8;
constexpr int kConditionalSoundRelativeOffset = 0xB3;
constexpr int kAmbientAreaVolumeOffset = 0x78;
constexpr int kAmbientAreaDelayOffset = 0x90;
constexpr int kAmbientAreaCountOffset = 0x1B4;

// The ConditionalSound field the decompilation calls `loop` is authored as
// "Relative to microphone" (condsound.c:92, LEVEL1/objects.qsc:78) and its
// effect is Sound_SetGlobal(sound, 1) (condsound.c:170-173), documented at the
// call site as "heard at any distance" (src/sound/soundsys.c:612-617). The
// consumer is Voice_SetParams: bit 0x4 suppresses both the distance fade and
// the pan (voice.c:586). The name `loop` in the struct is a misnomer; this
// module uses the authored name.
constexpr unsigned int kVoiceFlagPositional = 0x2;  // voice.c:586
constexpr unsigned int kVoiceFlagGlobal = 0x4;      // voice.c:559,586
constexpr unsigned int kVoiceFlagLoop = 0x10;       // voice.c:452, sounddef.c:78

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

// The six ConditionalSound parameters, in ParamTable_Add order
// (src/vehicle/condsound.c:87-92), with the names and types from
// LEVEL1/objects.qsc:78. `count` receives 6.
const SoundParamSpec* ConditionalSoundParamSpecs(int* count);

// The six AmbientArea parameters, in ParamTable_Add order
// (src/aiaction/ambientarea.c:74-79), with the names and types from
// LEVEL1/objects.qsc:55. `count` receives 6.
const SoundParamSpec* AmbientAreaParamSpecs(int* count);

// Values a type consumes: 3 for ObjectPos/Real64x3/Real32x9 (Param_Three,
// paramops.c:143 + the ObjectPos alias at :183), 1 for everything else
// (Param_One, paramops.c:99).
int SoundParamScriptArgs(SoundParamType type);

// The declared type name, i.e. what a Task_DeclareParameters line spells.
const char* SoundParamTypeName(SoundParamType type);

// Total script values a whole task type consumes, i.e. what follows the
// instance NAME in its Task_New call. 8 for ConditionalSound, 12 for
// AmbientArea. The retail QVM agrees: the Task_New call headers carry the
// argument counts 11 and 15 (three more for id, type and name).
int ConditionalSoundScriptArgs();
int AmbientAreaScriptArgs();

// ---------------------------------------------------------------------------
// Authored emitters
// ---------------------------------------------------------------------------

// A placed ConditionalSound. Field order and meaning follow the ParamTable.
struct ConditionalSoundEmitter {
    // "Condition", VarString. A script expression, evaluated by the game's VM;
    // this module never parses it. Empty means the emitter can never run: with
    // no expression VarString_HasExpr is false, so isRun stays 0 and
    // ConditionalSound_Run stops the sound every tick (condsound.c:123-131).
    std::string condition;
    // "SoundDef", String32 - a definition name resolved by Sound_Play
    // (src/sound/soundsys.c:545-577).
    std::string sound_def;
    // "Position", ObjectPos - used only when `relative_to_microphone` is false
    // (condsound.c:170-173).
    double position[3] = {0.0, 0.0, 0.0};
    // "Simple", bool8 - play through the sound HANDLE instead of a positioned
    // sound task (condsound.c:155-165).
    bool simple = false;
    // "OneShot", bool8 - after the first play, never stop and never replay
    // (condsound.c:128-129).
    bool one_shot = false;
    // "Relative to microphone", bool8 - the struct's `loop` field; plays
    // globally, heard at any distance (condsound.c:170-171).
    bool relative_to_microphone = false;
};

// A placed AmbientArea. `orientation` holds the three authored Euler angles;
// `matrix` is what ParamReal32x9_Parse builds from them.
struct AmbientAreaEmitter {
    // "Position", ObjectPos - the box centre.
    double position[3] = {0.0, 0.0, 0.0};
    // "Orientation", Real32x9 - THREE Euler angles (paramops.c:423-435).
    double orientation[3] = {0.0, 0.0, 0.0};
    // "Size", Real64x3 - HALF extents in metres; the box is size * 4096 world
    // units from the centre on each axis (ambientarea.c:142-143).
    double size[3] = {1.0, 1.0, 1.0};
    // "Min Delay", Real64 - seconds, the floor of the inter-sound delay.
    double min_delay = 0.0;
    // "Random wait", Real64 - seconds, the random part of that delay.
    double random_wait = 0.0;
    // "SoundDef", String256 - the raw authored list; see sounds() for the split.
    std::string sound_def;
};

// The nine-float rotation matrix ParamReal32x9_Parse builds for an authored
// AmbientArea/Building orientation (src/math/matrix.c:99-110: Rz * Ry * Rx,
// row-major, m[i*3+j] = row i, column j).
void MatrixFromEuler(const double euler[3], float matrix[9]);

// The resolved AmbientArea: the authored list split into names.
struct AmbientAreaResolved {
    // The instance name (argTokens[2]); the engine has no field for it, the
    // task's own name slot holds it, but the editor needs it for messages.
    std::string authored_name;
    AmbientAreaEmitter authored;
    float matrix[9] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    std::vector<std::string> sounds;  // at most kAmbientSoundSlots in retail
};

// Splits an authored AmbientArea sound list the way AmbientArea_Changed does:
// words separated by ' ', ';' or ',', empty words dropped, count unbounded in
// the engine (ambientarea.c:198-222). The engine writes each word into a
// 0x20-byte slot with no length clamp; this returns them as strings and leaves
// the capacity check to ValidateAmbientArea.
std::vector<std::string> SplitAmbientSoundList(const std::string& list);

// Parses one instance out of a level loader's argument tokens.
//
// `arg_tokens` is the editor's LevelObject::argTokens layout, which is the QSC
// /QVM order: [0] = task id, [1] = task type, [2] = instance name, [3] =
// declared parameter 0. Both parsers return false when the vector is too short
// or the numbers do not parse; the output is then left untouched.
bool ParseConditionalSound(const std::vector<std::string>& arg_tokens,
                           ConditionalSoundEmitter& out);
bool ParseAmbientArea(const std::vector<std::string>& arg_tokens, AmbientAreaResolved& out);

// ---------------------------------------------------------------------------
// Sound definitions - where an emitter's audible radius actually comes from
// ---------------------------------------------------------------------------

// One DefineSound(...) definition from a sounds.qsc/COMMON/SOUNDS/SOUNDS.QVM.
// Argument order is the one SoundDefSound_Write prints
// (src/define/sounddef.c:206-218): name, sample, x, y, z, volume, minDist,
// maxDist, volumeIndex, pitchIndex, sampleIndex, unk124, loop.
struct SoundDefinition {
    std::string name;
    std::string sample;
    double offset[3] = {0.0, 0.0, 0.0};
    double volume = 1.0;
    double min_dist = 0.0;  // metres; Voice_SetParams scales by 4096
    double max_dist = 1.0;  // metres
    int volume_index = -1;
    int pitch_index = -1;
    int sample_index = -1;
    double unk124 = 0.0;
    bool loop = false;
};

// Parses the 13 argument tokens of a DefineSound call. Returns false unless
// exactly 13 are present.
bool ParseSoundDefinition(const std::vector<std::string>& tokens, SoundDefinition& out);

// The definitions a level can see. game.c loads
// "LOCAL:common/sounds/" FIRST and "MISSION:sounds/" second
// (src/game/game.c:441-442), and SoundDefSound_Find only accepts a match when
// nothing has matched yet (src/define/sounddef.c:221-226), so the FIRST
// definition of a name wins. COMMON/SOUNDS/SOUNDS.QVM really does define
// machinery_01 twice, which is how that rule becomes observable.
class SoundDefinitionRegistry {
public:
    // Appends in load order. Adding a name that is already present is a no-op
    // for lookups, which is what first-wins means.
    void Add(SoundDefinition definition);
    const SoundDefinition* Find(const std::string& name) const;
    std::size_t size() const { return definitions_.size(); }
    const std::vector<SoundDefinition>& definitions() const { return definitions_; }

private:
    std::vector<SoundDefinition> definitions_;
};

// ---------------------------------------------------------------------------
// Engine arithmetic that is checkable without an audio device
// ---------------------------------------------------------------------------

// round_int (src/missile/missile.h:111-117, src/ui/ui.h:123-129): an x87
// `fistpll`, i.e. a store to a 64-bit integer with the CURRENT rounding mode,
// which is round-half-to-EVEN. It is NOT a truncation: round_int(0.5) is 0 and
// round_int(1.5) is 2. Both ConditionalSound and AmbientArea use it for the
// condition and for the random picks (condsound.c:125, ambientarea.c:122,164).
double RoundHalfEven(double value);

// `(char)round_int(value) != 0` (condsound.c:125, ambientarea.c:131). The cast
// to a signed char wraps: 256 is false, 255 is true.
bool ConditionHolds(double value);

// SoundDefSound_Play's hard cull: the sound may not start at all unless the
// listener is inside `range` = the definition's maxDist, compared as
// `r*r >= DistanceSq` with r = maxDist * 4096 (src/define/sounddef.c:65-71).
// Note the comparison is `!(r*r >= d2)` -> return, so equality passes.
bool SoundWithinAudibleRadius(double distance_units, double max_dist_m);

// Voice_SetParams' positional attenuation (src/transctx/voice.c:584-600):
//   dist >  max: gain 0
//   dist >= min: gain = clamp01((1 - (dist-min)/(max-min)) * volume)
//   dist <  min: gain = volume
// Both distances are scaled by kSoundDistScale first. `volume` is the
// definition's authored volume. The maxDist == minDist case divides by zero:
// at dist == min the quotient is NaN, `!(k >= 0)` is true, and the engine
// silences the sound (voice.c:596-597) - reproduced here.
double SoundAttenuatedVolume(double distance_units, double min_dist_m, double max_dist_m,
                             double volume);

// The raw distance factor with volume factored out, for a falloff preview:
// 1.0 inside minDist, 0.0 beyond maxDist, linear between.
double SoundDistanceFade(double distance_units, double min_dist_m, double max_dist_m);

// The stereo pan Voice_SetParams computes (src/transctx/voice.c:601-611):
//   r   = right . delta           (the listener matrix's first column, i.e.
//                                  the listener's x axis: voice.c:602 is the
//                                  same m[6],m[3],m[0] pattern as
//                                  ambientarea.c:138)
//   pan = clamp(-1, 1, r / dist * (dist / (maxDist + minDist)))
// The two `dist` factors cancel, so this is r / (maxDist + minDist); the
// engine's form is kept so the dist == 0 branch (pan 0) is reproduced.
double SoundPan(const double delta[3], const double listener_right[3], double distance_units,
                double min_dist_m, double max_dist_m);

// AmbientArea's world->local transform, exactly ambientarea.c:134-140: the
// listener delta rotated by the TRANSPOSE of the authored matrix, because the
// matrix is local->world. lx and ly are rounded to double, lz is kept in long
// double, matching the engine's mixed widths.
void AmbientAreaLocalPoint(const float matrix[9], const double position[3],
                           const double listener[3], double local[3]);

// The containment test of ambientarea.c:142-143: STRICT inequalities against
// +/- size * 4096, so a listener exactly on a face is OUTSIDE.
bool AmbientAreaContains(const double size[3], const double local[3]);

// The same test end to end, keeping the z axis in long double the way
// ambientarea.c:140 does. Use this rather than AmbientAreaContains whenever the
// answer has to match the engine exactly: AmbientAreaLocalPoint rounds z to
// double, so a listener within one double ULP of a z face can differ. This is
// what TickAmbientArea calls.
bool AmbientAreaContainsListener(const float matrix[9], const double position[3],
                                 const double size[3], const double listener[3]);

// `round_int(Random_Float * count)` (ambientarea.c:164). Random_Float returns
// [0, 1) with 16-bit granularity (src/math/random.c:22-26), so this can return
// `count` itself - one past the last valid slot - and it does so for the top
// sixth of the range when count is 3. That is the engine's behaviour, kept.
int AmbientSoundIndexForRandom(double random_unit, int count);

// `round_int((Random_Float * randDelay + minDelay) * 30)` (ambientarea.c:120-123),
// in frames.
int AmbientDelayFrames(double random_unit, double min_delay_seconds, double random_wait_seconds);

// ---------------------------------------------------------------------------
// ConditionalSound: the condition-gated state machine
// ---------------------------------------------------------------------------

// The five fields ConditionalSound_Run reads and writes, with their offsets in
// the 0x128-byte struct.
struct ConditionalSoundState {
    std::int8_t is_run = 0;       // 0xB0, `(char)round_int(condition)`
    std::int8_t is_last_run = 0;  // 0xB1
    bool is_playing = false;      // 0xB2
    bool played = false;          // 0x124, set by Play, gates `once`
    bool has_sound = false;       // cs->sound != NULL, i.e. a live sound task
    // `*cs->sound != NULL` (condsound.c:186): the engine learns the sound task
    // died when its TaskRef goes NULL, which is what a finished one-shot does.
    bool sound_alive = true;
    // The editor's stand-in for SoundHandle_IsPlaying (src/viewport/soundplay.c:139-152):
    // the engine asks DirectSound whether the streamed voice is still running.
    // The editor has no voice, so the caller supplies it; the default keeps a
    // started stream alive, which is what a looping sample does.
    bool stream_alive = true;
};

// What one ConditionalSound_Run tick asks the host to do.
struct ConditionalSoundTick {
    bool stop_sound = false;      // ConditionalSound_Stop deleted a live sound
    bool start_sound = false;     // ConditionalSound_Play created one
    bool request_stream = false;  // the Simple branch re-requested the handle
    bool is_playing = false;      // post-tick isPlaying
};

// One tick of ConditionalSound_Run (src/vehicle/condsound.c:121-136).
// `has_condition` is VarString_HasExpr(&cs->cond): when it is false the
// condition is not evaluated and isRun/isLastRun keep their previous values,
// which is exactly the engine's branch. `condition_value` is
// VarString_EvalReal(&cs->cond, cs).
ConditionalSoundTick TickConditionalSound(ConditionalSoundState& state,
                                          const ConditionalSoundEmitter& emitter,
                                          bool has_condition, double condition_value);

// ConditionalSound_Reset (condsound.c:257-262): Stop, then isRun = isLastRun = 0.
void ResetConditionalSound(ConditionalSoundState& state);

// ---------------------------------------------------------------------------
// AmbientArea: the fade integrator and the delay gate
// ---------------------------------------------------------------------------

struct AmbientAreaState {
    double volume = 0.0;    // 0x78
    int delay = 0;          // 0x90, frames until a sound may start
    bool has_sound = false; // a->sound != NULL
    // The editor's stand-in for `*a->sound != NULL` (ambientarea.c:156): the
    // engine learns the task died when its TaskRef goes NULL. A one-shot
    // definition that finished is what makes this false in practice.
    bool sound_alive = true;
};

struct AmbientAreaTick {
    double volume = 0.0;    // the clamped volume after the tick
    bool inside = false;    // the listener passed the containment test
    bool stop_sound = false;  // stop_sound(a) ran
    bool start_sound = false; // Sound_Play ran and the host should start it
    int sound_index = -1;     // which of sounds() was picked
    bool update_playing = false;  // an existing sound got pos and volume
};

// One tick of AmbientArea_Run (src/aiaction/ambientarea.c:125-179).
// `has_condition` is VarString_HasExpr(&a->cond); `condition_value` is
// VarString_EvalReal. `random_unit` is Random_Float(Game_GetRandom()) in
// [0, 1) - one draw is used for the sound pick and a separate one for the
// delay, so the caller passes the value for whichever the tick needs:
// `random_unit` is consumed by the pick (ambientarea.c:164), and
// `random_unit_for_delay` by the delay (ambientarea.c:157 via :122).
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
