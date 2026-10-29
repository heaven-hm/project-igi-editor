// heli_preview.cpp -- see heli_preview.h for the issue mapping (#60), the oracle
// files, the tick rule and what is deliberately out of scope.
//
// This file is glue, not rules. Every value it produces comes out of
// `heli_rules.h`; the only decisions made here are which tick performs the
// reset, which tick runs the motion, and where the body's velocity advances the
// position.
#include "runtime/heli_preview.h"

#include "runtime/mission_fog.h"  // FastLength3 -- Vec3_FastLength 0x4B2A10

#include <cmath>

namespace igi {

namespace {

// F32(0x4223D70A) = 40.96 (src/graphtask/physobj.c:47). The engine's "not
// moving" threshold, and the value `PhysicsObj_Collide` compares `body.speed`
// against before it adds the velocity to the position (physobj.c:422).
constexpr float kHeliPreviewSlop = 40.96f;

}  // namespace

HeliPreviewState BuildHeliPreviewState(const std::vector<std::string>& tokens,
                                       const HeliVehicleConfig& config) {
    HeliPreviewState state{};
    state.tick = -1;
    state.config = config;

    // The 19 authored parameters, exactly as the engine's own ParamTable_Add
    // order reads them (heli.c:186-200). A truncated list is a refusal, not a
    // defaults-filled helicopter: the same rule `ParseHeliParams` states.
    HeliParams params;
    if (!ParseHeliParams(tokens, &params)) {
        return state;
    }

    // v.startPos (heli.c:187) and v.startVel (heli.c:189).
    for (int i = 0; i < 3; ++i) {
        state.start_pos[i] = params.position[i];
        state.pos[i] = params.position[i];
        state.start_vel[i] = params.speed[i];
    }

    // v.startMatrix (heli.c:188) -- the Matrix_SetEuler product of the three
    // authored angles, which is what `h->v.matrix = h->v.startMatrix` copies on
    // tick 0 (heli.c:376).
    for (int i = 0; i < 9; ++i) state.start_matrix[i] = params.orientation_matrix[i];
    state.move.matrix_count = 0;

    // v.start_speed (heli.c:190) -- the authored "Original Thrust".
    state.start_speed = params.original_thrust;

    // The state a freshly created Heli is in: `Heli_Create` -> `Heli_Reset` ->
    // `RigidDynCubeObj_Init`/`Vehicle_Init`, which zeroes the controls
    // (Vehicle_ResetControls 0x4F4600, vehicle.c:189-197). Tick 0 then performs
    // the authored reset; until it does, the preview reports the authored pose.
    state.move.speed = 0.0f;
    state.move.target_speed = 0.0f;
    for (int i = 0; i < 3; ++i) {
        state.move.steer[i] = 0.0f;
        state.move.turn[i] = 0.0f;
    }
    for (int i = 0; i < 9; ++i) state.move.matrix[i] = params.orientation_matrix[i];

    state.valid = true;
    return state;
}

bool HeliPreviewTick(HeliPreviewState* state, int tick, const HeliPreviewInput& input) {
    if (state == nullptr || !state->valid) return false;

    // The control message the engine would have received this tick. `axes[0..2]`
    // are the three turn axes, `axes[3]` the throttle and `axes[4]` the pilot
    // animation (heli.c:360-364, :371-375).
    float axes[kHeliControlCount] = {input.steer[0], input.steer[1], input.steer[2],
                                     input.throttle, input.pilot_anim};

    if (tick == 0) {
        // heli.c:365-400. The axes assignment at :360-364 runs first and the
        // reset then overwrites all five slots, so neither the throttle nor the
        // steer passed in has any effect on this tick -- which is why the reset
        // is not conditional on them.
        HeliControlReset(&state->move, axes, state->start_pos, state->start_vel,
                         state->start_matrix, state->start_speed, state->vel);
        // DynCube_Move(Game_GetWorld(), h, &h->v.startPos, r) (heli.c:370) is
        // the world move the editor cannot reproduce; the authored position is
        // the result, and `v.pos` is where the helicopter then sits.
        for (int i = 0; i < 3; ++i) state->pos[i] = state->start_pos[i];
        // Body_Update(&h->v.body) (heli.c:394) refreshes body.speed from the
        // velocity it was just given.
        state->move.body_speed = FastLength3(state->vel);
        state->tick = 0;
        return false;
    }

    if (tick < 0) return false;

    // heli.c:360-364 -- the axes assignment, on every tick that is not the
    // reset. `HeliControlApplyAxes` implements the raw-bits sentinel test, so a
    // throttle of exactly -1.0f leaves target_speed where the last tick put it.
    HeliControlApplyAxes(&state->move, axes);

    // heli.c:401-404 -- `if (arg->tick > 0) { Vehicle_RunParts(h); Heli_Move(h); }`.
    // Vehicle_RunParts drives the attached magic objects (the rotor); the rotor's
    // own spin is already previewed by the renderer's `IgiRotorSpinMatrix`.
    HeliMoveResult result;
    HeliMoveStep(&state->move, state->config, &result);

    // PhysicsObj_Collide's integration (physobj.c:422-426), once per tick, on
    // the velocity the tick left behind. `Body_Update` is what the engine's
    // Body_Integrate tail calls to refresh body.speed (body.c:303).
    state->move.body_speed = FastLength3(state->vel);
    HeliPreviewAdvancePosition(state->pos, state->vel);

    state->tick = tick;
    return true;
}

void HeliPreviewRunToTick(HeliPreviewState* state, int tick, const HeliPreviewInput& input) {
    if (state == nullptr || !state->valid) return;
    for (int next = state->tick + 1; next <= tick; ++next) {
        HeliPreviewTick(state, next, input);
    }
}

int HeliPreviewTickFromSeconds(float seconds) {
    if (!(seconds > 0.0f)) return 0;  // also false for NaN
    return static_cast<int>(seconds * kHeliPreviewTicksPerSecond);
}

void HeliPreviewAdvancePosition(double pos[3], const float vel[3]) {
    if (pos == nullptr || vel == nullptr) return;
    // physobj.c:422 -- `if (o->body.speed > kSlop.f)`, with body.speed the
    // Vec3_FastLength of the velocity (body.c:325-339).
    if (!(FastLength3(vel) > kHeliPreviewSlop)) return;
    pos[0] = static_cast<double>(vel[0]) + pos[0];  // physobj.c:423
    pos[1] = static_cast<double>(vel[1]) + pos[1];  // physobj.c:424
    pos[2] = static_cast<double>(vel[2]) + pos[2];  // physobj.c:425
}

bool ResolveHeliVehicleConfig(const std::string& decompiled_physics_qvm,
                              const std::string& model, HeliVehicleConfig* out) {
    if (out == nullptr) return false;

    const std::vector<std::vector<std::string>> defs =
        ParseDefinePhysicsObjTypeCalls(decompiled_physics_qvm);

    for (const std::vector<std::string>& args : defs) {
        PhysicsDefHeader header;
        if (!ParsePhysicsDefHeader(args, &header)) continue;
        if (header.task_type != "TASKTYPE_HELI") continue;
        if (!model.empty() && header.model != model) continue;

        HeliVehicleConfig parsed;
        if (!ParseHeliVehicleConfig(args, header.first_arg, &parsed)) {
            // A matching definition that does not parse is a refusal, not a
            // reason to fall through to a different helicopter's config.
            return false;
        }
        *out = parsed;
        return true;
    }
    return false;
}

}  // namespace igi
