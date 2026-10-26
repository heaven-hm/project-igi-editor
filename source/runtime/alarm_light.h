// alarm_light.h - the retail "AlarmLight" task, resolved the way IGI-RE
// resolves it.
//
// Issue #43 asked for "create/edit lights and smoke FX"; the triage note points
// at `AlarmLight_* 0x4397C0` as "the closest retail light type". It is in fact
// the ONLY retail task type that is a light with runtime state, and it is not a
// light fixture: it is a switchable, destructible lamp whose visible "blink" is
// a reflector model that turns while the lamp is on.
//
// Everything here is cited to IGI-RE
// (`/Users/haseeb-mir/Documents/Code/IGI-RE`), the only evidence source used.
//
//   src/fxobj/alarm.c          AlarmLight_Init 0x4397C0 (the ParamTable and the
//                              reflector task type), AlarmLightReflector_Create
//                              0x439A50, AlarmLightReflector_Run 0x439B70,
//                              AlarmLightReflector_Draw 0x439EC0,
//                              AlarmLightReflector_DrawFlare 0x439F10
//   src/elevator/alarmlight.c  AlarmLight_Create 0x43A0E0, AlarmLight_Run
//                              0x43A270, AlarmLight_Delete 0x43A3C0,
//                              AlarmLight_Changed 0x43A450, AlarmLight_Hit
//                              0x43A5E0, AlarmLight_Destroy 0x43A5F0,
//                              AlarmLight_Blast 0x43A660, AlarmLight_Activate
//                              0x43A710, AlarmLight_Deactivate 0x43A790
//   src/elevator/elevatortypes.h:7-22   the AlarmLight layout
//   src/math/matrix.c          Matrix_SetEuler 0x4B38E0 and the SetRot/Mul it
//                              uses; Matrix_SetRotX 0x4B3B60
//   src/physobj/paramops.c     ParamReal32x9_Parse 0x4EFBA0 -- a "Real32x9"
//                              authored parameter is THREE Euler angles
//
// The authored parameter names and types come from the retail
// `Task_DeclareParameters("AlarmLight", ...)` call, which is present verbatim in
// the bytecode of every level that authors one. LEVEL9/OBJECTS.QVM pushes it at
// code offset 0x7070-0x7127; LEVEL6/7/8/14 carry the identical list.
//
// NO retail level ships a decompiled `objects.qsc` containing an AlarmLight, so
// the declaration is read from the QVM bytecode and the instances from the QVM
// as well (5 levels, 10 instances). See docs/igi-re-parity/SMOKE_LIGHTS.md.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace igi {

// ---------------------------------------------------------------------------
// Engine constants. Every one is IGI-RE DATA.
// ---------------------------------------------------------------------------

// alarm.c:71 kTurn. Exactly pi/30 to float precision: pi / kTurn =
// 29.999999165, so the reflector sweeps half a turn every 30 Run calls and the
// glow's red channel therefore pulses once per second.
constexpr float kAlarmLightTurn = 0x1.aceeap-4f;

// alarm.c:74 kPi is a DOUBLE; alarm.c:75 kInvPi and alarm.c:76 kHalfF are
// floats. The flare maths mixes them, and the mixing is load-bearing.
constexpr double kAlarmLightPi = 0x1.921fb54442d1bp+1;
constexpr float kAlarmLightInvPi = 0x1.45f306p-2f;
constexpr float kAlarmLightHalf = 0.5f;

// alarm.c:77 kFlareScale, alarm.c:232-233 the flare quad's half size, and
// alarm.c:242-243 the alpha.
constexpr float kAlarmLightFlareScale = 128.0f;
constexpr float kAlarmLightFlareSize = 6144.0f;
constexpr float kAlarmLightFlareAlpha = 0.5f;

// ParamTable_New(8) (alarm.c:108) and its eight ParamTable_Add calls
// (alarm.c:109-115). Two of the eight consume three script arguments, so the
// authored argument list is 12 tokens.
constexpr int kAlarmLightParamCount = 8;
constexpr int kAlarmLightTokenCount = 12;

// LevelObject::argTokens layout: [0]=task id, [1]=type, [2]=name,
// [3]=declared parameter 0.
constexpr std::size_t kAlarmLightFirstParam = 3;

// The three model strings, in ENGINE SLOT order. The slots are the three
// consecutive 16-byte fields alarm.c:112-113 registers with `l.offModel + 0x10 * i`,
// i.e. offsets 0x00, 0x10 and 0x20 of the AlarmLight struct
// (elevatortypes.h:11-13).
enum AlarmLightModelSlot {
    kAlarmLightModelSlotA = 0,  // offset 0x00, alarm.c:113 i=0
    kAlarmLightModelSlotB = 1,  // offset 0x10, alarm.c:113 i=1
    kAlarmLightModelSlotC = 2,  // offset 0x20, alarm.c:113 i=2
};

// Which slot each runtime transition installs. This is where the authored
// names and the runtime disagree, and the disagreement is real:
//
//   * ParamTable slot 3 is the authored "On model"  and writes slot A.
//   * ParamTable slot 4 is the authored "Off model" and writes slot B.
//   * AlarmLight_Run installs slot A on the ON -> OFF transition and slot B on
//     the OFF -> ON transition (elevator/alarmlight.c:59-64).
//
// So the value the level labels "On model" is the model shown when the lamp
// turns OFF. The port reproduces the positional mapping, which is what the
// engine does; it does not "fix" it.
constexpr int kAlarmLightModelSlotWhenTurningOff = kAlarmLightModelSlotA;
constexpr int kAlarmLightModelSlotWhenTurningOn = kAlarmLightModelSlotB;
// AlarmLight_Changed installs slot B at create / edit time
// (elevator/alarmlight.c:98), and AlarmLight_Destroy installs slot C
// (elevator/alarmlight.c:129).
constexpr int kAlarmLightModelSlotOnChange = kAlarmLightModelSlotB;
constexpr int kAlarmLightModelSlotWhenDestroyed = kAlarmLightModelSlotC;

// One row of the authored parameter table.
struct AlarmLightParamDesc {
    const char* authored_name;  // the retail declaration string, verbatim
    const char* engine_field;   // the IGI-RE struct field the slot writes
    const char* type_name;      // the retail declared type
    int token_count;            // script arguments consumed
    int model_slot;             // AlarmLightModelSlot, or -1 when not a model
};

// The 8 rows, in ParamTable order. Never null.
const AlarmLightParamDesc* AlarmLightParamTable();

// ---------------------------------------------------------------------------
// The authored task
// ---------------------------------------------------------------------------

// One task as the editor's loader hands it over.
struct AlarmLightTaskObject {
    std::string type;
    std::vector<std::string> arg_tokens;
};

// The resolved parameters of one "AlarmLight" instance. Field names follow the
// AUTHORED names; see the slot note above for how they reach the runtime.
struct AlarmLightParams {
    double place[3] = {0.0, 0.0, 0.0};
    // "Orientation" is declared Real32x9 but is authored as THREE Euler angles
    // (paramops.c:423-435 calls Matrix_SetEuler on them).
    float orientation[3] = {0.0f, 0.0f, 0.0f};
    float matrix[9] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    std::string on_expression;    // authored "On expression"  -> cond
    std::string on_model;         // authored "On model"       -> engine slot A
    std::string off_model;        // authored "Off model"      -> engine slot B
    std::string destroyed_model;  // authored "Destroyde model" (retail typo) -> slot C
    std::string on_sound;         // authored "On sound"
    std::string explode_sound;    // authored "Explode sound"

    // The authored value that the runtime installs for one engine slot. Out of
    // range yields an empty string.
    const std::string& ModelForSlot(int slot) const;
};

// Parses one authored "AlarmLight" call. Returns false when the call is shorter
// than the 12 tokens the eight parameters require.
bool ParseAlarmLightParams(const std::vector<std::string>& arg_tokens, AlarmLightParams* out);

// Every "AlarmLight" task in the list, in file order.
std::vector<AlarmLightParams> ResolveAlarmLightTasks(const std::vector<AlarmLightTaskObject>& objects);

// ---------------------------------------------------------------------------
// The per-tick state machine (AlarmLight_Run 0x43A270)
// ---------------------------------------------------------------------------

// `fistpll` -> long long with the x87 default rounding mode
// (round-to-nearest-even), as in animcam.h:67-73. The engine then casts the
// result to `char` (elevator/alarmlight.c:58), so 256 becomes 0 and 255 becomes
// a negative-but-truthy char; both are reproduced.
long long AlarmLightRoundLL(long double value);

struct AlarmLightState {
    bool is_on = false;          // AlarmLight.isOn, 0 at create (elevator/alarmlight.c:35)
    bool is_destroyed = false;   // AlarmLight.isDestroyed (elevator/alarmlight.c:36)
    bool sound_playing = false;  // AlarmLight.sound != NULL (elevator/alarmlight.c:39)
};

// The outcome of one Run call.
struct AlarmLightTick {
    // False when the task is destroyed: Run returns before evaluating the
    // condition, before the model swap, and before running its children
    // (elevator/alarmlight.c:55-56).
    bool ran_children = true;
    // True when the "On sound" task should exist after this tick. The engine
    // decides from the state at ENTRY, so the sound lags the lamp by one tick
    // in both directions (elevator/alarmlight.c:46-54).
    bool sound_wanted = false;
    bool sound_changed = false;
    bool is_on = false;
    // The engine model slot to install, or -1 to leave the model alone.
    int model_slot = -1;
};

// One AlarmLight_Run. `cond_value` is the value of the authored "On
// expression" for this tick (the engine evaluates it through
// VarString_EvalReal, elevator/alarmlight.c:57-58); pass `has_cond` false when
// the task authors no expression, in which case isOn never changes. The value
// is a real, not a bool, because the engine rounds it and then truncates it to
// a `char`.
AlarmLightTick AlarmLightRun(AlarmLightState& state, double cond_value, bool has_cond);

// ---------------------------------------------------------------------------
// The reflector: the visible "blink"
// ---------------------------------------------------------------------------
//
// There is no timer anywhere in AlarmLight_Run. The lamp's flash comes from its
// child "AlarmLightReflector" magic object, which turns while the lamp is on
// (alarm.c:158-167) and draws a glow sprite whose RED channel is a triangle
// wave of the accumulated angle (alarm.c:235-242).

// AlarmLightReflector_Run 0x439B70: `angle += kTurn`, but only while the lamp
// is on and not destroyed (alarm.c:158, :162, :166). Accumulated in float, so
// the port takes and returns a float.
float AlarmLightReflectorAngle(float angle, bool is_on, bool is_destroyed);

// The flare's red channel, alarm.c:235-242:
//   k = fmod(angle, pi) * (1/pi) - 0.5;  if (k < 0 || isnan(k)) k = -k;
//   red = (k + k) * 128
// Range 0..128, period pi in `angle`. Negating a NaN leaves it NaN, so a NaN
// angle yields a NaN red -- reproduced rather than clamped.
float AlarmLightFlareRed(float angle);

// The number of Run calls between two maxima of the red channel: pi / kTurn.
// Returned as a double because the exact ratio is 29.999999165173982 -- kTurn is
// a float approximation of pi/30 -- and as a float it rounds to exactly 30.0f.
// Either way the flash is one pulse per second at the engine's 30 Hz tick.
double AlarmLightFlarePeriodTicks();

// The red channel after `ticks` consecutive on-ticks, accumulating the angle
// the way the engine does (one float addition per tick).
float AlarmLightFlareRedAfterTicks(int ticks);

// AlarmLightReflector_DrawFlare's visibility test, alarm.c:220-228: the flare
// is drawn only when the camera is on the positive side of the light's local
// Y axis, i.e. when dot(camera - light_pos, (m[1], m[4], m[7])) > 0.
bool AlarmLightFlareFacesCamera(const double light_pos[3], const float matrix[9],
                                const double camera_pos[3]);

// The flare's green and blue channels are the literal 0.0f of alarm.c:242-243.
constexpr float kAlarmLightFlareGreen = 0.0f;
constexpr float kAlarmLightFlareBlue = 0.0f;

// ---------------------------------------------------------------------------
// Matrix_SetEuler (0x4B38E0) -- how "Orientation" becomes the 3x3 matrix
// ---------------------------------------------------------------------------

// matrix = RotZ(az) * RotY(ay) * RotX(ax), in the engine's row-major layout and
// with the engine's summation order (matrix.c:10-25 uses k = 0, 2, 1, which
// changes the floating-point result). `euler` is (ax, ay, az).
void AlarmLightEulerMatrix(const float euler[3], float out_matrix[9]);

// AlarmLight_Destroy 0x43A5F0 runs a fixed Explosion_Spawn and then switches
// the lamp to model slot C (elevator/alarmlight.c:126-129). The field names
// below are Explosion_Spawn's own parameter names (camera/camera.h:15-16,
// camera/explosion.c:88-89); where the reconstruction leaves an argument
// unnamed, the field keeps the `unk` name rather than inventing a meaning.
struct AlarmLightExplosion {
    int count = 1;                 // "1"
    int delay = 0;                 // "0"
    float unk50 = 0.05f;           // "0.05f"
    float radius = 4096.0f;        // "4096.0f"
    float unk58 = 0.0f;            // "0.0f"
    float burn_size = 2048.0f;     // "2048.0f"
    int unk68 = 5;                 // "5"
    int unk6c = 2;                 // "2"
    // `burn` is the Task** argument, and the engine passes a literal 0 (NULL).
    bool has_burn = false;
    const char* sprite = "1009_01_1";
    // `sound` is al->deadSound, the authored "Explode sound".
    bool sound_is_authored_explode_sound = true;
    int noise_type = 0x15;         // "0x15"
    int noise_unk8 = 2;            // "2"
    float noise_radius = 20480.0f; // "20480.0f"
    int unk84 = 0;                 // "0"
};
const AlarmLightExplosion& AlarmLightDestroyExplosion();

}  // namespace igi
