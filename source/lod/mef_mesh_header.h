#ifndef IGI_LOD_MEF_MESH_HEADER_H
#define IGI_LOD_MEF_MESH_HEADER_H

// The `MeshHeader` words the editor reads, taken straight out of a .mef:
// `lod_select`'s format/sphere/complexity, plus the `radius` that
// VirModel_GetRadius returns and the pickup touch range is built from.
//
// IGI-RE (faithful decompilation of retail IGI.exe):
//
//   MeshHeader (src/model/modeltypes.h:8-28, _Static_assert(sizeof == 0x90)):
//       float version;        /* 0x00 */
//       int   time[7];        /* 0x04 */
//       int   format;         /* 0x20  0 rigid, 1 bone, 3 lightmapped */
//       void *data;           /* 0x24 */
//       int   unk28;          /* 0x28 */
//       MeshColl *coll;       /* 0x2C */
//       Vec3f center;         /* 0x30 */
//       float sphere;         /* 0x3C  radius around center */
//       char  unk40[0x20];    /* 0x40 */
//       int   complexity;     /* 0x60 */
//       char  unk64[0x14];    /* 0x64 */
//       float radius;         /* 0x78 */
//       ...
//
//   `ModelObj_GetLOD` (HOOK 0x4CFCB0, src/lod/mesh3d.c:209-227) reads
//   `lod_header(vm->lods[0])->center` and `->sphere` for the distance reference
//   and the bounding radius; `lod_select`'s complexity branch
//   (src/lod/mesh3d.c:194-198) reads `lod_header(vm->lods[i])->complexity` for
//   every LOD; `Direct3DRender_DrawRigidMesh` reads `->format` to choose the
//   rigid (0) or lightmapped (3) draw path (src/render/render.c:976,999).
//
//   `version` (0x00) and `time[7]` (0x04) are the two MeshHeader words of the
//   engine's `.olm` accept test:
//
//       mh->format != 3                     -> no lightmaps at all  (lightmapbuild.c:53-54)
//       !mesh->unk8                         -> no lightmaps at all  (lightmapbuild.c:56)
//       feq(mh->version, file->version)     -> conjunct 2           (lightmapbuild.c:63)
//       DateTime_Compare(mh->time, file->time) < 0 -> conjunct 3    (lightmapbuild.c:64)
//
//   `time` is read ONLY there in IGI-RE (`grep -rn "mh->time" src/` matches
//   lightmapbuild.c:64 alone); the engine never writes it, so the seven ints are
//   authored by the model tool that produced the .mef, in DateTime_Get's order
//   (year, month, day, hour, minute, second, ms -- src/terredit/datetime.c:16-26).
//
//   For format 3, `mh->data` is the RD3D chunk (`Direct3DRender_LoadModel` case 3,
//   HOOK 0x49FB10, src/bonemesh/d3drender.c:224-230: `arg->header->data =
//   find_data(arg->res, ID_RD3D)`), and `Lightmap_BuildLod` views that same
//   payload as a `LightmapMesh` (lightmapbuild.c:55). So:
//
//       LightmapMesh.texCount  == RD3D + 0x04   (litmaptypes.h:139; the accept
//                                               test's conjunct 4, :64)
//       LightmapMesh.unk8      == RD3D + 0x08   (litmaptypes.h:140; the second
//                                               build gate, :56)
//       LightmapMesh.sizes     == RD3D + 0x18   (litmaptypes.h:145) which for
//                                               format 3 is the LTMP chunk
//                                               pointer (d3drender.c:229), i.e.
//                                               the LTMP payload IS the
//                                               `short[4]` page-size array the
//                                               builder indexes at
//                                               lightmapbuild.c:103-107.
//
//   MEASURED on the retail install (2,667 loose .mef files, read-only probe
//   scratch/probe_mef_lm.py): all 2,667 MESH chunks carry version bits
//   0x3DCCCCCD (0.1f, the value `feq` is asked to match at :63); 2,051 are
//   format 3; in all 2,051 the LTMP chunk size is exactly `texCount * 8`, which
//   is what pins `texCount` to RD3D + 0x04 and the LTMP payload to the `short[4]`
//   size array; and 24 of the 2,051 have `unk8 == 0`, the value that makes
//   `Lightmap_BuildLod` return NULL before it ever looks at a file (:56).
//
// The chunk walk is the engine's, not a heuristic: `Chunk_FirstChild`
// (HOOK 0x4B7490, src/res/chunk.c:17-32), `Chunk_Next` (HOOK 0x4B7470,
// src/res/chunk.c:9-15), `Chunk_Data` (HOOK 0x4B74D0, src/res/chunk.c:34-49) and
// `Chunk_Find` (HOOK 0x4B7590, src/res/chunk.c:74-79), which place a child's
// payload at the first multiple of the child's `align` at or after `chunk + 0x10`
// relative to the parent. Every retail .mef uses align 4 (measured 6190/6190 in
// the retail install).
//
// WHY THIS EXISTS AT ALL: `ParsedGeometry` (source/renderer/mef_native.h) keeps
// the render/collision data but not the MeshHeader words above, and
// mef_native.cpp's own `Resource`/chunk helpers are file-local. Until those
// words are exposed on `ParsedGeometry`, this is the only way to feed
// `igi::lod::SelectRigidLod` without inventing a radius or a complexity -- and
// the only way to feed `lightmap_rules::OlmHeaderAccepted` its mesh side.
// It reads, it never writes, and it is deliberately header-only.

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace igi {
namespace lod {

struct MefMeshHeader {
    bool valid = false;
    // MeshHeader.version (0x00) and MeshHeader.time[7] (0x04). Both are the mesh
    // side of the `.olm` accept test: `feq(mh->version, file->version)` and
    // `DateTime_Compare(mh->time, file->time) < 0` (lightmapbuild.c:63-64).
    float version = 0.0f;                 // 0x00
    int time[7] = {0, 0, 0, 0, 0, 0, 0};  // 0x04, DateTime_Get order
    int format = 0;              // MeshHeader.format, 0 rigid / 1 bone / 3 lightmapped
    float center[3] = {0.0f, 0.0f, 0.0f};  // MeshHeader.center
    float sphere = 0.0f;         // MeshHeader.sphere
    int complexity = 0;          // MeshHeader.complexity
    // MeshHeader.radius (0x78, src/model/modeltypes.h:21). This is a DIFFERENT
    // word from `sphere`: `sphere` is the bounding sphere around `center` (used
    // by the LOD distance test, src/lod/mesh3d.c:224), while `radius` is the
    // value VirModel_GetRadius 0x4D0950 returns (src/lod/mesh3d.c:381-387) and
    // is therefore what the pickup touch range is built from
    // (src/comprun/pickup.c:172-177). Measured in the retail install: the UZI
    // model 111_01_1 has radius 1268.1 but sphere 973.1.
    float radius = 0.0f;         // MeshHeader.radius
    // The format-3 `LightmapMesh` view of the RD3D payload (lightmapbuild.c:55,
    // litmaptypes.h:138-150). `has_lightmap_mesh` is true only when the file is
    // format 3 AND carries an RD3D chunk large enough to hold those words; for
    // every other format the two numbers below are not lightmap data and stay 0.
    bool has_lightmap_mesh = false;
    int lightmap_tex_count = 0;  // LightmapMesh.texCount = RD3D + 0x04 (accept conjunct 4)
    int lightmap_unk8 = 0;       // LightmapMesh.unk8     = RD3D + 0x08 (build gate, :56)
};

namespace mef_detail {

constexpr size_t kChunkHeaderSize = 0x10;
constexpr size_t kIlffHeaderSize = 0x14;
// MeshHeader (src/model/modeltypes.h:8-28): version 0x00, time[7] 0x04, format 0x20.
constexpr size_t kMeshVersion = 0x00;
constexpr size_t kMeshTime = 0x04;
constexpr size_t kMeshTimeCount = 7;
constexpr size_t kMeshFormat = 0x20;
constexpr size_t kMeshCenter = 0x30;
constexpr size_t kMeshSphere = 0x3C;
constexpr size_t kMeshComplexity = 0x60;
constexpr size_t kMeshRadius = 0x78;
constexpr size_t kMeshSize = 0x90;
// The format-3 RD3D chunk (`ID_RD3D` 0x52443344, "D3DR" on disk;
// src/bonemesh/d3drender.c:52 and the assignment at :225) viewed as the
// `LightmapMesh` that `Lightmap_BuildLod` casts `mh->data` to
// (src/litmap/litmaptypes.h:138-150): texCount at +0x04, unk8 at +0x08.
// MeshRender is 0x38 bytes (src/bonemesh/bonemesh.h:18-31), so the two words are
// in range for any well-formed chunk.
constexpr uint32_t kRd3dChunkId = 0x52443344;
constexpr size_t kRd3dTexCount = 0x04;
constexpr size_t kRd3dUnk8 = 0x08;
constexpr size_t kRd3dSize = 0x38;
// The LTMP chunk (`ID_LTMP` 0x4C544D50, "PMTL" on disk; src/bonemesh/d3drender.c:52
// and the assignment at :229), whose payload is the `short[4]` page-size array
// `LightmapMesh.sizes` points at for format 3 (litmaptypes.h:145,
// lightmapbuild.c:103).
constexpr uint32_t kLtmpChunkId = 0x4C544D50;

inline uint32_t ReadU32(const uint8_t* p) {
    uint32_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

inline float ReadF32(const uint8_t* p) {
    const uint32_t bits = ReadU32(p);
    float v = 0.0f;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

inline bool IsPow2(uint32_t x) { return (~(x - 1) & x) == x; }  // src/res/chunk.c:7

struct Walker {
    const uint8_t* b;
    size_t n;

    bool has(size_t off, size_t len) const { return off <= n && len <= n - off; }

    size_t firstChild(size_t c) const {
        if (!has(c, kChunkHeaderSize)) return kNo;
        const uint32_t size = ReadU32(b + c + 0x04);
        if (!size) return kNo;
        const uint32_t align = ReadU32(b + c + 0x08);
        if (!IsPow2(align)) return kNo;
        const uint32_t off = (align + 0x13) & ~(align - 1);
        return off < size ? c + off : kNo;
    }

    size_t next(size_t c) const {
        if (c == kNo || !has(c, kChunkHeaderSize)) return kNo;
        const uint32_t link = ReadU32(b + c + 0x0C);
        if (!link) return kNo;
        const size_t out = c + link;
        return has(out, kChunkHeaderSize) ? out : kNo;
    }

    // Chunk_Data: payload of child `c` inside `parent`.
    size_t data(size_t c, size_t parent) const {
        if (parent == kNo || c == kNo) return kNo;
        const size_t off = c - parent;
        if (!IsPow2(ReadU32(b + parent + 0x08)) || off >= ReadU32(b + parent + 0x04)) return kNo;
        const uint32_t align = ReadU32(b + c + 0x08);
        if (!IsPow2(align) || align > ReadU32(b + parent + 0x08)) return kNo;
        if (!ReadU32(b + c + 0x04)) return kNo;
        return parent + ((align + off + 0xF) & ~(static_cast<size_t>(align) - 1));
    }

    // Chunk_Find (HOOK 0x4B7590, src/res/chunk.c:74-79) over the children of
    // `parent`: the first sibling whose four-character id matches, as the payload
    // offset Chunk_Data gives it.
    size_t find(uint32_t id, size_t parent) const {
        for (size_t c = firstChild(parent); c != kNo; c = next(c)) {
            if (ReadU32(b + c) == id) return data(c, parent);
        }
        return kNo;
    }

    static constexpr size_t kNo = static_cast<size_t>(-1);
};

} // namespace mef_detail

// The MESH ("HSEM") chunk's header words, or `valid == false` when the buffer is
// not an ILFF container or carries no MESH chunk.
inline MefMeshHeader ReadMefMeshHeader(const std::vector<uint8_t>& bytes) {
    using namespace mef_detail;
    MefMeshHeader out;
    if (bytes.size() < kIlffHeaderSize || std::memcmp(bytes.data(), "ILFF", 4) != 0) return out;

    Walker w{bytes.data(), bytes.size()};
    for (size_t c = w.firstChild(0); c != Walker::kNo; c = w.next(c)) {
        if (std::memcmp(bytes.data() + c, "HSEM", 4) != 0) continue;  // MEF_MESH
        const size_t d = w.data(c, 0);
        if (d == Walker::kNo || !w.has(d, kMeshSize)) return out;
        // MeshHeader.version (0x00) and time[7] (0x04): the two mesh-side words of
        // the `.olm` accept test (lightmapbuild.c:63-64).
        out.version = ReadF32(bytes.data() + d + kMeshVersion);
        for (size_t i = 0; i < kMeshTimeCount; ++i)
            out.time[i] = static_cast<int>(ReadU32(bytes.data() + d + kMeshTime + 4 * i));
        out.format = static_cast<int>(ReadU32(bytes.data() + d + kMeshFormat));
        for (int i = 0; i < 3; ++i) out.center[i] = ReadF32(bytes.data() + d + kMeshCenter + 4 * i);
        out.sphere = ReadF32(bytes.data() + d + kMeshSphere);
        out.complexity = static_cast<int>(ReadU32(bytes.data() + d + kMeshComplexity));
        out.radius = ReadF32(bytes.data() + d + kMeshRadius);
        out.valid = true;

        // Format 3 only: `mh->data` is the RD3D chunk (Direct3DRender_LoadModel
        // case 3, HOOK 0x49FB10, src/bonemesh/d3drender.c:224-230) and
        // Lightmap_BuildLod views it as a LightmapMesh (lightmapbuild.c:55), so
        // texCount is RD3D + 0x04 and the second build gate unk8 is RD3D + 0x08
        // (src/litmap/litmaptypes.h:138-150). The walk is the engine's
        // Chunk_Find/Chunk_Data over the form's direct children.
        if (out.format == 3) {
            const size_t rd3d = w.find(kRd3dChunkId, 0);
            if (rd3d != Walker::kNo && w.has(rd3d, kRd3dSize)) {
                out.lightmap_tex_count =
                    static_cast<int>(ReadU32(bytes.data() + rd3d + kRd3dTexCount));
                out.lightmap_unk8 = static_cast<int>(ReadU32(bytes.data() + rd3d + kRd3dUnk8));
                out.has_lightmap_mesh = true;
            }
        }
        return out;
    }
    return out;
}

// The format-3 page-size array `Lightmap_BuildLod` reads as `mesh->sizes[i]`
// (src/dirlight/lightmapbuild.c:103-107, which copies `size[0]`/`size[1]` into
// `t->width`/`t->height`). In the engine that array IS the LTMP chunk payload:
// `LightmapMesh.sizes` is at +0x18 of the RD3D payload
// (src/litmap/litmaptypes.h:145) and for format 3 `Direct3DRender_LoadModel`
// stores `r->unk18 = find_data(arg->res, ID_LTMP)` there
// (HOOK 0x49FB10, src/bonemesh/d3drender.c:229) -- so the chunk holds `texCount`
// records of `short[4]` and nothing else. MEASURED: in all 2,051 format-3 .mef
// files of the retail install the LTMP chunk size is exactly `texCount * 8`.
//
// Used by the corpus test to pair a shipped `.olm` with the mesh that baked it
// (the `.olm`'s page records are copies of these sizes). Returns false when the
// buffer is not a format-3 .mef with an LTMP chunk.
inline bool ReadMefLightmapPageSizes(const std::vector<uint8_t>& bytes,
                                     std::vector<std::array<int16_t, 2>>* out) {
    using namespace mef_detail;
    if (out == nullptr) return false;
    out->clear();
    const MefMeshHeader header = ReadMefMeshHeader(bytes);
    if (!header.valid || !header.has_lightmap_mesh || header.lightmap_tex_count < 0) return false;
    if (bytes.size() < kIlffHeaderSize || std::memcmp(bytes.data(), "ILFF", 4) != 0) return false;

    Walker w{bytes.data(), bytes.size()};
    const size_t ltmp_chunk = [&] {
        for (size_t c = w.firstChild(0); c != Walker::kNo; c = w.next(c))
            if (ReadU32(bytes.data() + c) == kLtmpChunkId) return c;
        return Walker::kNo;
    }();
    if (ltmp_chunk == Walker::kNo) return false;
    const size_t d = w.data(ltmp_chunk, 0);
    const size_t chunk_size = ReadU32(bytes.data() + ltmp_chunk + 0x04);
    if (d == Walker::kNo) return false;
    // One record per page: `texCount` records of four shorts.
    if (chunk_size < static_cast<size_t>(header.lightmap_tex_count) * 8u) return false;
    if (!w.has(d, static_cast<size_t>(header.lightmap_tex_count) * 8u)) return false;
    out->reserve(static_cast<size_t>(header.lightmap_tex_count));
    for (int i = 0; i < header.lightmap_tex_count; ++i) {
        const uint8_t* p = bytes.data() + d + static_cast<size_t>(i) * 8u;
        std::array<int16_t, 2> size{};
        std::memcpy(&size[0], p, 2);
        std::memcpy(&size[1], p + 2, 2);
        out->push_back(size);
    }
    return true;
}

} // namespace lod
} // namespace igi

#endif // IGI_LOD_MEF_MESH_HEADER_H
