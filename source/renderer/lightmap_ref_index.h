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
// What this header deliberately does NOT decide: which page *inside* the file a
// drawn LOD uses. `lm->count = mesh->texCount` gives the page count, but the key
// the engine indexes `file->tex[i]` by on upload is not established from
// lightmapbuild.c alone, so no rule is asserted here. See
// docs/igi-re-parity/LIGHTMAP_REFS.md.

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

} // namespace lightmap
} // namespace igi

#endif // IGI_RENDERER_LIGHTMAP_REF_INDEX_H
