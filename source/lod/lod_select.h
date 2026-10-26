#ifndef IGI_LOD_LOD_SELECT_H
#define IGI_LOD_LOD_SELECT_H

// The per-frame LOD switch — `lod_select` and `ModelObj_GetLOD`, verbatim.
//
// IGI-RE (faithful decompilation of retail IGI.exe):
//
//   static int lod_select(const VirModel *vm, long double dist, long double r,
//                         long double k, float detail, float cull, int warn)   src/lod/mesh3d.c:161-208
//   int ModelObj_GetLOD(RigidMeshObj *o)                                       src/lod/mesh3d.c:209-227
//   HOOK(0x4CFCB0, ModelObj_GetLOD);
//
// The drawn mesh is chosen here, not in the chain rule: `VirModel_InitLods`
// (src/model/model.c:145-163, HOOK 0x4CED50) only builds `vm->lods[]`;
// `Direct3DRender_DrawRigidMesh` (HOOK 0x49DF60, src/render/render.c:930) picks
// which of those LODs is drawn with
//
//       lod = ModelObj_GetLOD(o);                 /* render.c:952 */
//       if (!o->layer) lod = 0;                   /* render.c:953-954 */
//       else if (lod == -1) return;               /* render.c:955-956 */
//       ml = vm->lods[lod];                       /* render.c:957 */
//
// so `-1` means "do not draw" and only the layer-1 objects get the distance
// switch. The `+0xEC` word this reads is `RigidDynCubeObj.hidden`
// (src/magicobj/magicobjtypes.h:30, _Static_assert(sizeof(RigidDynCubeObj) == 0xF0))
// under the gun code's name and `ModelObj.layer` (src/render/render.c:99,156,
// _Static_assert(offsetof(ModelObj, layer) == 0xEC)) here. `RigidDynCubeObj_Init`
// (HOOK 0x4C5710, src/magicobj/magicobj.c:355) sets it to 1, and
// `TransContext_Create` (HOOK 0x497D70, src/transctx/transctx.c:203) makes the main
// camera context `layer = 1`, while `select_layer_view` (src/render/render.c:252-264)
// builds the layer-0 context that `Direct3DRender_DrawBoneMesh`
// (HOOK 0x49F700, src/bonemesh/d3drender.c:126-141) uses for layer-0 objects.
// A placed rigid object — every object the editor draws — is therefore layer 1:
// the distance switch below applies to it, including the `-1` cull.
//
// TWO BRANCHES. `lod_select` picks the LOD from one of two sources:
//
//   1. The AUTHORED table, when `VirModel.unk24` is set. `unk24` is set only by
//      `ModelLODSettings_Changed` (HOOK 0x4EDB60, src/animtrig/lodsettings.c:157-171)
//      for the models a level's `lod.qvm` names, and it writes
//      `VirModel.unk28[i] = dist[i] * 4096` (src/math/mathvars.h:6, world units
//      per metre). `dists[4]` doubles as the cull distance.
//   2. Otherwise the COMPLEXITY RATIOS of the LOD meshes against LOD 0, combined
//      with the level-wide `LODSettings` task (`LODSettings_Changed`,
//      HOOK 0x4EDAA0, src/animtrig/lodsettings.c:129-137: the four detail/cull
//      percentages become factors through `* 0.02`, `blend` is raw and `range` is
//      `* 4096`). The per-LOD number is `MeshHeader.complexity`
//      (src/model/modeltypes.h:22, `lod_header(vm->lods[i])->complexity`).
//
// Both branches are ported. Neither is "the editor's rule": the engine reaches
// branch 2 for every model the level's authored table does not name.
//
// FLOAT DISCIPLINE. The engine's locals are `float e, q, inv, d` and
// `long double t`, and the long double -> float truncations are load-bearing
// (`q = t` truncates before the division; `e` is a float even though its
// initialiser is long double). The port keeps the same types at the same steps.
//
// NOT PORTED HERE: `Mesh3D_GetBoneMeshLOD` (HOOK 0x4CFF70, src/lod/mesh3d.c:229-246)
// and `Mesh3D_GetSplineMeshLOD` (HOOK 0x4D0180, src/lod/mesh3d.c:248-274) — the
// same `lod_select` with `k = 1.0`, a different radius source and the bone/spline
// detail+cull pair. `LodKind` below selects the pair so a caller can use them.

#include <cmath>
#include <cstdint>
#include <string>

#include "lod_chain.h"  // kVirModelLods (src/model/model.h:11)

namespace igi {
namespace lod {

// src/lod/mesh3d.c:84 `static const float kOne = 1.0f;`
constexpr float kOne = 1.0f;

// src/lod/mesh3d.c:144-149 — the largest scale component; a NaN takes the same
// branches as in the original (`!(s[1] >= s[0])` is true for NaN).
inline float MaxScale(const float s[3]) {
    float m = !(s[1] >= s[0]) ? s[0] : s[1];
    return m > s[2] ? m : s[2];
}

// Which object kind is asking. It selects only the detail/cull pair, exactly as
// `ModelObj_GetLOD` (gLodRigidDetail/gLodRigidCull), `Mesh3D_GetBoneMeshLOD`
// (gLodBoneDetail/gLodBoneCull) and `Mesh3D_GetSplineMeshLOD`
// (gLodSplineDetail/gLodSplineCull) do.
enum class LodKind { Rigid, Bone, Spline };

// The level-wide `LODSettings` task AFTER `LODSettings_Changed`
// (src/animtrig/lodsettings.c:129-137). The four percentages are already
// factors (`* 0.02`) and `range` is already in world units (`* 4096`).
struct LevelLodSettings {
    float splineDetail = 0.0f;  // gLodSplineDetail
    float rigidDetail = 0.0f;   // gLodRigidDetail
    float boneDetail = 0.0f;    // gLodBoneDetail
    float splineCull = 0.0f;    // gLodSplineCull
    float rigidCull = 0.0f;     // gLodRigidCull
    float boneCull = 0.0f;      // gLodBoneCull
    float blend = 1.0f;         // gLodBlend, raw
    float range = 4.0f * 4096.0f;  // gLodRange, s->range * 4096

    float detail(LodKind kind) const {
        return kind == LodKind::Bone ? boneDetail : kind == LodKind::Spline ? splineDetail : rigidDetail;
    }
    float cull(LodKind kind) const {
        return kind == LodKind::Bone ? boneCull : kind == LodKind::Spline ? splineCull : rigidCull;
    }
};

// src/animtrig/lodsettings.c:117-127 `LODSettings_Create` — the state a
// `LODSettings` task starts in (blend 1.0, range 4.0, the six percentages 0).
// The six `gLod*Detail/Cull` globals are BSS (0) until `LODSettings_Changed`
// runs, so this is also the global state when a level has no `LODSettings` task.
inline LevelLodSettings DefaultLevelLodSettings() {
    return LevelLodSettings{};
}

// src/animtrig/lodsettings.c:129-137 `LODSettings_Changed`:
//
//     gLodSplineDetail = s->splineDetail * kPercent;   /* kPercent = 0.02, :52 */
//     gLodRigidDetail  = s->rigidDetail  * kPercent;
//     gLodBoneDetail   = s->boneDetail   * kPercent;
//     gLodSplineCull   = s->splineCull   * kPercent;
//     gLodRigidCull    = s->rigidCull    * kPercent;
//     gLodBoneCull     = s->boneCull     * kPercent;
//     gLodBlend        = s->blend;
//     gLodRange        = s->range * k4096;
inline LevelLodSettings ScaleLevelLodSettings(float splineDetail, float rigidDetail, float boneDetail,
                                             float splineCull, float rigidCull, float boneCull,
                                             float blend, float range) {
    constexpr float kPercent = 0.02f;  // src/animtrig/lodsettings.c:52
    constexpr float k4096 = 4096.0f;   // src/animtrig/lodsettings.c:53
    LevelLodSettings s;
    s.splineDetail = splineDetail * kPercent;
    s.rigidDetail = rigidDetail * kPercent;
    s.boneDetail = boneDetail * kPercent;
    s.splineCull = splineCull * kPercent;
    s.rigidCull = rigidCull * kPercent;
    s.boneCull = boneCull * kPercent;
    s.blend = blend;
    s.range = range * k4096;
    return s;
}

// What `lod_select` reads off the `VirModel`.
struct LodSelectInput {
    bool hasAuthoredTable = false;             // VirModel.unk24
    float authoredDistance[kVirModelLods] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};  // VirModel.unk28
    int lodCount = 0;                          // VirModel.lod_count
    // `lod_header(vm->lods[i])->complexity` for i in [0, lodCount). Only the
    // complexity branch reads it; nullptr is treated as "all zero", which makes
    // every ratio 0/0 — a caller that cannot supply it must not take that branch.
    const int* complexity = nullptr;
    // `vm->lods[i]->name`, used by the warning message only. May be nullptr.
    const char* const* lodNames = nullptr;
};

struct LodSelectResult {
    int lod = -1;          // -1 = too far to draw (the engine's `return -1`)
    std::string warning;   // the `Warning_Show` text, when warn was requested
};

// The port of `lod_select` (src/lod/mesh3d.c:161-208). `dist` and `r` are in raw
// world units (4096/metre) — the same units as `authoredDistance` — `k` is
// `gLodScale` (DATA INIT 1.0f, src/lod/mesh3d.c:628; only the profile tool's
// `Mesh3D_SetLodScale` HOOK 0x4D0EE0 ever changes it, src/profile/profile.c:413),
// `detail`/`cull` are the already-scaled `gLod*Detail`/`gLod*Cull` globals and
// `fov` is `gTransContext.fov`.
//
// `fov` is the engine's TransContext field, which is the frustum half-width at
// unit depth: `TransContext_Create` sets `scaleX = gScene.halfW / fov`
// (src/transctx/transctx.c:196-197), so `fov == tan(halfHorizontalFov)`. Pass
// that, not the field of view in degrees.
//
// RELATION TO `igi::lod::SelectAuthoredLod` (source/lod/lod_settings.h): that is
// the same authored branch written against `float dist/radius` and kept as the
// host-tested primitive. This one keeps the engine's long double parameters, so
// the two are not the same function; do not call one from the other.
inline LodSelectResult LodSelect(const LodSelectInput& vm, long double dist, long double r,
                                 long double k, float detail, float cull, float fov, bool warn,
                                 const LevelLodSettings& level = LevelLodSettings{}) {
    LodSelectResult out;
    const float* dists = vm.authoredDistance;
    const int n = vm.lodCount;

    if (vm.hasAuthoredTable) {
        // mesh3d.c:172-184. `d` is a float local assigned a long double
        // expression: fov promotes, and the truncation happens once, after the
        // multiply — not on the inner product.
        const float d = static_cast<float>(fov * ((dist - r) * k));
        if (d > dists[4]) return out;  // -1

        int i = 0;
        // Written exactly as the oracle has it: not `dists[0] == 0.0f`, because
        // the double negation is also true for NaN.
        if (!(dists[0] < 0.0f || dists[0] > 0.0f)) {
            i = n - 1;
            if (i > 1) i = 1;
        }
        while (d > dists[i] && i < n - 1) ++i;
        out.lod = i;
        return out;
    }

    // mesh3d.c:186-207 — the complexity branch.
    const float e = static_cast<float>((level.range - r) * (kOne - level.blend) + r);
    const long double t = (dist - e) * k;
    const float q = static_cast<float>(t);
    if (!(t > 0.0f)) {
        out.lod = 0;
        return out;
    }
    const float inv = kOne / q;
    float thr[kVirModelLods] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    thr[0] = kOne / (detail * e);

    const int c0 = vm.complexity ? vm.complexity[0] : 0;
    for (int i = 1; i < n; ++i) {
        const int ci = vm.complexity ? vm.complexity[i] : 0;
        if (warn && ci > c0) {
            out.warning = "Detail level " + std::to_string(i) +
                          " is more complex than level 0 for model " +
                          (vm.lodNames && vm.lodNames[0] ? vm.lodNames[0] : "") + ".";
        }
        const long double ratio = static_cast<long double>(ci) / static_cast<long double>(c0);
        thr[i] = static_cast<float>(ratio * ratio * thr[0]);
    }

    int i = 0;
    while (i < n - 1 && fov * thr[i] > inv) ++i;
    if (i == n - 1 && !(cull * e >= fov * q)) return out;  // -1
    out.lod = i;
    return out;
}

// The inputs `ModelObj_GetLOD` reads (src/lod/mesh3d.c:209-227). Positions are in
// raw world units and `viewZ` is `gTransContext.matrix.m[6..8]` — the view
// matrix's Z row, which is what makes `dist` a view-space depth.
struct RigidLodQuery {
    bool hasModel = false;                 // `if (!vm) return 0;`
    long double objPos[3] = {0.0L, 0.0L, 0.0L};  // o->pos
    float camPos[3] = {0.0f, 0.0f, 0.0f};        // c->pos
    float viewZ[3] = {0.0f, 0.0f, 0.0f};         // c->matrix.m[6], [7], [8]
    float lod0Center[3] = {0.0f, 0.0f, 0.0f};    // lod_header(vm->lods[0])->center
    float lod0Sphere = 0.0f;                     // lod_header(vm->lods[0])->sphere
    float objScale[3] = {1.0f, 1.0f, 1.0f};      // o->scale
    float fov = 0.0f;                            // gTransContext.fov
    bool layer0 = false;                         // the `!o->layer` guard, render.c:953
};

// src/lod/mesh3d.c:216-222 — the view-space depth of the object's bounding
// sphere centre. Note the asymmetry the oracle has: `x` already carries
// `h->center.x`, while `y` and `z` get theirs added inside the dot product.
inline long double RigidViewDepth(const RigidLodQuery& q) {
    const long double x = q.objPos[0] - q.camPos[0] + q.lod0Center[0];
    const float y = static_cast<float>(q.objPos[1] - q.camPos[1]);
    const float z = static_cast<float>(q.objPos[2] - q.camPos[2]);
    // `((long double)z + h->center.z)` — the promotion to long double happens
    // BEFORE the add, so the sum is not rounded to float first.
    return q.viewZ[2] * (static_cast<long double>(z) + q.lod0Center[2])
           + q.viewZ[1] * (static_cast<long double>(y) + q.lod0Center[1]) + q.viewZ[0] * x;
}

// `ModelObj_GetLOD` (src/lod/mesh3d.c:209-227) followed by the draw-path guard
// (src/render/render.c:952-956):
//
//     lod = ModelObj_GetLOD(o);
//     if (!o->layer) lod = 0;
//     else if (lod == -1) return;
//
// `lod == -1` means "too far away to draw". A caller that is not the engine's
// renderer should treat it as "skip this object this frame", exactly as
// `Direct3DRender_DrawRigidMesh` does.
inline LodSelectResult SelectRigidLod(const RigidLodQuery& q, const LodSelectInput& vm,
                                      const LevelLodSettings& level, long double k = 1.0L) {
    LodSelectResult out;
    if (!q.hasModel || vm.lodCount <= 0) {
        // `if (!vm) return 0;` — and a model whose own name did not resolve has
        // lod_count 0, which `VirModel_Get` reports as not found
        // (src/model/model.c:118-139), so `o->model` is never such a model.
        out.lod = 0;
        return out;
    }
    const long double dist = RigidViewDepth(q);
    const long double r = static_cast<long double>(MaxScale(q.objScale)) * q.lod0Sphere;
    out = LodSelect(vm, dist, r, k, level.detail(LodKind::Rigid), level.cull(LodKind::Rigid), q.fov, true, level);
    if (q.layer0) out.lod = 0;  // render.c:953-954
    return out;
}

} // namespace lod
} // namespace igi

#endif // IGI_LOD_LOD_SELECT_H
