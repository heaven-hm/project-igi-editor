// test_terrain_snap_rules.cpp - the terrain-snap rules the editor's
// App::SnapObjectsToTerrain runs on (source/app_view.cpp), extracted into
// source/level/terrain_snap_rules.h so they can be proven in the host lane.
//
// WHY: one play session logged 24 "[WARN] [App] Snap FAILED for <id> at (x, y).
// Outside terrain?" lines for objects that are not failures at all --
// colbox2/colbox3 collision proxies (which the pass must never touch) and
// props/humans whose authored Z the engine itself keeps when a drop query
// misses.  The rules below pin both halves down.
//
// ORACLE (IGI-RE, read-only):
//   coll/coll.c:813-818    Coll_DropPointsMat HOOK 0x4CB9F0 (drop query)
//   coll/coll.c:518-616    Coll_Drop: a miss leaves results[i].found == 0
//   coll/coll.c:626-774    Coll_VerticalHit HOOK 0x4CB050 (terrain triangle hit)
//   game/misc.c:75-97      Game_DropToGround HOOK 0x4149B0: probe down, then up,
//                          and on a second miss `return` with pos untouched --
//                          the engine NEVER clamps or invents a Z
//   graphnode/lightmap.c:325-333  lightmap placement moves p.z only if (hit.found)
//   terredit/discard.c:10-12      DiscardTerrain: "tells the qcube walk to drop the
//                          terrain inside it, and takes collision points that fall
//                          inside it away from the terrain"
//   terredit/discard.c:113-117    DiscardTerrain_Visit HOOK 0x52C0B0 sets
//                          gQCubeSkipCells while the walk is inside the box
//   terredit/discard.c:75-87      DiscardTerrain_CollQuery HOOK 0x52BF00 clears
//                          active[] for points inside the box
//   renderer/mef_native.cpp:677-684  a colbox MEF's only geometry is the collision
//                          mesh (empty REND and XTRV); the engine never renders it
//
// The placement ids used below are the ones from the reported play log; they are
// real level-1 placements (retail <IGI>/editor/qed/temp/objects.qsc): 018_01_1 is
// a HumanSoldier, 700_01_1 a Heli, 432_03_1 / 470_01_1 Buildings (lifts and a
// helipad), and colbox2 / colbox3 are EditRigidObj collision proxies co-located
// with the Barracks and the EkksHQLift / WinchHouseLift buildings respectively.

#include "../source/level/terrain_snap_rules.h"

#include <gtest/gtest.h>

#include <string>

// ── collision proxies ────────────────────────────────────────────────────────

TEST(TerrainSnapRules, CollisionProxyIdsAreRecognised) {
    EXPECT_TRUE(igi::IsCollisionProxyModelId("colbox"));
    EXPECT_TRUE(igi::IsCollisionProxyModelId("colbox2"));
    EXPECT_TRUE(igi::IsCollisionProxyModelId("colbox3"));
    EXPECT_TRUE(igi::IsCollisionProxyModelId("colbox4"));
    EXPECT_TRUE(igi::IsCollisionProxyModelId("colbox66"));
}

TEST(TerrainSnapRules, RealModelsAreNotCollisionProxies) {
    // The ids the snap pass must still consider.
    EXPECT_FALSE(igi::IsCollisionProxyModelId("018_01_1"));  // HumanSoldier
    EXPECT_FALSE(igi::IsCollisionProxyModelId("700_01_1"));  // Heli
    EXPECT_FALSE(igi::IsCollisionProxyModelId("432_03_1"));  // Building (lift)
    EXPECT_FALSE(igi::IsCollisionProxyModelId("470_01_1"));  // Building (helipad)
    EXPECT_FALSE(igi::IsCollisionProxyModelId("000_01_1"));  // Player Jones
    EXPECT_FALSE(igi::IsCollisionProxyModelId("605_08_1"));  // collision-only *real* model
    EXPECT_FALSE(igi::IsCollisionProxyModelId("colbox1"));
    EXPECT_FALSE(igi::IsCollisionProxyModelId("colbox6"));
    EXPECT_FALSE(igi::IsCollisionProxyModelId(""));
}

// ── the decision ─────────────────────────────────────────────────────────────

TEST(TerrainSnapRules, QueryMissNeverMovesAnything) {
    // Game_DropToGround HOOK 0x4149B0 (game/misc.c:88-95): when both probes miss,
    // the engine returns with the pose untouched.  So a miss keeps the authored Z
    // for a human exactly as it does for a prop -- the old code treated the human
    // case as a failure and warned.
    const double authoredZ = 179656496.0;  // 018_01_1 in level 1
    const double zDeltaOnMiss = authoredZ - 0.0;  // GetTerrainZ leaves z = 0 on a miss

    EXPECT_EQ(igi::DecideTerrainSnap(false, /*isHuman=*/true, zDeltaOnMiss),
              igi::TerrainSnapDecision::KeepAuthoredZ);
    EXPECT_EQ(igi::DecideTerrainSnap(false, /*isHuman=*/false, zDeltaOnMiss),
              igi::TerrainSnapDecision::KeepAuthoredZ);
    EXPECT_EQ(igi::DecideTerrainSnap(false, /*isHuman=*/true, 0.0),
              igi::TerrainSnapDecision::KeepAuthoredZ);
    EXPECT_EQ(igi::DecideTerrainSnap(false, /*isHuman=*/true, -13000000.0),
              igi::TerrainSnapDecision::KeepAuthoredZ);
}

TEST(TerrainSnapRules, PropsAlwaysKeepTheirAuthoredZ) {
    // 700_01_1 (Heli), 432_03_1 and 470_01_1 (Buildings): non-humans keep the QSC Z
    // even when the terrain query succeeds, because dropping them destroys elevated
    // platforms, embedded foundations and stacked props.
    EXPECT_EQ(igi::DecideTerrainSnap(true, /*isHuman=*/false, 0.0),
              igi::TerrainSnapDecision::KeepAuthoredZ);
    EXPECT_EQ(igi::DecideTerrainSnap(true, /*isHuman=*/false, 1000000.0),
              igi::TerrainSnapDecision::KeepAuthoredZ);
    EXPECT_EQ(igi::DecideTerrainSnap(true, /*isHuman=*/false, -500000.0),
              igi::TerrainSnapDecision::KeepAuthoredZ);
}

TEST(TerrainSnapRules, HumansOnTheSurfaceSnap) {
    EXPECT_EQ(igi::DecideTerrainSnap(true, /*isHuman=*/true, 0.0),
              igi::TerrainSnapDecision::SnapToTerrain);
    EXPECT_EQ(igi::DecideTerrainSnap(true, /*isHuman=*/true, 1.0),
              igi::TerrainSnapDecision::SnapToTerrain);
    // The band is inclusive at 100: the original kept only `zDelta > 100`.
    EXPECT_EQ(igi::DecideTerrainSnap(true, /*isHuman=*/true, 100.0),
              igi::TerrainSnapDecision::SnapToTerrain);
    EXPECT_EQ(igi::DecideTerrainSnap(true, /*isHuman=*/true, 100.5),
              igi::TerrainSnapDecision::KeepAuthoredZ);
}

TEST(TerrainSnapRules, HumansOffTheSurfaceKeepTheirAuthoredZ) {
    // Above the surface band: standing on a platform/building.
    EXPECT_EQ(igi::DecideTerrainSnap(true, /*isHuman=*/true, 100.0001),
              igi::TerrainSnapDecision::KeepAuthoredZ);
    EXPECT_EQ(igi::DecideTerrainSnap(true, /*isHuman=*/true, 4096.0),
              igi::TerrainSnapDecision::KeepAuthoredZ);
    // Below the terrain at all: underground bunker / interior floor under the surface.
    EXPECT_EQ(igi::DecideTerrainSnap(true, /*isHuman=*/true, -0.5),
              igi::TerrainSnapDecision::KeepAuthoredZ);
    // ANYA_HQ sits at ~-13M world units below the terrain.
    EXPECT_EQ(igi::DecideTerrainSnap(true, /*isHuman=*/true, -13000000.0),
              igi::TerrainSnapDecision::KeepAuthoredZ);
}

// A miss must be decided before any human/zDelta reasoning: on a miss the editor's
// Level::GetTerrainZ leaves terrainZ at 0, so zDelta is the full authored Z (in the
// millions) and would otherwise look like "elevated".
TEST(TerrainSnapRules, MissIsDecidedBeforeTheElevationBand) {
    EXPECT_EQ(igi::DecideTerrainSnap(/*terrainFound=*/false, /*isHuman=*/true, 179656496.0),
              igi::TerrainSnapDecision::KeepAuthoredZ);
    EXPECT_EQ(igi::DecideTerrainSnap(/*terrainFound=*/true, /*isHuman=*/true, 179656496.0),
              igi::TerrainSnapDecision::KeepAuthoredZ);
}
