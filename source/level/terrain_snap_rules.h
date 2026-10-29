/******************************************************************************
 * @file    terrain_snap_rules.h
 * @brief   Host-portable rules for App::SnapObjectsToTerrain
 *          (source/app_view.cpp).  Pure functions only: no GL, no windows.h, no
 *          App/Renderer state, so the decisions can be unit-tested in the host
 *          lane (tests/test_terrain_snap_rules.cpp).
 *
 * WHY THIS EXISTS
 *   The snap pass logged "[WARN] [App] Snap FAILED for <id> at (x, y). Outside
 *   terrain?" 24 times in one play session, for objects that are perfectly
 *   valid: colbox2/colbox3 collision proxies and props (Heli, Building) that the
 *   pass never moves anyway, plus HumanSoldiers whose position the terrain query
 *   misses.  Two of the three rules below were implicit in the App code; making
 *   them explicit and testable is what stops the warning from coming back.
 *
 * ENGINE BASIS — a terrain height query is a drop query, and a miss is not an
 * error:
 *   * Coll_DropPointsMat (HOOK 0x4CB9F0, src/coll/coll.c:813-818) -> Coll_Drop
 *     (src/coll/coll.c:518-616) -> Coll_VerticalHit (HOOK 0x4CB050,
 *     src/coll/coll.c:626-774) sets results[i].found only when a terrain
 *     triangle is actually hit; a miss leaves found == 0.
 *   * Game_DropToGround (HOOK 0x4149B0, src/game/misc.c:75-97) is the engine's
 *     canonical "put this on the ground": it probes DOWN (dz = -1638400), then
 *     UP (-dz), and if both miss it returns with `pos` untouched:
 *         if (!hit.found) { dz = -dz; Coll_DropPoints(...);
 *                           if (!hit.found) return; }
 *         memcpy(&pos->z, &hit.pos.z, sizeof(double));
 *     There is NO clamp, no default Z, no error — the authored pose stands.
 *   * Lightmap placement does the same: `if (hit.found) p.z = hit.pos.z;`
 *     (src/graphnode/lightmap.c:325-333, Coll_DropPointsMat).
 *
 * ENGINE BASIS — why the query legitimately misses under a building:
 *   * A "DiscardTerrain" cube is a dynamic-cube tree node that "tells the qcube
 *     walk to drop the terrain inside it, and takes collision points that fall
 *     inside it away from the terrain" (src/terredit/discard.c:10-12).
 *     DiscardTerrain_Visit (HOOK 0x52C0B0, src/terredit/discard.c:113-117) sets
 *     gQCubeSkipCells = 1 while the cube walk is inside the box, and
 *     DiscardTerrain_CollQuery (HOOK 0x52BF00, src/terredit/discard.c:75-87)
 *     clears active[] for every point inside it.  So "no terrain under (x, y)"
 *     is a DESIGNED state for interiors/helipads/lifts, which is exactly where
 *     the failing objects sit.
 *   * The editor's own query honours the same flag: Terrain::RecursiveGetZ
 *     skips a child whose CUBE_FLAG_DISCARD_TERRAIN is set unless ignore_discard
 *     is passed (source/level/terrain_query.cpp:315), and App::SnapObjectsToTerrain
 *     calls Level::GetTerrainZ(..., ignore_discard = false).
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

// What the terrain snap must do with one placement once the terrain query has
// answered.
enum class TerrainSnapDecision {
    KeepAuthoredZ,   // leave obj.pos.z as the QSC authored it (snap_z_offset = 0)
    SnapToTerrain,   // human soldier on the surface: feet on terrainZ
};

// The surface band in world units within which a human soldier is considered to
// be standing on the terrain rather than on a platform above it (or below it).
inline constexpr double kSnapSurfaceBandAbove = 100.0;
// "Far below terrain" (e.g. ANYA_HQ at ~-13M).  NOTE: unreachable while
// kSnapSurfaceBandAbove is checked first, because every value below
// -1000000 is also < 0; kept because it documents the authored intent and
// App::SnapObjectsToTerrain still logs it.
inline constexpr double kSnapDeepUnderground = -1000000.0;

// The rule, in the order App::SnapObjectsToTerrain applies it:
//   1. no terrain under (x, y) -> keep.  The engine never invents a Z for a
//      miss (Game_DropToGround HOOK 0x4149B0, src/game/misc.c:88-95).
//   2. not a human -> keep.  Props, trees, buildings, fences and vehicles have
//      authoritative QSC Z values; dropping them destroys elevated platforms,
//      embedded foundations and stacked props.
//   3. human above the surface band, or below the terrain -> keep (standing on
//      a platform/building, or in an underground bunker).
//   4. otherwise -> snap the human to the terrain.
inline TerrainSnapDecision DecideTerrainSnap(bool terrainFound, bool isHuman, double zDelta) {
    if (!terrainFound) return TerrainSnapDecision::KeepAuthoredZ;
    if (!isHuman) return TerrainSnapDecision::KeepAuthoredZ;
    // RESTORED to the rule that shipped for months before this branch touched it
    // (source/app_view.cpp at 6cef506f^, the pre-Oct-28 form):
    //
    //     // Human soldiers: snap to terrain unless they are elevated (on a
    //     // platform/building) or below terrain (underground bunker).
    //     if (zDelta > 100.0 || zDelta < 0.0) { keep }
    //
    // An earlier commit on this branch dropped the `zDelta < 0.0` half on the
    // reasoning that Game_DropToGround (HOOK 0x4149B0, src/game/misc.c:75-97)
    // probes down AND up, so a soldier below the surface should be lifted onto it.
    // That reasoning is still defensible against the engine, but it changed what
    // the user saw -- the AI ended up below the ground -- and the user's report is
    // the authority on what the editor should do here.  Restored verbatim.
    if (zDelta > kSnapSurfaceBandAbove || zDelta < 0.0) return TerrainSnapDecision::KeepAuthoredZ;
    return TerrainSnapDecision::SnapToTerrain;
}

}  // namespace igi
