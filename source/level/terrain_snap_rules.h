/******************************************************************************
 * @file    terrain_snap_rules.h
 * @brief   Host-portable rules for App::SnapObjectsToTerrain
 *          (source/app_view.cpp).  Pure functions only: no GL, no windows.h, no
 *          App/Renderer state, so the decisions can be unit-tested in the host
 *          lane (tests/test_terrain_snap_rules.cpp).
 *
 * WHY THIS EXISTS
 *   The snap pass used to gate a terrain hit on an editor-invented 100-unit band
 *   (`zDelta > 100 || zDelta < 0 -> keep`).  The engine has no such band, and the
 *   user's log shows what the gate did on a real level:
 *
 *       [App] Snap complete. snapped=0 skipped=1843 no_terrain=26
 *
 *   -- not one placed AI was moved, so every soldier kept the authored QSC Z,
 *   which is the one value the engine never draws a human from.
 *
 * ENGINE BASIS — the engine's ground query, and why a hit IS the placement
 *   * Game_DropToGround (HOOK 0x4149B0, src/game/misc.c:75-97) is the engine's
 *     canonical "put this on the ground": probe DOWN by 1,638,400 units (400 m,
 *     :82 -> Coll_DropPoints :87); if that misses, probe UP by the same distance
 *     (:88-92); on a second miss `return` with `pos` untouched (:92-93); on a
 *     hit write exactly `hit.pos.z` (:95).  There is NO distance band, no clamp
 *     and no default Z.
 *   * The direction rule and its only tolerance live in Coll_LeafHit
 *     (HOOK 0x4CA010, src/coll/coll.c:290-516): DOWN keeps the highest triangle
 *     at or below `p.z + 0.1` (:15 kTenth, :321, :466-468); UP keeps the lowest
 *     at or above `p.z - 0.1` (:324, :469-471).  Coll_VerticalHit
 *     (HOOK 0x4CB050, src/coll/coll.c:626-774) rejects a hit farther than |dz|
 *     (:706-707).
 *   * The human's drawn Z is that hit, applied to the FOOT: Human_ProbeGround
 *     (HOOK 0x462390, src/humanbase/human.c:1674-1857) probes from
 *     `to + kWallMid - foot[2]` and, on a hit, sets
 *     `to[2] = foot[2] + hits[0].pos.z` (:1816).  So a placed human stands ON
 *     the collision surface; the authored QSC Z only seeds the fall.
 *
 * ENGINE BASIS — why the query legitimately misses under a building
 *   * A "DiscardTerrain" cube is a dynamic-cube tree node that "tells the qcube
 *     walk to drop the terrain inside it, and takes collision points that fall
 *     inside it away from the terrain" (src/terredit/discard.c:10-12).
 *     DiscardTerrain_Visit (HOOK 0x52C0B0, src/terredit/discard.c:113-117) sets
 *     gQCubeSkipCells = 1 while the cube walk is inside the box, and
 *     DiscardTerrain_CollQuery (HOOK 0x52BF00, :75-87) clears active[] for every
 *     point inside it.  So "no terrain under (x, y)" is a DESIGNED state for
 *     interiors/helipads/lifts.
 *   * The editor's own query honours the same flag: Terrain::RecursiveGetZ
 *     skips a child whose CUBE_FLAG_DISCARD_TERRAIN is set unless ignore_discard
 *     is passed (source/level/terrain_query.cpp:315), and App::SnapObjectsToTerrain
 *     calls Level::GetTerrainZ(..., ignore_discard = false).
 *
 * WHAT THE EDITOR'S HIT IS, AND WHAT IT IS NOT
 *   Level::GetTerrainZ -> Terrain::RecursiveGetZ reads the SAME terrain.ctr leaf
 *   modules the engine's Coll_LeafHit reads, at the same leaf level
 *   (LEAF_CUBE_LOD_LEVEL = 16, source/level/terrain.h:86, vs Coll_LeafHit's
 *   shift 16, src/coll/coll.c:705) and with the same packed-vertex scale
 *   (1 << (27 - 16) / 3 == 0x40000000 >> 16 / 24).  For the single-valued terrain
 *   height field the engine's down-then-up search therefore reduces to "the
 *   surface in the column", which is exactly what the editor's query returns.
 *   It does NOT include the rest of the engine's collision world -- the objects'
 *   XTVC/ECFC collision meshes, dynamic cubes and colboxes -- so an AI standing
 *   on a building floor or a colbox is placed against the terrain below it
 *   unless the earlier parent-building rule already owns it.
 *****************************************************************************/
#pragma once

#include <string>

namespace igi {

// Collision-proxy model ids.
//
// These are the engine's collision volumes, not geometry: retail ships models
// whose only geometry is the collision mesh (empty REND and XTRV — e.g.
// colbox.mef), which the engine never renders
// (source/renderer/mef_native.cpp:677-684) and never resolves to a mesh
// (Renderer_Objects::IsSkippedModelId).  They carry no authored ground Z and the
// engine never drops them, so no pass may treat them as snappable placements.
//
// This is the single list; Renderer_Objects::IsSkippedModelId forwards to it so
// the draw pass and the snap pass cannot drift apart again (colbox3 was missing
// from the draw pass once — commit 71379b67 — and then from the snap pass).
inline bool IsCollisionProxyModelId(const std::string& modelId) {
    if (modelId.empty()) return false;
    return modelId == "colbox" || modelId == "colbox2" || modelId == "colbox3" ||
           modelId == "colbox4" || modelId == "colbox66";
}

// What the snap must do with one placement once the engine's drop query has
// answered.
enum class TerrainSnapDecision {
    KeepAuthoredZ,   // leave obj.pos.z as the QSC authored it (snap_z_offset = 0)
    SnapToTerrain,   // human soldier on the surface: feet on the collision hit
};

// The rule, in the order App::SnapObjectsToTerrain applies it:
//   1. no hit at all -> keep.  The engine never invents a Z for a miss
//      (Game_DropToGround HOOK 0x4149B0, src/game/misc.c:88-95).
//   2. not a human -> keep.  Props, trees, buildings, fences and vehicles have
//      authoritative QSC Z values; the engine's load path drops humans through
//      Human_ProbeGround, not furniture, and dropping props destroys elevated
//      platforms, embedded foundations and stacked props.
//   3. otherwise -> snap the human.  A hit is a hit: the engine's probe reaches
//      400 m down and 400 m up with no window around the authored Z
//      (src/game/misc.c:82,88-92), and Human_ProbeGround reaches 1000 m
//      (src/humanbase/human.c:192-193).  The old 100-unit (2.4 cm) band here was
//      an editor invention and is gone.
//
// `hitFound` is the result of the engine's WHOLE drop query, not the terrain
// alone: Coll_Drop HOOK 0x4CABF0 body (src/coll/coll.c:575-614) queries the
// rigid objects AND the terrain and keeps the nearest hit, so a human standing
// on a building floor/roof over a DiscardTerrain hole still has a hit.
inline TerrainSnapDecision DecideCollisionSnap(bool hitFound, bool isHuman) {
    if (!hitFound) return TerrainSnapDecision::KeepAuthoredZ;
    if (!isHuman) return TerrainSnapDecision::KeepAuthoredZ;
    return TerrainSnapDecision::SnapToTerrain;
}

// The terrain-only subset, kept for callers/tests that have only the terrain
// answer; the terrain is one of the two halves Coll_Drop compares.
inline TerrainSnapDecision DecideTerrainSnap(bool terrainFound, bool isHuman) {
    return DecideCollisionSnap(terrainFound, isHuman);
}


}  // namespace igi
