/******************************************************************************
 * @file    collision_drop_rules.h
 * @brief   The engine's vertical drop query, as pure functions.
 *
 * WHY THIS EXISTS
 *   App::SnapObjectsToTerrain asks Level::GetTerrainZ -- a TERRAIN-only query --
 *   while the engine drops onto COLLISION geometry, which includes building
 *   floors, ramps and the colbox volumes.  Where there is no terrain (a designed
 *   DiscardTerrain hole under a building) the editor's query misses and the rule
 *   keeps the authored QSC Z -- which is the one value the engine would never
 *   draw from.  The user's own log shows the result: `no_terrain=26`, and AI
 *   rendering half above and half below the ground.
 *
 * ENGINE BASIS
 *   * Game_DropToGround HOOK 0x4149B0 (src/game/misc.c:75-97) is the engine's
 *     canonical "put this on the ground":
 *         dz = -1638400;
 *         Coll_DropPoints(world, pos, 1, dz, &hit, NULL);
 *         if (!hit.found) { dz = -dz; Coll_DropPoints(world, pos, 1, dz, &hit, NULL);
 *                           if (!hit.found) return; }
 *         memcpy(&pos->z, &hit.pos.z, sizeof(double));
 *     i.e. probe DOWN, and if that misses probe UP.  There is no band, no clamp
 *     and no default: a miss leaves the authored pose alone.
 *   * Coll_DropPointsMat HOOK 0x4CB9F0 (src/coll/coll.c:813-818) -> Coll_Drop
 *     (src/coll/coll.c:518-616) -> Coll_VerticalHit HOOK 0x4CB050
 *     (src/coll/coll.c:626-774), which sets results[i].found only when a surface
 *     is actually hit.
 *   * The geometry is already in the editor: XTVC collision vertices and ECFC
 *     faces are parsed per model (source/renderer/mef_native.h:40-63, populated
 *     at :153-162), and Direct3DRender_LoadModel HOOK 0x49FB10 fills exactly
 *     those chunks as collision data (src/bonemesh/d3drender.c:204-261).
 *
 * The editor-side divergence is stated rather than hidden: the engine's walk is
 * a cube tree over the whole collision world, while this takes the triangles the
 * caller supplies.  The RULE (down, then up, nearest hit wins, miss changes
 * nothing) is the engine's; the acceleration structure is the caller's.
 *****************************************************************************/
#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

namespace igi {

// One collision triangle in the same space as the query point.
struct DropTriangle {
    double v[3][3];  // v[i][axis]
};

// The engine's probe distance (Game_DropToGround, src/game/misc.c:75-97).
inline constexpr double kDropProbeDistance = 1638400.0;

struct DropHit {
    bool found = false;
    double z = 0.0;
};

// Vertical hit against one triangle.  A triangle is hit when the vertical line
// at (x, y) passes through it and the crossing lies in the probe direction.
// Uses the standard barycentric test on the XY projection; a degenerate
// (zero-area) projection is a miss, exactly as a ray parallel to a face is.
inline bool DropHitsTriangle(const DropTriangle& t, double x, double y, double dz, double& outZ) {
    const double x0 = t.v[0][0], y0 = t.v[0][1], z0 = t.v[0][2];
    const double x1 = t.v[1][0], y1 = t.v[1][1], z1 = t.v[1][2];
    const double x2 = t.v[2][0], y2 = t.v[2][1], z2 = t.v[2][2];

    const double denom = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2);
    if (denom == 0.0) return false;  // degenerate in XY: no vertical crossing

    const double a = ((y1 - y2) * (x - x2) + (x2 - x1) * (y - y2)) / denom;
    const double b = ((y2 - y0) * (x - x2) + (x0 - x2) * (y - y2)) / denom;
    const double c = 1.0 - a - b;
    // Inclusive edges, so a point exactly on a shared edge still hits.
    if (a < 0.0 || b < 0.0 || c < 0.0) return false;

    outZ = a * z0 + b * z1 + c * z2;
    // The probe is one-directional: a surface behind the start is not hit.
    if (dz < 0.0 && outZ > 0.0) return false;   // probe down: only below the origin
    if (dz > 0.0 && outZ < 0.0) return false;   // probe up:   only above it
    return true;
}

// Nearest hit in the probe direction over a set of triangles.  `dz` is negative
// to probe down and positive to probe up, and the point is at local z = 0.
inline DropHit DropOntoTriangles(const std::vector<DropTriangle>& tris, double x, double y,
                                 double dz) {
    DropHit best;
    for (const DropTriangle& t : tris) {
        double z = 0.0;
        if (!DropHitsTriangle(t, x, y, dz, z)) continue;
        if (!best.found ||
            (dz < 0.0 ? z > best.z     // down: the highest surface below wins
                      : z < best.z)) { // up:   the lowest surface above wins
            best.found = true;
            best.z = z;
        }
    }
    return best;
}

// Game_DropToGround's own two-probe rule: down first, then up, and a miss on
// both leaves the position untouched (`found == false`).
inline DropHit DropToGround(const std::vector<DropTriangle>& tris, double x, double y) {
    DropHit hit = DropOntoTriangles(tris, x, y, -kDropProbeDistance);
    if (hit.found) return hit;
    return DropOntoTriangles(tris, x, y, kDropProbeDistance);
}


// ---------------------------------------------------------------------------
// Gathering triangles from parsed collision geometry.
//
// The editor parses XTVC vertices and ECFC faces per model
// (source/renderer/mef_native.h:40-63, populated at :153-162) -- the same chunks
// Direct3DRender_LoadModel HOOK 0x49FB10 fills as collision data
// (src/bonemesh/d3drender.c:204-261).  This turns one model's face list into
// world-space DropTriangles for the drop query.
//
// It takes raw arrays rather than the renderer's types on purpose: the rule stays
// host-testable and free of the renderer, and the caller decides where the bytes
// come from.  `xform` is the object's 4x4 world matrix in COLUMN-MAJOR order, the
// same convention glm uses, and vertices are 3 floats each (the XTVC record's
// leading position; its remaining bytes are not needed by a vertical drop).
// ---------------------------------------------------------------------------
inline void AppendTransformedTriangles(const float* positions, std::size_t positionCount,
                                       const unsigned short* faces, std::size_t faceCount,
                                       const float* xform,
                                       std::vector<DropTriangle>& out) {
    if (positions == nullptr || faces == nullptr || xform == nullptr) return;
    for (std::size_t f = 0; f + 2 < faceCount; f += 3) {
        DropTriangle tri{};
        bool ok = true;
        for (int c = 0; c < 3; ++c) {
            const unsigned short idx = faces[f + c];
            if (static_cast<std::size_t>(idx) >= positionCount) { ok = false; break; }
            const float px = positions[idx * 3 + 0];
            const float py = positions[idx * 3 + 1];
            const float pz = positions[idx * 3 + 2];
            // Column-major 4x4: world = M * (p, 1).
            tri.v[c][0] = xform[0] * px + xform[4] * py + xform[8]  * pz + xform[12];
            tri.v[c][1] = xform[1] * px + xform[5] * py + xform[9]  * pz + xform[13];
            tri.v[c][2] = xform[2] * px + xform[6] * py + xform[10] * pz + xform[14];
        }
        if (ok) out.push_back(tri);
    }
}

}  // namespace igi
