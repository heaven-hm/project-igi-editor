#ifndef IGI_RENDERER_LIGHTMAP_REF_INDEX_H
#define IGI_RENDERER_LIGHTMAP_REF_INDEX_H

// Lightmap ref index and .olm resolution rules.


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

// Reserves `lod_count` refs for one object, advancing `cursor`.
inline RefLayout ReserveRefs(int lod_count, int& cursor) {
    RefLayout layout;
    layout.lod_count = lod_count > 0 ? lod_count : 0;
    if (layout.lod_count == 0) {
        return layout;
    }
    layout.base = cursor;
    cursor += layout.lod_count;
    return layout;
}

// The ref index for LOD `lod_index` of an object laid out as `layout`, or -1 when
// that LOD has no ref.
inline int RefIndexForLod(const RefLayout& layout, int lod_index) {
    if (layout.base < 0 || lod_index < 0 || lod_index >= layout.lod_count) return -1;
    return layout.base + lod_index;
}

// `<name>_%05d.olm` format.
// `ref_index` must be a non-negative ref index from RefIndexForLod.
inline std::string OlmFileName(const std::string& object_name, int ref_index) {
    if (ref_index < 0) return std::string();
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%s_%05d.olm", object_name.c_str(), ref_index);
    return std::string(buf);
}

// The name an object's first LOD uses.
inline std::string OlmFileNameForFirstLod(const std::string& object_name, const RefLayout& layout) {
    return OlmFileName(object_name, RefIndexForLod(layout, 0));
}

inline bool RefMeshIsIdentified(bool owner_binding, int owner_lod_count, int entry) {
    return owner_binding && entry >= 0 && entry < owner_lod_count;
}

constexpr int kNoLightmapPage = 0xFFFF;

// The page a drawn render group samples, or -1 when the group carries no lightmap page.
inline int PageIndexForGroup(int group_lightmap) {
    if (group_lightmap < 0) return -1;
    return group_lightmap == kNoLightmapPage ? -1 : group_lightmap;
}

// The file a drawn LOD samples: the ref index for that LOD, or -1 when the LOD has no ref.
inline int LightmapRefForDrawnLod(const RefLayout& layout, int lod_index) {
    return RefIndexForLod(layout, lod_index);
}

inline int OwnerLightmapEntryForDrawnLod(int lod_index) {
    return lod_index < 0 ? -1 : lod_index;
}

} // namespace lightmap
} // namespace igi

#endif // IGI_RENDERER_LIGHTMAP_REF_INDEX_H
