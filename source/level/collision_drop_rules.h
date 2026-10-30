/******************************************************************************
 * @file    collision_drop_rules.h
 * @brief   The engine's vertical drop query, as pure functions.
 *
 * WHY THIS EXISTS
 *   App::SnapObjectsToTerrain used to ask Level::GetTerrainZ (a TERRAIN-only
 *   triangle crossing) and then gate the result on an editor-invented 100-unit
 *   band.  The engine has no such band: it drops onto the COLLISION hit under
 *   the point, and a miss leaves the authored pose alone.  The user's log shows
 *   what the band did: `[App] Snap complete. snapped=0 skipped=1843
 *   no_terrain=26` -- no human was ever moved.
 *
 * ENGINE BASIS (IGI-RE, /Users/haseeb-mir/Documents/Code/IGI-RE, read-only)
 *
 *   * Game_DropToGround HOOK 0x4149B0 (src/game/misc.c:75-97) is the engine's
 *     canonical "put this on the ground":
 *         double dz = -1638400.0;                 // :82
 *         Coll_DropPoints(world, &hit, pos, 1, &dz, event, &ignore, -1);   // :87
 *         if (!hit.found) { dz = -dz; Coll_DropPoints(...); if (!hit.found)
 *                           return; }             // :88-93
 *         memcpy(&pos->z, &hit.pos.z, sizeof(double));                     // :95
 *     i.e. probe DOWN, and if that misses probe UP.  There is no band, no clamp
 *     and no default: a miss leaves the authored pose alone.  1638400 units is
 *     400 m at the engine's 4096 units/m (kUnitsToMeters = 2^-12,
 *     src/humanbase/human.c:165).
 *
 *   * Coll_DropPoints HOOK 0x4CABF0 (src/coll/coll.c:617-622) and
 *     Coll_DropPointsMat HOOK 0x4CB9F0 (:813-818) are thin wrappers over
 *     Coll_Drop (:518-616), which is where the drop is actually decided:
 *         int range = Coll_Fistp(dz[i]); if (range < 0) range = -range;     // :587-590
 *         if (!Coll_VerticalHit(&h, &c, &points[i], dz[i] > 0.0, range, ...))
 *             continue;                                                      // :591-592
 *         if (r->found && !(Abs(p.z - h.pos.z) < Abs(p.z - r->pos.z)))
 *             continue;                                                      // :593-594
 *         ... r->pos = h.pos; r->found = 1;                                  // :605-608
 *     So the vertical probe's direction is `dz > 0`, its reach is |dz| rounded
 *     to int, and between two candidates the one nearest to the probe start in
 *     Z wins.  (Coll_Drop also sends event 11 first, which lets a task cancel a
 *     hit at :602-604; the editor has no task world.)
 *
 *   * Coll_VerticalHit HOOK 0x4CB050 (src/coll/coll.c:626-774) walks the
 *     collision octree from the leaf that contains the probe start and rejects a
 *     hit farther than |dz|:
 *         if (Coll_Abs(p->z - hit.z) > range) break;                         // :706-707
 *
 *   * Coll_LeafHit HOOK 0x4CA010 (src/coll/coll.c:290-516) is the leaf rule,
 *     and it is DIRECTIONAL with a slop:
 *         if (!up) { zlim = p->z - base->z + kTenth; best = -inf; }          // :320-322
 *         else     { zlim = p->z - base->z - kTenth; best = +inf; }          // :323-325
 *         if (!up) { if (!(hz >= best) || hz > zlim) continue; }             // :466-468
 *         else     { if (hz > best || !(hz >= zlim)) continue; }             // :469-471
 *     kTenth = 0.1f (:15).  So the DOWN probe keeps the HIGHEST triangle at or
 *     below `p.z + 0.1`, and the UP probe keeps the LOWEST triangle at or above
 *     `p.z - 0.1`.  That 0.1 unit slop (0.024 mm at 4096 units/m) is the only
 *     tolerance in the query; there is no distance window around the point.
 *
 *   * The human's draw Z is this same kind of hit, with a longer reach and the
 *     mid-body probe height: Human_ProbeGround HOOK 0x462390
 *     (src/humanbase/human.c:1674-1857) probes `to + kWallMid - foot[2]` down by
 *     kFarDown = -4096000 and up by kFarUp (:192-193, :1754-1772), and on a hit
 *     sets `to[2] = foot[2] + hits[0].pos.z` (:1816) -- the FOOT, not the mesh's
 *     lowest vertex, is put on the collision surface.
 *
 * The editor-side divergence is stated rather than hidden: the engine's walk is
 * a cube tree over the whole collision world (terrain + dynamic cubes + the
 * objects' XTVC/ECFC collision meshes); the functions below take the triangles
 * the caller can supply.  The RULE (down then up, highest-below / lowest-above,
 * the 0.1 slop, |dz| reach, a miss changes nothing) is the engine's; the
 * acceleration structure and the triangle source are the caller's.
 *****************************************************************************/
#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

namespace igi {

// One collision triangle in the same space and units as the query point.
struct DropTriangle {
    double v[3][3];  // v[i][axis]
};

// The probe distance of Game_DropToGround (src/game/misc.c:82): 1,638,400 units
// = 400 m.  Human_ProbeGround uses 4,096,000 units for its own probe
// (kFarDown/kFarUp, src/humanbase/human.c:192-193).
inline constexpr double kDropProbeDistance = 1638400.0;

// Coll_LeafHit's kTenth (src/coll/coll.c:15): the only tolerance the vertical
// query has.  A surface up to this far on the wrong side of the probe start is
// still a hit of that direction (coll.c:321,324).
inline constexpr double kDropSurfaceSlop = 0.1;

struct DropHit {
    bool found = false;
    double z = 0.0;
};

// Vertical hit against one triangle, probing from `probe_z` (default 0: the
// query point is at the local origin).
//
// The crossing is the engine's own: the vertical line at (x, y) passes through
// the triangle's XY projection, and the hit z is the triangle's plane at that
// point (Coll_LeafHit, src/coll/coll.c:445-465).  A degenerate projection has
// no crossing, exactly like a ray parallel to a face.
//
// `dz` is negative to probe DOWN and positive to probe UP, as in Coll_Drop
// (src/coll/coll.c:591 `dz[i] > 0.0`).  The direction test carries the engine's
// 0.1-unit slop: DOWN hits a surface at or below probe_z + 0.1, UP hits one at
// or above probe_z - 0.1 (src/coll/coll.c:320-326,466-471).
inline bool DropHitsTriangle(const DropTriangle& t, double x, double y, double probe_z,
                             double dz, double& outZ) {
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
    const double local = outZ - probe_z;
    // The probe is one-directional, with Coll_LeafHit's kTenth slop.
    if (dz < 0.0) return local <= kDropSurfaceSlop;   // down: at or below the start
    if (dz > 0.0) return local >= -kDropSurfaceSlop;  // up:   at or above it
    return false;                                     // no reach, no hit
}

// Nearest hit in the probe direction over a set of triangles.
//
// DOWN keeps the HIGHEST surface at or below probe_z + slop; UP keeps the LOWEST
// at or above probe_z - slop (Coll_LeafHit, src/coll/coll.c:466-472).  A hit
// farther than |dz| is rejected (Coll_VerticalHit, src/coll/coll.c:706-707).
inline DropHit DropOntoTriangles(const std::vector<DropTriangle>& tris, double x, double y,
                                 double dz, double probe_z = 0.0) {
    DropHit best;
    const double range = std::fabs(dz);
    for (const DropTriangle& t : tris) {
        double z = 0.0;
        if (!DropHitsTriangle(t, x, y, probe_z, dz, z)) continue;
        if (std::fabs(z - probe_z) > range) continue;  // coll.c:706-707
        if (!best.found ||
            (dz < 0.0 ? z > best.z     // down: the highest surface below wins
                      : z < best.z)) { // up:   the lowest surface above wins
            best.found = true;
            best.z = z;
        }
    }
    return best;
}

// Game_DropToGround's own two-probe rule (src/game/misc.c:82-95): DOWN first,
// then UP, and a miss on both leaves the position untouched (`found == false`).
// `probe_z` is the position being dropped, in the triangles' units.
inline DropHit DropToGround(const std::vector<DropTriangle>& tris, double x, double y,
                            double probe_z = 0.0) {
    DropHit hit = DropOntoTriangles(tris, x, y, -kDropProbeDistance, probe_z);
    if (hit.found) return hit;
    return DropOntoTriangles(tris, x, y, kDropProbeDistance, probe_z);
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
//
// UNITS: the caller must scale the result into the query's units.  XTVC stores
// raw engine units, and the editor draws them through `40.96 * obj.scale`
// (source/renderer/renderer_objects.cpp:1319-1320); a world-space triangle
// therefore needs the same 40.96 factor that the draw applies.
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
