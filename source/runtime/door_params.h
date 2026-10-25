#pragma once

// `Door` argument layout, extracted from the engine's own parameter table so the
// mapping is a pure function that a host-native test can drive with real retail
// task lines (see `tests/test_door_params.cpp`).
//
// IGI-RE `src/door/door.c:257-269` registers the fields in this order:
//
//     ParamTable_Add(gDoorParams, NULL, FIELD(place),    Param_ObjectPos(), 7, NULL);      // 3,4,5
//     ParamTable_Add(gDoorParams, NULL, FIELD(slide[0]), Param_Real32(),    7, &slideRange); // 6
//     ParamTable_Add(gDoorParams, NULL, FIELD(slide[1]), Param_Real32(),    7, &slideRange); // 7
//     ParamTable_Add(gDoorParams, NULL, FIELD(slidePos), Param_Real32(),    7, &unitRange);  // 8
//     ParamTable_Add(gDoorParams, NULL, FIELD(base),     Param_Real32x9(),  7, NULL);      // 9,10,11
//     ParamTable_Add(gDoorParams, NULL, FIELD(model),    Param_String16(),  7, NULL);      // 12
//     ParamTable_Add(gDoorParams, NULL, FIELD(openAngle),Param_Real32(),    7, &angleRange); // 13
//     ParamTable_Add(gDoorParams, NULL, FIELD(openTime), Param_Real32(),    7, &timeRange);  // 14
//     ParamTable_Add(gDoorParams, NULL, FIELD(pickable), Param_Bool8(),     7, NULL);      // 15
//     ParamTable_Add(gDoorParams, NULL, FIELD(pickTime), Param_Real32(),    7, NULL);      // 16
//     ParamTable_Add(gDoorParams, NULL, FIELD(lockExpr), Param_VarString(), 7, NULL);      // 17
//     ParamTable_Add(gDoorParams, NULL, FIELD(openExpr), Param_VarString(), 7, NULL);      // 18
//     ParamTable_Add(gDoorParams, NULL, FIELD(closeExpr),Param_VarString(), 7, NULL);      // 19
//     ... three sound names                                                              // 20,21,22
//
// Two facts about that table decide the whole mapping:
//
//   * `Real32x9` is an EULER TRIPLE, not nine arguments. `ParamReal32x9_Parse`
//     (HOOK 0x4EFBA0, `src/physobj/paramops.c:423-436`) calls `ParamCursor_Need(c, 3)`,
//     reads three reals and advances the cursor by 3, then hands them to
//     `Matrix_SetEuler`. Every retail door has its yaw in argument 11 and its model
//     in argument 12, which is only consistent with a three-argument matrix.
//   * `struct Door` carries `float slide[2]` (`src/door/door.h:45`); the offset has
//     no Z component. `Door_Place` (HOOK 0x446290, `door.c:121-138`) uses only
//     `slide[0]` and `slide[1]`, and argument 8 (`slidePos`) is the initial open
//     fraction, folded into the offset as `u = slide[0] * slidePos * kSlideScale`.
//
// The authored units are metres (`gDoorSlideRange` is -10..10, `gDoorAngleRange`
// is -360..360 degrees) and the engine scales the offset by `kSlideScale`
// (`src/addr.c:29`, aliased to `kMeter`/`k4096` at 0x5333B8, i.e. 4096 units per
// metre). The editor's `igi::PlayerController::WORLD_METER` is 4096 for the same
// reason (`source/player_controller.h:60`), so the caller's conversion is the
// engine's own. Callers pass it in so this header stays free of editor state.

#include "runtime/door_state.h"

#include <string>
#include <vector>

namespace igi {

// Argument positions, named so a reader never has to count again.
inline constexpr size_t kDoorArgSlideX = 6;
inline constexpr size_t kDoorArgSlideY = 7;
inline constexpr size_t kDoorArgSlidePos = 8;
inline constexpr size_t kDoorArgOrientation = 9;  // consumes 9, 10, 11
inline constexpr size_t kDoorArgModel = 12;
inline constexpr size_t kDoorArgMaxAngle = 13;
inline constexpr size_t kDoorArgOpenTime = 14;
inline constexpr size_t kDoorArgPickable = 15;
inline constexpr size_t kDoorArgPickTime = 16;
inline constexpr size_t kDoorArgLockExpression = 17;
inline constexpr size_t kDoorArgOpenExpression = 18;
inline constexpr size_t kDoorArgCloseExpression = 19;
inline constexpr size_t kDoorArgOpenSound = 20;
inline constexpr size_t kDoorArgCloseSound = 21;
inline constexpr size_t kDoorArgMoveSound = 22;

// Strips one layer of surrounding double quotes, like the loader does.
inline std::string DoorArgToken(const std::vector<std::string>& tokens, size_t index) {
    if (index >= tokens.size()) {
        return {};
    }
    const std::string& token = tokens[index];
    if (token.size() >= 2 && token.front() == '"' && token.back() == '"') {
        return token.substr(1, token.size() - 2);
    }
    return token;
}

inline float DoorArgReal(const std::vector<std::string>& tokens, size_t index, float fallback) {
    const std::string token = DoorArgToken(tokens, index);
    if (token.empty()) {
        return fallback;
    }
    try {
        return std::stof(token);
    } catch (...) {
        return fallback;
    }
}

inline bool DoorArgBool(const std::vector<std::string>& tokens, size_t index) {
    const std::string token = DoorArgToken(tokens, index);
    return token == "1" || token == "TRUE" || token == "true";
}

// Fills every layout-dependent field of `definition` from a task's raw argument
// tokens (the same vector `LevelObject::argTokens` carries, indexed from the task
// id at 0, so argument N is `tokens[N]`).
inline void ApplyDoorArgumentLayout(const std::vector<std::string>& tokens,
                                    float units_per_metre,
                                    RuntimeDoorDefinition& definition) {
    definition.slide_offset_units = glm::vec3(
        DoorArgReal(tokens, kDoorArgSlideX, 0.0f) * units_per_metre,
        DoorArgReal(tokens, kDoorArgSlideY, 0.0f) * units_per_metre,
        0.0f);  // `slide[2]` does not exist; argument 8 is the initial open fraction.
    definition.maximum_angle_degrees = DoorArgReal(tokens, kDoorArgMaxAngle, 0.0f);
    definition.open_time_seconds = DoorArgReal(tokens, kDoorArgOpenTime, 1.0f);
    definition.pickable = DoorArgBool(tokens, kDoorArgPickable);
    definition.locked_expression = DoorArgToken(tokens, kDoorArgLockExpression);
    definition.open_expression = DoorArgToken(tokens, kDoorArgOpenExpression);
    definition.close_expression = DoorArgToken(tokens, kDoorArgCloseExpression);
    definition.open_sound = DoorArgToken(tokens, kDoorArgOpenSound);
    definition.close_sound = DoorArgToken(tokens, kDoorArgCloseSound);
    definition.move_sound = DoorArgToken(tokens, kDoorArgMoveSound);
}

}  // namespace igi
