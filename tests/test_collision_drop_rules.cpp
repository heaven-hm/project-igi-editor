// test_collision_drop_rules.cpp -- the engine's vertical drop query
// (source/level/collision_drop_rules.h).
//
// ORACLE: Game_DropToGround HOOK 0x4149B0 (src/game/misc.c:75-97) probes DOWN
// then UP and writes hit.pos.z only on a hit; Coll_VerticalHit HOOK 0x4CB050
// (src/coll/coll.c:626-774) sets found only on a real crossing.
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

TEST(CollisionDropRules, FindsTheSurfaceBelowThePoint) {
    const igi::DropHit hit = igi::DropOntoTriangles({kFloorBelow}, 10.0, 10.0, -1638400.0);
    ASSERT_TRUE(hit.found);
    EXPECT_DOUBLE_EQ(hit.z, -500.0);
}

TEST(CollisionDropRules, DownProbeIgnoresASurfaceAbove) {
    EXPECT_FALSE(igi::DropOntoTriangles({kFloorAbove}, 10.0, 10.0, -1638400.0).found);
}

// The engine's rule, and the whole point of this file: a soldier whose authored
// Z is BELOW the floor is lifted onto it by the UP probe.
TEST(CollisionDropRules, DropToGroundProbesDownThenUp) {
    const igi::DropHit below = igi::DropToGround({kFloorBelow}, 10.0, 10.0);
    ASSERT_TRUE(below.found);
    EXPECT_DOUBLE_EQ(below.z, -500.0);

    const igi::DropHit above = igi::DropToGround({kFloorAbove}, 10.0, 10.0);
    ASSERT_TRUE(above.found);
    EXPECT_DOUBLE_EQ(above.z, 500.0);
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
}

TEST(CollisionDropRules, ADegenerateTriangleIsNeverHit) {
    // All three XY corners collinear: no vertical crossing, exactly like a ray
    // parallel to a face.
    const igi::DropTriangle degenerate = Tri(0, 0, -500, 50, 50, -500, 100, 100, -500);
    EXPECT_FALSE(igi::DropToGround({degenerate}, 10.0, 10.0).found);
}
