// Regression tests for the rail/train placement applied in
// source/app_lookup.cpp (App::EvaluateTrainTrackPositions). The old code there
//   (a) added the rail length ONCE and clamped  -> a position more than one rail
//       length out of range collapsed to arc 0,
//   (b) sampled a straight-line polyline and rebuilt the heading with
//       atan2/asin instead of the engine's Hermite curve + Matrix_SetDirection
//       + orient(), and
//   (c) spaced every wagon by a cumulative offset that also drove its heading.
// Each test below asserts the IGI-RE behaviour the replacement helpers implement,
// with the old result named where it differs. Every expected value is derived
// from the reconstructed engine code; the file:line in each test is the source of
// the rule being pinned.

#include "../source/renderer/spline_geometry.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

using spline_geometry::BuildRailPath;
using spline_geometry::EngineEulerMatrix;
using spline_geometry::PlaceTrainOnRail;
using spline_geometry::RailPath;
using spline_geometry::SampleRailPoint;
using spline_geometry::SplineSpan;
using spline_geometry::TrackDirectionMatrix;
using spline_geometry::TrainPlacement;
using spline_geometry::WrapRailDistance;

// Train_ParamsChanged, src/missile/train.c:142-167: t->length is the distance
// between the model's local points 0 and 1. The editor does not parse those, so
// app_lookup.cpp carries it as a documented constant; the tests use it so the
// numbers match the call shape used in production.
constexpr double kCarLength = 52756.0;

// A straight rail along +X, split into `spans` equal spans.
RailPath StraightRail(double length, int spans) {
    std::vector<SplineSpan> out;
    for (int i = 0; i < spans; ++i) {
        SplineSpan span;
        span.from = glm::dvec3(length * i / spans, 0.0, 0.0);
        span.to = glm::dvec3(length * (i + 1) / spans, 0.0, 0.0);
        span.rotFrom = glm::dmat3(1.0);
        span.rotTo = glm::dmat3(1.0);
        span.segments = 4;
        span.rigid = true;  // straight, so the arc length equals the chord sum
        out.push_back(span);
    }
    return BuildRailPath(out);
}

// A rail in the XY plane whose waypoint frames turn by 15 degrees per span, so
// the Hermite curve genuinely bends away from the polyline. Long enough that
// several car lengths of consist fit inside it without hitting either end.
RailPath CurvedRail(int spans) {
    std::vector<SplineSpan> out;
    for (int i = 0; i < spans; ++i) {
        SplineSpan span;
        span.from = glm::dvec3(100000.0 * i, 20000.0 * i * i, 0.0);
        span.to = glm::dvec3(100000.0 * (i + 1), 20000.0 * (i + 1) * (i + 1), 0.0);
        span.rotFrom = EngineEulerMatrix(glm::dvec3(0.0, 0.0, i * kPi / 12.0));
        span.rotTo = EngineEulerMatrix(glm::dvec3(0.0, 0.0, (i + 1) * kPi / 12.0));
        span.segments = 6;
        span.rigid = false;
        out.push_back(span);
    }
    return BuildRailPath(out);
}

}  // namespace

// The retail rail from the shipped mission data:
//   /Users/haseeb-mir/Downloads/IGI/MISSIONS/location0/LEVEL1/objects.qsc
// `Task_New(1500, "SplineObj", "", FALSE, FALSE, FALSE, FALSE, 20, 0, 0, 0, 0,
//  3.1415929794311523, 1, 1, 1, 0, 0, 0, ...)` holds 11 `SplineObjWaypoint`
// children, and every `Train` in the level (task ids 1499, …) names 1500 as its
// rail. The waypoints carry their own NumAreas counts, so the engine's rail
// length is the sum of 10 Hermite spans with 20/10/10/10/10/10/20/20/10/10/10
// sub-segments (SplineObjSegment_Setup, src/sound/segsetup.c:256-366).
namespace {

struct RetailWaypoint {
    double ax, ay, az;   // args 3-5, Orientation
    double x, y, z;      // args 6-8, Position
    int segments;        // arg 11, NumAreas
};

const RetailWaypoint kLevel1Rail[] = {
    {0.0, 0.0, 3.1415929794311523, 25287152.0, -55948040.0, 174410960.0, 20},
    {0.0, 0.0, 3.1415929794311523, 25124074.0, -55958612.0, 174414016.0, 10},
    {0.0, 0.0, 3.1415929794311523, 24857834.0, -55958668.0, 174412928.0, 10},
    {0.0, 0.0, 2.8735930919647217, 24658190.0, -55807144.0, 174413168.0, 10},
    {0.0, 0.0, 3.1415929794311523, 24365666.0, -55777100.0, 174413120.0, 10},
    {6.2831811904907227, 6.0213789939880371, 3.1415920257568359, 24111474.0, -55776588.0, 174492288.0, 10},
    {0.0, 0.0, 3.1415929794311523, 23580308.0, -55810864.0, 174507184.0, 20},
    {0.0, 0.0, 3.7815918922424316, 23094192.0, -56028160.0, 174504480.0, 20},
    {0.0, 0.0, 4.2815909385681152, 22755560.0, -56439088.0, 174504624.0, 10},
    {0.0, 0.0, 4.2815909385681152, 22533198.0, -56922920.0, 174504640.0, 10},
    {0.0, 0.0, 4.2815909385681152, 22310838.0, -57406752.0, 174504640.0, 10},
};

// `useWaypointSegments = false` is the editor's current substitution: one
// container-wide "Number of Matrices / Segment" (arg 7 = 20) for every span,
// because LevelObject does not carry a waypoint's own NumAreas.
RailPath RetailRail(bool useWaypointSegments) {
    const int count = static_cast<int>(sizeof(kLevel1Rail) / sizeof(kLevel1Rail[0]));
    std::vector<SplineSpan> spans;
    for (int i = 0; i + 1 < count; ++i) {
        SplineSpan span;
        span.from = glm::dvec3(kLevel1Rail[i].x, kLevel1Rail[i].y, kLevel1Rail[i].z);
        span.to = glm::dvec3(kLevel1Rail[i + 1].x, kLevel1Rail[i + 1].y, kLevel1Rail[i + 1].z);
        span.rotFrom = EngineEulerMatrix(
            glm::dvec3(kLevel1Rail[i].ax, kLevel1Rail[i].ay, kLevel1Rail[i].az));
        span.rotTo = EngineEulerMatrix(
            glm::dvec3(kLevel1Rail[i + 1].ax, kLevel1Rail[i + 1].ay, kLevel1Rail[i + 1].az));
        span.segments = useWaypointSegments ? kLevel1Rail[i].segments : 20;
        span.rigid = false;  // container 1500 declares Linear Segments = FALSE
        spans.push_back(span);
    }
    return BuildRailPath(spans);
}

}  // namespace

// The shipped rail is 3,928,793.21 game units long (958.2 m) and its length is
// the engine's `SplineObj.length`, the sum of the per-joint segment lengths
// (src/tagitem/spline.c:824-830), not the 3,903,025.28-unit chord sum.
//
// Retail train 1499 authors `start_dist = -4045627.5`, which is MORE than one
// rail length back from the end: the engine's `do { d += obj->length; } while
// (!(d >= 0.0f))` (src/tagitem/spline.c:243-246) has to run TWICE. The old
// app_lookup.cpp arithmetic `clamp(totalLen + rawPos, 0, totalLen)` added the
// length once, leaving -116834.29, and then clamped the train to arc 0 — the
// start of the rail, about 390 m and 1.6 million units away from where the game
// puts it.
TEST(TrainRailPlacement, RetailLevel1Train1499NeedsMoreThanOneWrap) {
    const RailPath rail = RetailRail(true);
    ASSERT_NEAR(rail.length, 3928793.209947, 0.01);

    const double startDist = -4045627.5;
    const double wrapped = WrapRailDistance(startDist, rail.length);
    EXPECT_NEAR(wrapped, 3811958.919895, 0.01);

    // The old arithmetic, for contrast: one addition, then a clamp to the rail
    // start, which is where the train used to be drawn.
    const double oldSingleAdd =
        std::max(0.0, std::min(rail.length, rail.length + startDist));
    EXPECT_DOUBLE_EQ(oldSingleAdd, 0.0);
    EXPECT_GT(wrapped - oldSingleAdd, 3800000.0);

    // And the placement follows the wrapped arc, not the clamp.
    const TrainPlacement placed =
        PlaceTrainOnRail(rail, startDist, kCarLength, 0.0, 0.0, 0.0, true);
    ASSERT_TRUE(placed.found);
    glm::dvec3 railStart;
    ASSERT_TRUE(SampleRailPoint(rail, 0.0, railStart));
    const double error = glm::length(placed.position - railStart);
    EXPECT_GT(error, 1600000.0);
    EXPECT_NEAR(placed.position.x, 22359627.2, 1.0);
    EXPECT_NEAR(placed.position.z, 174504640.0, 1.0);
}

// The other three retail trains are inside one rail length, so the old and new
// arithmetic agree on them — which is exactly why the bug hid.
TEST(TrainRailPlacement, TheOtherRetailTrainsAgreeWithASingleAdd) {
    const RailPath rail = RetailRail(true);
    const double starts[] = {-575416.9375, -971400.125, -3744122.25};

    for (double startDist : starts) {
        const double wrapped = WrapRailDistance(startDist, rail.length);
        const double oldSingleAdd =
            std::max(0.0, std::min(rail.length, rail.length + startDist));
        EXPECT_NEAR(wrapped, oldSingleAdd, 1e-6);
        EXPECT_GT(wrapped, 0.0);
    }
}

// The editor substitutes the container's single segment count for every span,
// because a waypoint's own NumAreas (arg 11) is not loaded into LevelObject. On
// this rail the two lengths differ by 320 units out of 3.93 million (0.008%), so
// the substitution is a safe approximation for placement — but it is an
// approximation, and it is the one remaining divergence from the engine's rail.
TEST(TrainRailPlacement, UniformSegmentCountIsANegligibleApproximation) {
    const RailPath engine = RetailRail(true);
    const RailPath editor = RetailRail(false);

    EXPECT_NEAR(engine.length, 3928793.209947, 0.01);
    EXPECT_NEAR(editor.length, 3929113.429173, 0.01);
    EXPECT_LT(std::fabs(editor.length - engine.length), 400.0);
    EXPECT_LT(std::fabs(editor.length - engine.length) / engine.length, 1e-4);
}

// The engine wraps the signed scalar with a LOOP, not a single addition:
//   if (!(d >= 0.0f)) do d += obj->length; while (!(d >= 0.0f));
//   if (d >= obj->length) do d -= obj->length; while (d >= obj->length);
// (SplineObj_GetPoints, src/tagitem/spline.c:242-255). The old app_lookup.cpp
// arithmetic `clamp(totalLen + rawPos, 0, totalLen)` adds the length once, so for
// `rawPos = -(length + x)` it produced 0 (the rail start) instead of `length - x`.
TEST(TrainRailPlacement, SignedScalarWrapsMoreThanOnceLikeTheEngine) {
    const RailPath rail = StraightRail(1000.0, 4);
    ASSERT_DOUBLE_EQ(rail.length, 1000.0);

    // Exactly one length out of range: the single add is enough, both agree.
    EXPECT_DOUBLE_EQ(WrapRailDistance(-1000.0, rail.length), 0.0);

    // More than one length out of range: the old single add clamped to 0.
    EXPECT_DOUBLE_EQ(WrapRailDistance(-1250.0, rail.length), 750.0);
    EXPECT_DOUBLE_EQ(WrapRailDistance(-2500.0, rail.length), 500.0);
    EXPECT_DOUBLE_EQ(WrapRailDistance(-2250.0, rail.length), 750.0);

    // Symmetric for a scalar past the far end.
    EXPECT_DOUBLE_EQ(WrapRailDistance(1250.0, rail.length), 250.0);
    EXPECT_DOUBLE_EQ(WrapRailDistance(2000.0, rail.length), 0.0);

    // The old arithmetic, for contrast: one add then clamp, never the loop.
    const double oldSingleAdd = std::max(0.0, std::min(rail.length, rail.length + (-2500.0)));
    EXPECT_DOUBLE_EQ(oldSingleAdd, 0.0);
    EXPECT_NE(WrapRailDistance(-2500.0, rail.length), oldSingleAdd);
}

// The placement itself must land at the wrapped arc, not at the rail start.
// Train_Place, src/missile/train.c:221-233.
TEST(TrainRailPlacement, PlacementUsesTheWrappedArcNotTheClampedOne) {
    const RailPath rail = StraightRail(1000.0, 4);

    const TrainPlacement placed =
        PlaceTrainOnRail(rail, -2500.0, kCarLength, 0.0, 0.0, 0.0, false);
    ASSERT_TRUE(placed.found);

    // Wrapped to arc 500, i.e. x = 500 on a straight +X rail.
    EXPECT_NEAR(placed.position.x, 500.0, 1e-6);
    EXPECT_NEAR(placed.position.y, 0.0, 1e-6);

    // The old code's result would have been arc 0.
    EXPECT_GT(std::fabs(placed.position.x - 0.0), 100.0);
}

// The heading comes from the step to the NEXT point, and that step is ONE car
// length (offsets[0] = reverse ? +length : -length, src/missile/train.c:221),
// never the cumulative distance a wagon sits at. Note the sign: an unflipped car
// steps BACKWARDS along the arc (pts[1] is where the car came from), which
// orient() then turns into a forward-facing frame. On a bending rail a one-length
// step and a three-length step give different headings, which is exactly what the
// old cumulative-offset call got wrong.
TEST(TrainRailPlacement, HeadingComesFromASingleCarLengthStep) {
    const RailPath rail = CurvedRail(6);
    const double carsBack = 3.0 * kCarLength;  // fourth car of the consist
    // The lead car's arc, chosen so the car's own arc and both step targets (one
    // and three car lengths further back) stay strictly inside the rail.
    const double base = 7.0 * kCarLength;
    const bool reversed = false;
    const double signedCarsBack = reversed ? carsBack : -carsBack;
    const double arc = base + signedCarsBack;

    ASSERT_GT(arc - carsBack, 0.0);
    ASSERT_LT(arc, rail.length);

    const TrainPlacement car =
        PlaceTrainOnRail(rail, arc, kCarLength, 0.0, 0.0, 0.0, reversed);
    ASSERT_TRUE(car.found);

    // The engine's step: one car length in the direction of the offset.
    glm::dvec3 p0;
    glm::dvec3 p1;
    ASSERT_TRUE(SampleRailPoint(rail, arc, p0));
    ASSERT_TRUE(SampleRailPoint(rail, arc - kCarLength, p1));
    const glm::dmat3 expected = TrackDirectionMatrix(p1 - p0);

    for (int c = 0; c < 9; ++c) {
        EXPECT_NEAR(car.matrix[c / 3][c % 3], expected[c / 3][c % 3], 1e-12);
    }

    // And the heading a cumulative three-length step would have produced differs.
    glm::dvec3 p3;
    ASSERT_TRUE(SampleRailPoint(rail, arc - carsBack, p3));
    const glm::dmat3 wrongStep = TrackDirectionMatrix(p3 - p0);
    double maxDiff = 0.0;
    for (int c = 0; c < 9; ++c) {
        maxDiff = std::max(maxDiff, std::fabs(car.matrix[c / 3][c % 3] - wrongStep[c / 3][c % 3]));
    }
    EXPECT_GT(maxDiff, 1e-4);
}

// Flip Direction picks the sign of the spacing independently of the sign of the
// authored scalar (src/missile/train.c:221). app_lookup.cpp now reads the flag
// from arg 9 instead of inferring it from `start_dist < 0`.
//
// For an unflipped car the step is backwards, and orient() (src/missile/train.c:
// 172-190) turns that into a frame whose local X points FORWARD along the arc: on
// an axis-aligned +X rail the frame is the identity. Flipping the car reverses it
// to diag(-1, -1, 1).
TEST(TrainRailPlacement, FlipDirectionSetsTheSignOfTheSpacing) {
    const RailPath rail = StraightRail(1000.0, 4);
    const double start = 500.0;

    const TrainPlacement forward =
        PlaceTrainOnRail(rail, start, kCarLength, 0.0, 0.0, 0.0, false);
    const TrainPlacement flipped =
        PlaceTrainOnRail(rail, start, kCarLength, 0.0, 0.0, 0.0, true);
    ASSERT_TRUE(forward.found);
    ASSERT_TRUE(flipped.found);

    // Both cars sit at the same arc; only the heading flips.
    EXPECT_NEAR(forward.position.x, flipped.position.x, 1e-9);
    EXPECT_NEAR(forward.position.y, flipped.position.y, 1e-9);

    EXPECT_NEAR(forward.matrix[0][0], 1.0, 1e-12);
    EXPECT_NEAR(flipped.matrix[0][0], -1.0, 1e-12);
    // local Y flips with it.
    EXPECT_NEAR(forward.matrix[1][1], 1.0, 1e-12);
    EXPECT_NEAR(flipped.matrix[1][1], -1.0, 1e-12);
}

// The clamp at the rail end applies to the NEXT point (the heading source), not
// to the car's own arc: SplineObj_GetPoints walks target = dist + sum(offsets)
// and `goto off_end` copies the end joint for the point it could not reach
// (src/tagitem/spline.c:441-443). A car therefore stays on its own arc and simply
// faces the clamped end — it never wraps back to the start of the rail.
TEST(TrainRailPlacement, CarsPastTheRailEndClampTheHeadingNotTheArc) {
    const RailPath rail = StraightRail(1000.0, 4);

    const TrainPlacement placed =
        PlaceTrainOnRail(rail, 990.0, kCarLength, 0.0, 0.0, 0.0, true);
    ASSERT_TRUE(placed.found);

    // The car is where its own scalar put it.
    EXPECT_NEAR(placed.position.x, 990.0, 1e-6);

    // Its heading came from the clamped end of the rail, i.e. a step of +10.
    const glm::dmat3 expected = TrackDirectionMatrix(glm::dvec3(1000.0 - 990.0, 0.0, 0.0));
    for (int c = 0; c < 9; ++c) {
        EXPECT_NEAR(placed.matrix[c / 3][c % 3], expected[c / 3][c % 3], 1e-12);
    }
}

// Consecutive cars of a consist are exactly one car length apart along the arc,
// which is what the `(wagonIdx + 1) * length` chaining in app_lookup.cpp has to
// reproduce (Train_PlaceWagons, src/missile/train.c:277-337).
TEST(TrainRailPlacement, ConsecutiveCarsAreOneCarLengthApart) {
    const RailPath rail = StraightRail(1000000.0, 8);
    const double lead = 400000.0;

    double previousX = -1.0;
    for (int wagonIdx = 0; wagonIdx < 4; ++wagonIdx) {
        const double carsBack = (wagonIdx + 1) * kCarLength;
        const TrainPlacement placed =
            PlaceTrainOnRail(rail, lead - carsBack, kCarLength, 0.0, 0.0, 0.0, false);
        ASSERT_TRUE(placed.found);
        if (previousX >= 0.0) {
            EXPECT_NEAR(previousX - placed.position.x, kCarLength, 1e-6);
        }
        previousX = placed.position.x;
    }
    EXPECT_NEAR(previousX, lead - 4.0 * kCarLength, 1e-6);
}

// Displacement X/Y (args 10/11) are clamped to gTrainOffsetRange = +/-5 m and
// scaled by kFixed = 4096 (src/missile/train.c:240-248, :380-381).
//
// The engine reads the frame's elements in a fixed order —
//   ox = sx*m[1] + sy*m[2]; oy = sy*m[5] + sx*m[4]; oz = sx*m[7] + sy*m[8] —
// so on an axis-aligned rail (identity frame) it is Displacement X that moves the
// car in world Y and Displacement Y that does nothing. That quirk is the engine's,
// and this test pins it rather than "fixing" it.
TEST(TrainRailPlacement, DisplacementFollowsTheEnginesAxisOrder) {
    const RailPath rail = StraightRail(1000.0, 4);

    const TrainPlacement oneMetreX =
        PlaceTrainOnRail(rail, 500.0, kCarLength, 0.0, 1.0, 0.0, false);
    ASSERT_TRUE(oneMetreX.found);
    EXPECT_NEAR(oneMetreX.position.y, 4096.0, 1e-6);
    EXPECT_NEAR(oneMetreX.position.x, 500.0, 1e-6);

    const TrainPlacement oneMetreY =
        PlaceTrainOnRail(rail, 500.0, kCarLength, 0.0, 0.0, 1.0, false);
    ASSERT_TRUE(oneMetreY.found);
    EXPECT_NEAR(oneMetreY.position.y, 0.0, 1e-6);

    const TrainPlacement clamped =
        PlaceTrainOnRail(rail, 500.0, kCarLength, 0.0, 1000.0, 0.0, false);
    ASSERT_TRUE(clamped.found);
    EXPECT_NEAR(clamped.position.y, 5.0 * 4096.0, 1e-6);

    // The displacement is at most 5 m long in world units.
    const double dx = clamped.position.x - 500.0;
    EXPECT_NEAR(std::sqrt(dx * dx + clamped.position.y * clamped.position.y +
                          clamped.position.z * clamped.position.z),
                20480.0, 1e-3);
}

// The wrapped scalar can land exactly on 0, which takes the engine's at_start
// path: the first waypoint is repeated and the frame is left as the identity
// (SplineObj_GetPoints, src/tagitem/spline.c:447-453).
TEST(TrainRailPlacement, AWrappedScalarOfZeroTakesTheEngineStartPath) {
    const RailPath rail = StraightRail(1000.0, 4);

    const TrainPlacement placed =
        PlaceTrainOnRail(rail, -2000.0, kCarLength, 0.0, 0.0, 0.0, false);
    ASSERT_TRUE(placed.found);

    EXPECT_NEAR(placed.position.x, rail.joints.front().pos.x, 1e-9);
    EXPECT_NEAR(placed.position.y, rail.joints.front().pos.y, 1e-9);
    for (int c = 0; c < 9; ++c) {
        EXPECT_NEAR(placed.matrix[c / 3][c % 3], (c / 3 == c % 3) ? 1.0 : 0.0, 1e-12);
    }
}

// A degenerate rail (no positive length, or a single joint) reports not-found so
// the caller leaves the authored position alone rather than snapping it to the
// origin.
TEST(TrainRailPlacement, DegenerateRailsAreNotPlaced) {
    const RailPath empty = BuildRailPath({});
    EXPECT_FALSE(PlaceTrainOnRail(empty, 0.0, kCarLength, 0.0, 0.0, 0.0, false).found);

    const TrainPlacement nonFinite =
        PlaceTrainOnRail(StraightRail(1000.0, 4), std::nan(""), kCarLength, 0.0, 0.0, 0.0, false);
    EXPECT_FALSE(nonFinite.found);
}
