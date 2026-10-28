#ifndef IGI_RENDERER_LIGHTMAP_REF_INDEX_H
#define IGI_RENDERER_LIGHTMAP_REF_INDEX_H

// Which `.olm` file a lightmapped object's lightmap lives in, and which ref
// index identifies it -- the engine's rule, verbatim.
//
// IGI-RE (faithful decompilation of retail IGI.exe):
//
//   static void fill_refs(int *n, LightmapSet *set, RigidDynCubeObj *o)   src/sector/sector.c:544-563
//   {
//       int i;
//       o->lightmapRefs = &set->refs[*n];
//       for (i = 0; i < ((VirModel *)o->model)->lod_count; i++) {
//           LightmapRef *r = &set->refs[*n];
//           r->lodIndex = i;                                        /* :557 */
//           r->lodCount = ((VirModel *)o->model)->lod_count;        /* :558 */
//           r->lod = ((VirModel *)o->model)->lods[i];               /* :559 */
//           (*n)++;                                                 /* :561 */
//       }
//   }
//
//   Lightmaps *Lightmap_BuildLod(..., const char *name, int index, ...)   src/dirlight/lightmapbuild.c:44-45
//   {
//       MeshHeader *mh = ((ModelLod *)lodp)->mesh->header;
//       if (mh->format != 3) return NULL;                       /* :53  format-3 meshes only */
//       ...
//       crt_sprintf(path, "%s/%s_%05d.olm", Lightmap_GetDir(), name, index);   /* :59 */
//       ...
//       lm->count = mesh->texCount;                             /* :91  one page per mesh page */
//   }
//
// The three rules that follow, each with its oracle line:
//
//   1. One ref per LOD, not one per object. An object with `lod_count` LODs
//      contributes exactly `lod_count` consecutive entries to its set's ref
//      array (sector.c:549-561), each carrying its own `lodIndex` 0..lod_count-1
//      and its own `ModelLod` (sector.c:557-559).
//   2. The `.olm` file name is `<name>_<refIndex>.olm`, where the index is the
//      object's base ref index PLUS the LOD index -- the `index` parameter of
//      Lightmap_BuildLod is the ref's position in the set, and `fill_refs` is
//      what advances it (lightmapbuild.c:59 vs sector.c:561 `(*n)++`).
//   3. An object whose model does not resolve has `lod_count == 0`
//      (VirModel_InitLods, src/model/model.c:145-163) and therefore contributes
//      NO refs and does NOT advance the cursor: the loop body never runs
//      (sector.c:549). Skipping this is the easy way to shift every later
//      object's lightmap by one.
//
// WHICH PAGE INSIDE THE FILE, AND WHAT A LOD WITH NO BAKE DOES — both resolved
// from the draw path, not from the builder:
//
//   Direct3DRender_DrawRigidMesh   HOOK(0x49DF60)   src/render/render.c:1014-1058
//
//       } else if (h->format == 3) {
//           Lightmaps *set;
//           LmMesh *d = h->data;
//           LmGroup *g;
//           if (o->lightmaps && (set = o->lightmaps[lod].lightmaps) != NULL) {   /* :1019 */
//               ...
//               Direct3DRender_UploadLightmapMesh(d, &o->lightmaps[lod]);        /* :1023 */
//               ...
//               IDirect3DDevice7_SetTexture(DEV, 1, set->tex[g->lightmap].texture.surface);  /* :1033 */
//           } else {
//               Direct3DRender_UploadLightmapMesh(d, NULL);                      /* :1052 */
//               use_state(g3DTextureStates, gTexStageIdx);                       /* no atlas bound */
//               use_state(g3DRenderStates, gLitIdx);
//           }
//
//   and the sorted path, Direct3DRender_DrawSortedFaceGroup_Lightmap
//   HOOK(0x49F1C0) src/render/render.c:1213:
//       IDirect3DDevice7_SetTexture(DEV, 1, p->lightmap->tex[g->lightmap].texture.surface);
//
// Three rules follow, each with its oracle line:
//
//   4. The file a drawn LOD samples is `o->lightmaps[lod]` — the ref whose
//      `lodIndex` is the SELECTED LOD (render.c:1019, :1023). A lower LOD uses
//      its OWN `.olm`, never LOD 0's: `RefIndexForLod` above gives its ref index.
//   5. The page inside that file is the drawn group's own `LmGroup.lightmap`
//      (uint16, `0xFFFF` = no page; the group loops test exactly that at
//      src/render/render.c:1114 and :1098). `Lightmaps.count` is the page count
//      (`mesh->texCount`, lightmapbuild.c:91), so the field indexes `set->tex[]`
//      directly.
//   6. When the selected LOD has no `.olm` at all (`set == NULL`) the engine
//      draws that mesh with NO lightmap bound, in the ordinary lit state
//      (render.c:1052-1054). It never falls back to another LOD's atlas — so an
//      editor drawing a lower LOD without its own bake must bind nothing rather
//      than LOD 0's page.

#include <cstdio>
#include <string>

namespace igi {
namespace lightmap {

// One object's slice of a LightmapSet's ref array.
struct RefLayout {
    // Ref index of this object's first LOD, or -1 when the object contributes no
    // refs at all (lod_count <= 0). Not zero: zero is a valid ref index.
    int base = -1;
    // VirModel.lod_count, clamped to the non-negative range the engine's loop
    // bound uses.
    int lod_count = 0;
};

// Reserves `lod_count` refs for one object, advancing `cursor` exactly as
// `(*n)++` does inside fill_refs (src/sector/sector.c:561). `cursor` must start
// at 0 for a fresh set and be threaded through every object in the order
// SectorObject_FillMagicRefs walks them (sector.c:565-575, HOOK 0x4DC050: the task list, then
// each task's children).
inline RefLayout ReserveRefs(int lod_count, int& cursor) {
    RefLayout layout;
    layout.lod_count = lod_count > 0 ? lod_count : 0;
    if (layout.lod_count == 0) {
        // sector.c:549 -- `i < lod_count` is false immediately, so the cursor is
        // left exactly where it was.
        return layout;
    }
    layout.base = cursor;
    cursor += layout.lod_count;
    return layout;
}

// The ref index for LOD `lod_index` of an object laid out as `layout`, or -1 when
// that LOD has no ref. sector.c:557 sets `r->lodIndex = i` for
// `i < lod_count`, so only that range exists.
inline int RefIndexForLod(const RefLayout& layout, int lod_index) {
    if (layout.base < 0 || lod_index < 0 || lod_index >= layout.lod_count) return -1;
    return layout.base + lod_index;
}

// `<name>_%05d.olm`, byte for byte as Lightmap_BuildLod builds it
// (src/dirlight/lightmapbuild.c:59: `"%s/%s_%05d.olm"`, directory omitted).
// `ref_index` must be a non-negative ref index from RefIndexForLod.
inline std::string OlmFileName(const std::string& object_name, int ref_index) {
    if (ref_index < 0) return std::string();
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%s_%05d.olm", object_name.c_str(), ref_index);
    return std::string(buf);
}

// The name an object's first LOD uses, which is the form the loader stores keys
// in (source/renderer/object_lightmap.cpp strips any directory prefix).
inline std::string OlmFileNameForFirstLod(const std::string& object_name, const RefLayout& layout) {
    return OlmFileName(object_name, RefIndexForLod(layout, 0));
}

// Whether the editor can identify the MESH a resolved `.olm` entry belongs to,
// which is what the engine's accept test needs (lightmapbuild.c:63-64 compares
// `mh->version`, `mh->time` and `mesh->texCount` of THAT ref's LOD).
//
// `owner_binding` is true when the resolved file list came from the object's own
// lightmap binding (a task-id match), so the `LightmapInfo` is this object's own
// and ref 0 is this object's LOD 0 (`o->lightmapRefs = &set->refs[*n]`, sector.c:548).
// A list resolved by authored POSITION belongs to an ancestor's binding -- the
// object's model is merely referenced inside that tree -- so its entries are not
// this object's LODs and no mesh can be identified for them.
//
// `entry < owner_lod_count` is the second condition: refs 0..lod_count-1 of the
// set are the owner's own LODs (sector.c:549-561). The refs after them belong to
// the object's magic parts (`SectorObject_FillMagicRefs`, sector.c:565-575) and
// its rigid children (`SectorObject_FillRigidRefs`), whose order the editor does
// not walk -- so for those entries the editor knows the file but not the mesh,
// and must not guess one: a wrong mesh would reject a valid file.
//
// MEASURED over the retail install (all 14 level bundles, 25,337 shipped `.olm`
// files): only 1,120 entries are an owner's own LOD, and all 1,120 pass the four
// conjuncts against that LOD's mesh. The other 24,217 entries are the magic and
// child refs the editor cannot identify. See docs/igi-re-parity/LIGHTMAP_BAKE.md.
inline bool RefMeshIsIdentified(bool owner_binding, int owner_lod_count, int entry) {
    return owner_binding && entry >= 0 && entry < owner_lod_count;
}

// `LmGroup.lightmap` — src/render/render.c:45 (uint16_t) and the `0xFFFF`
// sentinel the group loops test (render.c:1098, :1114).
constexpr int kNoLightmapPage = 0xFFFF;

// The page `set->tex[...]` a drawn render group samples (rule 5 above), or -1
// when the group carries no lightmap page of its own. A group without a page
// keeps the previous group's scales (render.c:1107-1114), which is the engine's
// behaviour and not something a per-submesh texture lookup can express; the
// TEXTURE binding for such a group is the out-of-range read at render.c:1033,
// so the editor binds no atlas for it instead of reproducing undefined behaviour.
//
// Accepts both spellings a caller can hold: the engine's raw 0xFFFF sentinel and
// the editor's already-normalised -1 (`SubMesh.lightmapPage`, source/renderer/model.h).
inline int PageIndexForGroup(int group_lightmap) {
    if (group_lightmap < 0) return -1;
    return group_lightmap == kNoLightmapPage ? -1 : group_lightmap;
}

// The file a drawn LOD samples (rule 4): the ref index for that LOD, or -1 when
// the LOD has no ref at all — in which case the engine binds no atlas
// (rule 6, render.c:1052-1054) and the mesh must be drawn dynamically lit.
inline int LightmapRefForDrawnLod(const RefLayout& layout, int lod_index) {
    return RefIndexForLod(layout, lod_index);
}

// The entry a drawn LOD of the object that OWNS the `LightmapInfo` samples, in
// the group list the editor's resolver returns (`igi1conv
// ResolveLightmapFilesForLogicalId` collects every `<logicalId>_NNNNN.olm` and
// sorts ascending, so entry k is ref k).
//
// `SectorObject_LoadLightmaps` (HOOK 0x4DC3D0, src/sector/sector.c:641-680) fills
// the set in this order — `fill_refs` for `so->s.o` and its sectors first, then
// `SectorObject_FillMagicRefs` for each part's attached magic objects, then
// `SectorObject_FillRigidRefs` over the children — so the owner's refs are
// 0..lod_count-1 and its LOD n atlas is entry n.
//
// MEASURED on the retail corpus (LEVEL1, all 22 groups): every group's ref 0..k-1
// page sizes halve per ref exactly as `(1 << lodIndex)` predicts
// (LightmapInfo_Changed, src/dirlight/lightmapinfo.c:150), e.g. `obj00004` =
// 128x4, 64x2, 32x1, 16x1 for a 4-level model, and `obj00000` = 256x16, 256x8,
// 256x4, 256x2, 64x1 for the 5-level owner followed by 256x2/256x1, 64x1/8x1 and
// 128x4/64x2 for three 2-level dependants.
//
// The refs of the OTHER objects in the set (magic objects, rigid children) start
// at 0 + the owner's lod_count + every preceding object's lod_count, which needs
// `SectorObject_LoadLightmaps`'s walk — see docs/igi-re-parity/LIGHTMAP_REFS.md.
inline int OwnerLightmapEntryForDrawnLod(int lod_index) {
    return lod_index < 0 ? -1 : lod_index;
}

} // namespace lightmap
} // namespace igi

#endif // IGI_RENDERER_LIGHTMAP_REF_INDEX_H
