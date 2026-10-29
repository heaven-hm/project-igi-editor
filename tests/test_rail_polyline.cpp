// test_rail_polyline.cpp — the rail polyline the viewport overlay draws (GitHub
// issue #59, acceptance criterion 2: "rail path polyline visible in viewport for
// any level with a Train task").
//
// The GL draw itself is not host-testable (the editor needs a GL 4.1 context and
// this suite runs without one). What IS testable is everything the drawn line is
// made of, and that is what this file pins:
//
//   * `spline_geometry::RailPolyline` — the polyline is the engine's own
//     `setup_segments` joints (IGI-RE src/sound/segsetup.c:88-199) in order, so
//     joint i is exactly the point `SampleRailPoint` returns for `arc[i]` and the
//     drawn segment chain has the rail's own length. A "rail" drawn from anything
//     else (chords between waypoints, control points, a decimated curve) fails
//     here.
//   * `igi::BuildRailSource` — the container -> curve resolution. It is the same
//     function `App::EvaluateTrainTrackPositions` places cars with
//     (source/app_lookup.cpp), so pinning it pins that the preview cannot show a
//     different rail from the one the trains ride.
//   * `igi::RailContainersToShow` / `igi::SelectedRailContainer` — which rails
//     the overlay offers, derived from the level's own references rather than
//     from a model id, a task-type list or a level number.
//
// Oracle: /Users/haseeb-mir/Documents/Code/IGI-RE (read-only), the verified
// decompilation of retail IGI.exe. Every expectation names its function and
// file:line.

#include <gtest/gtest.h>

#include "level/level_objects.h"
#include "renderer/spline_geometry.h"
#include "runtime/rail_source.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace {

using spline_geometry::BuildRailPath;
using spline_geometry::EngineEulerMatrix;
using spline_geometry::RailPath;
using spline_geometry::RailPolyline;
using spline_geometry::SampleRailPoint;
using spline_geometry::SplineSpan;

// ─── Fixtures ───────────────────────────────────────────────────────────────

LevelObject Waypoint(int id, double x, double y, double z,
                     double ax = 0.0, double ay = 0.0, double az = 0.0) {
    LevelObject w;
    w.type = "SplineObjWaypoint";
    w.isSplineWaypoint = true;
    w.taskId = std::to_string(id);
    w.pos = glm::dvec3(x, y, z);
    w.rot = glm::dvec3(ax, ay, az);
    return w;
}

LevelObject Container(int id, int segments, bool linear) {
    LevelObject c;
    c.type = "SplineObj";
    c.isSplineContainer = true;
    c.taskId = std::to_string(id);
    c.splineSegmentCount = segments;
    c.linearSegments = linear;
    return c;
}

// A rail in the XY plane whose waypoint frames turn by 15 degrees per span, so
// the Hermite curve genuinely bends away from the chord polyline — a test that
// passes on a straight rail proves nothing about the curve.
RailPath CurvedRail(int spans, int segments) {
    std::vector<SplineSpan> out;
    for (int i = 0; i < spans; ++i) {
        SplineSpan span;
        span.from = glm::dvec3(100000.0 * i, 20000.0 * i * i, 0.0);
        span.to = glm::dvec3(100000.0 * (i + 1), 20000.0 * (i + 1) * (i + 1), 0.0);
        span.rotFrom = EngineEulerMatrix(glm::dvec3(0.0, 0.0, i * 3.14159265358979323846 / 12.0));
        span.rotTo = EngineEulerMatrix(glm::dvec3(0.0, 0.0, (i + 1) * 3.14159265358979323846 / 12.0));
        span.segments = segments;
        span.rigid = false;
        out.push_back(span);
    }
    return BuildRailPath(out);
}

// The shipped LEVEL1 rail, task 1500, exactly as test_train_rail.cpp carries it:
// twelve `SplineObjWaypoint` children (objects.qsc:486-497), each row
// {Orientation args 3-5, Position args 6-8, NumAreas arg 11}. Eleven Hermite
// spans. The placement-side expectations for this fixture (4461272.686198 with
// the waypoints' own NumAreas, 4461592.905424 with the editor's container-wide
// substitution) are pinned in that file; here the same data pins the resolver the
// OVERLAY uses, so the two cannot drift apart.
struct RetailWaypoint {
    double ax, ay, az;
    double x, y, z;
    int segments;
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
    {0.0, 0.0, 4.2815909385681152, 22088476.0, -57890580.0, 174504640.0, 10},
};

// The level as the editor loads it: one `SplineObj` container (task 1500, arg 7 =
// 20, "Linear Segments" = FALSE) whose `childrenIndices` are its twelve waypoint
// children. A waypoint's own NumAreas is not modelled by LevelObject, so the
// container's count stands for every span — the substitution the placement pass
// makes too (see PLACEMENT_SPLINES.md §13).
std::vector<LevelObject> RetailLevel1Objects() {
    std::vector<LevelObject> objects;
    LevelObject container = Container(1500, 20, false);
    const int count = static_cast<int>(sizeof(kLevel1Rail) / sizeof(kLevel1Rail[0]));
    for (int i = 0; i < count; ++i) {
        container.childrenIndices.push_back(static_cast<int>(objects.size()) + 1 + i);
    }
    objects.push_back(container);
    for (int i = 0; i < count; ++i) {
        objects.push_back(Waypoint(1501 + i, kLevel1Rail[i].x, kLevel1Rail[i].y,
                                   kLevel1Rail[i].z, kLevel1Rail[i].ax,
                                   kLevel1Rail[i].ay, kLevel1Rail[i].az));
    }
    return objects;
}

void ExpectVecNear(const glm::dvec3& a, const glm::dvec3& b, double tol) {
    EXPECT_NEAR(a.x, b.x, tol);
    EXPECT_NEAR(a.y, b.y, tol);
    EXPECT_NEAR(a.z, b.z, tol);
}

}  // namespace

// ─── The polyline itself ────────────────────────────────────────────────────

// `RailPolyline` is the joint list, in order, unmodified: `BuildRailPath` chains
// one joint per `setup_segments` sample (src/sound/segsetup.c:107-199) and the
// overlay draws consecutive pairs. Anything that reorders, sorts, decimates or
// re-interpolates the joints would draw a different line from the one
// `SampleRailPoint` walks.
TEST(RailPolyline, IsTheJointListInOrder) {
    const RailPath rail = CurvedRail(3, 5);
    const std::vector<glm::dvec3> polyline = RailPolyline(rail);

    ASSERT_EQ(polyline.size(), rail.joints.size());
    ASSERT_EQ(polyline.size(), rail.arc.size());
    for (size_t i = 0; i < polyline.size(); ++i) {
        ExpectVecNear(polyline[i], rail.joints[i].pos, 0.0);
    }
    // A curve that bends: the middle joints are NOT on the chord between the
    // first and last joint, so this is not a chord polyline.
    const glm::dvec3 chordMid = (polyline.front() + polyline.back()) * 0.5;
    const glm::dvec3 jointMid = polyline[polyline.size() / 2];
    EXPECT_GT(glm::length(jointMid - chordMid), 1000.0);
}

// The contract that makes the drawn line the rail the trains ride: the point the
// placement sampler returns for `arc[i]` IS `polyline[i]`
// (`SampleRailPoint` -> `SampleArcNoWrap`, source/renderer/spline_geometry.cpp).
// The last joint is the one exception the engine itself defines: its arc IS the
// rail length, and `WrapRailDistance` keeps a scalar in [0, length), so a
// distance of exactly `length` wraps to the rail start (SplineObj_GetPoints,
// src/tagitem/spline.c:242-255). Pinned explicitly below rather than skipped.
TEST(RailPolyline, JointIsTheSampledPointAtItsOwnArc) {
    const RailPath rail = CurvedRail(4, 6);
    const std::vector<glm::dvec3> polyline = RailPolyline(rail);
    ASSERT_GE(polyline.size(), 3u);

    for (size_t i = 0; i + 1 < polyline.size(); ++i) {
        glm::dvec3 sampled(0.0);
        ASSERT_TRUE(SampleRailPoint(rail, rail.arc[i], sampled)) << "arc " << i;
        ExpectVecNear(sampled, polyline[i], 1e-6 * (1.0 + glm::length(polyline[i])));
    }

    // The end of the rail is the start of the wrap: the drawn chain closes onto
    // the first joint at exactly `length`, which is what the cars see too.
    ASSERT_DOUBLE_EQ(rail.arc.back(), rail.length);
    glm::dvec3 wrapped(0.0);
    ASSERT_TRUE(SampleRailPoint(rail, rail.length, wrapped));
    ExpectVecNear(wrapped, polyline.front(), 1e-6 * (1.0 + glm::length(polyline.front())));
    // Just inside the end the last joint IS the sample.
    glm::dvec3 nearEnd(0.0);
    ASSERT_TRUE(SampleRailPoint(rail, rail.length * (1.0 - 1e-12), nearEnd));
    ExpectVecNear(nearEnd, polyline.back(), 1e-3 * (1.0 + glm::length(polyline.back())));
}

// The drawn chain's own length is the engine's `SplineObj.length`, the sum of the
// per-joint segment lengths (`setup_segments`, src/sound/segsetup.c:141,174,195
// -> `o->length = len + o->length`) — not the waypoint chord sum, which is 25,768
// units shorter on the retail rail below.
TEST(RailPolyline, SegmentChainHasTheEngineRailLength) {
    const RailPath rail = CurvedRail(3, 5);
    const std::vector<glm::dvec3> polyline = RailPolyline(rail);
    ASSERT_GE(polyline.size(), 2u);

    double walked = 0.0;
    for (size_t i = 0; i + 1 < polyline.size(); ++i) {
        walked += glm::length(polyline[i + 1] - polyline[i]);
    }
    EXPECT_NEAR(walked, rail.length, 1e-9 * (1.0 + rail.length));

    // And on the shipped rail, the same number the placement tests pin.
    const std::vector<LevelObject> objects = RetailLevel1Objects();
    const igi::RailSource source = igi::BuildRailSource(objects, 0);
    ASSERT_TRUE(source.valid);
    const std::vector<glm::dvec3> retail = RailPolyline(source.path);
    ASSERT_EQ(retail.size(), 11u * 20u + 1u);   // 11 spans x 20 sub-segments + 1
    double retailWalked = 0.0;
    for (size_t i = 0; i + 1 < retail.size(); ++i) {
        retailWalked += glm::length(retail[i + 1] - retail[i]);
    }
    EXPECT_NEAR(retailWalked, source.path.length, 0.01);
    EXPECT_NEAR(source.path.length, 4461592.905424, 0.01);
    // The waypoint chord sum the old preview used is NOT the rail: on this rail
    // the curve is 25,768 units longer (PLACEMENT_SPLINES.md §3.2).
    const double chord = glm::length(retail.back() - retail.front());
    EXPECT_GT(retailWalked - chord, 20000.0);
}

// A rail with fewer than two joints has no segment to draw; the overlay skips it
// rather than emitting a degenerate GL_LINES pair.
TEST(RailPolyline, FewerThanTwoJointsHasNoSegment) {
    RailPath empty;
    EXPECT_TRUE(RailPolyline(empty).empty());

    RailPath single;
    single.joints.push_back(spline_geometry::SplineJoint{});
    single.arc.assign(1, 0.0);
    const std::vector<glm::dvec3> polyline = RailPolyline(single);
    ASSERT_EQ(polyline.size(), 1u);
    EXPECT_LT(polyline.size(), 2u);
}

// ─── The container -> curve resolver ────────────────────────────────────────

// The engine has one segment per waypoint pair (SplineObj_GetPoints, HOOK
// 0x4FEC10, src/tagitem/spline.c:219-454): consecutive spans share the waypoint
// they meet at, so a chain of N spans with S sub-segments each has N*S + 1 joints
// and the shared waypoint appears ONCE. `BuildRailPath` drops the duplicate.
TEST(RailSource, ChainsSpansWithoutDuplicatingTheSharedWaypoint) {
    std::vector<LevelObject> objects;
    LevelObject container = Container(700, 4, false);
    container.childrenIndices = {1, 2, 3};
    objects.push_back(container);
    objects.push_back(Waypoint(701, 0.0, 0.0, 0.0));
    objects.push_back(Waypoint(702, 100000.0, 0.0, 0.0, 0.0, 0.0, 0.3));
    objects.push_back(Waypoint(703, 200000.0, 50000.0, 0.0, 0.0, 0.0, 0.6));

    const igi::RailSource source = igi::BuildRailSource(objects, 0);
    ASSERT_TRUE(source.valid);
    EXPECT_EQ(source.container_index, 0);
    EXPECT_EQ(source.path.joints.size(), 2u * 4u + 1u);
    // Joint 0 is the first waypoint's point and the last joint is the last
    // waypoint's point: `setup_segments` evaluates t = 0 and t = 1 exactly
    // (src/sound/segsetup.c:107-110).
    ExpectVecNear(source.path.joints.front().pos, glm::dvec3(0.0, 0.0, 0.0), 0.0);
    ExpectVecNear(source.path.joints.back().pos, glm::dvec3(200000.0, 50000.0, 0.0), 1e-6);
    // SplineObj_GetWaypointPos (HOOK 0x502200, src/editobj/spline.c:155-163)
    // returns `w->point`, the AUTHORED waypoint — which is what the Elevator
    // "Linear" branch interpolates.
    ExpectVecNear(source.first_waypoint, glm::dvec3(0.0, 0.0, 0.0), 0.0);
    ExpectVecNear(source.last_waypoint, glm::dvec3(200000.0, 50000.0, 0.0), 0.0);
}

// The resolver must be the placement's construction, not a second opinion: build
// the spans by hand exactly as `App::EvaluateTrainTrackPositions` documents them
// (container arg 7 clamped, container arg 3 as `rigid`, waypoint frames as the
// Hermite tangents) and require the same curve.
TEST(RailSource, MatchesThePlacementConstruction) {
    const std::vector<LevelObject> objects = RetailLevel1Objects();
    const igi::RailSource source = igi::BuildRailSource(objects, 0);
    ASSERT_TRUE(source.valid);

    std::vector<SplineSpan> spans;
    const LevelObject& container = objects[0];
    for (size_t c = 0; c + 1 < container.childrenIndices.size(); ++c) {
        const LevelObject& from = objects[container.childrenIndices[c]];
        const LevelObject& to = objects[container.childrenIndices[c + 1]];
        SplineSpan span;
        span.from = from.pos;
        span.to = to.pos;
        span.rotFrom = EngineEulerMatrix(from.rot);
        span.rotTo = EngineEulerMatrix(to.rot);
        span.segments = std::clamp(container.splineSegmentCount, 2, 20);
        span.rigid = container.linearSegments;
        span.reversed = false;
        spans.push_back(span);
    }
    const RailPath expected = BuildRailPath(spans);

    ASSERT_EQ(source.path.joints.size(), expected.joints.size());
    for (size_t i = 0; i < expected.joints.size(); ++i) {
        ExpectVecNear(source.path.joints[i].pos, expected.joints[i].pos, 0.0);
    }
    EXPECT_DOUBLE_EQ(source.path.length, expected.length);
}

// `SplineObjWaypoint_Changed` (HOOK 0x500EF0, src/tagitem/spline.c:771-772) caps
// the sub-segment count at 20, and `SplineObjSegment_Setup` forces a rigid span to
// ONE sub-segment (src/sound/segsetup.c:268-270). The floor of 2 is the editor's
// guard against `inv = kOne / o->segments` (src/sound/segsetup.c:107) at 0.
TEST(RailSource, ClampsTheSegmentCountAndCollapsesRigidSpans) {
    std::vector<LevelObject> objects;
    objects.push_back(Container(700, 99, false));
    objects.push_back(Waypoint(701, 0.0, 0.0, 0.0));
    objects.push_back(Waypoint(702, 100000.0, 0.0, 0.0));
    objects[0].childrenIndices = {1, 2};
    EXPECT_EQ(igi::BuildRailSource(objects, 0).path.joints.size(), 21u);   // 20 + 1

    objects[0].splineSegmentCount = 0;
    EXPECT_EQ(igi::BuildRailSource(objects, 0).path.joints.size(), 3u);    // 2 + 1

    objects[0].splineSegmentCount = 20;
    objects[0].linearSegments = true;   // "Linear Segments" = TRUE
    EXPECT_EQ(igi::BuildRailSource(objects, 0).path.joints.size(), 2u);    // rigid -> 1
}

// A container with no segment, an out-of-range index and a stale child index all
// yield no rail instead of a crash: the overlay must be safe on any level, and a
// stale index is undefined behaviour in the placement pass that this resolver
// refuses.
TEST(RailSource, RefusesWhatHasNoRail) {
    std::vector<LevelObject> objects;
    objects.push_back(Container(700, 20, false));
    objects.push_back(Waypoint(701, 0.0, 0.0, 0.0));
    objects[0].childrenIndices = {1};                 // one child: no span
    EXPECT_FALSE(igi::BuildRailSource(objects, 0).valid);

    EXPECT_FALSE(igi::BuildRailSource(objects, -1).valid);
    EXPECT_FALSE(igi::BuildRailSource(objects, 2).valid);

    objects[0].childrenIndices = {1, 99};             // 99 does not exist
    EXPECT_FALSE(igi::BuildRailSource(objects, 0).valid);

    objects.push_back(Waypoint(702, 50000.0, 0.0, 0.0));
    objects[0].childrenIndices = {1, 2};
    EXPECT_TRUE(igi::BuildRailSource(objects, 0).valid);
}

// `task id -> index` is last-declaration-wins, the same map the placement pass
// builds, so both resolve a rail reference identically when a level carries a
// duplicate id.
TEST(RailSource, TaskIndexKeepsTheLastDeclaration) {
    std::vector<LevelObject> objects;
    objects.push_back(Container(700, 20, false));
    objects.push_back(Container(700, 20, false));
    const std::unordered_map<std::string, int> index = igi::BuildTaskIndex(objects);
    ASSERT_EQ(index.count("700"), 1u);
    EXPECT_EQ(index.at("700"), 1);
    // The overlay's allocation-free scan resolves the same way.
    EXPECT_EQ(igi::FindLastTaskIndexById(objects, "700"), index.at("700"));
    EXPECT_EQ(igi::FindLastTaskIndexById(objects, "701"), -1);
    EXPECT_EQ(igi::FindLastTaskIndexById(objects, ""), -1);
}

// ─── Which rails the overlay offers ─────────────────────────────────────────

// The set is derived from the level's own references: every `SplineObj` a rail
// consumer names, plus the selected object's own rail. No model id, task type or
// level number is consulted — a `Train`'s "RailroadQTaskID" (arg 5) and an
// `Elevator`'s "SplineObj taskid" (arg 10) are the only two fields the loader
// writes into `LevelObject::splineTaskId`
// (source/level/level_objects.cpp:363,556).
TEST(RailOverlaySelection, FollowsTheLevelsOwnRailReferences) {
    std::vector<LevelObject> objects;
    LevelObject railA = Container(100, 20, false);
    LevelObject railB = Container(200, 20, false);
    LevelObject railC = Container(300, 20, false);   // nothing rides this one
    const int idxA = static_cast<int>(objects.size()); objects.push_back(railA);
    const int idxB = static_cast<int>(objects.size()); objects.push_back(railB);
    const int idxC = static_cast<int>(objects.size()); objects.push_back(railC);

    LevelObject train;
    train.type = "Train";
    train.taskId = "400";
    train.splineTaskId = "100";
    const int idxTrain = static_cast<int>(objects.size()); objects.push_back(train);

    LevelObject elevator;
    elevator.type = "Elevator";
    elevator.taskId = "500";
    elevator.splineTaskId = "200";
    objects.push_back(elevator);

    LevelObject prop;   // a task that names no rail
    prop.type = "Building";
    prop.taskId = "600";
    const int idxProp = static_cast<int>(objects.size()); objects.push_back(prop);

    // Two rails are ridden; the third is not shown until it is selected.
    std::vector<int> shown = igi::RailContainersToShow(objects, -1);
    EXPECT_EQ(shown, (std::vector<int>{idxA, idxB}));

    // Selecting a rail shows it even with nothing placed on it.
    shown = igi::RailContainersToShow(objects, idxC);
    EXPECT_EQ(shown, (std::vector<int>{idxA, idxB, idxC}));

    // Selecting a rider shows the rail it rides (already in the set: no repeat).
    shown = igi::RailContainersToShow(objects, idxTrain);
    EXPECT_EQ(shown, (std::vector<int>{idxA, idxB}));
    EXPECT_EQ(igi::SelectedRailContainer(objects, idxTrain), idxA);

    // A deleted rider contributes nothing, and neither does a reference that
    // resolves to nothing.
    objects[idxTrain].deleted = true;
    LevelObject ghost;
    ghost.type = "Train";
    ghost.taskId = "401";
    ghost.splineTaskId = "999";
    objects.push_back(ghost);
    shown = igi::RailContainersToShow(objects, -1);
    EXPECT_EQ(shown, (std::vector<int>{idxB}));

    // A deleted rail is not drawn even while a live rider names it.
    objects[idxB].deleted = true;
    shown = igi::RailContainersToShow(objects, idxB);
    EXPECT_TRUE(shown.empty());

    // Selecting a task that names no rail selects no rail.
    EXPECT_EQ(igi::SelectedRailContainer(objects, idxProp), -1);
    EXPECT_EQ(igi::SelectedRailContainer(objects, -1), -1);
    EXPECT_EQ(igi::SelectedRailContainer(objects, 999), -1);
}
