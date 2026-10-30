/******************************************************************************
 * @file    collision_world.h
 * @brief   The engine's OBJECT collision world, as a vertical probe.
 *
 * WHY THIS EXISTS
 *   App::SnapObjectsToTerrain asked Level::GetTerrainZ and nothing else, so a
 *   human standing on a placed object (a building floor, a roof, a tower
 *   platform, an invisible colbox) was dropped to the terrain below it.  The
 *   engine does not do that: its ground probe queries the terrain AND every
 *   rigid object in the dynamic-cube trees, and takes the nearest hit.
 *
 * ENGINE BASIS (IGI-RE, /Users/haseeb-mir/Documents/Code/IGI-RE, read-only)
 *
 *   * Human_ProbeGround HOOK 0x462390 (src/humanbase/human.c:1746-1772):
 *         Coll_DropPointsEx(world, hits, pts, 2, dz, ev, &filter, -1, 1);  // :1759
 *         ... if (!hits[0].found) Coll_DropPointsEx(..., dz = +kFarUp, 1, ...); // :1768-1771
 *     The last argument is 1, which reaches Coll_Drop's `unk3` and becomes the
 *     collision-set index (see the mesh note below).  A miss on both probes
 *     leaves `pos` untouched; a hit writes the surface into the FOOT (:1816).
 *
 *   * Coll_Drop HOOK 0x4CABF0 body (src/coll/coll.c:520-615) runs BOTH halves
 *     when `flags` has both bits (the human passes -1):
 *         Coll_LinesEx(t, results, points, ends, count, event, filter,
 *                      flags & 0x7FFFFFFF, unk3);                  // :577
 *         ... terrain: Coll_VerticalHit(...);                       // :591
 *         if (r->found && !(Abs(p.z - h.pos.z) < Abs(p.z - r->pos.z)))
 *             continue;                                             // :593-594
 *     So the object/task hit (`r`, from the line query) and the terrain hit
 *     (`h`) compete on |z - probe|, and the OBJECT hit wins a tie: the terrain
 *     only replaces it when it is strictly nearer.
 *
 *   * Coll_LinesEx HOOK 0x4CDB20 (src/model/coll.c:432-560) walks the dynamic
 *     cube trees 0, 1, 4 and 5 (:497-505) and sends every object a box/line
 *     query (slot 10 for a vertical segment, :502).  Trees 2 and 3 -- the bone
 *     objects (humans and their skinned collision) -- are NOT walked, so a
 *     human never lands on another human's mesh here.
 *
 *   * RigidDynCubeObj_LineHit HOOK 0x4C1840 (src/qasm/rigidhit.c:104-470) is
 *     what a rigid object answers with.  It
 *       - rejects an object with no model or no current LOD (:121-123),
 *       - takes collision set `q->mesh` (:126  `cm = &data->coll[q->mesh]`),
 *       - skips a triangle whose CMAT record's +4 word is not -1 unless the
 *         query asked for all faces (:255-256; q.allFaces is 0 for a drop,
 *         src/model/coll.c:495),
 *       - converts the WORLD segment into model space as
 *             local = (M^T * (p - obj->pos)) / obj->scale[0]        (:170-199)
 *         with M the object's row-major 3x3 matrix, and converts the hit back
 *         as  world = obj->pos + M * (local * obj->scale)            (:415-422).
 *
 *   * The collision mesh is the HSMC chunk of the model's .mef:
 *         Direct3DRender_LoadModel HOOK 0x49FB10 (src/bonemesh/d3drender.c:245-260)
 *     which fills `part[0]` and `part[1]` from the 1st and 2nd CVTX/CFCE/CMAT/
 *     CSPH chunks.  `q->mesh = 1` for the human probe therefore selects the
 *     SECOND set -- the walkable/human set.  Measured on the retail data: the
 *     invisible `colbox2/3/4/66` volumes carry their box ONLY in set 1 and an
 *     empty set 0; a building's set 1 is a coarse one-surface mesh (one CMAT
 *     record, portal 0xFFFF) while set 0 is the multi-material bullet/LOS mesh.
 *
 *   * Units: XTVC positions are engine world units.  The editor divides the
 *     RENDER stream by 40.96 and multiplies it back at draw time
 *     (source/renderer/mef_native.cpp:548, source/renderer/renderer_objects.cpp:1319);
 *     the collision stream is not scaled by the engine's loader at all.  The
 *     corpus case below pins this: the water-tower platform in LEVEL1 lands at
 *     Z = 174487421.875 with the authored sniper at 174487424.
 *
 * WHAT THE EDITOR'S WORLD IS
 *   One mesh per distinct model id (raw model-space vertices/faces, shared by
 *   every placement of that model), plus one instance per placed object (an
 *   object index, a world position, an Euler rotation and the object scale).
 *   A probe transforms the probe SEGMENT into model space exactly as the
 *   engine does, tests the solid triangles, and keeps the nearest hit.  No
 *   spatial acceleration is built beyond what correctness needs: the instance
 *   keeps the world AABB of its mesh, which rejects the objects a vertical
 *   column cannot touch, and the per-triangle box test is the engine's own
 *   (:240-254).
 *
 * WHAT IS NOT REPRODUCED (and why)
 *   * `data->unk28` (:124) is an unknown word in the model data; a non-zero
 *     value would skip an object.  Its writer is not in IGI-RE, so nothing is
 *     assumed about it.
 *   * Qvm_GetArgLine (:327) is a per-triangle QVM script test the engine runs
 *     before accepting a hit.  The editor does not execute object QVM scripts;
 *     static retail geometry has no such line in its object QVM.
 *   * The CSPH sphere walk (:237-260) is an acceleration structure, not a
 *     rule; this module tests the same triangles the walk reaches.
 *   * The 0.1-unit `close`/side tie-break (:330-345) only reorders hits that
 *     lie on the same plane; the returned Z is the plane's.
 *   * Objects that MOVE (doors, lifts, trains, vehicles) are queried at their
 *     AUTHORED pose: the editor has no runtime and no clock in the snap pass.
 *****************************************************************************/
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace igi {

// ---------------------------------------------------------------------------
// The model's collision mesh (HSMC set; RigidCollMesh src/qasm/rigidhit.c:26-37)
// ---------------------------------------------------------------------------

struct CollisionVertex {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

struct CollisionFace {
    uint16_t v[3] = {0, 0, 0};
    uint16_t surface = 0;  // index into surface_portal (CMAT record)
};

// The CMAT record word at +4 that RigidDynCubeObj_LineHit tests (:255): the
// face is solid for a normal query only when it is -1 (0xFFFF).  In the editor's
// TAMC reader this is `TamcRecord::portal`
// (source/renderer/mef_native.h:100-107).
constexpr uint16_t kCollisionSolidPortal = 0xFFFF;

struct CollisionMesh {
    std::vector<CollisionVertex> verts;
    std::vector<CollisionFace> faces;
    std::vector<uint16_t> surface_portal;  // CMAT +4 per surface

    float min[3] = {0.0f, 0.0f, 0.0f};
    float max[3] = {0.0f, 0.0f, 0.0f};

    bool empty() const { return verts.empty() || faces.empty(); }

    // A face is solid for the human drop when its surface's portal is 0xFFFF.
    // An out-of-range surface index is refused rather than read (the engine would
    // read past the chunk; the editor's parsers never do).
    bool FaceIsSolid(const CollisionFace& f) const {
        if (f.surface >= surface_portal.size()) return false;
        return surface_portal[f.surface] == kCollisionSolidPortal;
    }

    std::size_t SolidFaceCount() const {
        std::size_t n = 0;
        for (const CollisionFace& f : faces) {
            if (FaceIsSolid(f)) ++n;
        }
        return n;
    }
};

// ---------------------------------------------------------------------------
// ILFF .mef chunk reader for the collision set
//
// The engine finds the chunks with Chunk_FirstChild / Chunk_Next / Chunk_Find /
// Chunk_Data (src/res/chunk.c:9-81; called at src/bonemesh/d3drender.c:246-260).
// This is the same walk the editor's MEF reader documents
// (source/renderer/mef_native.cpp:95-112): a 0x10-byte chunk header
// {id, size, align, next}, children at (align + 0x13) & ~(align - 1) from the
// chunk, payload at parent + ((align + offset + 0xF) & ~(align - 1)).
// ---------------------------------------------------------------------------

// Parse the `set`-th collision set (0 or 1) of an in-memory .mef.
// Returns false only when the buffer is not a readable ILFF form; a model whose
// requested set is absent/empty returns true with `out.empty()` -- the engine
// treats an empty collision part as no collision, not as an error
// (src/bonemesh/d3drender.c:247-248 "a missing CMSH is a clean return").
bool ParseCollisionMeshSet(const uint8_t* bytes, std::size_t size, int set, CollisionMesh& out);

// ---------------------------------------------------------------------------
// One placed object (RigidDynCubeObj) in the world
// ---------------------------------------------------------------------------

struct CollisionInstance {
    const CollisionMesh* mesh = nullptr;
    int object_index = -1;     // LevelObjects index, for self-exclusion
    double pos[3] = {0.0, 0.0, 0.0};
    float matrix[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};  // row-major, world = M * v
    float scale[3] = {1.0f, 1.0f, 1.0f};
    // World AABB of the transformed mesh, for the column reject.
    double bmin[3] = {0.0, 0.0, 0.0};
    double bmax[3] = {0.0, 0.0, 0.0};
};

// The engine's Matrix_SetEuler (HOOK 0x4B38E0, src/math/matrix.c) composes
// Rz(az) * Ry(ay) * Rx(ax); the editor's canonical transcription is
// source/renderer/igi_transform.h:24-35.  `out` is ROW-major (m[r*3+c] = M[r][c]),
// the layout RigidDynCubeObj_LineHit reads (src/qasm/rigidhit.c:186-189).
inline void CollisionMatrixFromEuler(double ax, double ay, double az, float out[9]) {
    const double cx = std::cos(ax), sx = std::sin(ax);
    const double cy = std::cos(ay), sy = std::sin(ay);
    const double cz = std::cos(az), sz = std::sin(az);
    // Rx
    const double rx[9] = {1, 0, 0, 0, cx, -sx, 0, sx, cx};
    // Ry
    const double ry[9] = {cy, 0, sy, 0, 1, 0, -sy, 0, cy};
    // Rz
    const double rz[9] = {cz, -sz, 0, sz, cz, 0, 0, 0, 1};
    double m[9];
    double t[9];
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            t[r * 3 + c] = ry[r * 3 + 0] * rx[0 * 3 + c] + ry[r * 3 + 1] * rx[1 * 3 + c] +
                           ry[r * 3 + 2] * rx[2 * 3 + c];
        }
    }
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            m[r * 3 + c] = rz[r * 3 + 0] * t[0 * 3 + c] + rz[r * 3 + 1] * t[1 * 3 + c] +
                           rz[r * 3 + 2] * t[2 * 3 + c];
        }
    }
    for (int i = 0; i < 9; ++i) out[i] = static_cast<float>(m[i]);
}

// ---------------------------------------------------------------------------
// The world
// ---------------------------------------------------------------------------

struct CollisionProbeHit {
    bool found = false;
    double x = 0.0, y = 0.0, z = 0.0;  // world-space hit point
    int object_index = -1;             // the instance that answered
    double distance = 0.0;             // |hit - probe| (the engine's Coll_LinesEx nearest test)
};

class CollisionWorld {
public:
    void Clear() {
        instances_.clear();
        meshes_.clear();
    }

    // Copies the mesh under a model key and returns the stored mesh, or nullptr
    // when the key already exists with an empty mesh.  A later AddInstance uses
    // the stored mesh, so every placement of one model shares one copy.
    const CollisionMesh* InternMesh(const std::string& key, CollisionMesh mesh) {
        auto it = meshes_.find(key);
        if (it != meshes_.end()) return it->second.empty() ? nullptr : &it->second;
        auto ins = meshes_.emplace(key, std::move(mesh));
        const CollisionMesh& stored = ins.first->second;
        return stored.empty() ? nullptr : &stored;
    }

    // Adds one placed object.  An empty mesh adds nothing (the engine returns
    // without a hit when the collision part is empty, rigidhit.c:126-128).
    bool AddInstance(int object_index, const CollisionMesh* mesh, const double pos[3],
                     const float matrix[9], float scale) {
        if (mesh == nullptr || mesh->empty()) return false;
        CollisionInstance in;
        in.mesh = mesh;
        in.object_index = object_index;
        for (int i = 0; i < 3; ++i) in.pos[i] = pos[i];
        for (int i = 0; i < 9; ++i) in.matrix[i] = matrix[i];
        in.scale[0] = in.scale[1] = in.scale[2] = scale;

        // World AABB from the eight corners of the model-space box.  The columns
        // that cannot contain a triangle are exactly what it rejects.
        bool first = true;
        for (int c = 0; c < 8; ++c) {
            const float vx = (c & 1) ? mesh->max[0] : mesh->min[0];
            const float vy = (c & 2) ? mesh->max[1] : mesh->min[1];
            const float vz = (c & 4) ? mesh->max[2] : mesh->min[2];
            const double sx = static_cast<double>(vx) * scale;
            const double sy = static_cast<double>(vy) * scale;
            const double sz = static_cast<double>(vz) * scale;
            const double wx = static_cast<double>(matrix[0]) * sx + static_cast<double>(matrix[1]) * sy +
                              static_cast<double>(matrix[2]) * sz + pos[0];
            const double wy = static_cast<double>(matrix[3]) * sx + static_cast<double>(matrix[4]) * sy +
                              static_cast<double>(matrix[5]) * sz + pos[1];
            const double wz = static_cast<double>(matrix[6]) * sx + static_cast<double>(matrix[7]) * sy +
                              static_cast<double>(matrix[8]) * sz + pos[2];
            if (first) {
                in.bmin[0] = in.bmax[0] = wx;
                in.bmin[1] = in.bmax[1] = wy;
                in.bmin[2] = in.bmax[2] = wz;
                first = false;
            } else {
                if (wx < in.bmin[0]) in.bmin[0] = wx;
                if (wx > in.bmax[0]) in.bmax[0] = wx;
                if (wy < in.bmin[1]) in.bmin[1] = wy;
                if (wy > in.bmax[1]) in.bmax[1] = wy;
                if (wz < in.bmin[2]) in.bmin[2] = wz;
                if (wz > in.bmax[2]) in.bmax[2] = wz;
            }
        }
        instances_.push_back(in);
        return true;
    }

    std::size_t instance_count() const { return instances_.size(); }
    std::size_t mesh_count() const { return meshes_.size(); }

    std::size_t triangle_count() const {
        std::size_t n = 0;
        for (const auto& kv : meshes_) n += kv.second.faces.size();
        return n;
    }

    // Bytes retained by the meshes (vertices + faces + surface table + names).
    std::size_t byte_size() const {
        std::size_t n = 0;
        for (const auto& kv : meshes_) {
            n += kv.second.verts.size() * sizeof(CollisionVertex);
            n += kv.second.faces.size() * sizeof(CollisionFace);
            n += kv.second.surface_portal.size() * sizeof(uint16_t);
            n += kv.first.size();
        }
        n += instances_.size() * sizeof(CollisionInstance);
        return n;
    }

    const std::vector<CollisionInstance>& instances() const { return instances_; }

    // A vertical probe at (x, y) starting at `probe_z`, going DOWN when `up` is
    // false and UP when it is true, reaching `range` engine units.  It answers
    // with the NEAREST hit along the segment -- the engine's own order
    // (RigidDynCubeObj_LineHit's `best`, then Coll_LinesEx's DistanceSq compare).
    // `exclude_object` implements the query filter Game_CollFilterSkip
    // (HOOK 0x416AB0, src/game/game.c:883-887): the object the query belongs to
    // never hits itself.
    CollisionProbeHit ProbeVertical(double x, double y, double probe_z, bool up, double range,
                                    int exclude_object = -1) const {
        CollisionProbeHit best;
        if (range < 0.0) range = -range;
        const double end_z = up ? probe_z + range : probe_z - range;

        for (const CollisionInstance& in : instances_) {
            if (in.object_index == exclude_object) continue;
            const CollisionMesh* mesh = in.mesh;
            if (mesh == nullptr || mesh->empty()) continue;

            // Column reject: no triangle can be hit outside the world AABB.
            if (!(x >= in.bmin[0] && x <= in.bmax[0] && y >= in.bmin[1] && y <= in.bmax[1])) {
                continue;
            }
            const double zlo = probe_z < end_z ? probe_z : end_z;
            const double zhi = probe_z < end_z ? end_z : probe_z;
            if (!(in.bmin[2] <= zhi && in.bmax[2] >= zlo)) continue;

            CollisionProbeHit hit = ProbeInstance(in, x, y, probe_z, end_z);
            if (!hit.found) continue;
            if (!best.found || hit.distance < best.distance) best = hit;
        }
        return best;
    }

private:
    // RigidDynCubeObj_LineHit (src/qasm/rigidhit.c:170-260, 305-410): the segment
    // in object space (M^T * (p - pos) / scale[0] on every axis), the per-triangle
    // segment intersection on the plane projection with the largest normal, and
    // the nearest hit converted back with M * (local * scale) + pos.
    static CollisionProbeHit ProbeInstance(const CollisionInstance& in, double x, double y,
                                           double probe_z, double end_z) {
        CollisionProbeHit out;
        const CollisionMesh* mesh = in.mesh;
        const float* m = in.matrix;
        const float s0 = in.scale[0];
        if (!(s0 != 0.0f)) return out;

        // rigidhit.c:170-199: both endpoints into object space.  `long double` in
        // the original, truncated to float on assignment to the walk.
        const double ax = x - in.pos[0];
        const double ay = y - in.pos[1];
        const double az = probe_z - in.pos[2];
        const double bx = ax;
        const double by = ay;
        const double bz = end_z - in.pos[2];

        auto to_local = [&](double wx, double wy, double wz, float& lx, float& ly, float& lz) {
            const float rl_x = static_cast<float>(wz * m[6] + wx * m[0] + wy * m[3]);
            const float rl_y = static_cast<float>(wz * m[7] + wx * m[1] + wy * m[4]);
            const float rl_z = static_cast<float>(wz * m[8] + wx * m[2] + wy * m[5]);
            lx = rl_x / s0;
            ly = rl_y / s0;
            lz = rl_z / s0;
        };
        float p0x = 0, p0y = 0, p0z = 0, p1x = 0, p1y = 0, p1z = 0;
        to_local(ax, ay, az, p0x, p0y, p0z);
        to_local(bx, by, bz, p1x, p1y, p1z);

        const float dx = p1x - p0x;
        const float dy = p1y - p0y;
        const float dz = p1z - p0z;
        const double len2 = static_cast<double>(dx) * dx + static_cast<double>(dy) * dy +
                            static_cast<double>(dz) * dz;
        if (!(len2 > 0.0)) return out;
        const double len = std::sqrt(len2);

        // rigidhit.c:64-70: the segment's box, padded by 0.1.
        const float box_min_x = (p0x < p1x ? p0x : p1x) - 0.1f;
        const float box_max_x = (p0x > p1x ? p0x : p1x) + 0.1f;
        const float box_min_y = (p0y < p1y ? p0y : p1y) - 0.1f;
        const float box_max_y = (p0y > p1y ? p0y : p1y) + 0.1f;
        const float box_min_z = (p0z < p1z ? p0z : p1z) - 0.1f;
        const float box_max_z = (p0z > p1z ? p0z : p1z) + 0.1f;

        double best_dist = 0.0;
        float hx = 0, hy = 0, hz = 0;
        bool found = false;

        for (const CollisionFace& f : mesh->faces) {
            if (!mesh->FaceIsSolid(f)) continue;
            const unsigned i0 = f.v[0], i1 = f.v[1], i2 = f.v[2];
            if (i0 >= mesh->verts.size() || i1 >= mesh->verts.size() || i2 >= mesh->verts.size()) {
                continue;
            }
            const CollisionVertex& v0 = mesh->verts[i0];
            const CollisionVertex& v1 = mesh->verts[i1];
            const CollisionVertex& v2 = mesh->verts[i2];

            // rigidhit.c:238-254: the six box comparisons.
            if (!(box_min_x <= v0.x || box_min_x <= v1.x || box_min_x <= v2.x)) continue;
            if (!(box_min_y <= v0.y || box_min_y <= v1.y || box_min_y <= v2.y)) continue;
            if (!(box_min_z <= v0.z || box_min_z <= v1.z || box_min_z <= v2.z)) continue;
            if (!(v0.x <= box_max_x || v1.x <= box_max_x || v2.x <= box_max_x)) continue;
            if (!(v0.y <= box_max_y || v1.y <= box_max_y || v2.y <= box_max_y)) continue;
            if (!(v0.z <= box_max_z || v1.z <= box_max_z || v2.z <= box_max_z)) continue;

            // rigidhit.c:256-271: n = -(e1 x e2), kept in the original's order.
            const float e1x = v1.x - v0.x, e1y = v1.y - v0.y, e1z = v1.z - v0.z;
            const float e2x = v2.x - v0.x, e2y = v2.y - v0.y, e2z = v2.z - v0.z;
            const float nx = e1z * e2y - e1y * e2z;
            const float ny = -(e1z * e2x - e1x * e2z);
            const float nz = e1y * e2x - e1x * e2y;
            const double den = static_cast<double>(nz) * dz + static_cast<double>(ny) * dy +
                               static_cast<double>(nx) * dx;
            if (den == 0.0) continue;
            const double tl = ((static_cast<double>(v0.z - p0z) * nz) +
                               (static_cast<double>(v0.y - p0y) * ny) +
                               (static_cast<double>(v0.x - p0x) * nx)) / den;
            const float t = static_cast<float>(tl);
            if (tl < 0.0 || t > 1.0f) continue;

            const float px = t * dx + p0x;
            const float py = t * dy + p0y;
            const float pz = t * dz + p0z;

            // rigidhit.c:292-326: barycentrics in the plane the normal faces most.
            const float ax2 = nx < 0.0f ? -nx : nx;
            const float ay2 = ny < 0.0f ? -ny : ny;
            const float az2 = nz < 0.0f ? -nz : nz;
            double a = 0.0, b = 0.0;
            if (ax2 > ay2 && ax2 > az2) {
                if (nx == 0.0f) continue;
                const float wz = pz - v0.z;
                const float wy = py - v0.y;
                b = (wz * e2y - wy * e2z) / nx;
                if (b < 0.0 || b > 1.0) continue;
                if ((e2y < 0.0f ? -e2y : e2y) > (e2z < 0.0f ? -e2z : e2z)) {
                    a = (wy - e1y * b) / e2y;
                } else {
                    if (e2z == 0.0f) continue;
                    a = (wz - e1z * b) / e2z;
                }
            } else if (ay2 > az2) {
                if (ny == 0.0f) continue;
                const float wz = pz - v0.z;
                const float wx = px - v0.x;
                b = (wz * e2x - wx * e2z) / ny;
                if (b < 0.0 || b > 1.0) continue;
                if ((e2x < 0.0f ? -e2x : e2x) > (e2z < 0.0f ? -e2z : e2z)) {
                    a = (wx - e1x * b) / e2x;
                } else {
                    if (e2z == 0.0f) continue;
                    a = (wz - e1z * b) / e2z;
                }
            } else {
                if (nz == 0.0f) continue;
                const float wy = py - v0.y;
                const float wx = px - v0.x;
                b = (wy * e2x - wx * e2y) / nz;
                if (b < 0.0 || b > 1.0) continue;
                if ((e2x < 0.0f ? -e2x : e2x) > (e2y < 0.0f ? -e2y : e2y)) {
                    a = (wx - e1x * b) / e2x;
                } else {
                    if (e2y == 0.0f) continue;
                    a = (wy - e1y * b) / e2y;
                }
            }
            if (a < 0.0 || a + b > 1.0) continue;

            // rigidhit.c:346-360: the nearest hit along the segment wins.
            const double d = static_cast<double>(t) * len;
            if (found && !(d < best_dist)) continue;
            found = true;
            best_dist = d;
            hx = px;
            hy = py;
            hz = pz;
        }
        if (!found) return out;

        // rigidhit.c:415-422: back to world with the per-axis scale and M, then
        // translate.  The object scale is uniform in every static placement, so
        // dividing by scale[0] above is the exact inverse here.
        const double sx = static_cast<double>(hx) * in.scale[0];
        const double sy = static_cast<double>(hy) * in.scale[1];
        const double sz = static_cast<double>(hz) * in.scale[2];
        const float wx = static_cast<float>(sy * m[1] + sz * m[2] + sx * m[0]);
        const float wy = static_cast<float>(sz * m[5] + sx * m[3] + sy * m[4]);
        const float wz = static_cast<float>(sy * m[7] + sz * m[8] + sx * m[6]);

        out.found = true;
        out.x = static_cast<double>(wx) + in.pos[0];
        out.y = static_cast<double>(wy) + in.pos[1];
        out.z = static_cast<double>(wz) + in.pos[2];
        out.object_index = in.object_index;
        out.distance = std::sqrt((out.x - x) * (out.x - x) + (out.y - y) * (out.y - y) +
                                 (out.z - probe_z) * (out.z - probe_z));
        return out;
    }

    std::unordered_map<std::string, CollisionMesh> meshes_;
    std::vector<CollisionInstance> instances_;
};

// ---------------------------------------------------------------------------
// ParseCollisionMeshSet
//
// Chunk ids (src/bonemesh/d3drender.c:53-57; the on-disk bytes read backwards):
//   ID_CVTX 'XTVC'  verts  {float x,y,z; int bone}      16 bytes
//   ID_CFCE 'ECFC'  faces  {u16 a,b,c; u16 surface}      8 bytes
//   ID_CMAT 'TAMC'  mats   {u32; u16 portal; u16; u16 mat; u16} 12 bytes
// RigidDynCubeObj_LineHit reads the portal word at CMAT +4 (:255-256,
// RigidCollFace.unk4) -- the editor's TamcRecord::portal
// (source/renderer/mef_native.h:100-107).
// ---------------------------------------------------------------------------
inline bool ParseCollisionMeshSet(const uint8_t* bytes, std::size_t size, int set, CollisionMesh& out) {
    out = CollisionMesh{};
    if (bytes == nullptr || set < 0 || set > 1 || size < 0x18) return false;

    auto read_u32 = [&](std::size_t off, uint32_t& v) -> bool {
        if (off + 4 > size) return false;
        std::memcpy(&v, bytes + off, 4);
        return true;
    };
    auto is_pow2 = [](uint32_t x) { return x != 0 && (~(x - 1) & x) == x; };

    uint32_t root_align = 0;
    if (!read_u32(8, root_align) || !is_pow2(root_align)) return false;

    // Chunk_FirstChild (src/res/chunk.c:17-32): (align + 0x13) & ~(align - 1).
    std::size_t p = (static_cast<std::size_t>(root_align) + 0x13) & ~(static_cast<std::size_t>(root_align) - 1);

    struct Found {
        std::size_t off[2] = {0, 0};
        uint32_t align[2] = {0, 0};
        uint32_t size[2] = {0, 0};
        int count = 0;
    } verts, faces, mats;

    // The top-level sibling chain, as Chunk_Next walks it (:9-15).
    int guard = 0;
    while (p + 0x10 <= size && guard++ < 4096) {
        uint32_t id = 0, csize = 0, align = 0, next = 0;
        if (!read_u32(p + 0, id) || !read_u32(p + 4, csize) || !read_u32(p + 8, align) ||
            !read_u32(p + 12, next)) {
            return false;
        }
        if (!is_pow2(align) || align > root_align) {
            // Chunk_Data would refuse this chunk (src/res/chunk.c:34-49).
            return false;
        }
        Found* dst = nullptr;
        if (id == 0x43565458u) {        // 'XTVC'
            dst = &verts;
        } else if (id == 0x43464345u) { // 'ECFC'
            dst = &faces;
        } else if (id == 0x434D4154u) { // 'TAMC'
            dst = &mats;
        }
        if (dst != nullptr && dst->count < 2) {
            dst->off[dst->count] = p;
            dst->align[dst->count] = align;
            dst->size[dst->count] = csize;
            dst->count++;
        }
        if (next == 0) break;
        const std::size_t np = p + next;
        if (np <= p || np + 0x10 > size) break;
        p = np;
    }

    // An absent set is an empty collision part, not an error (d3drender.c:247-248).
    if (verts.count <= set || faces.count <= set || mats.count <= set) return true;

    // Chunk_Data (src/res/chunk.c:34-49) with the file root as parent.
    auto data_off = [](std::size_t c, uint32_t align) {
        return (static_cast<std::size_t>(align) + c + 0xF) & ~(static_cast<std::size_t>(align) - 1);
    };
    const std::size_t voff = data_off(verts.off[set], verts.align[set]);
    const std::size_t foff = data_off(faces.off[set], faces.align[set]);
    const std::size_t moff = data_off(mats.off[set], mats.align[set]);
    const std::size_t vcount = verts.size[set] / 16;
    const std::size_t fcount = faces.size[set] / 8;
    const std::size_t mcount = mats.size[set] / 12;
    if (voff + vcount * 16 > size || foff + fcount * 8 > size || moff + mcount * 12 > size) {
        return false;
    }

    out.verts.resize(vcount);
    for (std::size_t i = 0; i < vcount; ++i) {
        std::memcpy(&out.verts[i].x, bytes + voff + i * 16 + 0, 4);
        std::memcpy(&out.verts[i].y, bytes + voff + i * 16 + 4, 4);
        std::memcpy(&out.verts[i].z, bytes + voff + i * 16 + 8, 4);
    }
    out.faces.resize(fcount);
    for (std::size_t i = 0; i < fcount; ++i) {
        std::memcpy(&out.faces[i].v[0], bytes + foff + i * 8 + 0, 2);
        std::memcpy(&out.faces[i].v[1], bytes + foff + i * 8 + 2, 2);
        std::memcpy(&out.faces[i].v[2], bytes + foff + i * 8 + 4, 2);
        std::memcpy(&out.faces[i].surface, bytes + foff + i * 8 + 6, 2);
    }
    out.surface_portal.resize(mcount);
    for (std::size_t i = 0; i < mcount; ++i) {
        std::memcpy(&out.surface_portal[i], bytes + moff + i * 12 + 4, 2);
    }

    if (!out.verts.empty()) {
        out.min[0] = out.max[0] = out.verts[0].x;
        out.min[1] = out.max[1] = out.verts[0].y;
        out.min[2] = out.max[2] = out.verts[0].z;
        for (const CollisionVertex& v : out.verts) {
            if (v.x < out.min[0]) out.min[0] = v.x;
            if (v.x > out.max[0]) out.max[0] = v.x;
            if (v.y < out.min[1]) out.min[1] = v.y;
            if (v.y > out.max[1]) out.max[1] = v.y;
            if (v.z < out.min[2]) out.min[2] = v.z;
            if (v.z > out.max[2]) out.max[2] = v.z;
        }
    }
    return true;
}

}  // namespace igi
