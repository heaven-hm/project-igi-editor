#ifndef IGI_LOD_MEF_MESH_HEADER_H
#define IGI_LOD_MEF_MESH_HEADER_H

// The three `MeshHeader` words `lod_select` reads, taken straight out of a .mef.
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
// The chunk walk is the engine's, not a heuristic: `Chunk_FirstChild`
// (HOOK 0x4B7490, src/res/chunk.c:17-32), `Chunk_Next` (HOOK 0x4B7470,
// src/res/chunk.c:9-15) and `Chunk_Data` (HOOK 0x4B74D0, src/res/chunk.c:34-49),
// which place a child's payload at the first multiple of the child's `align` at
// or after `chunk + 0x10` relative to the parent. Every retail .mef uses
// align 4 (measured 6190/6190 in the retail install).
//
// WHY THIS EXISTS AT ALL: `ParsedGeometry` (source/renderer/mef_native.h) keeps
// the render/collision data but not the MeshHeader words above, and
// mef_native.cpp's own `Resource`/chunk helpers are file-local. Until those
// three words are exposed on `ParsedGeometry`, this is the only way to feed
// `igi::lod::SelectRigidLod` without inventing a radius or a complexity.
// It reads, it never writes, and it is deliberately header-only.

#include <cstdint>
#include <cstring>
#include <vector>

namespace igi {
namespace lod {

struct MefMeshHeader {
    bool valid = false;
    int format = 0;              // MeshHeader.format, 0 rigid / 1 bone / 3 lightmapped
    float center[3] = {0.0f, 0.0f, 0.0f};  // MeshHeader.center
    float sphere = 0.0f;         // MeshHeader.sphere
    int complexity = 0;          // MeshHeader.complexity
};

namespace mef_detail {

constexpr size_t kChunkHeaderSize = 0x10;
constexpr size_t kIlffHeaderSize = 0x14;
constexpr size_t kMeshFormat = 0x20;
constexpr size_t kMeshCenter = 0x30;
constexpr size_t kMeshSphere = 0x3C;
constexpr size_t kMeshComplexity = 0x60;
constexpr size_t kMeshSize = 0x90;

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
        out.format = static_cast<int>(ReadU32(bytes.data() + d + kMeshFormat));
        for (int i = 0; i < 3; ++i) out.center[i] = ReadF32(bytes.data() + d + kMeshCenter + 4 * i);
        out.sphere = ReadF32(bytes.data() + d + kMeshSphere);
        out.complexity = static_cast<int>(ReadU32(bytes.data() + d + kMeshComplexity));
        out.valid = true;
        return out;
    }
    return out;
}

} // namespace lod
} // namespace igi

#endif // IGI_LOD_MEF_MESH_HEADER_H
