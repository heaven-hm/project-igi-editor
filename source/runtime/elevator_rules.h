// elevator_rules.h - the retail `Elevator` task's authored fields and the rule
// that places the platform on its rail.
//
// GitHub issue #40 asks (among other things) for "Moving platforms/conveyors"
// to have "a path or animation preview": "Moving platforms reference a path
// spline or waypoint list stored in the level's object property block. The
// editor must: Read the path reference from the object's property fields;
// Visualize the path as a spline/line in the viewport; Optionally animate the
// platform along the path in preview mode."
//
// The issue names no retail type, so the first question is which task IS a
// moving platform. It is `Elevator`. The shipped missions place it in
// LEVEL3/5/8/11/12/13/14 (verified by scanning the shipped `OBJECTS.QVM` string
// tables for the task-type name), and the retail declaration in the shipped
// LEVEL3 script is, verbatim:
//
//   Task_DeclareParameters("Elevator", "Position", "ObjectPos", "Orientation",
//     "Real32x9", "Model", "String16", "SplineObj taskid", "Int32", "Linear",
//     "bool8", "Force orientation", "bool8", "Pos", "Real32", "Max speed",
//     "Real32", "Speed inertia", "Real32", "Floor down", "VarString",
//     "Floor up", ...)
//
// which is exactly the order `Elevator_Init` HOOK 0x43CC70 registers
// (`src/elevator/elevator.c:44-83`). `SplineObj taskid` is the rail reference
// the issue asks for; `Pos` is the platform's scalar position along it. The
// retail data confirms the index of every one of them: LEVEL3 authors
// `Task_New(698, "Elevator", "", 24557880.0, -55710968.0, 174421536.0, 0, 0, 0,
// "200_01_1", 699, TRUE, FALSE, 0, 5.0, 1, ...)` where 699 is the id of the
// `SplineObj` declared immediately above it.
//
// The placement rule is `Elevator_UpdatePlace` HOOK 0x43D960
// (`src/props/elevator.c:41-83`). It reuses the same `SplineObj_GetPoints`
// walk the train path uses, so the editor's existing `RailPath` is the right
// input.
//
// Everything here is cited to IGI-RE (`/Users/haseeb-mir/Documents/Code/IGI-RE`,
// read-only), function + HOOK/ORIG address + `file:line`. Nothing here touches
// GL or Windows, so the whole rule is unit-testable on the host build.
//
// Named divergences (never silently "fixed"):
//   * The oracle is compiled with `#pragma GCC optimize("excess-precision=standard")`
//     so its x87 intermediates keep 80 bits. This port keeps `long double`
//     intermediates and rounds at the same assignments, but `std::sqrt` /
//     `std::sin` are the host libm's, not the engine's `fsqrt` / `fsin`.
//   * `Elevator_UpdatePlace` writes back into `e->height` and leaves
//     `e->obj.matrix` untouched on two of its three paths. Both are reproduced
//     by returning the clamped height and by taking the caller's matrix as the
//     starting point.
#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "../renderer/spline_geometry.h"

namespace igi {

// ---------------------------------------------------------------------------
// Authored parameters
// ---------------------------------------------------------------------------

// `Elevator_Init` HOOK 0x43CC70 (src/elevator/elevator.c:44-83) registers 25
// parameters. `Param_ObjectPos` consumes 3 script tokens and `Param_Real32x9`
// consumes 3 (it is three Euler angles — `ParamReal32x9_Parse` HOOK 0x4EFBA0,
// src/physobj/paramops.c:423-435), so the QSC argument index of parameter N is
// 3 + N. The four this module reads are therefore:
constexpr int kElevatorGraphArg = 10;             // "SplineObj taskid", Param_Int32
constexpr int kElevatorLinearArg = 11;            // "Linear", Param_Bool8
constexpr int kElevatorForceOrientationArg = 12;  // "Force orientation", Param_Bool8
constexpr int kElevatorStartHeightArg = 13;       // "Pos", Param_Real32

// The shortest authored call this module can read: 14 tokens, i.e. through
// "Pos". A shorter call is reported as unparsed rather than half-filled.
constexpr std::size_t kElevatorMinTokens = 14;

// `kTenth` DATA(0x5333C8) (src/missile/missile.c:819, declared
// src/math/mathvars.h:11). `Elevator_UpdatePlace` steps the target back by this
// much when the authored position is at or past the end of the rail, so that
// `SplineObj_GetPoints` does not wrap it to the start.
constexpr float kElevatorEndEpsilon = 0.1f;

// One authored `Elevator` call.
struct ElevatorParams {
    bool parsed = false;
    int graph = -1;                  // task id of the `SplineObj` rail, -1 = none
    bool linear = false;             // "Linear": straight line first -> last floor
    bool force_orientation = false;  // "Force orientation": keep the authored frame
    double start_height = 0.0;       // "Pos": scalar distance along the rail
};

// Reads the four fields above out of an editor `LevelObject::argTokens` vector
// ([0] = task id, [1] = type, [2] = name, [3] = parameter 0). Returns a value
// with `parsed == false` when the call is too short or `graph` is not an
// integer; `graph` then stays -1, which is the engine's own "no rail" state
// (`Elevator_CreateHandler` HOOK 0x43D010, src/elevator/elevator.c:96).
ElevatorParams ParseElevatorParams(const std::vector<std::string>& arg_tokens);

// ---------------------------------------------------------------------------
// The rail the platform runs on
// ---------------------------------------------------------------------------

// `SplineObj_GetLength` HOOK 0x5020C0 (src/editobj/spline.c:107-121) is the sum
// of every waypoint segment's `length`, and a segment's `length` is the sum of
// its per-joint chord lengths (`setup_segments`, src/sound/segsetup.c:106-199).
// That is exactly `spline_geometry::RailPath::length`, so the editor's rail is
// used unchanged. `first_waypoint` / `last_waypoint` are what the `Linear`
// branch needs: `SplineObj_GetWaypointPos` HOOK 0x502200
// (src/editobj/spline.c:155-163) returns `w->point`, the AUTHORED waypoint
// position, not a joint of the bent curve.
struct ElevatorRail {
    spline_geometry::RailPath path;
    glm::dvec3 first_waypoint{0.0};
    glm::dvec3 last_waypoint{0.0};
};

// ---------------------------------------------------------------------------
// Placement
// ---------------------------------------------------------------------------

struct ElevatorPlacement {
    bool found = false;
    glm::dvec3 position{0.0};  // the engine's `e->place`
    glm::dmat3 matrix{1.0};    // the engine's `e->obj.matrix`
    double height = 0.0;       // the clamped `e->height` the engine writes back
};

// `Elevator_UpdatePlace` HOOK 0x43D960 (src/props/elevator.c:41-83).
//
// `place_matrix` is the object's authored orientation frame (`placeMatrix`,
// "Orientation"), which `Elevator_ParamsChanged` HOOK 0x43DDB0
// (src/props/elevator.c:114-144) has already copied into `e->obj.matrix` before
// it calls this function — so on the two paths that do not recompute the frame,
// the authored frame is what the object keeps.
//
// The three paths, reproduced exactly:
//   * `graph == -1`            -> not found, nothing written.
//   * `linear`                 -> `place` is the straight-line interpolation
//                                 between the FIRST and LAST waypoint at
//                                 `height / length`; the matrix is NOT touched.
//   * otherwise                -> `place` is the point `dist` along the rail and
//                                 the frame is the blended joint rotation; the
//                                 object's matrix becomes `place_matrix * frame`
//                                 unless "Force orientation" is set, in which
//                                 case it is left alone.
//
// The engine also writes `e->height` back, clamped to `[0, length]`, and it is
// that CLAMPED value the `linear` branch divides by. `height` in the result is
// the clamped value.
ElevatorPlacement PlaceElevatorOnRail(const ElevatorRail& rail,
                                      const ElevatorParams& params,
                                      const glm::dmat3& place_matrix) noexcept;

// `Segment_BlendRot` (static, src/tagitem/spline.c:120-182): the rotation
// between segment joints `a` and `b` at parameter `t`, as a matrix. The blend is
// a quaternion slerp whose weights are `sin(angle*(1-t))/sin(angle)` and
// `sin(angle*t)/sin(angle)` — note the order: `a` gets the `1-t` weight. Below
// `kSmallAngle` (0.01f, DATA(0x533714), src/tagitem/spline.c:850) the engine
// switches to a plain linear blend of the quaternion components.
//
// `a` and `b` are engine joint frames (`SplineObjSegment.segRot`, row-major as
// the engine stores them; `glm::dmat3` element `[col][row]` is `m[row*3+col]`,
// the convention `spline_geometry`'s `ToGlm`/`FromGlm` establish).
glm::dmat3 BlendJointRotation(const glm::dmat3& a, const glm::dmat3& b, double t) noexcept;

}  // namespace igi
