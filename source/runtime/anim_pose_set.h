#pragma once

// ── Which AI get a POSE on a frame ──────────────────────────────────────────
//
// The engine builds an AI's bone pose only for the objects it is about to DRAW,
// and the test is two-part — `Human_WakeIfVisible` HOOK 0x45F140
// (src/humanbase/human.c:262-287):
//
//     if (!h->frozen) return;                        // posed at most once a frame
//     Viewport_GetCurrentCamera();
//     ... h->pos into VIEW space with gTransContext.matrix ...
//     if (Mesh3D_Exclude(c, h->radius, out)) return; // 0x4D03D0 — frustum test
//     lod = Mesh3D_GetBoneMeshLOD(h);                // 0x4CFF70 — lod_select
//     if (lod != -1 && lod < 4) Human_Wake(h);       // 0x45F240 = BoneObj_Update
//
// and it is reached only from the draw handlers (`Human_DrawHandler` 0x45F280,
// `Human_NearClipHandler` 0x45F050 — src/humanbase/human.c:235-258, 300-320),
// which run over the camera's sector and the sectors its portals reach
// (`Sector_DrawVisible` 0x4DD5D0 -> `Sector_Render` 0x4DD1C0 ->
// `HANDLER(4, o->task.type)`, src/sector/sector.c:928-1057).  The CLOCK, by
// contrast, runs for every ACTIVE AI whether drawn or not (`Soldier_Update`
// 0x45D080 -> `Human_FireAnimTrigger` 0x4613D0 -> `BoneObj_GetEvents` 0x4D3210,
// src/soldier/soldier.c:877, src/humanbase/human.c:1368-1387) — that split is
// `AnimPlayback::Update` vs `AnimPlayback::AdvanceClock`.
//
// The retail EDITOR's own loop has NO oracle in IGI-RE (it decompiles IGI.exe
// only, and the retail editor binary is not in the install), so the editor maps
// the ENGINE's rule onto its own viewport: pose the AI the editor's visual pass
// would DRAW.  That mapping is what makes an AI that is not selected animate at
// all — the editor used to pose the SELECTED object alone, so every other AI
// kept a running clock and a frozen mesh.
//
// The mapping is exact rather than a heuristic, because the editor's visual pass
// (`Renderer_Objects::Draw`) applies the same two tests the engine does:
//
//   * `Mesh3D_Exclude` 0x4D03D0 asks the render mode whether a sphere is
//     outside the view (src/lod/mesh3d.c:285-310).  The editor's stand-in is the
//     conservative view test in `Renderer_Objects::Draw` — reject only a
//     bounding sphere wholly behind the eye plane, or outside a cone of
//     half-angle atan(1/2) ~= 26.57 deg widened by the radius (a >= 120 deg
//     cone, wider than any editor viewport).  `PassesEditorViewCone` below IS
//     that test and the visual pass calls it, so the two cannot drift.
//   * `Mesh3D_GetBoneMeshLOD` 0x4CFF70 is `lod_select` over the model's LOD0
//     header with the BONE detail/cull pair, and -1 means "too far to draw"
//     (src/lod/mesh3d.c:229-246).  Its radius input is `o->radius`, which the
//     engine sets to `VirModel_GetRadius` 0x4D0950 — `BoneDynCubeObj_Init`
//     0x4D9620 and `BoneDynCubeObj_SetModel` 0x4D9700 both do exactly that
//     (src/haze/bonedyncube.c:110-128, 137-147) — i.e. the LOD0 MeshHeader
//     radius (src/lod/mesh3d.c:381-387).  The editor's visual pass makes the
//     same `lod_select` call for the object it is about to draw
//     (`Renderer_Objects::SelectObjectDrawLod`), and the pose set reuses that
//     ONE call, so a posed object is always an object the viewport draws.

#include <cmath>

#include <glm/glm.hpp>

namespace igi {

// The visual pass's own conservative view cull, verbatim
// (Renderer_Objects::Draw — source/renderer/renderer_objects.cpp).  It rejects an
// object only when its whole bounding sphere is behind the eye plane
// (`facing < -bound_radius`) or outside a cone of half-angle atan(1/2) widened
// by the bounding radius (`facing < sqrt(dist_sq) * 0.5 - bound_radius`).
//
// `camera_forward_len` is the pass's own `cameraFwdLen > 0.01f` guard: with no
// usable forward vector the pass draws everything, so this returns true.
//
// `to_object` is `obj.pos - camera.pos` in RAW world units and `bound_radius` is
// the same bounding radius the pass computes for the object
// (`length(mesh.halfExtents) * 40.96 * max(obj.scale, 0.001)`).
inline bool PassesEditorViewCone(const glm::vec3& to_object,
                                 float bound_radius,
                                 const glm::vec3& camera_forward,
                                 float camera_forward_len) {
    if (!(camera_forward_len > 0.01f)) return true;
    const float dist_sq = glm::dot(to_object, to_object);
    const float facing = glm::dot(to_object, camera_forward);
    if (facing < -bound_radius) return false;
    if (dist_sq > bound_radius * bound_radius &&
        facing < std::sqrt(dist_sq) * 0.5f - bound_radius) {
        return false;
    }
    return true;
}

// `Human_WakeIfVisible` 0x45F140's LOD guard is `lod != -1 && lod < 4`
// (src/humanbase/human.c:281-283): -1 is `lod_select`'s "too far to draw"
// (src/lod/mesh3d.c:186-207) and 4 is the coarsest level of VIRMODEL_LODS,
// which retail does NOT wake.  Mirrored here as one named constant so the
// comparison cannot be rewritten by accident.
inline constexpr int kLastPosedLod = 4;

// One object's pose decision for this frame, in the engine's shape:
//
//   * the INSPECTED (selected) object is posed unconditionally.  That is an
//     editor affordance, not an engine rule: the visual pass never culls the
//     selected object either (`keepVisible`, renderer_objects.cpp), and the
//     pre-existing editor behaviour was to pose it even when the view cull or
//     the LOD select would have rejected it.  Keeping it preserves "you can
//     always inspect the animation of the object you selected".
//   * every other object needs BOTH engine tests: the view cone
//     (`Mesh3D_Exclude`'s stand-in) and a drawable LOD
//     (`Mesh3D_GetBoneMeshLOD`'s `lod != -1 && lod < 4`).
//
// `passes_view_cone` and `draw_lod` must come from the visual pass's own tests
// (`PassesEditorViewCone` and `Renderer::GetObjectDrawLod`), never from a
// second, parallel implementation.
inline bool ShouldPoseAnimationNow(bool is_selected, bool passes_view_cone, int draw_lod) {
    if (is_selected) return true;
    return passes_view_cone && draw_lod != -1 && draw_lod < kLastPosedLod;
}

}  // namespace igi
