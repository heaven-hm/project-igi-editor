#pragma once
/******************************************************************************
 * @file    heli_preview.h
 * @brief   The placed-helicopter FLIGHT PREVIEW -- a "Heli" task's authored
 *          parameters driven through the engine's own per-tick control and
 *          motion rule (GitHub issue #60).
 *
 * WHY THIS EXISTS. `source/runtime/heli_rules.{h,cpp}` is the faithful
 * translation of `Heli_Control` (0x431B70) and `Heli_Move` (0x431E30), and until
 * now nothing called it: `grep -rn heli_rules source/` matched only its own
 * translation unit. The editor spun the rotor (via `igi::IgiRotorSpinMatrix`,
 * `source/renderer/renderer_objects_atta.cpp`) but the helicopter's own motion
 * was the static authored placement. This module is the missing link between
 * the two, and it is the same defect class the cutscene fix closed for
 * `cutscene_rules` (commit 0e0c3d9f, `source/runtime/cutscene_camera.{h,cpp}`):
 * a correct port with no caller.
 *
 * Everything here is cited to IGI-RE (`/Users/haseeb-mir/Documents/Code/IGI-RE`,
 * read-only), which is the only evidence source used. The rule itself is NOT
 * restated: `HeliControlApplyAxes`, `HeliControlReset` and `HeliMoveStep` all
 * live in `heli_rules.h` and are called, not copied. This header adds only the
 * authored-parameters -> per-tick-state step and the tick counter.
 *
 * The oracle files this module is built from:
 *
 *   src/heli/heli.c          Heli_Control 0x431B70 (:358-405) -- the axes
 *                            assignment (:360-364), the tick-0 reset (:365-400)
 *                            and the `if (tick > 0) Heli_Move` gate (:401-404)
 *   src/heli/heli.c          Heli_Move 0x431E30 (:412-520)
 *   src/vehbase/vehicle.c    Vehicle_ResetControls 0x4F4600 (:189-197)
 *   src/vehbase/animtask.c   AnimTask_Seek 0x4F3EF0 (:403-421), AnimTask_Run
 *                            0x4F3B40 (:292-298) and AnimTask_SendControl
 *                            0x4F4060 (:461-475) -- who sends the control
 *                            message, and with which tick
 *   src/graphtask/physobj.c  PhysicsObj_Collide 0x4FC0E0 (:404-447) -- the one
 *                            place the body's velocity advances the position
 *   src/animtrig/body.c      Body_Update 0x4ECDB0 (:325-339) -- speed and
 *                            direction from the velocity
 *   src/file/vec.c           Vec3_FastLength 0x4B2A10 (:140-181) -- the length
 *                            `Body_Update` stores as `body.speed`
 *   src/math/mathvars.h      kFps (:8) -- the 30 Hz tick rate
 *
 * THE TICK RULE, established from those functions and nothing else:
 *
 *   1. The retail game has no free-running helicopter. `Heli_Control` runs only
 *      when something broadcasts the control event: the player's input handler
 *      (`Heli_ReadKeys`, heli.c:687-719, which parks -1.0f in axes[3] when the
 *      throttle keys are idle) or an `AnimTask`, which plays authored key
 *      channels into the vehicle once per tick (`AnimTask_Seek` then
 *      `AnimTask_SendControl`, animtask.c:403-475).
 *   2. `AnimTask_Seek(t, 0)` zeroes every channel and calls `AnimTask_Step`
 *      once with `t->tick == 0` (animtask.c:412-419), so the first control
 *      message a vehicle ever sees carries tick 0. `AnimTask_Run` then advances
 *      the tick by one per frame (animtask.c:292-298).
 *   3. Tick 0 is the reset: `Heli_Control` teleports the helicopter to the
 *      authored start position, restores the authored orientation matrix and
 *      BOTH speed fields to the authored "Original Thrust", zeroes turn and
 *      steer, rewrites the caller's axes to (0,0,0,-1,-2) and recomputes the
 *      body velocity as `matrix * authored "Speed"` (heli.c:365-400).
 *   4. Tick > 0 is one `Heli_Move` (heli.c:401-404): the axes assignment
 *      (`target_speed` only when axes[3] is not the -1.0f sentinel), then the
 *      speed ramp, the smoothed three-axis turn forces, the nose pitch, the
 *      physics gate and the rotor sound parameters.
 *
 * WHAT THE PREVIEW THEREFORE IS: the engine's own tick sequence evaluated on the
 * authored values the editor already parses, from tick 0 upward, with no pilot
 * input beyond the throttle the caller asks for. With `throttle = -1.0f`
 * (`kHeliThrustNone`, the value `Heli_ReadKeys` parks when the keys are idle)
 * the helicopter flies at the authored "Original Thrust" with no turn input --
 * the flight the retail game shows for a helicopter whose driver never touches
 * the controls.
 *
 * Out of scope, deliberately, and why:
 *   * `Heli_UpdateWheels` (0x432550) and `PhysicsObj_Collide`'s collision half
 *     (0x4FC0E0) need the retail world (`Coll_Lines`, `DynCube_Move`); the
 *     editor has no world. The lift/landing geometry is ported and tested in
 *     `heli_rules.h` (`HeliWheelLift`, `HeliBuildWheelProbe`) but is not driven
 *     from here.
 *   * `Body_AddAeroForces` (body.c:47-...) and the contact impulses need the
 *     authored part list and the world.
 *   * The `AnimTask` key channels: the editor does not parse a helicopter's
 *     AnimTask, so the preview offers the throttle as a caller-supplied
 *     parameter instead of inventing a timeline.
 *****************************************************************************/

#include "runtime/heli_rules.h"
// `FastLength3` -- the Vec3_FastLength port the position gate's body speed uses.
// `heli_preview.cpp` includes it too; it is re-exported here so a caller that
// only wants `HeliPreviewAdvancePosition` can check the gate's input.
#include "runtime/mission_fog.h"

#include <string>
#include <vector>

namespace igi {

// `kFps` (src/math/mathvars.h:8) -- the tick rate the whole game runs at. The
// preview advances at this rate so a preview second is a retail second.
constexpr float kHeliPreviewTicksPerSecond = kHeliTickRate;

// One placed helicopter's flight-preview state. The fields named after engine
// fields are the ones `Heli_Control` and `Heli_Move` read and write; `pos` and
// `vel` are the engine's `v.pos` and `v.body.vel`.
struct HeliPreviewState {
    HeliMoveState move;        // v.speed / v.target_speed / v.steer / v.turn / v.matrix
    double pos[3];             // v.pos
    float vel[3];              // v.body.vel, world space (heli.c:390-393)
    double start_pos[3];       // v.startPos
    float start_vel[3];        // v.startVel
    float start_matrix[9];     // v.startMatrix
    float start_speed;         // v.start_speed -- the authored "Original Thrust"
    HeliVehicleConfig config;  // phys->data (Vehicle_ParseConfig 0x5031F0)
    int tick;                  // the last tick applied; -1 before any
    bool valid;                // false when the authored parameters did not parse
};

// The control message a tick carries -- the engine's `ControlArg` (`heli.h:64-67`:
// `{int tick; float *axes}`) minus the tick, which is the parameter the caller
// passes separately.
//
// The five slots are `heli.c:360-375`'s own: `axes[0..2]` are the three turn
// axes (roll, pitch, yaw, in the order `Heli_GetControlInfo` publishes them,
// `heli.c:754`), `axes[3]` is the throttle and `axes[4]` the pilot animation.
//
// `kHeliThrustNone` (-1.0f) in `throttle` is "no request": the assignment at
// heli.c:360-361 then leaves `target_speed` alone, exactly as `Heli_ReadKeys`
// leaves it when the throttle keys are idle (heli.c:696). Any other value is
// taken as the requested throttle -- including a NaN, because the engine tests
// the raw bits.
//
// `pilot_anim` defaults to `kHeliPilotAnimNone` (-2.0f), the value
// `Heli_ReadKeys` parks there (heli.c:707). The engine never reads it in
// `Heli_Control` or `Heli_Move`; it is carried so the message is the engine's
// whole shape rather than a subset.
struct HeliPreviewInput {
    float steer[3] = {0.0f, 0.0f, 0.0f};
    float throttle = kHeliThrustNone;
    float pilot_anim = kHeliPilotAnimNone;
};

// Builds the preview state for one authored "Heli" instance.
//
// `tokens` is the object's `LevelObject::argTokens` ([0] = task id, [1] = "Heli",
// [2] = name, [3] = parameter 0) -- the same layout `ParseHeliParams` consumes
// (`heli_rules.h`). `config` is the vehicle config of the helicopter's own
// physics definition (`ParseHeliVehicleConfig` over its HELI.QVM entry); pass a
// default-constructed one when the level does not resolve it, and the preview
// then uses the engine's own arithmetic on zero rates rather than guessing.
//
// The returned state is the object BEFORE any tick has run: `tick == -1`, the
// authored position and matrix, and a zeroed move state. Tick 0 is what performs
// the engine's reset (see `HeliPreviewTick`).
//
// `valid` is false when the token list is too short to hold the 19 authored
// parameters, which is the same refusal `ParseHeliParams` documents: a preview
// built from a truncated argument list would look plausible and be wrong.
HeliPreviewState BuildHeliPreviewState(const std::vector<std::string>& tokens,
                                       const HeliVehicleConfig& config);

// The engine's per-tick sequence, one tick at a time. Returns true when the tick
// produced motion (`Heli_Move` ran, i.e. tick > 0), false when the tick was the
// reset or the state is invalid.
//
// `input` is the control message this tick carries. `tick` is the tick being
// applied: 0 performs the reset (and, as in the engine, the reset overwrites the
// axes, so `input` has no effect on that tick); any other value runs one
// `Heli_Move`.
bool HeliPreviewTick(HeliPreviewState* state, int tick, const HeliPreviewInput& input);

// `HeliPreviewTick` from the state's current tick up to and including `tick`, in
// order. A `tick` at or below the state's current tick does nothing, which is
// what makes the preview seekable: a caller can hold one state and step it
// forward, or rebuild and run it to any tick.
void HeliPreviewRunToTick(HeliPreviewState* state, int tick, const HeliPreviewInput& input);

// The tick an editor preview clock at `seconds` corresponds to, at the engine's
// 30 Hz (`kFps`, src/math/mathvars.h:8). Negative input clamps to 0.
int HeliPreviewTickFromSeconds(float seconds);

// ---------------------------------------------------------------------------
// The one engine step the preview performs itself
// ---------------------------------------------------------------------------

// `PhysicsObj_Collide`'s position integration (graphtask/physobj.c:422-426):
//
//     if (o->body.speed > kSlop.f) {
//         pos.x = o->body.vel.x + pos.x;  /* and y, z */
//     }
//
// `kSlop` is F32(0x4223D70A) = 40.96 (physobj.c:47), the engine's "not moving"
// threshold -- one metre in raw units. The comparison is `body.speed`, which is
// `Vec3_FastLength(vel)` (body.c:325-339, vec.c:140-181).
//
// The retail tick damps the velocity first (`PhysicsObj_Integrate`,
// physobj.c:384-386). `Heli_Move` calls `PhysicsObj_Collide` with damp = 1.0
// (heli.c:500 -- `0x1.d70a3ep-1f` is the spinDamp), so for a helicopter the
// velocity the preview carries is already the value this adds; no damping is
// re-applied here.
//
// `pos` is updated in place and is the engine's `double` position.
void HeliPreviewAdvancePosition(double pos[3], const float vel[3]);

// ---------------------------------------------------------------------------
// The vehicle config a helicopter's own physics definition carries
// ---------------------------------------------------------------------------

// `Heli_Move` reads `phys->data` -- the `VehicleConfig` `Vehicle_ParseConfig`
// (0x5031F0, src/editobj/vehconfig.c:16-39) parsed out of the helicopter's
// `DefinePhysicsObjType` entry. `phys` is set from the authored model name
// (`Heli_ParseArgs` 0x432F60 -> `Heli_ParseConfig` 0x433030), so a BELL and a
// MIL helicopter have different `axis[i].rate` and `accel` values.
//
// The editor does not track which physics definition a placement resolves to, so
// this helper takes the decompiled text of the HELI.QVM the level's helicopters
// reference and the model name the placement authored, and returns the first
// `TASKTYPE_HELI` definition whose `model` matches. When `model` is empty the
// first `TASKTYPE_HELI` definition wins.
//
// Returns false -- leaving `out` untouched -- when no definition matches, when
// the text has no physics definition at all, or when the matching definition
// does not parse. A caller that gets false must NOT invent rates: pass a
// default-constructed `HeliVehicleConfig` to `BuildHeliPreviewState` and the
// preview will use the engine's own arithmetic on zero rates.
bool ResolveHeliVehicleConfig(const std::string& decompiled_physics_qvm,
                              const std::string& model, HeliVehicleConfig* out);

}  // namespace igi
