// Binary MEF reader — a faithful C++ transcription of the retail IGI loader.
//
// Oracle: /Users/haseeb-mir/Documents/Code/IGI-RE (decompiled retail game).  Every
// structural rule below is copied from it: same constants, same order of
// operations, same branches, same early-outs.
//
//   Direct3DRender_LoadModel   HOOK 0x49FB10  src/bonemesh/d3drender.c:204-261
//   Direct3DRender_UnloadModel HOOK 0x49FE60  src/bonemesh/d3drender.c:263-279
//   Mesh_Load                  HOOK 0x4CF390  src/model/model.c:347-405
//   Chunk_Next                 HOOK 0x4B7470  src/res/chunk.c:9-15
//   Chunk_FirstChild           HOOK 0x4B7490  src/res/chunk.c:17-32
//   Chunk_Data                 HOOK 0x4B74D0  src/res/chunk.c:34-49
//   Chunk_Find                 HOOK 0x4B7590  src/res/chunk.c:74-81
//   gChunkError                BSS 0xA43E5C  src/res/chunk.c:84
//   MeshGroup                  src/render/rendertypes.h:6-18
//   LmGroup                    src/render/render.c:40-54
//   SkinVertex                 src/dxinit/d3drender.c:37-44 (sizeof 0x28)
//   BoneMesh_Skin              HOOK 0x49B700  src/dxinit/d3drender.c:226-313
//   MeshBatch_Add              HOOK 0x49B430  src/dxinit/d3drender.c:140-152
//   MeshBatch_Flush            HOOK 0x49B4B0  src/dxinit/d3drender.c:154-223
//   gMeshFVF                   src/dxinit/d3drender.c:359 (274 / 530 / 274)
//   rigid draw walk (format 0) src/render/render.c:983-1005
//   lightmap walk  (format 3)  src/render/render.c:1018-1120
//   bone draw walk (format 1)  src/bonemesh/d3drender.c:163-183
//   collision parts            src/bonemesh/d3drender.c:245-260
//   MeshHeader                 src/model/modeltypes.h:8-28
//   MeshRender                 src/bonemesh/bonemesh.h:18-31
//   MeshColl                   src/bonemesh/bonemesh.h:33-43
//   mesh chunk ids             src/model/model.h:26-32, src/bonemesh/d3drender.c:49-57
//
// Host-side divergences from the engine are marked "HOST:" and are limited to
// (a) memory safety — the engine reads a mapped executable image, this reader
// must not read past a std::vector — and (b) replacing the engine's
// `Error_Show(...); for(;;);` fatal handler with an exception, because a hang is
// not an acceptable outcome for an editor.  The fatal *conditions*, their order
// and their messages are identical.

#include "mef_native.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "renderer/hardcoded_bones.h"

namespace {

// ---------------------------------------------------------------------------
// Chunk ids
//
// The ids are the engine's own constants: a 4-byte ASCII tag read as a
// little-endian uint32 (CHUNK_ID, src/res/resource.h:40), so the on-disk tag is
// the reverse of the engine's symbolic name.
// ---------------------------------------------------------------------------

constexpr uint32_t ChunkTag(char a, char b, char c, char d)
{
    return static_cast<uint32_t>(static_cast<uint8_t>(a))
        | (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8)
        | (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16)
        | (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
}

/* .mef chunk ids — src/model/model.h:26-32 */
constexpr uint32_t kIdMesh = 0x4D455348; /* MEF_MESH, "HSEM" on disk */
constexpr uint32_t kIdAtta = 0x41545441; /* MEF_ATTA, "ATTA" on disk */
constexpr uint32_t kIdMvtx = 0x4D565458; /* MEF_MVTX, "XTVM" on disk */
constexpr uint32_t kIdPort = 0x504F5254; /* MEF_PORT, "TROP" on disk */
constexpr uint32_t kIdPtfc = 0x50544643; /* MEF_PTFC, "CFTP" on disk */
constexpr uint32_t kIdPtvx = 0x50545658; /* MEF_PTVX, "XVTP" on disk */

/* RD3D stream ids — src/bonemesh/d3drender.c:49-53 */
constexpr uint32_t kIdRd3d = 0x52443344; /* ID_RD3D, "D3DR" on disk */
constexpr uint32_t kIdVrtx = 0x56525458; /* ID_VRTX, "XTRV" on disk */
constexpr uint32_t kIdRend = 0x52454E44; /* ID_REND, "DNER" on disk */
constexpr uint32_t kIdLtmp = 0x4C544D50; /* ID_LTMP, "PMTL" on disk */

/* collision stream ids — src/bonemesh/d3drender.c:54-57 */
constexpr uint32_t kIdCmsh = 0x434D5348; /* ID_CMSH, "HSMC" on disk */
constexpr uint32_t kIdCvtx = 0x43565458; /* ID_CVTX, "XTVC" on disk */
constexpr uint32_t kIdCfce = 0x43464345; /* ID_CFCE, "ECFC" on disk */
constexpr uint32_t kIdCmat = 0x434D4154; /* ID_CMAT, "TAMC" on disk */
constexpr uint32_t kIdCsphere = 0x43535048; /* ID_CSPH, "HPSC" on disk */

static_assert(kIdMesh == ChunkTag('H', 'S', 'E', 'M'), "MEF_MESH is \"HSEM\" on disk");
static_assert(kIdVrtx == ChunkTag('X', 'T', 'R', 'V'), "ID_VRTX is \"XTRV\" on disk");
static_assert(kIdRend == ChunkTag('D', 'N', 'E', 'R'), "ID_REND is \"DNER\" on disk");
static_assert(kIdLtmp == ChunkTag('P', 'M', 'T', 'L'), "ID_LTMP is \"PMTL\" on disk");
static_assert(kIdCvtx == ChunkTag('X', 'T', 'V', 'C'), "ID_CVTX is \"XTVC\" on disk");
static_assert(kIdCmsh == ChunkTag('H', 'S', 'M', 'C'), "ID_CMSH is \"HSMC\" on disk");

/* ---------------------------------------------------------------------------
 * Chunk layout (src/res/resource.h:31-37)
 *
 *   struct Chunk { unsigned id; unsigned size; unsigned align; unsigned next; unsigned form; };
 *
 * An ILFF form carries all five words (0x14 bytes); Chunk_Data places every
 * other chunk's payload at the first multiple of `align` at or after
 * (chunk + 0x10) relative to the parent — 0x10 for align 4, the only alignment
 * any retail .mef uses (measured: 6190/6190 members of the retail install).
 * ------------------------------------------------------------------------- */
constexpr size_t kChunkHeaderSize = 0x10;
constexpr size_t kIlffHeaderSize = 0x14; /* ILFF form: header + form word */

/* ---------------------------------------------------------------------------
 * MeshHeader — src/model/modeltypes.h:8-28 (sizeof 0x90).  Mesh_Load
 * (HOOK 0x4CF390, src/model/model.c:375-378) stores five further chunk pointers
 * at +0x88..+0x98, so the on-disk MESH chunk is 0x9C = 156 bytes (measured:
 * 156 in 6190/6190 retail members; mef_writer writes 156):
 *
 *   +0x00 float version      +0x20 int   format      +0x24 void *data (RD3D)
 *   +0x28 int   unk28        +0x2C ptr   coll        +0x30 Vec3f center
 *   +0x3C float sphere       +0x78 float radius      +0x7C int16 vertex_count
 *   +0x7E int16 attach_count +0x88 ptr   MVTX        +0x8C ptr   ATTA
 *   +0x90 ptr   PTFC         +0x94 ptr   PTVX        +0x98 ptr   PORT
 *
 * The reader locates MVTX/ATTA/PTFC/PTVX/PORT by chunk id rather than through
 * those header words, which is the same lookup (find_data).
 * ------------------------------------------------------------------------- */
constexpr size_t kMeshVersion = 0x00; /* float, must be 0.1f exactly */
constexpr size_t kMeshFormat = 0x20; /* int: 0 rigid, 1 bone, 3 lightmapped */
constexpr size_t kMeshUnk28 = 0x28; /* int: non-zero is fatal */

/* Mesh_Load HOOK 0x4CF390, src/model/model.c:367: the version word must be
 * exactly 0.1f, compared as the raw 32-bit pattern 0x3DCCCCCD. */
constexpr uint32_t kMeshVersionBits = 0x3DCCCCCD;

/* ---------------------------------------------------------------------------
 * MeshRender — src/bonemesh/bonemesh.h:18-31 (sizeof 0x38).
 *
 * THE TRAP: one word means different streams per format.
 *   unkC  format 0: vertex count        format 1: extra-influence count
 *   unk10 format 0: vertices (VRTX)     format 1: base vertex count   format 3: vertex count
 *   unk14 format 0: groups (REND)       format 3: vertices (VRTX)
 *   unk18 format 1: vertices (VRTX)     format 3: LTMP
 *   unk1C format 1/3: groups (REND)
 * A "vertex chunk / index chunk" field pair cannot represent it: unk14 is the
 * group stream in format 0 and the vertex stream in format 3.  Only unkC and
 * unk10 are on-disk words the loader reads (+0x0C, +0x10); the four pointer
 * words are assigned by the switch below and are modelled as MeshRenderView
 * members named after their engine offsets.
 * ------------------------------------------------------------------------- */
constexpr size_t kRenderUnkC = 0x0C;
constexpr size_t kRenderUnk10 = 0x10;

/* vertex strides — MeshBatch_Flush HOOK 0x49B4B0, src/dxinit/d3drender.c:179-211
 *   case 0 (format 0): memcpy of unkC vertices * 0x20, rec->stride = 0x20
 *   case 3 (format 3): memcpy of unk10 vertices * 0x28, rec->stride = 0x28
 *   case 1 (format 1): no batch; BoneMesh_Skin reads SkinVertex (0x28) */
constexpr uint32_t kRigidVertexStride = 0x20; /* gMeshFVF[0] 274 = XYZ|NORMAL|TEX1 */
constexpr uint32_t kLightmapVertexStride = 0x28; /* gMeshFVF[1] 530 = XYZ|NORMAL|TEX2 */
constexpr uint32_t kSkinVertexStride = 0x28; /* sizeof(SkinVertex) */

/* group records — MeshGroup (rendertypes.h:6-18), LmGroup (render.c:40-54) */
constexpr size_t kMeshGroupSize = 0x1C;
constexpr size_t kLmGroupSize = 0x20;
constexpr uint16_t kLastGroup = 0xFFFF; /* `next` terminator */
constexpr uint16_t kNoTexture = 0xFFFF; /* g->texture == 0xFFFF means untextured */

/* collision records */
constexpr size_t kCvtxVertexSize = 0x10;
constexpr size_t kCfceFaceSize = 0x08;
constexpr size_t kCmatSize = 0x0C;
constexpr size_t kAttaRecordSize = 0x44;

constexpr size_t kNoChunk = static_cast<size_t>(-1);

/* ---------------------------------------------------------------------------
 * Chunk layer — a transcription of src/res/chunk.c over a byte buffer.
 *
 * Every function keeps the engine's arithmetic and its null semantics; the only
 * additions are bounds checks that set chunkError_ (the host analogue of
 * gChunkError, BSS 0xA43E5C) and stop a walk instead of following a corrupt
 * link out of the buffer.
 * ------------------------------------------------------------------------- */
class Resource {
public:
    Resource(const std::vector<uint8_t>& bytes, std::string source)
        : bytes_(bytes)
        , source_(std::move(source))
    {
    }

    const std::string& source() const { return source_; }
    bool chunkError() const { return chunkError_; }
    void setChunkError() const { chunkError_ = true; }
    size_t size() const { return bytes_.size(); }

    bool contains(size_t off, size_t len) const
    {
        return off <= bytes_.size() && len <= bytes_.size() - off;
    }

    /* The engine dereferences chunk payloads unchecked; a truncated file is a
     * HOST-only error, reported instead of read out of bounds. */
    void require(size_t off, size_t len, const char* what) const
    {
        if (!contains(off, len))
            throw std::runtime_error(std::string("MEF truncated: ") + what + " in " + source_);
    }

    const uint8_t* ptr(size_t off, size_t len, const char* what) const
    {
        require(off, len, what);
        return bytes_.data() + off;
    }

    uint8_t u8(size_t off) const
    {
        require(off, 1, "byte read");
        return bytes_[off];
    }

    uint16_t u16(size_t off) const
    {
        uint16_t v = 0;
        std::memcpy(&v, ptr(off, sizeof(v), "word read"), sizeof(v));
        return v;
    }

    int16_t i16(size_t off) const { return static_cast<int16_t>(u16(off)); }

    uint32_t u32(size_t off) const
    {
        uint32_t v = 0;
        std::memcpy(&v, ptr(off, sizeof(v), "dword read"), sizeof(v));
        return v;
    }

    int32_t i32(size_t off) const { return static_cast<int32_t>(u32(off)); }

    float f32(size_t off) const
    {
        const uint32_t bits = u32(off);
        float v = 0.0f;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }

    /* ---- src/res/chunk.c ------------------------------------------------ */

    /* chunk.c:7 */
    static bool IsPow2(uint32_t x) { return (~(x - 1) & x) == x; }

    bool tryU32(size_t off, uint32_t* out) const
    {
        if (!contains(off, 4))
            return false;
        std::memcpy(out, bytes_.data() + off, sizeof(*out));
        return true;
    }

    uint32_t chunkId(size_t c) const
    {
        uint32_t v = 0;
        if (!tryU32(c + 0x00, &v)) {
            setChunkError();
            return 0;
        }
        return v;
    }

    uint32_t chunkSize(size_t c) const
    {
        uint32_t v = 0;
        if (!tryU32(c + 0x04, &v)) {
            setChunkError();
            return 0;
        }
        return v;
    }

    uint32_t chunkAlign(size_t c) const
    {
        uint32_t v = 0;
        if (!tryU32(c + 0x08, &v)) {
            setChunkError();
            return 0;
        }
        return v;
    }

    uint32_t chunkNextField(size_t c) const
    {
        uint32_t v = 0;
        if (!tryU32(c + 0x0C, &v)) {
            setChunkError();
            return 0;
        }
        return v;
    }

    /* Chunk_Next HOOK 0x4B7470, src/res/chunk.c:9-15 */
    size_t chunkNext(size_t c) const
    {
        if (c == kNoChunk)
            return kNoChunk;
        const uint32_t next = chunkNextField(c);
        if (!next)
            return kNoChunk;
        const size_t out = c + next;
        /* HOST: the engine follows the link blindly; stop at the buffer. */
        if (!contains(out, kChunkHeaderSize)) {
            setChunkError();
            return kNoChunk;
        }
        return out;
    }

    /* Chunk_FirstChild HOOK 0x4B7490, src/res/chunk.c:17-32 */
    size_t chunkFirstChild(size_t c) const
    {
        if (c == kNoChunk)
            return kNoChunk;
        const uint32_t size = chunkSize(c);
        if (!size)
            return kNoChunk;
        const uint32_t align = chunkAlign(c);
        if (!IsPow2(align)) {
            setChunkError();
            return kNoChunk;
        }
        const uint32_t off = (align + 0x13) & ~(align - 1);
        if (off < size)
            return c + off;
        return kNoChunk;
    }

    /* Chunk_Data HOOK 0x4B74D0, src/res/chunk.c:34-49 */
    size_t chunkData(size_t c, size_t parent) const
    {
        if (parent == kNoChunk || c == kNoChunk)
            return kNoChunk;
        const size_t off = c - parent;
        if (!IsPow2(chunkAlign(parent)) || off >= chunkSize(parent) || !IsPow2(chunkAlign(c))
            || chunkAlign(c) > chunkAlign(parent)) {
            setChunkError();
            return kNoChunk;
        }
        if (!chunkSize(c))
            return kNoChunk;
        const size_t align = chunkAlign(c);
        return parent + ((align + off + 0xF) & ~(align - 1));
    }

    /* Chunk_Find HOOK 0x4B7590, src/res/chunk.c:74-81 */
    size_t chunkFind(size_t first, uint32_t id) const
    {
        for (size_t c = first; c != kNoChunk; c = chunkNext(c)) {
            if (chunkId(c) == id)
                return c;
        }
        return kNoChunk;
    }

    /* find_data — src/bonemesh/d3drender.c:199-202 */
    size_t findData(uint32_t id) const { return chunkData(chunkFind(chunkFirstChild(0), id), 0); }

    /* payload size of a chunk found by id (0 when absent) */
    size_t findSize(uint32_t id) const { return chunkSize(chunkFind(chunkFirstChild(0), id)); }

private:
    const std::vector<uint8_t>& bytes_;
    std::string source_;
    mutable bool chunkError_ = false; /* gChunkError, BSS 0xA43E5C */
};

/* ---------------------------------------------------------------------------
 * Direct3DRender_LoadModel — HOOK 0x49FB10, src/bonemesh/d3drender.c:204-261
 *
 * The engine writes the resolved chunk pointers into the RD3D record.  The same
 * record is modelled here as a view: the on-disk words plus the pointers the
 * switch assigns, all kept under their engine names.
 * ------------------------------------------------------------------------- */
struct MeshRenderView {
    int32_t unkC = 0; /* format 0: vertex count; format 1: extra influences */
    int32_t unk10 = 0; /* formats 1/3: base vertex count / vertex count */
    size_t unk10Ptr = kNoChunk; /* format 0: the vertex pointer the engine stores in unk10 */
    size_t unk14 = kNoChunk; /* format 0: groups (REND); format 3: vertices (VRTX) */
    size_t unk18 = kNoChunk; /* format 1: vertices (VRTX); format 3: LTMP */
    size_t unk1C = kNoChunk; /* format 1/3: groups (REND) */

    /* resolved roles */
    size_t vertexStream = kNoChunk;
    size_t groupStream = kNoChunk;
    size_t lightmapStream = kNoChunk;
    uint32_t vertexStride = 0;
};

[[noreturn]] void FatalMeshFormat()
{
    /* src/bonemesh/d3drender.c:232-235 (unsupported format) and :239-243
     * (header->unk28) — both are `Error_Show("Mesh format not supported"); for(;;);` */
    throw std::runtime_error("Mesh format not supported");
}

MeshRenderView LoadModel(const Resource& res, size_t header)
{
    MeshRenderView r;
    const int32_t format = res.i32(header + kMeshFormat);

    /* The format switch comes first: an unsupported format is fatal before any
     * chunk is touched (src/bonemesh/d3drender.c:210-235). */
    if (format != 0 && format != 1 && format != 3)
        FatalMeshFormat();

    const size_t rd3dChunk = res.chunkFind(res.chunkFirstChild(0), kIdRd3d);
    if (rd3dChunk == kNoChunk) {
        /* HOST: the engine assigns NULL and dereferences it at once
         * (src/bonemesh/d3drender.c:212/219/225). */
        throw std::runtime_error("MEF has no D3DR (RD3D) chunk: " + res.source());
    }
    const size_t rd3d = res.chunkData(rd3dChunk, 0);
    const uint32_t rd3dSize = res.chunkSize(rd3dChunk);

    /* The engine reads these words straight out of the RD3D record; a record
     * shorter than the field is corrupt, so HOST: report instead of reading the
     * next chunk's header. */
    const auto rd3dWord = [&](size_t off) -> int32_t {
        if (off + 4 > rd3dSize)
            throw std::runtime_error("MEF D3DR (RD3D) record is " + std::to_string(rd3dSize)
                + " bytes, too short for field +" + std::to_string(off) + ": " + res.source());
        return res.i32(rd3d + off);
    };

    switch (format) {
    case 0:
        /* data = RD3D; unk10 = VRTX; unk14 = REND; MeshBatch_Add(0, header, unkC)
         * (src/bonemesh/d3drender.c:211-217; d3drender.c:180,193) */
        r.unkC = rd3dWord(kRenderUnkC);
        r.unk10Ptr = res.findData(kIdVrtx);
        r.unk14 = res.findData(kIdRend);
        r.vertexStream = r.unk10Ptr;
        r.groupStream = r.unk14;
        r.vertexStride = kRigidVertexStride;
        break;
    case 1:
        /* data = RD3D; unk18 = VRTX; unk1C = REND (src/bonemesh/d3drender.c:218-222) */
        r.unkC = rd3dWord(kRenderUnkC);
        r.unk10 = rd3dWord(kRenderUnk10);
        r.unk18 = res.findData(kIdVrtx);
        r.unk1C = res.findData(kIdRend);
        r.vertexStream = r.unk18;
        r.groupStream = r.unk1C;
        r.vertexStride = kSkinVertexStride;
        break;
    case 3:
        /* data = RD3D; unk14 = VRTX; unk1C = REND; unk18 = LTMP;
         * MeshBatch_Add(1, header, unk10) (src/bonemesh/d3drender.c:224-231;
         * d3drender.c:205,210) */
        r.unk10 = rd3dWord(kRenderUnk10);
        r.unk14 = res.findData(kIdVrtx);
        r.unk1C = res.findData(kIdRend);
        r.unk18 = res.findData(kIdLtmp);
        r.vertexStream = r.unk14;
        r.groupStream = r.unk1C;
        r.lightmapStream = r.unk18;
        r.vertexStride = kLightmapVertexStride;
        break;
    default:
        FatalMeshFormat();
    }

    if (res.i32(header + kMeshUnk28)) {
        /* src/bonemesh/d3drender.c:238-243 */
        FatalMeshFormat();
    }

    return r;
}

/* ---------------------------------------------------------------------------
 * Collision — Direct3DRender_LoadModel, src/bonemesh/d3drender.c:245-260
 *
 *   c = Chunk_Find(Chunk_FirstChild(res), ID_CMSH);
 *   header->coll = NULL;
 *   if (!c) return;                     <- a missing CMSH is a clean return
 *   header->coll = Chunk_Data(c, res);
 *   for (i = 0; i < 2; i++) { vtx/fce/mat/sph = Chunk_Find(Chunk_Next(prev), ID_...);
 *                             coll->part[i].{verts,faces,mats,sphere} = Chunk_Data(...); }
 *
 * Both parts are loaded unconditionally, so a model with one collision set
 * reports part[1] with NULL members — not an error.
 * ------------------------------------------------------------------------- */
struct MeshCollPart {
    size_t verts = kNoChunk;
    size_t faces = kNoChunk;
    size_t mats = kNoChunk;
    size_t sphere = kNoChunk;
};

struct MeshCollView {
    bool present = false;
    size_t chunk = kNoChunk;
    MeshCollPart part[2];
};

MeshCollView LoadCollision(const Resource& res)
{
    MeshCollView coll;
    const size_t first = res.chunkFirstChild(0);
    size_t vtx = first, fce = first, mat = first, sph = first;

    const size_t c = res.chunkFind(first, kIdCmsh);
    coll.present = (c != kNoChunk);
    if (!coll.present)
        return coll;
    coll.chunk = res.chunkData(c, 0);

    for (int i = 0; i < 2; i++) {
        vtx = res.chunkFind(res.chunkNext(vtx), kIdCvtx);
        fce = res.chunkFind(res.chunkNext(fce), kIdCfce);
        mat = res.chunkFind(res.chunkNext(mat), kIdCmat);
        sph = res.chunkFind(res.chunkNext(sph), kIdCsphere);
        coll.part[i].verts = res.chunkData(vtx, 0);
        coll.part[i].faces = res.chunkData(fce, 0);
        coll.part[i].mats = res.chunkData(mat, 0);
        coll.part[i].sphere = res.chunkData(sph, 0);
    }
    return coll;
}

/* ---------------------------------------------------------------------------
 * Vertex streams.  One reader per format with the engine's stride and field
 * offsets; the count comes from the header word the engine reads, never from
 * the chunk length.  HOST: the copy is bounded by the chunk's own payload
 * because the engine trusts the header and reads whatever follows.
 * ------------------------------------------------------------------------- */

/* format 0: FVF 274 = XYZ|NORMAL|TEX1 (gMeshFVF, src/dxinit/d3drender.c:359) */
std::vector<RenderVertex> ParseRigidVertices(const Resource& res, size_t stream, size_t count, size_t capacity)
{
    std::vector<RenderVertex> out;
    if (stream == kNoChunk)
        return out;
    const size_t n = std::min(count, capacity);
    out.resize(n);
    for (size_t i = 0; i < n; i++) {
        const size_t base = stream + i * kRigidVertexStride;
        RenderVertex& v = out[i];
        v.rawPos = glm::vec3(res.f32(base + 0), res.f32(base + 4), res.f32(base + 8));
        v.pos = v.rawPos * kMefNativeScale;
        v.normal = glm::vec3(res.f32(base + 12), res.f32(base + 16), res.f32(base + 20));
        v.uv = glm::vec2(res.f32(base + 24), res.f32(base + 28));
        v.weight = 1.0f;
    }
    return out;
}

/* format 1: SkinVertex — src/dxinit/d3drender.c:37-44 (p[3], n[3], uv[2], w,
 * target, bone).  The stream is two runs: the first `baseCount` vertices are
 * what the draw call indexes, the remaining `extraCount` are influences that
 * accumulate into a base vertex (BoneMesh_Skin HOOK 0x49B700,
 * src/dxinit/d3drender.c:232-245, 265-283). */
std::vector<RenderVertex> ParseSkinVertices(const Resource& res, size_t stream, size_t total, size_t capacity)
{
    std::vector<RenderVertex> out;
    if (stream == kNoChunk)
        return out;
    const size_t n = std::min(total, capacity);
    out.resize(n);
    for (size_t i = 0; i < n; i++) {
        const size_t base = stream + i * kSkinVertexStride;
        RenderVertex& v = out[i];
        v.rawPos = glm::vec3(res.f32(base + 0), res.f32(base + 4), res.f32(base + 8));
        v.pos = v.rawPos * kMefNativeScale;
        v.normal = glm::vec3(res.f32(base + 12), res.f32(base + 16), res.f32(base + 20));
        v.uv = glm::vec2(res.f32(base + 24), res.f32(base + 28));
        v.weight = res.f32(base + 32); /* SkinVertex::w */
        v.localVertexId = res.u16(base + 36); /* SkinVertex::target */
        v.boneIndex = res.u16(base + 38); /* SkinVertex::bone */
    }
    return out;
}

/* format 3: FVF 530 = XYZ|NORMAL|TEX2 — 10 floats; the second texture
 * coordinate is the lightmap coordinate, scaled per group by
 * Direct3DRender_UploadLightmapMesh HOOK 0x49EB50, src/render/render.c:1096-1125. */
std::vector<RenderVertex> ParseLightmapVertices(const Resource& res, size_t stream, size_t count, size_t capacity)
{
    std::vector<RenderVertex> out;
    if (stream == kNoChunk)
        return out;
    const size_t n = std::min(count, capacity);
    out.resize(n);
    for (size_t i = 0; i < n; i++) {
        const size_t base = stream + i * kLightmapVertexStride;
        RenderVertex& v = out[i];
        v.rawPos = glm::vec3(res.f32(base + 0), res.f32(base + 4), res.f32(base + 8));
        v.pos = v.rawPos * kMefNativeScale;
        v.normal = glm::vec3(res.f32(base + 12), res.f32(base + 16), res.f32(base + 20));
        v.uv = glm::vec2(res.f32(base + 24), res.f32(base + 28));
        v.uv2 = glm::vec2(res.f32(base + 32), res.f32(base + 36));
        v.weight = 1.0f;
    }
    return out;
}

/* ---------------------------------------------------------------------------
 * Render groups.
 *
 *   format 0  MeshGroup, g = (char *)g + g->next + 0x1C
 *             Direct3DRender_DrawRigidMesh HOOK 0x49DF60, src/render/render.c:985-1005
 *   format 1  MeshGroup, g = (char *)g + sizeof(MeshGroup) + g->next
 *             Direct3DRender_DrawBoneMesh HOOK 0x49F700, src/bonemesh/d3drender.c:169-183
 *   format 3  LmGroup, g = (char *)g + g->next + 0x20
 *             src/render/render.c:1047,1081,1110-1120
 *
 * The chain is terminated only by `next == 0xFFFF`; no header count bounds it.
 * A NULL group stream (absent or empty REND chunk: Chunk_Data returns NULL when
 * the chunk has no payload) draws nothing — the engine guards with `if (g)`.
 * ------------------------------------------------------------------------- */
struct RenderGroupView {
    size_t off = kNoChunk;
    float center[3] = { 0.0f, 0.0f, 0.0f };
    uint16_t indexCount = 0;
    uint16_t next = 0;
    uint16_t texture = kNoTexture;
    int32_t lightmap = -1; /* format 3 only */
    uint16_t base = 0;
    uint16_t vertexCount = 0;
    uint16_t sorted = 0;
    uint8_t flags = 0; /* bit 0: additive (src/render/render.c:1185) */
    uint8_t alpha = 0; /* alpha-test reference (src/render/render.c:1203) */
    size_t indices = kNoChunk;
};

std::vector<RenderGroupView> WalkGroups(const Resource& res, size_t stream, int32_t format)
{
    std::vector<RenderGroupView> groups;
    if (stream == kNoChunk)
        return groups;

    const size_t stride = (format == 3) ? kLmGroupSize : kMeshGroupSize;
    for (size_t g = stream;;) {
        if (!res.contains(g, stride)) {
            res.setChunkError(); /* HOST: the engine would read past the chunk */
            break;
        }
        RenderGroupView v;
        v.off = g;
        v.center[0] = res.f32(g + 0x00);
        v.center[1] = res.f32(g + 0x04);
        v.center[2] = res.f32(g + 0x08);
        v.indexCount = res.u16(g + 0x0C);
        v.next = res.u16(g + 0x0E);
        v.texture = res.u16(g + 0x10);
        if (format == 3) {
            v.lightmap = res.u16(g + 0x12);
            v.base = res.u16(g + 0x14);
            v.vertexCount = res.u16(g + 0x16);
            v.sorted = res.u16(g + 0x18);
            v.flags = res.u8(g + 0x1C);
            v.alpha = res.u8(g + 0x1F);
        } else {
            v.base = res.u16(g + 0x12);
            v.vertexCount = res.u16(g + 0x14);
            v.sorted = res.u16(g + 0x16);
            v.flags = res.u8(g + 0x18);
            v.alpha = res.u8(g + 0x1B);
        }
        v.indices = g + stride;
        groups.push_back(v);

        if (v.next == kLastGroup)
            break;
        g = g + stride + v.next;
    }
    return groups;
}

/* ---------------------------------------------------------------------------
 * Collision chunks as geometry.  EDITOR-ONLY fallback: the engine never renders
 * CVTX/CFCE (they are the HSMC collision sets), but retail ships models whose
 * only geometry is the collision mesh (empty REND and XTRV — e.g. colbox.mef,
 * 605_08_1.mef).  It reads part[0] of the HSMC walk above.
 * ------------------------------------------------------------------------- */
std::vector<XtvcVertex> ParseXtvcVerts(const Resource& res, size_t data, size_t size);
std::vector<EcfcFace> ParseEcfcFaces(const Resource& res, size_t data, size_t size);
std::vector<TamcRecord> ParseTamcRecords(const Resource& res, size_t data, size_t size);

ParsedGeometry ParseCollisionGeometry(const Resource& res, const MeshCollView& coll)
{
    ParsedGeometry geometry;
    const size_t verts = coll.part[0].verts;
    const size_t faces = coll.part[0].faces;
    if (verts == kNoChunk || faces == kNoChunk)
        return geometry;

    /* part[0] is the first CVTX/CFCE after the first child, which is exactly
     * what Chunk_Find(Chunk_FirstChild(res), ID_CVTX) returns. */
    const size_t vertexCount = res.findSize(kIdCvtx) / kCvtxVertexSize;
    const size_t faceCount = res.findSize(kIdCfce) / kCfceFaceSize;

    geometry.vertices.resize(vertexCount);
    for (size_t i = 0; i < vertexCount; i++) {
        const size_t base = verts + i * kCvtxVertexSize;
        const float x = res.f32(base + 0);
        const float y = res.f32(base + 4);
        const float z = res.f32(base + 8);
        geometry.vertices[i].pos = glm::vec3(x, y, z) * kMefNativeScale;
        /* EDITOR-ONLY: CVTX carries no texture coordinates; the editor
         * fabricates planar UVs so the fallback geometry can be textured. */
        geometry.vertices[i].uv = glm::vec2(x * 0.1f, z * 0.1f);
    }

    geometry.triangles.reserve(faceCount);
    for (size_t i = 0; i < faceCount; i++) {
        const size_t base = faces + i * kCfceFaceSize;
        geometry.triangles.push_back({ res.u16(base + 0), res.u16(base + 2), res.u16(base + 4) });
    }

    geometry.collisionVertexCount = vertexCount;
    geometry.collisionFaceCount = faceCount;
    return geometry;
}

/* ---------------------------------------------------------------------------
 * Optional chunk families the editor exposes.  None of them participate in
 * Direct3DRender_LoadModel; Mesh_Load stores their pointers in the MeshHeader
 * (HOOK 0x4CF390, src/model/model.c:375-378).
 * ------------------------------------------------------------------------- */

/* CVTX (ID_CVTX): 16 bytes — 3 floats + a bone/type word. */
std::vector<XtvcVertex> ParseXtvcVerts(const Resource& res, size_t data, size_t size)
{
    std::vector<XtvcVertex> verts;
    if (data == kNoChunk)
        return verts;
    const size_t count = size / kCvtxVertexSize;
    verts.resize(count);
    for (size_t i = 0; i < count; i++) {
        const size_t base = data + i * kCvtxVertexSize;
        verts[i].px = res.f32(base + 0);
        verts[i].py = res.f32(base + 4);
        verts[i].pz = res.f32(base + 8);
        verts[i].boneIndex = res.u32(base + 12);
        verts[i].reserved = 0;
    }
    return verts;
}

/* CFCE (ID_CFCE): 8 bytes — 3 uint16 indices + a uint16 material. */
std::vector<EcfcFace> ParseEcfcFaces(const Resource& res, size_t data, size_t size)
{
    std::vector<EcfcFace> faces;
    if (data == kNoChunk)
        return faces;
    const size_t count = size / kCfceFaceSize;
    faces.resize(count);
    for (size_t i = 0; i < count; i++) {
        const size_t base = data + i * kCfceFaceSize;
        faces[i].a = res.u16(base + 0);
        faces[i].b = res.u16(base + 2);
        faces[i].c = res.u16(base + 4);
        faces[i].mat = res.u16(base + 6);
        faces[i].lmp = 0;
        faces[i].vrt = 0;
    }
    return faces;
}

/* CMAT (ID_CMAT): 12-byte surface record. */
std::vector<TamcRecord> ParseTamcRecords(const Resource& res, size_t data, size_t size)
{
    std::vector<TamcRecord> recs;
    if (data == kNoChunk)
        return recs;
    const size_t count = size / kCmatSize;
    recs.resize(count);
    for (size_t i = 0; i < count; i++) {
        const size_t base = data + i * kCmatSize;
        recs[i].opacity = res.f32(base + 0);
        recs[i].portal = res.u16(base + 4);
        recs[i].diffuse = res.i16(base + 6);
        recs[i].matId = res.i16(base + 8);
        recs[i].unknown = res.u16(base + 10);
        recs[i].unknown0 = 0;
        recs[i].unknown1 = 0;
    }
    return recs;
}

/* MVTX (MEF_MVTX): magic vertices, 16 bytes each.  Mesh_Load stores the chunk
 * at header+0x88 (src/model/model.c:375); the per-record layout is not visible
 * in the decompiled source — the 3-float + type-word split is the editor's
 * existing reading of that chunk.  UNVERIFIED. */
std::vector<XtvmVertex> ParseXtvmVerts(const Resource& res, size_t data, size_t size)
{
    std::vector<XtvmVertex> verts;
    if (data == kNoChunk)
        return verts;
    const size_t count = size / 16;
    verts.resize(count);
    for (size_t i = 0; i < count; i++) {
        const size_t base = data + i * 16;
        verts[i].px = res.f32(base + 0);
        verts[i].py = res.f32(base + 4);
        verts[i].pz = res.f32(base + 8);
        verts[i].magicType = res.i32(base + 12);
    }
    return verts;
}

/* ATTA (MEF_ATTA): 0x44-byte records, stored at header+0x8C with the record
 * count at header+0x7E (src/model/model.c:375).  The 0x44 record size is fixed
 * by the file format; the field offsets are the editor's reading.  UNVERIFIED. */
std::vector<MefAttachment> ParseAttachments(const Resource& res, size_t data, size_t size)
{
    std::vector<MefAttachment> attas;
    if (data == kNoChunk)
        return attas;
    const size_t count = size / kAttaRecordSize;
    attas.resize(count);
    for (size_t i = 0; i < count; i++) {
        const size_t base = data + i * kAttaRecordSize;
        std::memcpy(attas[i].name, res.ptr(base, 16, "ATTA name"), 16);
        attas[i].px = res.f32(base + 16);
        attas[i].py = res.f32(base + 20);
        attas[i].pz = res.f32(base + 24);
        attas[i].r00 = res.f32(base + 28);
        attas[i].r01 = res.f32(base + 32);
        attas[i].r02 = res.f32(base + 36);
        attas[i].r03 = res.f32(base + 40);
        attas[i].r04 = res.f32(base + 44);
        attas[i].r05 = res.f32(base + 48);
        attas[i].r06 = res.f32(base + 52);
        attas[i].r07 = res.f32(base + 56);
        attas[i].r08 = res.f32(base + 60);
        attas[i].boneId = res.i32(base + 64);
    }
    return attas;
}

/* Attachment view of the same ATTA records (the editor logs the count and uses
 * the names for its attachment cache). */
std::vector<Attachment> ToAttachmentView(const std::vector<MefAttachment>& attas)
{
    std::vector<Attachment> out(attas.size());
    for (size_t i = 0; i < attas.size(); i++) {
        const char* name = attas[i].name;
        size_t len = 0;
        while (len < 16 && name[len] != '\0')
            ++len;
        out[i].name.assign(name, len);
        out[i].pos = glm::vec3(attas[i].px, attas[i].py, attas[i].pz);
        out[i].boneId = attas[i].boneId;
    }
    return out;
}

/* Portals: MEF_PORT (TROP) records index the MEF_PTVX (XVTP) vertex array and
 * the MEF_PTFC (CFTP) face array (src/model/model.c:376-378).  The chunk ids are
 * the engine's; the record layouts are the editor's reading.  UNVERIFIED beyond
 * the measured TROP record size (a multiple of 20 bytes in every retail member). */
void ParsePortals(const Resource& res, ParsedGeometry& geometry)
{
    const size_t trop = res.findData(kIdPort);
    const size_t xvtp = res.findData(kIdPtvx);
    const size_t cftp = res.findData(kIdPtfc);

    std::vector<glm::vec3> xvtpVerts;
    if (xvtp != kNoChunk) {
        const size_t count = res.findSize(kIdPtvx) / 12;
        xvtpVerts.reserve(count);
        for (size_t i = 0; i < count; i++) {
            const size_t base = xvtp + i * 12;
            xvtpVerts.push_back(glm::vec3(res.f32(base + 0), res.f32(base + 4), res.f32(base + 8)));
        }
    }

    std::vector<std::array<uint32_t, 3>> cftpFaces;
    if (cftp != kNoChunk) {
        const size_t count = res.findSize(kIdPtfc) / 12;
        cftpFaces.reserve(count);
        for (size_t i = 0; i < count; i++) {
            const size_t base = cftp + i * 12;
            cftpFaces.push_back({ res.u32(base + 0), res.u32(base + 4), res.u32(base + 8) });
        }
    }

    if (trop == kNoChunk)
        return;
    const size_t count = res.findSize(kIdPort) / 20;
    for (size_t i = 0; i < count; i++) {
        const size_t base = trop + i * 20;
        const uint32_t vertsoff = res.u32(base + 0);
        const uint32_t vertsnum = res.u32(base + 4);
        const uint32_t facesoff = res.u32(base + 8);
        const uint32_t facesnum = res.u32(base + 12);
        const uint32_t portalid = res.u32(base + 16);

        PortalRecord portal;
        portal.portalId = portalid;
        /* EDITOR-ONLY: ID_LTMP ("PMTL") used to be read here as a portal
         * material table.  The engine assigns it to MeshRender::unk18 for
         * format 3 (src/bonemesh/d3drender.c:229) and never reads it, so it
         * carries no material ids. */
        portal.materialId = 0;

        const size_t vertsEnd = static_cast<size_t>(vertsoff) + vertsnum;
        for (size_t j = vertsoff; j < vertsEnd && j < xvtpVerts.size(); ++j)
            portal.verts.push_back(xvtpVerts[j]);

        const size_t facesEnd = static_cast<size_t>(facesoff) + facesnum;
        for (size_t j = facesoff; j < facesEnd && j < cftpFaces.size(); ++j)
            portal.faces.push_back(cftpFaces[j]);

        geometry.portals.push_back(std::move(portal));
    }
}

/* ---------------------------------------------------------------------------
 * Helper: compute world positions from BoneInfo (for vertex baking)
 * ------------------------------------------------------------------------- */

std::vector<glm::vec3> ComputeBoneWorldPositions(const std::vector<BoneInfo>& bones)
{
    const size_t numBones = bones.size();
    std::vector<glm::vec3> worldPos(numBones);
    for (size_t i = 0; i < numBones; ++i) {
        glm::vec3 acc = bones[i].pivot;
        int p = bones[i].parent;
        while (p != -1 && p < static_cast<int>(numBones)) {
            acc += bones[p].pivot;
            p = bones[p].parent;
        }
        worldPos[i] = acc;
    }
    return worldPos;
}

std::vector<uint8_t> ReadWholeFile(const std::string& filepath)
{
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        throw std::runtime_error("Failed to open MEF file: " + filepath);

    const std::streamsize size = file.tellg();
    if (size < 0)
        throw std::runtime_error("Failed to read MEF file size: " + filepath);

    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    if (!bytes.empty() && !file.read(reinterpret_cast<char*>(bytes.data()), size))
        throw std::runtime_error("Failed to read MEF file bytes: " + filepath);
    return bytes;
}

/* ---------------------------------------------------------------------------
 * The reader proper.
 * ------------------------------------------------------------------------- */
ParsedGeometry ParseMefGeometry(const std::vector<uint8_t>& bytes, const std::string& source)
{
    ParsedGeometry geometry;
    Resource res(bytes, source);

    /* The engine is handed an ILFF resource and walks it from the first child
     * (Chunk_FirstChild(res), src/res/chunk.c:17). */
    if (bytes.size() < kIlffHeaderSize || std::memcmp(bytes.data(), "ILFF", 4) != 0)
        throw std::runtime_error("Invalid MEF magic header (expected ILFF): " + source);

    const size_t mesh = res.findData(kIdMesh);
    if (mesh == kNoChunk)
        throw std::runtime_error("MEF has no MESH (HSEM) chunk: " + source);
    res.require(mesh, kMeshVersion + 4, "MESH version");

    /* Mesh_Load HOOK 0x4CF390, src/model/model.c:367-373 */
    if (res.u32(mesh + kMeshVersion) != kMeshVersionBits) {
        const double found = static_cast<double>(res.f32(mesh + kMeshVersion));
        char version[32];
        std::snprintf(version, sizeof(version), "%.2f", found);
        /* Same wording as the engine's Error_Show (src/model/model.c:368-372). */
        throw std::runtime_error("Mesh \"" + source + "\" and source code are incompatible. (Mesh \""
            + source + "\" is version " + version + ", source code is version 0.10.)");
    }

    const int32_t format = res.i32(mesh + kMeshFormat);
    geometry.modelType = static_cast<uint32_t>(format);

    /* Direct3DRender_LoadModel HOOK 0x49FB10, src/bonemesh/d3drender.c:204-261 */
    const MeshRenderView render = LoadModel(res, mesh);
    /* format 3 only: unk18 = LTMP ("PMTL").  Assigned by the loader
     * (src/bonemesh/d3drender.c:229) and never read by the engine. */
    const size_t lightmapChunkSize = (render.lightmapStream == kNoChunk) ? 0 : res.findSize(kIdLtmp);

    /* The engine's vertex count is the header word (MeshBatch_Flush
     * HOOK 0x49B4B0, src/dxinit/d3drender.c:180,193,205,210; BoneMesh_Skin
     * HOOK 0x49B700, src/dxinit/d3drender.c:232-245). */
    const size_t vertexCapacity = res.findSize(kIdVrtx) / render.vertexStride;
    switch (format) {
    case 0:
        geometry.vertices = ParseRigidVertices(res, render.vertexStream,
            static_cast<size_t>(std::max(0, render.unkC)), vertexCapacity);
        break;
    case 1: {
        const size_t base = static_cast<size_t>(std::max(0, render.unk10));
        const size_t extra = static_cast<size_t>(std::max(0, render.unkC));
        const size_t total = std::min(base + extra, vertexCapacity);
        geometry.vertices = ParseSkinVertices(res, render.vertexStream, total, vertexCapacity);
        /* The split the engine uses: `count = unk10` base vertices, then unkC
         * influences (src/dxinit/d3drender.c:232,240,264-283). */
        geometry.baseVertexCount = static_cast<uint32_t>(std::min(base, total));
        geometry.extraInfluenceCount = static_cast<uint32_t>(total - std::min(base, total));
        break;
    }
    case 3:
        geometry.vertices = ParseLightmapVertices(res, render.vertexStream,
            static_cast<size_t>(std::max(0, render.unk10)), vertexCapacity);
        break;
    default:
        FatalMeshFormat();
    }

    /* ---- render partitions: the REND group chain ------------------------- */
    const std::vector<RenderGroupView> groups = WalkGroups(res, render.groupStream, format);
    geometry.renderBlockCount = groups.size();

    uint32_t accumulatedIndices = 0;
    for (const RenderGroupView& g : groups) {
        const size_t blockTriangleStart = geometry.triangles.size();
        const bool indicesReadable = (g.indices != kNoChunk)
            && res.contains(g.indices, static_cast<size_t>(g.indexCount) * 2);

        if (indicesReadable) {
            for (uint32_t i = 0; i + 2 < g.indexCount; i += 3) {
                const uint16_t a = res.u16(g.indices + static_cast<size_t>(i + 0) * 2);
                const uint16_t b = res.u16(g.indices + static_cast<size_t>(i + 1) * 2);
                const uint16_t c = res.u16(g.indices + static_cast<size_t>(i + 2) * 2);
                /* indices are relative to g->base: DrawIndexedPrimitiveVB(...,
                 * g->base + base, ...) src/render/render.c:531, and
                 * Direct3DRender_SplineNearZ src/render/render.c:479-481. */
                geometry.triangles.push_back({ static_cast<uint32_t>(g.base) + a,
                    static_cast<uint32_t>(g.base) + b, static_cast<uint32_t>(g.base) + c });
            }
        } else if (g.indexCount != 0) {
            res.setChunkError(); /* HOST: the engine would read past the chunk */
        }

        const size_t blockTriangleCount = geometry.triangles.size() - blockTriangleStart;
        if (blockTriangleCount > 0) {
            ParsedGeometry::RenderBlock block;
            block.triangleStart = blockTriangleStart;
            block.triangleCount = blockTriangleCount;
            /* g->texture is the model's texture slot; 0xFFFF means untextured
             * (src/render/render.c:997,1060,1191). */
            block.materialSlot = (g.texture == kNoTexture) ? -1 : static_cast<int>(g.texture);
            /* EDITOR CONVENTION: the engine's `sorted` flag is what selects the
             * depth-sorted alpha-blended pass (src/render/render.c:994,1030);
             * the editor expresses that as opacity < 1. */
            block.opacity = (g.sorted != 0) ? 0.5f : 1.0f;
            block.lightmapSlot = (format == 3) ? g.lightmap : -1;
            block.sorted = g.sorted;
            block.flags = g.flags;
            block.alpha = g.alpha;
            geometry.renderBlocks.push_back(block);
        }

        /* The editor's ASCII-export view of the same records: one DnerRecord
         * per group, with the group's inline index array recorded as "ECAF"
         * faces (there is no ECAF chunk in these files).  This is a view, not a
         * second walk. */
        {
            DnerRecord rec;
            rec.px = g.center[0];
            rec.py = g.center[1];
            rec.pz = g.center[2];
            rec.numFace = g.indexCount;
            rec.td = static_cast<int16_t>(g.texture);
            rec.tb = static_cast<int16_t>(format == 3 ? g.lightmap : -1);
            rec.offVerts = g.base;
            rec.numVerts = g.vertexCount;
            rec.rawOpacity = g.sorted;
            rec.eflame = g.flags;
            rec.opacitd = g.alpha;
            rec.offsetIndex = static_cast<uint16_t>(accumulatedIndices);
            geometry.dnerRecords.push_back(rec);

            if (indicesReadable) {
                for (uint32_t i = 0; i + 2 < g.indexCount; i += 3) {
                    EcafFace face;
                    face.a = res.u16(g.indices + static_cast<size_t>(i + 0) * 2) + g.base;
                    face.b = res.u16(g.indices + static_cast<size_t>(i + 1) * 2) + g.base;
                    face.c = res.u16(g.indices + static_cast<size_t>(i + 2) * 2) + g.base;
                    geometry.ecafFaces.push_back(face);
                }
            }
        }
        accumulatedIndices += g.indexCount;
    }

    geometry.fromRenderMesh = !geometry.vertices.empty() && !geometry.triangles.empty();
    geometry.renderLayout = geometry.fromRenderMesh
        ? (format == 1 ? "type1 packed DNER" : "packed DNER")
        : "collision fallback";

    /* ---- bone meshes ----------------------------------------------------- */
    if (format == 1 && geometry.fromRenderMesh) {
        std::string modelName = source;
        const size_t lastSlash = modelName.find_last_of("\\/");
        if (lastSlash != std::string::npos)
            modelName = modelName.substr(lastSlash + 1);
        const size_t lastDot = modelName.find_last_of(".");
        if (lastDot != std::string::npos)
            modelName = modelName.substr(0, lastDot);

        uint32_t maxBoneIdx = 0;
        for (const RenderVertex& v : geometry.vertices) {
            if (v.boneIndex > maxBoneIdx)
                maxBoneIdx = v.boneIndex;
        }
        /* The rig itself lives in the .mtp (REIH/MANB) and is not part of the
         * MEF; the editor substitutes a hardcoded retail rig. */
        geometry.bones = GetIgi1HardcodedBones(modelName, maxBoneIdx);

        std::vector<glm::vec3> restPos;
        std::vector<glm::vec3> restN;
        SkinSkinnedVertices(geometry, nullptr, restPos, restN);
        const size_t applyCount = std::min(restPos.size(), geometry.vertices.size());
        for (size_t i = 0; i < applyCount; ++i) {
            geometry.vertices[i].pos = restPos[i];
            geometry.vertices[i].normal = restN[i];
        }
    }

    /* ---- collision (HSMC) ------------------------------------------------ */
    const MeshCollView coll = LoadCollision(res);
    const bool collisionPresent = coll.present;

    if (!geometry.fromRenderMesh) {
        /* EDITOR-ONLY: retail ships models whose only geometry is the collision
         * mesh (empty REND and XTRV).  The engine draws nothing for them; the
         * editor renders collision part[0] so they are not invisible.  This
         * replaces the geometry wholesale, exactly as before this module. */
        geometry = ParseCollisionGeometry(res, coll);
        geometry.renderLayout = "collision fallback";
    }
    geometry.collisionPresent = collisionPresent;
    geometry.lightmapChunkSize = lightmapChunkSize;

    /* ---- optional chunks (Mesh_Load HOOK 0x4CF390, model.c:375-378) ------ */
    const size_t first = res.chunkFirstChild(0);
    const size_t xtvc0 = res.chunkFind(first, kIdCvtx);
    const size_t ecfc0 = res.chunkFind(first, kIdCfce);
    const size_t tamc0 = res.chunkFind(first, kIdCmat);
    const size_t xtvc1 = res.chunkFind(res.chunkNext(xtvc0), kIdCvtx);
    const size_t ecfc1 = res.chunkFind(res.chunkNext(ecfc0), kIdCfce);
    const size_t tamc1 = res.chunkFind(res.chunkNext(tamc0), kIdCmat);
    const size_t atta = res.chunkFind(first, kIdAtta);
    const size_t mvtx = res.chunkFind(first, kIdMvtx);

    if (xtvc0 != kNoChunk)
        geometry.xtvcVerts = ParseXtvcVerts(res, res.chunkData(xtvc0, 0), res.chunkSize(xtvc0));
    if (ecfc0 != kNoChunk)
        geometry.ecfcFaces = ParseEcfcFaces(res, res.chunkData(ecfc0, 0), res.chunkSize(ecfc0));
    if (tamc0 != kNoChunk)
        geometry.tamcRecords = ParseTamcRecords(res, res.chunkData(tamc0, 0), res.chunkSize(tamc0));
    if (xtvc1 != kNoChunk)
        geometry.xtvcVerts1 = ParseXtvcVerts(res, res.chunkData(xtvc1, 0), res.chunkSize(xtvc1));
    if (ecfc1 != kNoChunk)
        geometry.ecfcFaces1 = ParseEcfcFaces(res, res.chunkData(ecfc1, 0), res.chunkSize(ecfc1));
    if (tamc1 != kNoChunk)
        geometry.tamcRecords1 = ParseTamcRecords(res, res.chunkData(tamc1, 0), res.chunkSize(tamc1));
    if (mvtx != kNoChunk)
        geometry.xtvmVerts = ParseXtvmVerts(res, res.chunkData(mvtx, 0), res.chunkSize(mvtx));
    if (atta != kNoChunk) {
        geometry.mefAttachments = ParseAttachments(res, res.chunkData(atta, 0), res.chunkSize(atta));
        geometry.attachments = ToAttachmentView(geometry.mefAttachments);
    }

    ParsePortals(res, geometry);

    /* chunkError() records a malformed chunk link (gChunkError, BSS 0xA43E5C).
     * The engine keeps going and fails later; the reader reports it. */
    geometry.chunkError = res.chunkError();

    return geometry;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

ParsedGeometry ParseMefFile(const std::string& filepath)
{
    const std::vector<uint8_t> bytes = ReadWholeFile(filepath);
    return ParseMefGeometry(bytes, filepath);
}

ParsedGeometry ParseMefFileFromMemory(const std::vector<uint8_t>& bytes, const std::string& modelId)
{
    /* Preserve the resource identity when parsing archive bytes.  Type-1 MEFs
     * use the model name to select the retail-compatible hardcoded rig. */
    const std::string sourceName = modelId.empty() ? std::string("<memory>") : modelId + ".mef";
    return ParseMefGeometry(bytes, sourceName);
}

std::vector<glm::vec3> ComputeBoneWorldPositionsPublic(const std::vector<BoneInfo>& bones)
{
    return ComputeBoneWorldPositions(bones);
}

glm::vec3 RigRootOriginRaw(const std::vector<BoneInfo>& bones)
{
    // The rig's root is the first bone with no parent (PLST[i] == -1); a rig with
    // no root has no origin to add, so the caller's placement stands unchanged.
    const std::vector<glm::vec3> world = ComputeBoneWorldPositions(bones);
    for (size_t i = 0; i < bones.size(); ++i) {
        if (bones[i].parent < 0) return world[i];
    }
    return glm::vec3(0.0f);
}

void SkinSkinnedVertices(const ParsedGeometry& geometry,
    const std::vector<glm::mat4>* boneWorldTransforms,
    std::vector<glm::vec3>& outPos,
    std::vector<glm::vec3>& outNormal)
{
    const size_t baseCount = std::min<size_t>(geometry.baseVertexCount, geometry.vertices.size());
    outPos.assign(baseCount, glm::vec3(0.0f));
    outNormal.assign(baseCount, glm::vec3(0.0f, 0.0f, 1.0f));
    if (baseCount == 0)
        return;

    const std::vector<glm::vec3> restPositions = boneWorldTransforms == nullptr
        ? ComputeBoneWorldPositions(geometry.bones)
        : std::vector<glm::vec3> {};
    const auto poseVertex = [&](const RenderVertex& vertex) -> glm::vec3 {
        if (boneWorldTransforms != nullptr && vertex.boneIndex < boneWorldTransforms->size()) {
            return glm::vec3((*boneWorldTransforms)[vertex.boneIndex]
                * glm::vec4(vertex.rawPos * kMefNativeScale, 1.0f));
        }
        glm::vec3 position = vertex.rawPos;
        if (vertex.boneIndex < restPositions.size())
            position += restPositions[vertex.boneIndex];
        return position * kMefNativeScale;
    };

    for (size_t i = 0; i < baseCount; ++i) {
        const RenderVertex& vertex = geometry.vertices[i];
        outPos[i] = poseVertex(vertex) * vertex.weight;
        outNormal[i] = (boneWorldTransforms != nullptr && vertex.boneIndex < boneWorldTransforms->size())
            ? glm::mat3((*boneWorldTransforms)[vertex.boneIndex]) * vertex.normal
            : vertex.normal;
    }

    const size_t influenceEnd = std::min<size_t>(
        geometry.vertices.size(), static_cast<size_t>(geometry.baseVertexCount) + geometry.extraInfluenceCount);
    for (size_t i = baseCount; i < influenceEnd; ++i) {
        const RenderVertex& influence = geometry.vertices[i];
        const size_t target = influence.localVertexId;
        if (target < baseCount)
            outPos[target] += poseVertex(influence) * influence.weight;
    }
}

EngineSkinVertices BuildEngineSkinVertices(const ParsedGeometry& geometry)
{
    // The engine's SkinVertex array, in the engine's own order: `mesh[0x10]`
    // base vertices first, then `mesh[0x0C]` extra influences
    // (src/dxinit/d3drender.c:230-245,275). The editor's parser already stores
    // the two runs that way (mef_native.cpp:1009-1010).
    //
    // BOTH COUNTS ARE CLAMPED TO WHAT THE ARRAY ACTUALLY HOLDS. `BoneMesh_Skin`
    // does not bound-check either run - it walks `count` records and then
    // `mesh[0x0C]` more from the same pointer (d3drender.c:254-274) - so an
    // over-declared count in a malformed MEF would read past the parse. The
    // engine has no guard to reproduce here; the port refuses the read instead.
    EngineSkinVertices out;
    const int total = static_cast<int>(geometry.vertices.size());
    const int declared_base = static_cast<int>(geometry.baseVertexCount);
    out.base_count = std::min(std::max(declared_base, 0), total);
    out.extra_count = std::min(std::max(static_cast<int>(geometry.extraInfluenceCount), 0),
                               total - out.base_count);

    const int record_count = out.base_count + out.extra_count;
    out.records.resize(static_cast<size_t>(record_count));
    for (int i = 0; i < record_count; ++i) {
        const RenderVertex& v = geometry.vertices[static_cast<size_t>(i)];
        igi::SkinTransformInput& rec = out.records[static_cast<size_t>(i)];
        rec.p[0] = v.rawPos.x;      // RAW units, exactly as XTRV stores them
        rec.p[1] = v.rawPos.y;
        rec.p[2] = v.rawPos.z;
        rec.n[0] = v.normal.x;
        rec.n[1] = v.normal.y;
        rec.n[2] = v.normal.z;
        rec.uv[0] = v.uv.x;
        rec.uv[1] = v.uv.y;
        rec.w = v.weight;
        rec.bone = static_cast<int>(v.boneIndex);
        rec.target = static_cast<int>(v.localVertexId);
    }
    return out;
}
