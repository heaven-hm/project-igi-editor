// test_elevator_rules.cpp - the retail `Elevator` task's authored fields and the
// rule that places the platform on its rail (GitHub issue #40, "moving
// platforms").
//
// Oracle: /Users/haseeb-mir/Documents/Code/IGI-RE (read-only), the verified
// decompilation of retail IGI.exe. Every expectation below names the function
// and file:line it comes from. The retail fixture is the shipped LEVEL3 script
// as the editor's own QVM decompiler emits it.
#include <gtest/gtest.h>

#include "../source/renderer/spline_geometry.h"
#include "../source/runtime/elevator_rules.h"

#include <cmath>
#include <string>
#include <vector>

namespace {

using igi::BlendJointRotation;
using igi::ElevatorParams;
using igi::ElevatorPlacement;
using igi::ElevatorRail;
using igi::ParseElevatorParams;
using igi::PlaceElevatorOnRail;
using spline_geometry::EngineEulerMatrix;
using spline_geometry::RailPath;
using spline_geometry::SplineSpan;

// ─── Retail fixture ─────────────────────────────────────────────────────────
// The editor's decompiler emits, for the shipped LEVEL3 objects:
//
//   Task_New(698, "Elevator", "", 24557880.0, -55710968.0, 174421536.0, 0, 0, 0,
//            "200_01_1", 699, TRUE, FALSE, 0, 5.0, 1, "", "", <9 expressions>,
//            "elv_start_1", "elv_stop_1", "elv_move_1", ...)
//
// and rail 699 immediately above it:
//
//   Task_New(699, "SplineObj", "", FALSE, FALSE, FALSE, FALSE, 20, 0, 0, 0, 0,
//            3.1415929794311523, 1, 1, 1, 0, 0, 0,
//     Task_New(-1, "SplineObjWaypoint", "", 0, 0, 6.2482781410217285,
//              24557880.0, -55710968.0, 174421536.0, "", "", 20, FALSE, FALSE, FALSE),
//     Task_New(-1, "SplineObjWaypoint", "", 0, 0, 6.2482781410217285,
//              24557880.0, -55710968.0, 174546368.0, "", "", 20, FALSE, FALSE, FALSE));
//
// So the platform is a vertical lift of 124832 units (30.48 m at 4096 units/m)
// and `SplineObj taskid` really is the rail's task id.
constexpr double kRetailAz = 6.2482781410217285;   // waypoint "Orientation" az
constexpr double kRetailX = 24557880.0;
constexpr double kRetailY = -55710968.0;
constexpr double kRetailZBottom = 174421536.0;
constexpr double kRetailZTop = 174546368.0;

std::vector<std::string> RetailElevatorTokens() {
    // Index:            0    1          2   3          4           5
    return {
        "698", "Elevator", "", "24557880.0", "-55710968.0", "174421536.0",  // 0..5
        "0", "0", "0",                                                      // 6..8  Orientation
        "\"200_01_1\"",                                                     // 9     Model
        "699",                                                             // 10    SplineObj taskid
        "TRUE",                                                            // 11    Linear
        "FALSE",                                                           // 12    Force orientation
        "0",                                                               // 13    Pos
        "5.0", "1",                                                        // 14..15 Max speed, Speed inertia
        "\"\"", "\"\"", "\"Door_689.isClosed && Door_690.isClosed\"",      // 16..18
    };
}

// The retail rail 699, built through the same path the editor uses.
ElevatorRail RetailRail() {
    SplineSpan span;
    span.from = glm::dvec3(kRetailX, kRetailY, kRetailZBottom);
    span.to = glm::dvec3(kRetailX, kRetailY, kRetailZTop);
    span.rotFrom = EngineEulerMatrix(glm::dvec3(0.0, 0.0, kRetailAz));
    span.rotTo = EngineEulerMatrix(glm::dvec3(0.0, 0.0, kRetailAz));
    span.segments = 20;      // SplineObj "Number of Matrices / Segment" = 20
    span.rigid = false;      // SplineObj "Linear Segments" = FALSE
    span.reversed = false;   // SplineObjWaypoint "Flip" = FALSE

    ElevatorRail rail;
    rail.path = spline_geometry::BuildRailPath({span});
    rail.first_waypoint = span.from;
    rail.last_waypoint = span.to;
    return rail;
}

// A straight rail along +X with a rigid span, so its joints lie exactly on the
// chord and `path.length` is exactly the chord length. Used where the test
// needs an exact arc-length parameterisation rather than the retail curve.
ElevatorRail StraightRail(double length, int waypoints) {
    SplineSpan span;
    span.from = glm::dvec3(0.0, 0.0, 0.0);
    span.to = glm::dvec3(length, 0.0, 0.0);
    span.segments = 2;
    span.rigid = true;

    ElevatorRail rail;
    rail.path = spline_geometry::BuildRailPath({span});
    rail.first_waypoint = span.from;
    rail.last_waypoint = span.to;
    (void)waypoints;
    return rail;
}

}  // namespace

// ─── Authored fields ────────────────────────────────────────────────────────

TEST(ElevatorParams, ReadsTheRetailLevel3ElevatorCall) {
    const ElevatorParams params = ParseElevatorParams(RetailElevatorTokens());
    ASSERT_TRUE(params.parsed);
    // `Elevator_Init` HOOK 0x43CC70 (src/elevator/elevator.c:44-83) registers
    // "SplineObj taskid" as parameter 3, "Linear" 4, "Force orientation" 5 and
    // "Pos" 6. Param_ObjectPos is 3 tokens and Param_Real32x9 is 3
    // (ParamReal32x9_Parse HOOK 0x4EFBA0, src/physobj/paramops.c:423-435), so
    // the script indices are 3 + 3 + N: 10, 11, 12, 13.
    EXPECT_EQ(params.graph, 699);
    EXPECT_TRUE(params.linear);
    EXPECT_FALSE(params.force_orientation);
    EXPECT_DOUBLE_EQ(params.start_height, 0.0);
}

TEST(ElevatorParams, ShortCallIsReportedRatherThanHalfFilled) {
    std::vector<std::string> tokens = RetailElevatorTokens();
    tokens.resize(13);  // through "Force orientation" but not "Pos"
    const ElevatorParams params = ParseElevatorParams(tokens);
    EXPECT_FALSE(params.parsed);
    EXPECT_EQ(params.graph, -1);  // the engine's own "no rail" state
}

TEST(ElevatorParams, NonNumericRailIdIsRefused) {
    std::vector<std::string> tokens = RetailElevatorTokens();
    tokens[igi::kElevatorGraphArg] = "\"not_a_task\"";
    EXPECT_FALSE(ParseElevatorParams(tokens).parsed);
}

TEST(ElevatorParams, NoRailMeansNothingIsPlaced) {
    ElevatorParams params;
    params.parsed = true;
    params.graph = -1;  // Elevator_CreateHandler HOOK 0x43D010 sets graph = -1
    const ElevatorPlacement placed =
        PlaceElevatorOnRail(RetailRail(), params, glm::dmat3(1.0));
    EXPECT_FALSE(placed.found);
    EXPECT_DOUBLE_EQ(placed.height, 0.0);
}

// ─── The blended joint frame ────────────────────────────────────────────────

TEST(BlendJointRotation, IdenticalFramesReturnThatFrame) {
    // `Segment_BlendRot` (src/tagitem/spline.c:120-182): when the two joint
    // frames are equal the slerp angle is zero and the engine copies q0
    // verbatim, so the result is the input frame.
    const glm::dmat3 a = EngineEulerMatrix(glm::dvec3(0.3, -0.2, 1.1));
    const glm::dmat3 blended = BlendJointRotation(a, a, 0.37);
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            EXPECT_NEAR(blended[col][row], a[col][row], 1e-9)
                << "row " << row << " col " << col;
        }
    }
}

TEST(BlendJointRotation, InterpolatesBetweenTwoFrames) {
    // The weights are sin(angle*(1-t))/sin(angle) for `a` and
    // sin(angle*t)/sin(angle) for `b` (src/tagitem/spline.c:148-150), so t = 0
    // must land on `a` and t = 1 on `b`.
    const glm::dmat3 a = EngineEulerMatrix(glm::dvec3(0.0, 0.0, 0.0));
    const glm::dmat3 b = EngineEulerMatrix(glm::dvec3(0.0, 0.0, 1.0));

    const glm::dmat3 at0 = BlendJointRotation(a, b, 0.0);
    const glm::dmat3 at1 = BlendJointRotation(a, b, 1.0);
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            EXPECT_NEAR(at0[col][row], a[col][row], 1e-6);
            EXPECT_NEAR(at1[col][row], b[col][row], 1e-6);
        }
    }

    // Halfway through a pure Z rotation is half the angle, not the chord
    // midpoint: a slerp, not a linear blend of matrix elements.
    const glm::dmat3 half = BlendJointRotation(a, b, 0.5);
    const glm::dmat3 expected = EngineEulerMatrix(glm::dvec3(0.0, 0.0, 0.5));
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            EXPECT_NEAR(half[col][row], expected[col][row], 1e-6);
        }
    }
}

// ─── The linear branch ──────────────────────────────────────────────────────

TEST(PlaceElevatorOnRail, LinearBranchInterpolatesFirstToLastWaypoint) {
    // `if (e->unk18C) { ... t = e->height / len; place = a + (b - a) * t; }`
    // (src/props/elevator.c:58-72). The matrix is NOT touched on this path.
    const ElevatorRail rail = StraightRail(1000.0, 2);
    ASSERT_NEAR(rail.path.length, 1000.0, 1e-9);

    ElevatorParams params;
    params.parsed = true;
    params.graph = 1;
    params.linear = true;

    const glm::dmat3 authored = EngineEulerMatrix(glm::dvec3(0.1, 0.2, 0.3));

    params.start_height = 250.0;
    ElevatorPlacement placed = PlaceElevatorOnRail(rail, params, authored);
    ASSERT_TRUE(placed.found);
    EXPECT_NEAR(placed.position.x, 250.0, 1e-9);
    EXPECT_DOUBLE_EQ(placed.height, 250.0);
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            EXPECT_DOUBLE_EQ(placed.matrix[col][row], authored[col][row]);
        }
    }

    // The retail LEVEL3 lift is authored at height 0 and travels the whole rail.
    const ElevatorRail retail = RetailRail();
    ElevatorParams retail_params = ParseElevatorParams(RetailElevatorTokens());
    ASSERT_TRUE(retail_params.parsed);
    ASSERT_TRUE(retail_params.linear);

    placed = PlaceElevatorOnRail(retail, retail_params, glm::dmat3(1.0));
    ASSERT_TRUE(placed.found);
    EXPECT_NEAR(placed.position.x, kRetailX, 1e-6);
    EXPECT_NEAR(placed.position.y, kRetailY, 1e-6);
    EXPECT_NEAR(placed.position.z, kRetailZBottom, 1e-6);

    retail_params.start_height = retail.path.length;
    placed = PlaceElevatorOnRail(retail, retail_params, glm::dmat3(1.0));
    ASSERT_TRUE(placed.found);
    EXPECT_NEAR(placed.position.z, kRetailZTop, 1e-3);
    EXPECT_DOUBLE_EQ(placed.height, retail.path.length);
}

TEST(PlaceElevatorOnRail, HeightIsClampedToTheRail) {
    // `if (dist >= len) { dist = len - kTenth; MOV32(e->height, len); }` and
    // `if (!(dist >= kZero)) { dist = 0; e->height = 0; }` (src/props/elevator.c:48-56).
    const ElevatorRail rail = StraightRail(1000.0, 2);
    ElevatorParams params;
    params.parsed = true;
    params.graph = 1;
    params.linear = true;

    params.start_height = 5000.0;
    ElevatorPlacement placed = PlaceElevatorOnRail(rail, params, glm::dmat3(1.0));
    ASSERT_TRUE(placed.found);
    EXPECT_DOUBLE_EQ(placed.height, 1000.0);
    EXPECT_NEAR(placed.position.x, 1000.0, 1e-9);  // t = height / len = 1

    params.start_height = -25.0;
    placed = PlaceElevatorOnRail(rail, params, glm::dmat3(1.0));
    ASSERT_TRUE(placed.found);
    EXPECT_DOUBLE_EQ(placed.height, 0.0);
    EXPECT_NEAR(placed.position.x, 0.0, 1e-12);
}

// ─── The rail branch ────────────────────────────────────────────────────────

TEST(PlaceElevatorOnRail, RailBranchWalksTheCurveAndComposesTheFrame) {
    // `SplineObj_GetPoints(graph, &m, &e->place, &found, dist, 0, 0, offset)`
    // followed by `if (!e->unk18D) mat_mul(&e->obj.matrix, &e->placeMatrix, &m, 4)`
    // (src/props/elevator.c:73-80). mat_mul is `out = a * b`
    // (src/props/props.h:69-87).
    const ElevatorRail rail = StraightRail(1000.0, 2);
    ElevatorParams params;
    params.parsed = true;
    params.graph = 1;
    params.linear = false;

    params.start_height = 400.0;
    const glm::dmat3 authored = EngineEulerMatrix(glm::dvec3(0.0, 0.0, 0.5));
    ElevatorPlacement placed = PlaceElevatorOnRail(rail, params, authored);
    ASSERT_TRUE(placed.found);
    EXPECT_NEAR(placed.position.x, 400.0, 1e-6);

    // The rail's own joint frame, blended at t = 0.4. A rigid span builds each
    // joint as Matrix_SetRotZ(yaw) (src/sound/segsetup.c:133), so the frame is a
    // pure Z rotation and `authored * frame` is a Z rotation by 0.5 + yaw.
    const glm::dmat3 frame = BlendJointRotation(rail.path.joints[0].rot,
                                                rail.path.joints[1].rot, 0.4);
    const glm::dmat3 expected = authored * frame;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            EXPECT_NEAR(placed.matrix[col][row], expected[col][row], 1e-9);
        }
    }

    // "Force orientation" keeps the authored frame: the engine skips mat_mul.
    params.force_orientation = true;
    placed = PlaceElevatorOnRail(rail, params, authored);
    ASSERT_TRUE(placed.found);
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            EXPECT_DOUBLE_EQ(placed.matrix[col][row], authored[col][row]);
        }
    }
}

TEST(PlaceElevatorOnRail, RailBranchRefusesAZeroDistance) {
    // `if (IsZeroL(d)) goto at_start;` and, after the wrap,
    // `if (!(d > 0.0f)) { *found = 0; return; }` (src/tagitem/spline.c:256-261):
    // an authored position of exactly 0 leaves the object where it was.
    const ElevatorRail rail = StraightRail(1000.0, 2);
    ElevatorParams params;
    params.parsed = true;
    params.graph = 1;
    params.linear = false;
    params.start_height = 0.0;

    const ElevatorPlacement placed = PlaceElevatorOnRail(rail, params, glm::dmat3(1.0));
    EXPECT_FALSE(placed.found);
}

TEST(PlaceElevatorOnRail, RailBranchStopsOneEpsilonShortOfTheEnd) {
    // `if (dist >= len) { dist = len - kTenth; MOV32(e->height, len); }`
    // (src/props/elevator.c:48-53) with `kTenth` DATA(0x5333C8) INIT(0.1f)
    // (src/missile/missile.c:819). The step back exists so that
    // `SplineObj_GetPoints` does not wrap the distance back to the start of the
    // rail (its own `if (d >= obj->length) { do d -= obj->length; ... }`,
    // src/tagitem/spline.c:243-255). So a position at or past the end lands
    // exactly `kTenth` short -- never on the last joint, and never wrapped.
    const ElevatorRail rail = StraightRail(1000.0, 2);
    ElevatorParams params;
    params.parsed = true;
    params.graph = 1;
    params.linear = false;

    params.start_height = 1000.0;
    ElevatorPlacement placed = PlaceElevatorOnRail(rail, params, glm::dmat3(1.0));
    ASSERT_TRUE(placed.found);
    EXPECT_DOUBLE_EQ(placed.height, 1000.0);  // e->height is written back as len
    EXPECT_NEAR(placed.position.x, 1000.0 - igi::kElevatorEndEpsilon, 1e-9);

    // Past the end is the same clamp, not a wrap to the start.
    params.start_height = 5000.0;
    placed = PlaceElevatorOnRail(rail, params, glm::dmat3(1.0));
    ASSERT_TRUE(placed.found);
    EXPECT_DOUBLE_EQ(placed.height, 1000.0);
    EXPECT_NEAR(placed.position.x, 1000.0 - igi::kElevatorEndEpsilon, 1e-9);
    EXPECT_GT(placed.position.x, 0.0);
}
