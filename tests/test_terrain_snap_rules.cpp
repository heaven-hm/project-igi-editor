// test_terrain_snap_rules.cpp - the terrain-snap rules the editor's
// App::SnapObjectsToTerrain runs on (source/app_view.cpp), extracted into
// source/level/terrain_snap_rules.h so they can be proven in the host lane.
//
// WHY: the pass used to gate a terrain hit on an editor-invented 100-unit band
// (`zDelta > 100 || zDelta < 0 -> keep`).  The engine has no such band, and on a
// real level the gate left every placed AI unsnapped -- the report's own log:
//     [App] Snap complete. snapped=0 skipped=1843 no_terrain=26
//
// ORACLE (IGI-RE, read-only):
//   game/misc.c:75-97      Game_DropToGround HOOK 0x4149B0: probe DOWN by
//                          1,638,400 units (:82, :87), then UP (:88-92), then
//                          `return` with pos untouched on a second miss (:92-95).
//                          No band, no clamp, no default Z.
//   coll/coll.c:626-774    Coll_VerticalHit HOOK 0x4CB050: a hit farther than
//                          |dz| is rejected (:706-707).
//   coll/coll.c:290-516    Coll_LeafHit HOOK 0x4CA010: DOWN keeps the highest
//                          triangle at or below p.z + kTenth (0.1, :15, :321,
//                          :466-468); UP keeps the lowest at or above p.z - 0.1
//                          (:324, :469-471).
//   humanbase/human.c:1674-1857  Human_ProbeGround HOOK 0x462390: probes 1000 m
//                          (:192-193, :1754-1772) and puts the FOOT on the hit
//                          (`to[2] = foot[2] + hits[0].pos.z`, :1816).
//   coll/coll.c:813-818    Coll_DropPointsMat HOOK 0x4CB9F0 (drop query)
//   graphnode/lightmap.c:325-333  lightmap placement moves p.z only if hit.found
//   terredit/discard.c:10-12, :75-87, :113-117  DiscardTerrain: a designed
//                          "no terrain here" hole, which is the normal state
//                          under interiors/helipads (and the reason a miss is
//                          information, not a failure).
//   renderer/mef_native.cpp:677-684  a colbox MEF's only geometry is the
//                          collision mesh; the engine never renders it.
//
// The placement ids used below are real level-1 placements (retail
// <IGI>/editor/qed/temp/objects.qsc): 018_01_1 is a HumanSoldier, 700_01_1 a
// Heli, 432_03_1 / 470_01_1 Buildings (lifts and a helipad), and colbox2 /
// colbox3 are EditRigidObj collision proxies co-located with the Barracks and
// the EkksHQLift / WinchHouseLift buildings respectively.

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
    EXPECT_EQ(igi::DecideTerrainSnap(/*terrainFound=*/false, /*isHuman=*/true),
              igi::TerrainSnapDecision::KeepAuthoredZ);
    EXPECT_EQ(igi::DecideTerrainSnap(/*terrainFound=*/false, /*isHuman=*/false),
              igi::TerrainSnapDecision::KeepAuthoredZ);
}

TEST(TerrainSnapRules, PropsAlwaysKeepTheirAuthoredZ) {
    // 700_01_1 (Heli), 432_03_1 and 470_01_1 (Buildings): non-humans keep the QSC Z
    // even when the terrain query succeeds, because dropping them destroys elevated
    // platforms, embedded foundations and stacked props.
    EXPECT_EQ(igi::DecideTerrainSnap(/*terrainFound=*/true, /*isHuman=*/false),
              igi::TerrainSnapDecision::KeepAuthoredZ);
}

// THE BAND IS GONE.  The engine's drop reaches 1,638,400 units down and the same
// up (game/misc.c:82,88-92) and Human_ProbeGround 4,096,000 units
// (humanbase/human.c:192-193); a hit anywhere in that reach is the ground.  The
// old rule kept every |zDelta| > 100 units -- 100 units is 2.4 cm at the
// engine's 4096 units/m (humanbase/human.c:165 kUnitsToMeters) -- and that is
// why the shipped AI was never snapped.  Every former band case now snaps:
// 0, +-0.5, 100, 100.0001, +-4096 and the +-13,000,000 "deep underground" value
// all end on the same branch, because the decision no longer takes a distance.
TEST(TerrainSnapRules, AHitSnapsTheHumanAtAnyDistance) {
    EXPECT_EQ(igi::DecideTerrainSnap(/*terrainFound=*/true, /*isHuman=*/true),
              igi::TerrainSnapDecision::SnapToTerrain);
}
