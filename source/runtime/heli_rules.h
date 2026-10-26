// heli_rules.h - the retail "Heli" vehicle task and the "Rotor" magic object,
// resolved the way IGI-RE resolves them.
//
// Issue #60 asked for "Helicopter rotor + flight preview". The triage recorded
// the cited address 0x431B70 as `Heli_Control`; this module is the pure-logic
// half of that family plus the rotor that a helicopter actually spins. The
// editor already draws the helicopter's ATTA children (see
// `source/renderer/atta_children.h`), but it knew nothing about the authored
// "Heli" parameters, the per-tick control semantics that drive a flight
// preview, or the rotor's own spin/lift rules.
//
// Everything here is cited to IGI-RE (`/Users/haseeb-mir/Documents/Code/IGI-RE`),
// which is the only evidence source used. The authored parameter names and
// types come from the retail `Task_DeclareParameters("Heli", ...)` line, which
// ships inside every OBJECTS.QVM that authors a helicopter and is also present
// as decompiled text in `location0/LEVEL2/objects.qsc:62-63`.
//
// The IGI-RE files this module is built from:
//
//   src/heli/heli.c        Heli_Init 0x4310F0, Heli_Reset 0x431560,
//                          Heli_Control 0x431B70, Heli_Move 0x431E30,
//                          Heli_UpdateWheels 0x432550, Heli_GetControlInfo
//                          0x432BE0, Heli_GetSoundParams 0x432BB0
//   src/heli/heli.h        Vehicle/VehicleConfig/ControlArg/ControlInfo layouts
//   src/heli/helitypes.h   Heli and HeliConfig layouts
//   src/plane/heli.c       Heli_ParseArgs 0x432F60, Heli_ParseConfig 0x433030
//   src/rotor/rotor.c      Rotor_Init 0x42D2B0, Rotor_Run 0x42D440,
//                          Rotor_Move 0x42D680, Rotor_DrawPrim 0x42C630,
//                          Rotor_ParseDef 0x42D9F0
//   src/rotor/rotor.h      RotorDef and Part layouts
//   src/vehicle/parts.c    HeliWheel_ParseArgs 0x427240
//   src/editobj/vehconfig.c Vehicle_ParseConfig 0x5031F0
//   src/physobj/physobjtype.c DefinePhysicsObjType 0x4EDD20 -- where the
//                          physics-def argument list is laid out and where the
//                          task-type parse handler is handed its start index
//   src/physobj/paramops.c the script argument counts per type (Param_One /
//                          Param_Three) and ParamReal32x9_Parse 0x4EFBA0
//   src/math/matrix.c      Matrix_SetRotX 0x4B3B60, Matrix_SetRotZ 0x4B3BE0,
//                          Matrix_SetEuler 0x4B38E0 and the inlined 3x3
//                          multiply
//   src/math/mathvars.h, src/plane/plane.c, src/missile/missile.c,
//   src/scamera/scameralens.c, src/animtrig/body.c  the shared exe constants
//
// A "Heli" task consumes exactly 19 argument tokens (3 for ObjectPos, 3 for
// Real32x9, 3 for Real32x3 and 10 scalars), which every one of the 29 retail
// helicopter instances in the shipped QVMs satisfies. A "Rotor" physics
// definition consumes 6 tokens appended to the physics-def list. See
// docs/igi-re-parity/HELICOPTER.md.
//
// Named divergences (never silently fixed):
//   * x87 `fsin`/`fcos` (matrix.c:113-115, :148-150) are not bit-identical to
//     libm `sinf`/`cosf`; this port uses libm and is exact to ~1 ulp of the
//     64-bit x87 mantissa, not bit-exact. The same applies to the x87 `fsqrt`
//     in Heli_Move's heading normalisation (heli.c:464).
//   * The engine writes `#pragma GCC optimize("excess-precision=standard")`
//     over these translation units, i.e. every `float` operation rounds to
//     `float` at each step. This port spells those intermediates out as
//     `float` so the same rounding happens on a non-x87 host.
//   * `Heli_Move` reuses one local for two different things: when the heading
//     basis is degenerate it reads the slot that held the pitch force
//     (heli.c:473-476, the original's own comment "the original reuses a stack
//     slot here"). The local is `float t0, t1, t2, stale = 0` (heli.c:417), so
//     it is 0 unless the turn-force block ran this tick, in which case it holds
//     `f2[0]` (heli.c:452). That is reproduced as `HeliMoveState::stale` rather
//     than corrected to a zero.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace igi {

// ---------------------------------------------------------------------------
// Engine constants. Every one is IGI-RE DATA; the file:line is where the value
// is stored (tools/gendefs.py renders the exe's own bytes).
// ---------------------------------------------------------------------------

constexpr float kHeliUnit = 4096.0f;            // mathvars.h:7   kFixed
constexpr float kHeliTickRate = 30.0f;          // mathvars.h:8   kFps
constexpr float kHeliOne = 1.0f;                // flatwater.c:646 kOne
constexpr float kHeliZero = 0.0f;               // flatwater.c:648 kZero
constexpr double kHeliOneD = 1.0;               // mathvars.h:15  kOneD
constexpr double kHeliZeroD = 0.0;              // mathvars.h:5   kZeroD
constexpr float kHeliTwo = 2.0f;                // ammotype/gun.c:655 kTwo
constexpr float kHeliSevenTenths = 0.7f;        // missile/missile.c:834
constexpr float kHeliHundredth = 0.01f;         // tagitem/spline.c:850 kSmallAngle
constexpr float kHeliMilli = 0.001f;            // plane/plane.c:772
constexpr float kHeliDegToRad = 0.017453292f;   // scamera/scameralens.c:670
constexpr float kHeliQuarter = 0.25f;           // heli/heli.c:740
constexpr float kHeliTwentieth = 0.05f;         // heli/heli.c:742
constexpr float kHeliTwoHundredth = 0.005f;     // heli/heli.c:744
constexpr float kHeliHeliDamp = 0.95f;          // heli/heli.c:741
constexpr float kHeliDampZ = 0.97f;             // plane/hatch.c:93 kHatchKeep
constexpr float kHeliDamp98 = 0.98f;            // plane/plane.c:775 kKeep
constexpr float kHeliPitchDown = -0.7f;         // heli/heli.c:746
constexpr float kHeliRollForce = 0.08f;         // heli/heli.c:747
constexpr float kHeliYawForce = -0.8f;          // heli/heli.c:748
constexpr float kHeliLift = 11.150222f;         // heli/heli.c:750
constexpr float kHeliTwoMeters = 8192.0f;       // heli/heli.c:751
constexpr double kHeliTwoMetersD = 8192.0;      // plane/plane.c:774 kWheelUp
constexpr double kHeliPitchScale = 0.0002908882086657217;  // heli/heli.c:745
constexpr double kHeliAccelSpeed = 0.8;         // plane/plane.c:778 kThrustMin
constexpr double kHeliMaxThrottle = 1.25;       // plane/plane.c:780 kThrustMax
constexpr double kHeliSpeedStep = 0.01;         // plane/plane.c:779 kThrustStep
// The aero coefficient Heli_Move hands Body_AddAeroForces (heli.c:498). It is
// the same bit pattern as the rotor's kLift.
constexpr float kHeliAeroK = 0x1.56e264p-12f;   // 0.00032699998700991273

// Heli_Move's collision call (heli.c:500): radius, elasticity, friction.
constexpr float kHeliCollideRadius = 0x1.64ce9ep+5f;  // 44.600887298583984
constexpr float kHeliCollideElasticity = 1.0f;
constexpr float kHeliCollideFriction = 0x1.d70a3ep-1f;  // 0.9200000166893005

// ParamTable_New(13) (heli.c:183) and the 13 ParamTable_Add calls that follow
// (heli.c:186-200). The authored argument list is 19 tokens long because
// ObjectPos and Real32x9 each consume three script arguments and Real32x3
// consumes three more (paramops.c:182, :185, :423-435).
constexpr int kHeliParamCount = 13;
constexpr int kHeliTokenCount = 19;
// The editor's argTokens layout: [0]=task id, [1]=type, [2]=name, [3]=param 0
// (source/level/level_objects.h:116).
constexpr int kHeliTokenPrefix = 3;

// The throttle sentinel. Heli_Control tests the raw bits of axes[3] against
// 0xBF800000, i.e. exactly -1.0f, and treats any other value as a throttle
// request (heli.c:363-364). Heli_ReadKeys parks -1.0f there when the throttle
// keys are idle (heli.c:696) and -2.0f in axes[4] for the pilot animation
// (heli.c:707).
constexpr uint32_t kHeliThrustNoneBits = 0xBF800000u;
constexpr float kHeliThrustNone = -1.0f;
constexpr float kHeliPilotAnimNone = -2.0f;
constexpr int kHeliControlCount = 5;

// Heli_ReadKeys' ten throttle steps (heli.c:689).
constexpr int kHeliThrottleStepCount = 10;

// ---------------------------------------------------------------------------
// The authored parameter table
// ---------------------------------------------------------------------------

// One ParamTable_Add row. `engine_field` is the field the engine binds the
// authored slot to (heli.c:186-200) and is the load-bearing column: the
// authored *name* is what the level script writes, the engine field is what
// the runtime then reads.
struct HeliParamDesc {
    const char* authored_name;   // Task_DeclareParameters (objects.qsc:62-63)
    const char* engine_field;    // heli.c:186-200
    const char* type_name;       // Task_DeclareParameters
    int token_count;             // script argument count for that type
    bool has_range;              // ParamTable_Add's `extra` argument
    float range_min;
    float range_max;
};

const HeliParamDesc* HeliParamTable();

// The five vehicle controls, as `Heli_GetControlInfo` (heli.c:671-681) hands
// them out: `gHeliControls` (heli.c:754). Note the ranges are the exe's, and
// axes[3] (Thrust) is 0..1 while the sentinel parked in it is -1.0f.
struct HeliControlInfo {
    const char* name;
    float min;
    float max;
};

const HeliControlInfo* HeliControlTable();

// ---------------------------------------------------------------------------
// The parsed authored form
// ---------------------------------------------------------------------------

// A "Heli" task's 13 authored parameters. `orientation` is the three authored
// Euler angles in radians and `orientation_matrix` is the matrix the engine
// actually stores: ParamReal32x9_Parse (paramops.c:423-435) reads THREE angles
// and calls Matrix_SetEuler (matrix.c:99-110), which composes Rz*Ry*Rx.
struct HeliParams {
    double position[3];           // "Position"      ObjectPos -> v.startPos
    float orientation[3];         // "Orientation"   Real32x9  -> three angles
    float orientation_matrix[9];  // the Matrix_SetEuler result
    float original_thrust;        // "Original Thrust" Real32  -> v.start_speed
    float speed[3];               // "Speed"         Real32x3  -> v.startVel
    std::string model;            // "Model"         String16  -> v.name
    bool collision_detection;     // "Collision detection" bool8 -> v.collide
    bool force_z;                 // "Force Z"       bool8     -> v.airborne
    std::string open_door;        // "Open door"     VarString -> rotor_expr
    std::string can_fire;         // "Can fire"      VarString -> light_expr
    std::string play_sound;       // "Play sound"    VarString -> engine_expr
    float viewcone_alpha;         // "ViewCone Alpha (degrees)" Degrees
    float viewcone_gamma;         // "ViewCone Gamma (degrees)" Degrees
    float viewcone_length;        // "ViewCone Length (meter)"  Real32
};

// Parses the editor's argTokens layout for one authored "Heli" instance.
// Returns false when the list is too short to hold 19 parameters.
bool ParseHeliParams(const std::vector<std::string>& tokens, HeliParams* out);

// ---------------------------------------------------------------------------
// 3x3 rotation helpers (src/math/matrix.c)
// ---------------------------------------------------------------------------

// The engine's inlined 3x3 multiply, `Mul` (matrix.c:12-27). The original sums
// the three products in a fixed order per call site; `lastN` is the k that is
// added last in row N, exactly as rotor.h:159-174 documents for mat_mul.
void HeliMatrixMul(float out[9], const float a[9], const float b[9], int last0, int last1, int last2);

// Matrix_SetRotX (matrix.c:113-128) / Matrix_SetRotZ (matrix.c:148-164).
void HeliSetRotX(float m[9], float a);
void HeliSetRotZ(float m[9], float a);

// Matrix_SetEuler (matrix.c:99-110): Rz(az) * Ry(ay) * Rx(ax), count forced 0.
void HeliSetEuler(float m[9], float ax, float ay, float az);

// `fne` (heli.h:119-122): the x87 "not equal" test the engine writes as
// `a < b || a > b`, which is FALSE when either operand is NaN.
bool HeliFne(long double a, long double b);

// ---------------------------------------------------------------------------
// The flight-preview state (the fields Heli_Control and Heli_Move touch)
// ---------------------------------------------------------------------------

// The common vehicle physics config, parsed by Vehicle_ParseConfig
// (editobj/vehconfig.c:16-39) from the tail of a DefinePhysicsObjType argument
// list. `start_speed_scale` is the engine's `VehicleConfig.unk0`, which
// Rotor_DrawPrim reads as the rotor's speed scale (rotor.c:117, :140).
struct HeliVehicleConfig {
    float start_speed_scale;      // unk0
    struct {
        float rate;               // degrees
        float smooth;
    } axis[3];
    float thrust;
    float accel;
    float accel_slow;             // used below speed 0.8
    int part_count;
    float parts[8][7];
    float unk10C[3];
    bool valid;
};

// A helicopter's per-tick state, named after the engine fields.
struct HeliMoveState {
    float speed;                  // v.speed
    float target_speed;           // v.target_speed
    float steer[3];               // v.steer
    float turn[3];                // v.turn
    float matrix[9];              // v.matrix
    int matrix_count;             // Matrix.count
    float body_speed;             // v.body.speed
    float mass;                   // v.phys->mass
    int touching;                 // v.touching
    // The stack slot Heli_Move reuses for `pitch` when the heading basis is
    // degenerate (heli.c:419, :452, :475). It holds f2[0] from the turn-force
    // block when that block ran, and 0 otherwise.
    float stale;
    // The PhysicsObj_IsResting inputs (graphtask/physobj.c:515-527).
    int moved;                    // PhysObj.unk690 -- consumed and cleared by the test
    int force_count;              // Body.forceCount
    float ang_vel[3];             // Body.angVel
    float rest_speed;             // PhysType.restSpeed
    float rest_spin;              // PhysType.restSpin (already squared)
};

// A force as Body_AddForce stores it (graphtasktypes.h:34-37).
struct HeliForce {
    float force[3];
    float point[3];
};

// What one Heli_Move tick produced, in the order the engine produced it.
struct HeliMoveResult {
    float speed;
    float turn[3];
    float matrix[9];
    int matrix_count;
    int force_count;
    HeliForce forces[8];
    bool turn_forces_added;
    bool nose_pitch_applied;
    bool stale_used;              // the degenerate-basis branch was taken
    bool resting_gate_open;       // !PhysicsObj_IsResting(h)
    bool sound_updated;
    float sound_params[3];        // [0]=count, [1]=speed, [2]=fast
};

// ---------------------------------------------------------------------------
// Heli_Control (0x431B70, heli.c:359-407)
// ---------------------------------------------------------------------------

// The axes[3] sentinel test: `*(uint32_t *)&axes[3] != 0xBF800000` (heli.c:363).
bool HeliControlThrustRequested(const float axes_in[5]);

// The tick-0 reset (heli.c:365-401). Writes the caller's axes array back the
// way the engine does (axes[3] = -1, axes[4] = -2, axes[0..2] = 0), clears the
// smoothed turn and steer, restores speed/target_speed to the authored
// "Original Thrust" and the matrix to the authored "Orientation", and recomputes
// the start velocity as `matrix * startVel` (heli.c:392-395).
//
// `start_vel` is the authored "Speed" triplet and `start_matrix` the authored
// "Orientation" matrix; `start_speed` is the authored "Original Thrust".
// `state->matrix_count` is set to 0, which is what the engine ends up with: the
// assignment `h->v.matrix = h->v.startMatrix` (heli.c:376) copies the parsed
// start matrix, and ParamReal32x9_Parse always leaves count 0 (matrix.c:107).
void HeliControlReset(HeliMoveState* state, float axes_out[5], const double start_pos[3],
                      const float start_vel[3], const float start_matrix[9], float start_speed,
                      float out_vel[3]);

// The assignment Heli_Control performs on EVERY tick, before the reset branch
// (heli.c:363-366).
void HeliControlApplyAxes(HeliMoveState* state, const float axes_in[5]);

// Heli_ReadKeys' throttle ladder (heli.c:689): the ten digital throttle steps,
// which the editor's flight preview can offer as presets.
const float* HeliThrottleSteps();

// The two key-driven target-speed nudges (heli.c:706-719): -/+ kHeliSpeedStep,
// clamped to [kHeliAccelSpeed, kHeliMaxThrottle].
float HeliThrottleStep(float target_speed, int delta_steps);

// ---------------------------------------------------------------------------
// Heli_Move (0x431E30, heli.c:412-520)
// ---------------------------------------------------------------------------

// heli.c:416 -- the acceleration used this tick, chosen by whether the current
// speed has reached 0.8.
float HeliAccelRate(float speed, const HeliVehicleConfig& config);

// heli.c:422-433 -- the speed ramp plus the "snap a standstill to exactly 0"
// rule. `speed` is the current v.speed, `target` the requested throttle.
float HeliApproachSpeed(float speed, float target, float rate);

// The same ramp, before the single narrowing on the store into v.speed. The
// engine keeps `s` as a long double and uses it again for the roll force
// (heli.c:451), so the wide value is exposed rather than re-read from the
// narrowed field.
long double HeliApproachSpeedWide(float speed, float target, float rate);

// heli.c:434-439 -- the first-order smoothing of the three steer axes.
void HeliSmoothTurn(const float steer[3], float turn[3], const HeliVehicleConfig& config,
                    float out_turn[3]);

// heli.c:440-456 -- the three turning forces. Returns the number of forces the
// engine adds (0 or 3) and writes `out_stale` (f2[0]) for Heli_Move's reused
// local. The gate is `(t0||t1||t2 non-zero) && (!touching || body.speed > 2)`.
// `speed` is the WIDE ramp value (see HeliApproachSpeedWide).
int HeliTurnForces(const float turn[3], const HeliVehicleConfig& config, long double speed,
                   float mass,
                   int touching, float body_speed, HeliForce out_forces[3], float* out_stale);

// heli.c:463-476 -- the nose pitch angle. When the heading basis is degenerate
// (`m[0] == 0 && m[3] == 0`, where "== 0" is the x87 unordered-safe test) the
// engine reads the reused stack slot instead, and `*used_stale` is set.
long double HeliNosePitch(const float matrix[9], float stale, bool* used_stale);

// heli.c:477-484 -- the pitch rotation composed onto the current matrix:
// `nm = m * r` with r = Matrix_SetRotX(pitch * axis[2].rate * kPitchScale).
// The body-frame post-multiply is what makes the pitch follow the nose rather
// than the world; the engine writes the product out inline instead of calling
// `Mul`, and its per-row term order is reproduced exactly.
void HeliComposeNoseMatrix(const float matrix[9], int matrix_count,
                           const HeliVehicleConfig& config, long double pitch, float out_matrix[9],
                           int* out_count);

// heli.c:508-515 -- the rotor sound parameters. `sound_params[0]` is the count
// SoundParams_New(2) stored (soundsys.c:526-541); [1] is the speed and [2] is
// the `fast` flag, which is 0 only when the speed is not > 0.01.
void HeliRotorSoundParams(float speed, float out_sound_params[3]);

// PhysicsObj_IsResting (graphtask/physobj.c:515-527). The engine's version also
// consumes `moved` (it clears PhysObj.unk690); this port takes the value and
// leaves the caller to clear it, because the editor has no PhysObj.
bool HeliIsResting(int touching, bool moved, int force_count, float body_speed, float rest_speed,
                   const float ang_vel[3], float rest_spin);

// The whole deterministic Heli_Move tick, in the engine's order. The collision
// step (heli.c:500 PhysicsObj_Collide), the world moves (DynCube_Move) and the
// part broadcast are NOT modelled: they need the retail world and the editor
// has no equivalent. Everything that can be reproduced without a world is.
void HeliMoveStep(HeliMoveState* state, const HeliVehicleConfig& config, HeliMoveResult* out);

// ---------------------------------------------------------------------------
// HeliWheel (the landing gear Heli_UpdateWheels pushes on)
// ---------------------------------------------------------------------------

// HeliWheel_ParseArgs (vehicle/parts.c:284-292): TWO authored reals, stored
// SWAPPED into params[1] and params[0] and scaled by 4096. `params[0]` is the
// probe length Heli_UpdateWheels calls `len` and `params[1]` the `top`.
struct HeliWheelParams {
    float len;   // params[0] = authored arg 1 * 4096
    float top;   // params[1] = authored arg 0 * 4096
};

HeliWheelParams ParseHeliWheelParams(const std::vector<std::string>& tokens);

// One wheel's probe geometry, as Heli_UpdateWheels builds it (heli.c:542-559).
struct HeliWheelProbe {
    float len;        // params[0]
    float top;        // params[1]
    float offset[3];  // the wheel's offset at +0xF0
    double depth;     // -(len + 8192.0)
    double start_z;   // the wheel's pos.z + 8192.0
    double end_z;     // depth + start_z
};

HeliWheelProbe HeliBuildWheelProbe(const HeliWheelParams& params, const double wheel_pos[3],
                                   const float offset[3]);

// One wheel's contribution (heli.c:567-609). `found`/`hit_dist` are what
// Coll_Lines returned for this wheel; Coll_Lines itself needs the world and is
// not modelled.
struct HeliWheelLiftResult {
    bool applied;      // the wheel produced a force and bumped `touching`
    bool landed;       // the `landed` local's value after this iteration
    float force[6];
    double max_lift;   // only meaningful when `landed`
};

HeliWheelLiftResult HeliWheelLift(const HeliWheelProbe& probe, bool found, double hit_dist,
                                  float mass);

// heli.c:603-608 -- the per-contact velocity/spin damping.
void HeliWheelDamp(float vel[3], float ang_mom[3]);

// ---------------------------------------------------------------------------
// Rotor (src/rotor/rotor.c)
// ---------------------------------------------------------------------------

constexpr float kRotorLift = 44.600887298583984f;   // rotor.c:70  kLift
constexpr float kRotorTilt = 2.5f;                  // rotor.c:71  kTilt
constexpr float kRotorTiltY = 8192.0f;              // rotor.c:73  kTiltY
constexpr float kRotorMinPitch = -0.10000000149011612f;  // rotor.c:75 kMinPitch
constexpr float kRotorYawTilt = 0.0263671875f;      // rotor.c:76  kYawTilt
constexpr float kRotorMassTilt = 0.4000000059604645f;  // rotor.c:77 kMassTilt
constexpr float kRotorSpin = 21.362831115722656f;   // rotor.c:78  kSpin
constexpr double kRotorDegToRad = 0.017453293005625408;  // rotor.c:79
// sizeof(RotorDef) (rotor.c:58-68): char lift, char tilt, pad[2], float angle,
// float speed, int dir, char name[0x10].
constexpr int kRotorDefSize = 0x20;
constexpr int kRotorDefArgCount = 6;

// The six physics-def tokens Rotor_ParseDef consumes (rotor.c:302-316).
struct RotorDef {
    int lift;
    int tilt;
    int dir;        // the number of blade copies drawn per frame
    float angle;    // authored DEGREES, stored as radians
    float speed;
    std::string name;
    float angle_degrees;  // kept so the round-trip stays lossless
};

// The physics-def argument index the task-type parser is handed:
// `init.index = arg + 2` with `arg = 10 + 6 * joint_count`
// (physobjtype.c:64-122), i.e. 12 + 6 * joint_count. For the two retail
// helicopters that author joints this is 24.
int RotorDefArgIndex(int joint_count);

// Parses the six tokens starting at `index` of a DefinePhysicsObjType argument
// list. Returns false when the list is too short.
bool ParseRotorDef(const std::vector<std::string>& args, int index, RotorDef* out);

// The full DefinePhysicsObjType header (physobjtype.c:64-122): type, unk, mass,
// size[3], unk18, unk38, model, joint count, then the joints.
struct PhysicsDefHeader {
    std::string task_type;   // "TASKTYPE_HELI" / "TASKTYPE_ROTOR"
    int type_id;
    float mass;
    float size[3];
    std::string model;
    int joint_count;
    int first_arg;           // 12 + 6 * joint_count
};

bool ParsePhysicsDefHeader(const std::vector<std::string>& args, PhysicsDefHeader* out);

// Vehicle_ParseConfig (editobj/vehconfig.c:16-39) over the tail of a vehicle
// physics definition.
bool ParseHeliVehicleConfig(const std::vector<std::string>& args, int first_arg,
                            HeliVehicleConfig* out);

// The full HeliConfig: Vehicle_ParseConfig, then the two authored lists
// (plane/heli.c:77-88). `list[0..count1-1]` is parsed and stored but NEVER read
// anywhere in the retail engine -- only `count2` and `list[3..3+count2-1]` are
// consumed, as the explosion points (heli.c:258-278). The engine's own comment
// on Heli_ParseConfig records the second hazard: `list[3 + i]` overlaps the
// first list whenever count1 exceeds three.
struct HeliConfig {
    HeliVehicleConfig v;
    int count1;
    int list[13];
    int count2;
};

bool ParseHeliConfig(const std::vector<std::string>& args, int first_arg, HeliConfig* out);

// rotor.c:260 -- the spin accumulator. `value` advances by dir * speed *
// car_speed each tick.
float RotorRunValue(float value, const RotorDef& def, float car_speed);

// rotor.c:145 -- the angle the blade copies are drawn at.
float RotorSpinAngle(float value);

// rotor.c:131, :163 -- the angular step between consecutive blade copies.
float RotorBladeStep(const RotorDef& def, float car_speed);

// rotor.c:133-144 -- the per-frame blade tilt. `owner_matrix` is the owner's
// 3x3, `owner_vel` its body velocity, `start_speed_scale` the owner's
// VehicleConfig.unk0 (rotor.c:117, :140).
float RotorBladeTilt(const RotorDef& def, float car_speed, const float owner_matrix[9],
                     const float owner_vel[3], float start_speed_scale);

// rotor.c:218-243 -- the lift force. Returns false when the rotor has no lift
// or the lift product is exactly zero. `out_force` is a BodyForce.
bool RotorLiftForce(const RotorDef& def, float mass, float car_speed, const float owner_matrix[9],
                    HeliForce* out_force);

// rotor.c:245-259 -- the tilt force. Returns false when the gate fails
// (`speed != 0 && (tilt || lift) && !touching`).
bool RotorTiltForce(const RotorDef& def, float mass, float car_speed, int touching,
                    HeliForce* out_force);

// rotor.c:268-277 -- Rotor_Move's placement of the part under its owner.
void RotorOffsetPos(const float offset[3], const float owner_matrix[9], const double owner_pos[3],
                    double out_pos[3]);

// rotor.c:280 -- `mat_mul(&f, a->matrix, &o->local, 1, 0, 0)`.
void RotorLocalMatrix(float out[9], const float owner_matrix[9], const float local[9]);

// rotor.c:151-153 -- `copy = base * RotX(a)` then `copy = copy * RotZ(tilt)`, the
// two multiplies Rotor_DrawPrim performs per blade.
void RotorBladeMatrix(float out[9], const float base[9], float angle, float tilt);

}  // namespace igi
