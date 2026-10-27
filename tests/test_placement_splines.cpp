// Regression tests for the SplineObj span/rail port. Every expected value below
// is derived from the reconstructed engine code in IGI-RE; the file:line cited in
// each test is the source of the constant or transform being asserted.

#include "../source/renderer/spline_geometry.h"

#include <gtest/gtest.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

using spline_geometry::BendSegmentMesh;
using spline_geometry::BuildRailPath;
using spline_geometry::BuildSplineSpan;
using spline_geometry::EngineEulerMatrix;
using spline_geometry::MakeWaypointTangent;
using spline_geometry::PlaceTrainOnRail;
using spline_geometry::RailPath;
using spline_geometry::SampleRailPoint;
using spline_geometry::SplineSpan;
using spline_geometry::SplineSpanJoints;
using spline_geometry::TrackDirectionMatrix;
using spline_geometry::WrapRailDistance;

RenderVertex MakeVertex(double x, double y, double z) {
    RenderVertex vertex;
    vertex.rawPos = glm::vec3(static_cast<float>(x), static_cast<float>(y),
                              static_cast<float>(z));
    vertex.normal = glm::vec3(0.0f, 0.0f, 1.0f);
    return vertex;
}

// A flat "road" model: a square cross-section at x = minX and x = maxX, with one
// triangle per end so the bent mesh has geometry to emit.
ParsedGeometry MakeBox(double minX, double maxX, double halfY, double halfZ) {
    ParsedGeometry geometry;
    for (double x : {minX, maxX}) {
        for (double y : {-halfY, halfY}) {
            for (double z : {-halfZ, halfZ}) {
                geometry.vertices.push_back(MakeVertex(x, y, z));
            }
        }
    }
    geometry.triangles.push_back({0, 1, 2});
    geometry.triangles.push_back({4, 5, 6});
    return geometry;
}

// A model whose local X range is minX..maxX but which carries one extra vertex at
// probeX, so a single bend can be probed at a chosen joint parameter.
ParsedGeometry MakeProbeBox(double minX, double maxX, double probeX, double y, double z) {
    ParsedGeometry geometry;
    geometry.vertices.push_back(MakeVertex(minX, y, z));
    geometry.vertices.push_back(MakeVertex(maxX, y, z));
    geometry.vertices.push_back(MakeVertex(probeX, y, z));
    geometry.triangles.push_back({0, 1, 2});
    return geometry;
}

// The probe vertex is the third one emitted, i.e. interleaved[20..29]. The mesh
// stores worldOffset / 40.96 (AppendBentVertex, the editor's render scale), so the
// value is scaled back here to compare against the engine's raw offsets.
glm::dvec3 ProbeOffset(const spline_geometry::BentSegmentMesh& bent) {
    return glm::dvec3(bent.interleaved[20], bent.interleaved[21], bent.interleaved[22])
        * 40.96;
}

// The engine's RENDER path (BuildEngineSpanMesh) stores the span-relative
// position of vertex `i` at the same 1/40.96 render scale.
glm::dvec3 EngineSpanOffset(const spline_geometry::EngineSpanMesh& mesh, size_t vertex) {
    const float* v = mesh.interleaved.data() + vertex * 10;
    return glm::dvec3(v[0], v[1], v[2]) * 40.96;
}

spline_geometry::EngineSpanInput EngineSpan(double fromX, double toX) {
    spline_geometry::EngineSpanInput span;
    span.from = glm::dvec3(fromX, 0.0, 0.0);
    span.to = glm::dvec3(toX, 0.0, 0.0);
    return span;
}

SplineSpan StraightSpan(double length, int segments) {
    SplineSpan span;
    span.from = glm::dvec3(0.0);
    span.to = glm::dvec3(length, 0.0, 0.0);
    span.segments = segments;
    return span;
}

// The engine's curved-branch blend: the joint index is the truncated joint
// parameter, then the two joints' frames are lerped (src/shadow/spline.c:60-88).
glm::dvec3 ExpectedBlend(const SplineSpanJoints& built,
                         double minX,
                         double maxX,
                         double localX,
                         const glm::dvec3& local) {
    const int segments = static_cast<int>(built.joints.size()) - 1;
    const double scale = segments / (maxX - minX);
    const double f = (-minX + localX) * scale;
    int k = static_cast<int>(f);
    if (k == segments) k--;
    const double frac = std::clamp(f - k, 0.0, 1.0);
    const glm::dvec3 a = built.joints[k].pos +
        glm::dvec3(built.joints[k].rot * local);
    const glm::dvec3 b = built.joints[k + 1].pos +
        glm::dvec3(built.joints[k + 1].rot * local);
    return a + (b - a) * frac;
}

} // namespace

// Matrix_SetEuler (HOOK 0x4B38E0, src/math/matrix.c:99-110) composes Rz * Ry * Rx.
// Checked against glm's own axis rotations so the convention is not self-confirming.
TEST(PlacementSplines, EngineEulerMatrixComposesRzRyRx) {
    const glm::dvec3 euler(0.3, -0.2, 0.7);

    const glm::dmat3 rx(glm::rotate(glm::dmat4(1.0), euler.x, glm::dvec3(1, 0, 0)));
    const glm::dmat3 ry(glm::rotate(glm::dmat4(1.0), euler.y, glm::dvec3(0, 1, 0)));
    const glm::dmat3 rz(glm::rotate(glm::dmat4(1.0), euler.z, glm::dvec3(0, 0, 1)));
    const glm::dmat3 expected = rz * ry * rx;

    const glm::dmat3 actual = EngineEulerMatrix(euler);
    for (int col = 0; col < 3; ++col) {
        for (int row = 0; row < 3; ++row) {
            EXPECT_NEAR(actual[col][row], expected[col][row], 1e-12)
                << "column " << col << " row " << row;
        }
    }
}

// setup_segments (src/sound/segsetup.c:99-101) uses the waypoint frame's first
// column (row-major elements 0, 3, 6) as the Hermite tangent, i.e. the local X
// axis — the same axis MakeWaypointTangent already produced.
TEST(PlacementSplines, WaypointTangentIsTheEngineLocalXAxis) {
    const glm::dvec3 euler(0.41, -0.63, 2.1);
    const glm::dmat3 frame = EngineEulerMatrix(euler);
    const glm::dvec3 localX(frame[0][0], frame[0][1], frame[0][2]);

    const glm::dvec3 tangent = MakeWaypointTangent(euler, 1.0);
    EXPECT_LT(glm::length(tangent - localX), 1e-12);
}

// A span whose two waypoints share the identity frame is a straight line, so the
// joints sit on the chord and the accumulated length equals the chord length.
TEST(PlacementSplines, StraightSpanJointsSpanTheChord) {
    const auto built = BuildSplineSpan(StraightSpan(1000.0, 8));

    ASSERT_EQ(built.joints.size(), 9u);
    EXPECT_NEAR(built.length, 1000.0, 1e-9);
    EXPECT_NEAR(built.mid.x, 500.0, 1e-12);

    EXPECT_NEAR(built.joints.front().pos.x, -500.0, 1e-9);
    EXPECT_NEAR(built.joints.back().pos.x, 500.0, 1e-9);
    for (const auto& joint : built.joints) {
        EXPECT_NEAR(joint.pos.y, 0.0, 1e-9);
        EXPECT_NEAR(joint.pos.z, 0.0, 1e-9);
    }
}

// segLength[i] is the distance from joint i-1 (src/sound/segsetup.c:186-191) and
// SplineObj.length is their sum (src/tagitem/spline.c:824-830).
TEST(PlacementSplines, JointLengthsSumToSpanLength) {
    SplineSpan span;
    span.from = glm::dvec3(0.0, 0.0, 0.0);
    span.to = glm::dvec3(1000.0, 1000.0, 250.0);
    span.rotFrom = EngineEulerMatrix(glm::dvec3(0.0, 0.0, 0.0));
    span.rotTo = EngineEulerMatrix(glm::dvec3(0.0, 0.0, 1.2));
    span.segments = 6;

    const auto built = BuildSplineSpan(span);
    ASSERT_EQ(built.joints.size(), 7u);

    double sum = 0.0;
    for (size_t i = 1; i < built.joints.size(); ++i) {
        const double len = glm::length(built.joints[i].pos - built.joints[i - 1].pos);
        EXPECT_NEAR(built.segLength[i], len, 1e-12);
        sum += len;
    }
    EXPECT_NEAR(built.length, sum, 1e-9);
    EXPECT_NEAR(built.joints.front().pos.x, -500.0, 1e-9);
    EXPECT_NEAR(built.joints.back().pos.x, 500.0, 1e-9);
}

// SplineObjSegment_Setup (src/sound/segsetup.c:267-270) forces a rigid span to a
// single sub-segment, and src/sound/segsetup.c:129-141 gives every joint the
// chord's XY heading via Matrix_SetRotZ. Vec3_ToAngles (HOOK 0x4B2D00,
// src/file/vec.c:233-246) leaves yaw at 0 when the chord has no X component, so a
// heading is only produced for a chord that runs across X.
TEST(PlacementSplines, RigidSpanHasOneSubSegmentAndRotatesWithTheChordHeading) {
    SplineSpan span;
    span.from = glm::dvec3(0.0, 0.0, 0.0);
    span.to = glm::dvec3(2000.0, 2000.0, 500.0);
    span.rigid = true;
    span.segments = 12;   // ignored for rigid spans

    const auto built = BuildSplineSpan(span);

    ASSERT_EQ(built.joints.size(), 2u);
    EXPECT_NEAR(built.length, glm::length(span.to - span.from), 1e-9);

    // yaw = atan2(2000, 2000) = pi/4, so local X maps to (sqrt2/2, sqrt2/2, 0).
    const double c = std::sqrt(0.5);
    for (const auto& joint : built.joints) {
        const glm::dvec3 localX(joint.rot[0][0], joint.rot[0][1], joint.rot[0][2]);
        EXPECT_NEAR(localX.x, c, 1e-12);
        EXPECT_NEAR(localX.y, c, 1e-12);
        EXPECT_NEAR(localX.z, 0.0, 1e-12);
    }
}

// A rigid span whose chord has no X component keeps yaw = 0, exactly as
// Vec3_ToAngles leaves it (src/file/vec.c:234-237).
TEST(PlacementSplines, RigidSpanHeadingIsZeroForAChordWithoutAnXComponent) {
    SplineSpan span;
    span.from = glm::dvec3(0.0, 0.0, 0.0);
    span.to = glm::dvec3(0.0, 2000.0, 500.0);
    span.rigid = true;

    const auto built = BuildSplineSpan(span);
    ASSERT_EQ(built.joints.size(), 2u);
    const glm::dvec3 localX(built.joints[0].rot[0][0], built.joints[0].rot[0][1],
                            built.joints[0].rot[0][2]);
    EXPECT_NEAR(localX.x, 1.0, 1e-12);
    EXPECT_NEAR(localX.y, 0.0, 1e-12);
}

// setup_rigid (src/sound/segsetup.c:203-250) scales the model's local X range onto
// the chord, and SplineObj_BendVerts' rigid branch (src/shadow/spline.c:29-46)
// rotates each vertex about the model's own local mid. The bent offsets are
// relative to the span mid, so the model's ends land exactly on from and to.
TEST(PlacementSplines, RigidBendMapsTheModelXRangeOntoTheChordEnds) {
    const ParsedGeometry geometry = MakeBox(-200.0, 200.0, 25.0, 5.0);

    SplineSpan span;
    span.from = glm::dvec3(1000.0, 2000.0, 3000.0);
    span.to = glm::dvec3(3000.0, 2000.0, 3000.0);
    span.rigid = true;

    const auto built = BuildSplineSpan(span);
    const auto bent = BendSegmentMesh(geometry, span);
    ASSERT_FALSE(bent.interleaved.empty());

    // The first vertex emitted is the authored (minX, -halfY, -halfZ); the mesh
    // stores worldOffset / 40.96, so scale it back before adding the span mid.
    const glm::dvec3 first(bent.interleaved[0], bent.interleaved[1], bent.interleaved[2]);
    const glm::dvec3 world = built.mid + first * 40.96;
    EXPECT_NEAR(world.x, span.from.x, 1e-6);
    EXPECT_NEAR(world.y, span.from.y - 25.0, 1e-6);
    EXPECT_NEAR(world.z, span.from.z - 5.0, 1e-6);
}

// SplineObj_BendVerts' curved branch (src/shadow/spline.c:48-89) indexes the joint
// pair with an x87 float-to-int conversion. The engine sets the x87 control word to
// round toward zero at startup (FPU_SetTruncate, HOOK 0x498460,
// src/dxinit/dxinit.c:75-81, called from src/win/appmain.c:212), so the index is
// the truncated f and the residual is the blend factor. At f = 3.6 that is joint 3
// with frac 0.6, not joint 4.
TEST(PlacementSplines, CurvedBendUsesTruncatedJointIndexLikeTheEngine) {
    // minX = -1000, maxX = 1000, 4 segments => f = (1000 + x) * 4 / 2000.
    const double minX = -1000.0;
    const double maxX = 1000.0;
    const double probeX = 800.0;
    const ParsedGeometry geometry = MakeProbeBox(minX, maxX, probeX, 40.0, -20.0);

    SplineSpan span;
    span.from = glm::dvec3(0.0, 0.0, 0.0);
    span.to = glm::dvec3(4000.0, 0.0, 0.0);
    span.rotFrom = EngineEulerMatrix(glm::dvec3(0.0, 0.0, 0.0));
    span.rotTo = EngineEulerMatrix(glm::dvec3(0.0, 0.0, 0.6));
    span.segments = 4;

    const auto built = BuildSplineSpan(span);
    ASSERT_EQ(built.joints.size(), 5u);

    const auto bent = BendSegmentMesh(geometry, span);
    ASSERT_EQ(bent.submeshVertexCounts.size(), 1u);
    const glm::dvec3 actual = ProbeOffset(bent);

    const glm::dvec3 expected =
        ExpectedBlend(built, minX, maxX, probeX, glm::dvec3(0.0, 40.0, -20.0));
    // The bent mesh stores single-precision offsets divided by the render scale,
    // so allow the float round-trip (still ~1000x tighter than the discriminator
    // against nearest-integer rounding below).
    EXPECT_LT(glm::length(actual - expected), 1e-3);

    // Nearest-integer rounding would have selected joint 4 outright.
    const glm::dvec3 nearest = built.joints[4].pos +
        glm::dvec3(built.joints[4].rot * glm::dvec3(0.0, 40.0, -20.0));
    EXPECT_GT(glm::length(actual - nearest), 1.0);
}

// Vertices either side of a joint boundary use the same joint pair, so their bent
// offsets must converge instead of stepping.
TEST(PlacementSplines, CurvedBendIsContinuousAcrossAJointBoundary) {
    const double minX = -1000.0;
    const double maxX = 1000.0;
    const ParsedGeometry below = MakeProbeBox(minX, maxX, -500.001, 40.0, -20.0);
    const ParsedGeometry above = MakeProbeBox(minX, maxX, -499.999, 40.0, -20.0);

    SplineSpan span;
    span.from = glm::dvec3(0.0, 0.0, 0.0);
    span.to = glm::dvec3(4000.0, 1000.0, 0.0);
    span.rotFrom = EngineEulerMatrix(glm::dvec3(0.0, 0.0, 0.0));
    span.rotTo = EngineEulerMatrix(glm::dvec3(0.0, 0.0, 1.2));
    span.segments = 4;

    const auto bentBelow = BendSegmentMesh(below, span);
    const auto bentAbove = BendSegmentMesh(above, span);
    ASSERT_EQ(bentBelow.submeshVertexCounts.size(), 1u);
    ASSERT_EQ(bentAbove.submeshVertexCounts.size(), 1u);

    EXPECT_LT(glm::length(ProbeOffset(bentBelow) - ProbeOffset(bentAbove)), 0.5);
}

// SplineObj_GetPoints (HOOK 0x4FEC10, src/tagitem/spline.c:238-244): a negative
// scalar is measured backwards from the end of the rail and wrapped into range.
// Mission 1 authors the train at -971400 on a rail a few million units long.
TEST(PlacementSplines, SignedRailScalarWrapsFromTheEnd) {
    EXPECT_NEAR(WrapRailDistance(-971400.0, 4700000.0), 3728600.0, 1e-9);
    EXPECT_NEAR(WrapRailDistance(0.0, 4700000.0), 0.0, 1e-12);
    EXPECT_NEAR(WrapRailDistance(4700000.0, 4700000.0), 0.0, 1e-9);
    EXPECT_NEAR(WrapRailDistance(4700001.0, 4700000.0), 1.0, 1e-9);
    EXPECT_NEAR(WrapRailDistance(-971400.0 - 4700000.0, 4700000.0), 3728600.0, 1e-6);
}

// Consecutive spans share the waypoint they meet at, and SplineObj.length is the
// sum of the per-segment lengths (src/tagitem/spline.c:824-830).
TEST(PlacementSplines, RailPathChainsSpansWithoutDuplicatingSharedWaypoints) {
    std::vector<SplineSpan> spans;
    spans.push_back(StraightSpan(1000.0, 4));
    SplineSpan second;
    second.from = glm::dvec3(1000.0, 0.0, 0.0);
    second.to = glm::dvec3(1000.0, 1000.0, 0.0);
    // The Hermite tangents are the waypoint frames' local X axes
    // (src/sound/segsetup.c:99-101), so a span that should be straight needs
    // frames pointing along its own chord — this is what the engine's Automatic
    // Orientation fills in.
    second.rotFrom = EngineEulerMatrix(glm::dvec3(0.0, 0.0, kPi / 2.0));
    second.rotTo = second.rotFrom;
    second.segments = 4;
    spans.push_back(second);

    const RailPath path = BuildRailPath(spans);

    EXPECT_EQ(path.joints.size(), 9u);
    EXPECT_NEAR(path.length, 2000.0, 1e-6);
    EXPECT_NEAR(path.joints.front().pos.x, 0.0, 1e-9);
    EXPECT_NEAR(path.joints.back().pos.y, 1000.0, 1e-9);

    glm::dvec3 atStart;
    ASSERT_TRUE(SampleRailPoint(path, 0.0, atStart));
    EXPECT_NEAR(atStart.x, 0.0, 1e-9);

    glm::dvec3 half;
    ASSERT_TRUE(SampleRailPoint(path, 1000.0, half));
    EXPECT_NEAR(half.x, 1000.0, 1e-6);
    EXPECT_NEAR(half.y, 0.0, 1e-6);
}

// Matrix_SetDirection (HOOK 0x4B4010, src/math/matrix.c:244-277) plus the train's
// fixed axis remap `orient` (src/missile/train.c:172-190). For a track running
// along +X the remapped frame maps the model's local X to -X.
TEST(PlacementSplines, TrackDirectionMatrixAppliesTheTrainAxisRemap) {
    const glm::dmat3 matrix = TrackDirectionMatrix(glm::dvec3(1.0, 0.0, 0.0));
    const glm::dvec3 localX = matrix * glm::dvec3(1.0, 0.0, 0.0);

    EXPECT_NEAR(localX.x, -1.0, 1e-12);
    EXPECT_NEAR(localX.y, 0.0, 1e-12);
    EXPECT_NEAR(localX.z, 0.0, 1e-12);
}

// Train_Place (HOOK 0x42A2C0, src/missile/train.c:209-274): offsets[0] is
// -length for an unflipped train, the position is the arc point advanced by frac
// of that spacing, and the displacement is clamped to gTrainOffsetRange
// (src/missile/train.c:380-381) and scaled by kFixed = 4096 (src/math/mathvars.h:7).
TEST(PlacementSplines, TrainPlacementHonoursTheNegativeSignedScalar) {
    std::vector<SplineSpan> spans;
    SplineSpan rail;
    rail.from = glm::dvec3(0.0, 0.0, 0.0);
    rail.to = glm::dvec3(4700000.0, 0.0, 0.0);
    rail.segments = 20;
    spans.push_back(rail);
    const RailPath path = BuildRailPath(spans);
    ASSERT_NEAR(path.length, 4700000.0, 1e-6);

    const auto placed = PlaceTrainOnRail(path, -971400.0, 100000.0, 0.5, 0.0, 0.0, false);
    ASSERT_TRUE(placed.found);

    // arc0 = 3728600, arc1 = 3728600 - 100000, pos = arc0 + (arc1 - arc0) * 0.5.
    EXPECT_NEAR(placed.position.x, 3678600.0, 1e-6);
    EXPECT_NEAR(placed.position.y, 0.0, 1e-9);
    EXPECT_NEAR(placed.position.z, 0.0, 1e-9);
    EXPECT_GT(placed.position.x, 0.0) << "a negative scalar must not collapse to the map origin";

    // Flipping reverses the spacing, so the blend point moves the other way.
    const auto flipped = PlaceTrainOnRail(path, -971400.0, 100000.0, 0.5, 0.0, 0.0, true);
    ASSERT_TRUE(flipped.found);
    EXPECT_NEAR(flipped.position.x, 3778600.0, 1e-6);
}

TEST(PlacementSplines, TrainDisplacementIsClampedToFiveMetresAndScaledBy4096) {
    std::vector<SplineSpan> spans;
    SplineSpan rail;
    rail.from = glm::dvec3(0.0, 0.0, 0.0);
    rail.to = glm::dvec3(1000000.0, 0.0, 0.0);
    rail.segments = 20;
    spans.push_back(rail);
    const RailPath path = BuildRailPath(spans);

    const auto placed = PlaceTrainOnRail(path, 100000.0, 50000.0, 0.0, 100.0, 0.0, false);
    ASSERT_TRUE(placed.found);

    const glm::dvec3 offset = placed.position - glm::dvec3(100000.0, 0.0, 0.0);
    EXPECT_NEAR(glm::length(offset), 5.0 * 4096.0, 1e-6);
}

// ---------------------------------------------------------------------------
// The engine's RENDER path: Direct3DRender_FillSplineVB (HOOK 0x49CC80,
// src/render/render.c:613-670) plus the INDEXED draw (draw_group, :522-532).
//
// `BendSegmentMesh` above is the collision twin and EXPANDS each indexed
// triangle into its own vertices; the renderer used to draw spans that way. The
// engine does not: it transforms the model's vertex buffer IN PLACE - output
// vertex i is source vertex i - and draws it with the model's own index buffer.
// ---------------------------------------------------------------------------

TEST(PlacementSplines, EngineSpanMeshTransformsInPlaceAndKeepsTheModelIndices) {
    const ParsedGeometry geometry = MakeBox(-200.0, 200.0, 25.0, 5.0);
    const auto mesh = spline_geometry::BuildEngineSpanMesh(geometry, EngineSpan(0.0, 4000.0));

    ASSERT_EQ(mesh.vertexCount, static_cast<int>(geometry.vertices.size()))
        << "FillSplineVertices writes one vertex per source vertex";
    ASSERT_EQ(mesh.interleaved.size(), geometry.vertices.size() * 10u);

    // The model's own index buffer, per render block (the geometry has none, so
    // the whole triangle list is one block).
    ASSERT_EQ(mesh.indices.size(), 1u);
    ASSERT_EQ(mesh.indices[0].size(), geometry.triangles.size() * 3u);
    for (size_t t = 0; t < geometry.triangles.size(); ++t) {
        EXPECT_EQ(mesh.indices[0][t * 3 + 0], geometry.triangles[t][0]);
        EXPECT_EQ(mesh.indices[0][t * 3 + 1], geometry.triangles[t][1]);
        EXPECT_EQ(mesh.indices[0][t * 3 + 2], geometry.triangles[t][2]);
    }
}

TEST(PlacementSplines, EngineSpanMeshCarriesUv2FromTheSameSourceVertexAndFlipsV) {
    ParsedGeometry geometry = MakeBox(-200.0, 200.0, 25.0, 5.0);
    geometry.vertices[3].uv = glm::vec2(0.25f, 0.75f);
    geometry.vertices[3].uv2 = glm::vec2(0.5f, 0.125f);

    const auto mesh = spline_geometry::BuildEngineSpanMesh(geometry, EngineSpan(0.0, 4000.0));
    ASSERT_EQ(mesh.vertexCount, 8);

    // uv2 is the editor's own attribute - the engine's vertex is 8 floats
    // (src/render/render.c:56-61) and has no second UV - and it comes from the
    // source vertex at the same index. V is flipped for the GL texture origin.
    const float* v3 = mesh.interleaved.data() + 3 * 10;
    EXPECT_FLOAT_EQ(v3[6], 0.25f);
    EXPECT_FLOAT_EQ(v3[7], 1.0f - 0.75f);
    EXPECT_FLOAT_EQ(v3[8], 0.5f);
    EXPECT_FLOAT_EQ(v3[9], 0.125f);

    const float* v0 = mesh.interleaved.data();
    EXPECT_FLOAT_EQ(v0[8], geometry.vertices[0].uv2.x);
    EXPECT_FLOAT_EQ(v0[9], geometry.vertices[0].uv2.y);
}

// setup_rigid (src/sound/segsetup.c:203-251) scales the model's local X range
// onto the chord; Direct3DRender_FillSplineVB's straight branch (:637-648)
// applies that frame and copies the normal STRAIGHT THROUGH, without rotating it.
TEST(PlacementSplines, EngineSpanMeshRigidSpanMapsTheModelXRangeOntoTheChordEnds) {
    ParsedGeometry geometry = MakeBox(-200.0, 200.0, 25.0, 5.0);
    geometry.vertices[0].normal = glm::vec3(1.0f, 0.0f, 0.0f);

    spline_geometry::EngineSpanInput span;
    span.from = glm::dvec3(1000.0, 2000.0, 3000.0);
    span.to = glm::dvec3(3000.0, 2000.0, 3000.0);
    span.rotFrom = glm::dvec3(0.0, 0.0, 0.7);   // rotated frames must not matter
    span.rotTo = glm::dvec3(0.0, 0.0, 0.7);
    span.rigid = true;

    const auto mesh = spline_geometry::BuildEngineSpanMesh(geometry, span);
    ASSERT_EQ(mesh.vertexCount, 8);
    const glm::dvec3 mid = (span.from + span.to) * 0.5;

    const glm::dvec3 first = mid + EngineSpanOffset(mesh, 0);
    EXPECT_NEAR(first.x, span.from.x, 1e-3);
    EXPECT_NEAR(first.y, span.from.y - 25.0, 1e-3);
    EXPECT_NEAR(first.z, span.from.z - 5.0, 1e-3);

    const glm::dvec3 last = mid + EngineSpanOffset(mesh, 4);
    EXPECT_NEAR(last.x, span.to.x, 1e-3);
    EXPECT_NEAR(last.y, span.to.y - 25.0, 1e-3);
    EXPECT_NEAR(last.z, span.to.z - 5.0, 1e-3);

    // The straight branch never touches dst[3..7]: the normal passes through.
    const float* v0 = mesh.interleaved.data();
    EXPECT_FLOAT_EQ(v0[3], 1.0f);
    EXPECT_FLOAT_EQ(v0[4], 0.0f);
    EXPECT_FLOAT_EQ(v0[5], 0.0f);
}

// The same rigid span with "Flip" set negates the chord direction, so the model's
// minX end lands on the TO waypoint (setup_rigid, src/sound/segsetup.c:230-235).
TEST(PlacementSplines, EngineSpanMeshRigidFlipPutsMinXOnTheToWaypoint) {
    const ParsedGeometry geometry = MakeBox(-200.0, 200.0, 25.0, 5.0);

    spline_geometry::EngineSpanInput span;
    span.from = glm::dvec3(1000.0, 2000.0, 3000.0);
    span.to = glm::dvec3(3000.0, 2000.0, 3000.0);
    span.rigid = true;
    span.reversed = true;

    const auto mesh = spline_geometry::BuildEngineSpanMesh(geometry, span);
    ASSERT_EQ(mesh.vertexCount, 8);
    const glm::dvec3 mid = (span.from + span.to) * 0.5;

    const glm::dvec3 first = mid + EngineSpanOffset(mesh, 0);
    EXPECT_NEAR(first.x, span.to.x, 1e-3);
    EXPECT_NEAR(first.y, span.to.y - 25.0, 1e-3);
}

// spline_point (src/render/render.c:286-317) blends the two joints around the
// vertex's local X, so the model's own X extremes land exactly on the waypoints.
TEST(PlacementSplines, EngineSpanMeshCurvedSpanEndsLandOnTheWaypoints) {
    const ParsedGeometry geometry = MakeProbeBox(-2.0, 2.0, 0.0, 40.0, -20.0);
    const auto mesh = spline_geometry::BuildEngineSpanMesh(geometry, EngineSpan(0.0, 100.0));

    ASSERT_EQ(mesh.vertexCount, 3);
    const glm::dvec3 mid(50.0, 0.0, 0.0);

    const glm::dvec3 atMinX = mid + EngineSpanOffset(mesh, 0);
    EXPECT_NEAR(atMinX.x, 0.0, 1e-3);
    EXPECT_NEAR(atMinX.y, 40.0, 1e-3);
    EXPECT_NEAR(atMinX.z, -20.0, 1e-3);

    const glm::dvec3 atMaxX = mid + EngineSpanOffset(mesh, 1);
    EXPECT_NEAR(atMaxX.x, 100.0, 1e-3);
    EXPECT_NEAR(atMaxX.y, 40.0, 1e-3);
    EXPECT_NEAR(atMaxX.z, -20.0, 1e-3);

    // The probe at x = 0 sits on the chord's midpoint for this symmetric span.
    const glm::dvec3 atMid = mid + EngineSpanOffset(mesh, 2);
    EXPECT_NEAR(atMid.x, 50.0, 1e-3);
}

// SplineObjSegment_Setup seeds BOTH bounds at zero (`o->minX = 0; o->maxX = 0;`,
// src/sound/segsetup.c:277-278), so the range always contains the model origin.
// A model sitting entirely at x > 0 is therefore parameterised from 0, not from
// its own minimum: the vertex at x = 10 of a 10..20 model lands mid-span.
TEST(PlacementSplines, EngineSpanMeshExtentScanStartsAtTheModelOrigin) {
    const ParsedGeometry geometry = MakeProbeBox(10.0, 20.0, 10.0, 40.0, -20.0);
    const auto mesh = spline_geometry::BuildEngineSpanMesh(geometry, EngineSpan(0.0, 100.0));

    ASSERT_EQ(mesh.vertexCount, 3);
    const glm::dvec3 mid(50.0, 0.0, 0.0);
    const glm::dvec3 probe = mid + EngineSpanOffset(mesh, 2);
    EXPECT_NEAR(probe.x, 50.0, 1e-3)
        << "a zero-seeded scan gives the 10..20 model the range 0..20";

    // A model that extends below the origin keeps its own negative minimum.
    const ParsedGeometry negative = MakeProbeBox(-10.0, 20.0, -10.0, 40.0, -20.0);
    const auto negMesh =
        spline_geometry::BuildEngineSpanMesh(negative, EngineSpan(0.0, 100.0));
    ASSERT_EQ(negMesh.vertexCount, 3);
    const glm::dvec3 negProbe = mid + EngineSpanOffset(negMesh, 2);
    EXPECT_NEAR(negProbe.x, 0.0, 1e-3);
}

// SplineObjWaypoint_Changed (src/tagitem/spline.c:771-772) clamps the waypoint's
// "NumAreas" against the container's "Number of Matrices/Segment" and against 20;
// SplineObjSegment_Setup (src/sound/segsetup.c:268-270) forces ONE sub-segment for
// a rigid span, whatever the authored count says.
TEST(PlacementSplines, EngineSpanMeshUsesTheAuthoredSegmentCount) {
    const ParsedGeometry geometry = MakeProbeBox(-50.0, 50.0, -37.5, 40.0, -20.0);

    spline_geometry::EngineSpanInput four = EngineSpan(0.0, 100.0);
    four.rotFrom = glm::dvec3(0.0, 0.0, 1.5707963267948966);  // local X along +Y
    four.waypointSegments = 4;
    spline_geometry::EngineSpanInput twenty = four;
    twenty.waypointSegments = 20;

    const auto meshFour = spline_geometry::BuildEngineSpanMesh(geometry, four);
    const auto meshTwenty = spline_geometry::BuildEngineSpanMesh(geometry, twenty);
    ASSERT_EQ(meshFour.vertexCount, 3);
    ASSERT_EQ(meshTwenty.vertexCount, 3);
    EXPECT_GT(glm::length(EngineSpanOffset(meshFour, 2) - EngineSpanOffset(meshTwenty, 2)), 0.1)
        << "the joint count must change where a mid-span vertex is sampled";

    // The container's count is the upper bound: min(waypoint, container).
    spline_geometry::EngineSpanInput containerClamped = four;
    containerClamped.waypointSegments = 20;
    containerClamped.maxSegments = 4;
    const auto meshClamped = spline_geometry::BuildEngineSpanMesh(geometry, containerClamped);
    EXPECT_EQ(meshClamped.interleaved, meshFour.interleaved);

    // A waypoint that declares no "NumAreas" keeps the container's count
    // (SplineObjWaypoint_Create, src/tagitem/spline.c:544).
    spline_geometry::EngineSpanInput undeclared = four;
    undeclared.waypointSegments = 0;
    undeclared.maxSegments = 20;
    const auto meshUndeclared = spline_geometry::BuildEngineSpanMesh(geometry, undeclared);
    EXPECT_EQ(meshUndeclared.interleaved, meshTwenty.interleaved);

    // Rigid spans are a single sub-segment: 1, not 4 or 20.
    spline_geometry::EngineSpanInput rigidFour = four;
    rigidFour.rigid = true;
    spline_geometry::EngineSpanInput rigidTwenty = twenty;
    rigidTwenty.rigid = true;
    const auto meshRigidFour = spline_geometry::BuildEngineSpanMesh(geometry, rigidFour);
    const auto meshRigidTwenty = spline_geometry::BuildEngineSpanMesh(geometry, rigidTwenty);
    ASSERT_EQ(meshRigidFour.vertexCount, 3);
    EXPECT_EQ(meshRigidFour.interleaved, meshRigidTwenty.interleaved);
    EXPECT_NE(meshRigidFour.interleaved, meshTwenty.interleaved);
}

// The swap must not move a rigid span. `BendSegmentMesh` is the collision twin
// (SplineObj_BendVerts, HOOK 0x4E1440, src/shadow/spline.c:29-46) the renderer
// used to draw with; its rigid branch applies the same setup_rigid frame the
// engine's straight branch reads out of ModelObj.matrix, so the two must agree on
// the bent POSITIONS (their normals differ on purpose: the straight branch copies
// the normal through, :643-647, while the collision twin rotates it).
TEST(PlacementSplines, EngineSpanMeshRigidPositionsMatchTheCollisionTwin) {
    const ParsedGeometry geometry = MakeBox(-200.0, 200.0, 25.0, 5.0);

    const glm::dvec3 from(1000.0, 2000.0, 3000.0);
    const glm::dvec3 to(2400.0, 1300.0, 3120.0);
    const glm::dvec3 rotFrom(0.0, 0.0, 0.7);
    const glm::dvec3 rotTo(0.0, 0.0, 1.1);

    spline_geometry::EngineSpanInput engineSpan;
    engineSpan.from = from;
    engineSpan.to = to;
    engineSpan.rotFrom = rotFrom;
    engineSpan.rotTo = rotTo;
    engineSpan.rigid = true;

    SplineSpan collisionSpan;
    collisionSpan.from = from;
    collisionSpan.to = to;
    collisionSpan.rotFrom = EngineEulerMatrix(rotFrom);
    collisionSpan.rotTo = EngineEulerMatrix(rotTo);
    collisionSpan.rigid = true;

    const auto mesh = spline_geometry::BuildEngineSpanMesh(geometry, engineSpan);
    const auto bent = BendSegmentMesh(geometry, collisionSpan);
    ASSERT_EQ(mesh.vertexCount, 8);
    ASSERT_EQ(bent.submeshVertexCounts.size(), 1u);

    // The collision twin expands each triangle, so its emitted vertex `t * 3 + k`
    // is the source vertex `geometry.triangles[t][k]`.
    for (size_t t = 0; t < geometry.triangles.size(); ++t) {
        for (size_t k = 0; k < 3; ++k) {
            const size_t source = geometry.triangles[t][k];
            const float* emitted = bent.interleaved.data() + (t * 3 + k) * 10;
            const glm::dvec3 twin(emitted[0], emitted[1], emitted[2]);
            EXPECT_LT(glm::length(EngineSpanOffset(mesh, source) - twin * 40.96), 1e-3)
                << "triangle " << t << " corner " << k;
        }
    }
}
