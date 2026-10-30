// test_collision_drop_rules.cpp -- the engine's vertical drop query
// (source/level/collision_drop_rules.h).
//
// ORACLE (IGI-RE, read-only):
//   game/misc.c:75-97      Game_DropToGround HOOK 0x4149B0: dz = -1638400.0
//                          (:82), Coll_DropPoints (:87), down then UP (:88-92),
//                          a second miss returns with pos untouched (:92-93),
//                          a hit writes exactly hit.pos.z (:95).
//   coll/coll.c:518-616    Coll_Drop: range = |fistp(dz)| (:587-590), vertical
//                          hit only (:591-592), nearest hit wins (:593-594).
//   coll/coll.c:626-774    Coll_VerticalHit HOOK 0x4CB050: a hit farther than
//                          |dz| is rejected (:706-707).
//   coll/coll.c:290-516    Coll_LeafHit HOOK 0x4CA010: kTenth = 0.1 (:15);
//                          DOWN keeps the highest triangle at or below
//                          p.z + kTenth (:321, :466-468); UP keeps the lowest at
//                          or above p.z - kTenth (:324, :469-471).
//
// The tests below pin the engine's own arithmetic, not a heuristic: the probe
// distance, the 0.1-unit slop, the range, the down-then-up order, and the
// "a miss changes nothing" result.

#include "level/collision_drop_rules.h"

#include <gtest/gtest.h>

namespace {

igi::DropTriangle Tri(double x0, double y0, double z0, double x1, double y1, double z1,
                      double x2, double y2, double z2) {
    igi::DropTriangle t{};
    t.v[0][0] = x0; t.v[0][1] = y0; t.v[0][2] = z0;
    t.v[1][0] = x1; t.v[1][1] = y1; t.v[1][2] = z1;
    t.v[2][0] = x2; t.v[2][1] = y2; t.v[2][2] = z2;
    return t;
}

// A 100x100 slab at z = -500, i.e. a floor BELOW the query point.
const igi::DropTriangle kFloorBelow = Tri(0, 0, -500, 100, 0, -500, 0, 100, -500);
// The same slab ABOVE the query point -- the "sunk soldier" case: the authored
// Z is below the surface, and only an UP probe finds it.
const igi::DropTriangle kFloorAbove = Tri(0, 0, 500, 100, 0, 500, 0, 100, 500);

}  // namespace

// The engine's constants, pinned: 400 m of reach (game/misc.c:82) and the only
// tolerance the query has (coll/coll.c:15 kTenth).
TEST(CollisionDropRules, PinsTheEnginesProbeDistanceAndSlop) {
    EXPECT_DOUBLE_EQ(igi::kDropProbeDistance, 1638400.0);
    EXPECT_DOUBLE_EQ(igi::kDropSurfaceSlop, 0.1);
}

TEST(CollisionDropRules, FindsTheSurfaceBelowThePoint) {
    const igi::DropHit hit = igi::DropOntoTriangles({kFloorBelow}, 10.0, 10.0, -1638400.0);
    ASSERT_TRUE(hit.found);
    EXPECT_DOUBLE_EQ(hit.z, -500.0);
}

TEST(CollisionDropRules, DownProbeIgnoresASurfaceAbove) {
    // 500 units above the probe point is outside Coll_LeafHit's 0.1 slop, so the
    // down probe misses (coll.c:321,466-468).
    EXPECT_FALSE(igi::DropOntoTriangles({kFloorAbove}, 10.0, 10.0, -1638400.0).found);
}

// Coll_LeafHit's kTenth (coll.c:15, :321): a surface up to 0.1 units ABOVE the
// probe start is still a hit of the DOWN probe.  This is the engine's only
// tolerance; the old editor rule used a 100-unit band instead.
TEST(CollisionDropRules, TheDownProbeAcceptsTheEnginesSlopAboveThePoint) {
    const igi::DropTriangle slop = Tri(0, 0, 0.05, 100, 0, 0.05, 0, 100, 0.05);
    const igi::DropHit down = igi::DropOntoTriangles({slop}, 10.0, 10.0, -1638400.0);
    ASSERT_TRUE(down.found);
    EXPECT_DOUBLE_EQ(down.z, 0.05);

    // 0.2 is outside the slop: the DOWN probe does not hit it...
    const igi::DropTriangle outside = Tri(0, 0, 0.2, 100, 0, 0.2, 0, 100, 0.2);
    EXPECT_FALSE(igi::DropOntoTriangles({outside}, 10.0, 10.0, -1638400.0).found);
    // ...but the UP probe does, which is Game_DropToGround's second attempt.
    const igi::DropHit up = igi::DropOntoTriangles({outside}, 10.0, 10.0, 1638400.0);
    ASSERT_TRUE(up.found);
    EXPECT_DOUBLE_EQ(up.z, 0.2);
}

// THE BAND DOES NOT EXIST.  A surface 1,000,000 units (244 m) below is still a
// hit: the engine's reach is 1,638,400 units (game/misc.c:82, coll.c:587-590),
// not 100.  This is the arithmetic behind removing the editor's 100-unit band.
TEST(CollisionDropRules, TheProbeHasNoHundredUnitBand) {
    const igi::DropTriangle far = Tri(0, 0, -1000000, 100, 0, -1000000, 0, 100, -1000000);
    const igi::DropHit hit = igi::DropToGround({far}, 10.0, 10.0);
    ASSERT_TRUE(hit.found);
    EXPECT_DOUBLE_EQ(hit.z, -1000000.0);

    // And a surface 1,000,000 units ABOVE (an authored Z far below the ground)
    // is lifted by the UP probe, as game/misc.c:88-92 does.
    const igi::DropTriangle high = Tri(0, 0, 1000000, 100, 0, 1000000, 0, 100, 1000000);
    const igi::DropHit lifted = igi::DropToGround({high}, 10.0, 10.0);
    ASSERT_TRUE(lifted.found);
    EXPECT_DOUBLE_EQ(lifted.z, 1000000.0);
}

// Coll_VerticalHit's range check (coll.c:706-707): beyond |dz| the hit is
// rejected, and Game_DropToGround then gives up and leaves the position alone.
TEST(CollisionDropRules, AHitBeyondTheProbeDistanceIsRejected) {
    const igi::DropTriangle tooFar =
        Tri(0, 0, -1700000, 100, 0, -1700000, 0, 100, -1700000);
    EXPECT_FALSE(igi::DropToGround({tooFar}, 10.0, 10.0).found);
}

// Game_DropToGround's rule, and the whole point of this file: a soldier whose
// authored Z is BELOW the floor is lifted onto it by the UP probe.
TEST(CollisionDropRules, DropToGroundProbesDownThenUp) {
    const igi::DropHit below = igi::DropToGround({kFloorBelow}, 10.0, 10.0);
    ASSERT_TRUE(below.found);
    EXPECT_DOUBLE_EQ(below.z, -500.0);

    const igi::DropHit above = igi::DropToGround({kFloorAbove}, 10.0, 10.0);
    ASSERT_TRUE(above.found);
    EXPECT_DOUBLE_EQ(above.z, 500.0);
}

// The drop starts at the AUTHORED position, not at z = 0.  This is what makes
// the down/up order observable: from z = -900 the surface at -500 is ABOVE the
// probe point, so the down probe misses and the up probe lifts to -500.
TEST(CollisionDropRules, TheProbeStartsAtTheAuthoredZ) {
    const igi::DropHit hit = igi::DropToGround({kFloorBelow}, 10.0, 10.0, -900.0);
    ASSERT_TRUE(hit.found);
    EXPECT_DOUBLE_EQ(hit.z, -500.0);

    // From above, the same surface is a DOWN hit.
    const igi::DropHit fromAbove = igi::DropToGround({kFloorBelow}, 10.0, 10.0, 900.0);
    ASSERT_TRUE(fromAbove.found);
    EXPECT_DOUBLE_EQ(fromAbove.z, -500.0);
}

TEST(CollisionDropRules, AMissChangesNothing) {
    EXPECT_FALSE(igi::DropToGround({}, 10.0, 10.0).found);
    // Outside the triangle's XY footprint.
    EXPECT_FALSE(igi::DropToGround({kFloorBelow}, 1000.0, 1000.0).found);
}

TEST(CollisionDropRules, TheNearestSurfaceInTheProbeDirectionWins) {
    const igi::DropTriangle high = Tri(0, 0, -100, 100, 0, -100, 0, 100, -100);
    const igi::DropTriangle low  = Tri(0, 0, -900, 100, 0, -900, 0, 100, -900);
    const igi::DropHit hit = igi::DropOntoTriangles({low, high}, 10.0, 10.0, -1638400.0);
    ASSERT_TRUE(hit.found);
    EXPECT_DOUBLE_EQ(hit.z, -100.0);  // the first surface going down, not the deepest

    // Up is the mirror image: the LOWEST surface above wins (coll.c:469-471).
    const igi::DropTriangle upHigh = Tri(0, 0, 900, 100, 0, 900, 0, 100, 900);
    const igi::DropTriangle upLow  = Tri(0, 0, 100, 100, 0, 100, 0, 100, 100);
    const igi::DropHit up = igi::DropOntoTriangles({upHigh, upLow}, 10.0, 10.0, 1638400.0);
    ASSERT_TRUE(up.found);
    EXPECT_DOUBLE_EQ(up.z, 100.0);
}

TEST(CollisionDropRules, ADegenerateTriangleIsNeverHit) {
    // All three XY corners collinear: no vertical crossing, exactly like a ray
    // parallel to a face.
    const igi::DropTriangle degenerate = Tri(0, 0, -500, 50, 50, -500, 100, 100, -500);
    EXPECT_FALSE(igi::DropToGround({degenerate}, 10.0, 10.0).found);
}

// The gatherer: one model's collision faces become world-space drop triangles.
TEST(CollisionDropRules, GathersTransformedTriangles) {
    // A unit square at local z = -500, two faces, wound as triangles.
    const float positions[] = {
        0.0f, 0.0f, -500.0f,
        1.0f, 0.0f, -500.0f,
        1.0f, 1.0f, -500.0f,
        0.0f, 1.0f, -500.0f,
    };
    const unsigned short faces[] = {0, 1, 2, 0, 2, 3};
    // Identity with a +1000 translation on X, column-major (glm order).
    const float xform[16] = {1, 0, 0, 0,
                             0, 1, 0, 0,
                             0, 0, 1, 0,
                             1000, 0, 0, 1};
    std::vector<igi::DropTriangle> tris;
    igi::AppendTransformedTriangles(positions, 4, faces, 6, xform, tris);
    ASSERT_EQ(tris.size(), 2u);

    // The point must be inside the TRANSLATED footprint, not the local one.
    EXPECT_FALSE(igi::DropToGround(tris, 0.5, 0.5).found);
    const igi::DropHit hit = igi::DropToGround(tris, 1000.5, 0.5);
    ASSERT_TRUE(hit.found);
    EXPECT_DOUBLE_EQ(hit.z, -500.0);
}

TEST(CollisionDropRules, GathererRefusesOutOfRangeIndicesAndNulls) {
    const float positions[] = {0.0f, 0.0f, 0.0f};
    const unsigned short faces[] = {0, 1, 2};  // index 1 and 2 are out of range
    const float xform[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::vector<igi::DropTriangle> tris;
    igi::AppendTransformedTriangles(positions, 1, faces, 3, xform, tris);
    EXPECT_TRUE(tris.empty());

    igi::AppendTransformedTriangles(nullptr, 4, faces, 6, xform, tris);
    igi::AppendTransformedTriangles(positions, 4, nullptr, 6, xform, tris);
    igi::AppendTransformedTriangles(positions, 4, faces, 6, nullptr, tris);
    EXPECT_TRUE(tris.empty());
}
