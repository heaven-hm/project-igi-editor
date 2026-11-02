// elevator_rules.h - Elevator task authored parameter layout and rail placement rules.
//
// MIT License.
#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "../renderer/spline_geometry.h"

namespace igi {

// ---------------------------------------------------------------------------
// Authored parameters
// ---------------------------------------------------------------------------

// Elevator task parameter indices in authored scripts.
constexpr int kElevatorGraphArg = 10;             // "SplineObj taskid", Param_Int32
constexpr int kElevatorLinearArg = 11;            // "Linear", Param_Bool8
constexpr int kElevatorForceOrientationArg = 12;  // "Force orientation", Param_Bool8
constexpr int kElevatorStartHeightArg = 13;       // "Pos", Param_Real32

// The shortest authored call this module can read: 14 tokens, i.e. through
// "Pos". A shorter call is reported as unparsed rather than half-filled.
constexpr std::size_t kElevatorMinTokens = 14;

// Small epsilon offset stepped back when the authored position is at or past
// the end of the rail, so the spline lookup does not wrap it to the start.
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
// integer; `graph` then stays -1 (no rail).
ElevatorParams ParseElevatorParams(const std::vector<std::string>& arg_tokens);

// ---------------------------------------------------------------------------
// The rail the platform runs on
// ---------------------------------------------------------------------------

// Spline rail path geometry and authored endpoints.
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
    glm::dvec3 position{0.0};  // calculated position
    glm::dmat3 matrix{1.0};    // calculated orientation matrix
    double height = 0.0;       // clamped height along the rail
};

// Calculates elevator position and orientation on rail based on authored params.
//
// Paths:
//   * `graph == -1`            -> not found, nothing written.
//   * `linear`                 -> position is straight-line interpolation
//                                 between the first and last waypoint at
//                                 `height / length`; matrix is untouched.
//   * otherwise                -> position is point along rail and frame is
//                                 blended joint rotation; matrix becomes
//                                 `place_matrix * frame` unless force orientation
//                                 is enabled.
ElevatorPlacement PlaceElevatorOnRail(const ElevatorRail& rail,
                                      const ElevatorParams& params,
                                      const glm::dmat3& place_matrix) noexcept;

// Blends rotation between segment joints at parameter t via quaternion slerp.
glm::dmat3 BlendJointRotation(const glm::dmat3& a, const glm::dmat3& b, double t) noexcept;

}  // namespace igi
