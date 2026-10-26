//
// mef_writer.cpp — OBJ -> binary .MEF writer for IGI 1.
//
// Evidence rule for this file: every statement that encodes a rule of the MEF
// format carries a citation of the form
//     <function> HOOK(0xADDR), src/path/file.c:LINE
// pointing at the verified decompilation of retail IGI.exe in
// /Users/haseeb-mir/Documents/Code/IGI-RE.  Measurements taken over the shipped
// retail assets are labelled "measured (corpus)".  Anything neither cited nor
// measured is labelled UNVERIFIED.  See docs/igi-re-parity/MEF_IMPORT.md.
//

#include "renderer/mef_writer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <unordered_map>

namespace mef_writer {
namespace {

// ---------------------------------------------------------------------------
// Little-endian byte helpers
//
// The ILFF container is read with plain 32-bit loads on x86
// (Chunk_Next HOOK 0x4B7470, src/res/chunk.c:9-15 reads `c->next` as a field),
// so the on-disk byte order is the host's little-endian order.  We emit bytes
// explicitly so the writer is correct on any host.
// ---------------------------------------------------------------------------

void PutU16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
}

void PutU32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
}

void PutI32(std::vector<uint8_t>& out, int32_t v) { PutU32(out, static_cast<uint32_t>(v)); }

void PutF32(std::vector<uint8_t>& out, float v) {
    uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    PutU32(out, bits);
}

uint32_t ReadU32(const std::vector<uint8_t>& in, size_t at) {
    if (at + 4 > in.size())
        return 0;
    return static_cast<uint32_t>(in[at]) | (static_cast<uint32_t>(in[at + 1]) << 8) |
           (static_cast<uint32_t>(in[at + 2]) << 16) | (static_cast<uint32_t>(in[at + 3]) << 24);
}

constexpr size_t kIlffHeaderSize = 20;   // "ILFF" + size + align + next + form id
constexpr size_t kChunkHeaderSize = 16;  // name[4] + size + align + next

size_t Align4(size_t n) { return (n + 3u) & ~static_cast<size_t>(3u); }

void SetError(std::string* error, const std::string& message) {
    if (error)
        *error = message;
}

// ---------------------------------------------------------------------------
// Minimal ILFF chunk reader (read side only, used to lift a reference MEF's
// ATTA chunk and to read its format).
//
// Semantics mirror the game's own accessors:
//   Chunk_Next      HOOK 0x4B7470, src/res/chunk.c:9-15   next chunk = c + c->next
//   Chunk_FirstChild HOOK 0x4B7490, src/res/chunk.c:17-32 first child = parent + 20
//                                                        for align == 4
//   Chunk_Data      HOOK 0x4B74D0, src/res/chunk.c:34-49  payload = chunk + 16
// ---------------------------------------------------------------------------

struct RawChunk {
    char name[5] = {0, 0, 0, 0, 0};
    uint32_t size = 0;
    size_t payload = 0;
};

bool WalkIlff(const std::vector<uint8_t>& bytes, std::vector<RawChunk>* out,
              std::string* error) {
    if (bytes.size() < kIlffHeaderSize) {
        SetError(error, "not an ILFF file (shorter than the 20-byte form header)");
        return false;
    }
    if (std::memcmp(bytes.data(), "ILFF", 4) != 0) {
        SetError(error, "not an ILFF file (missing 'ILFF' form signature)");
        return false;
    }
    // The form's declared size is the whole file.  The editor's own reader
    // rejects a mismatch, so reject it here too.
    const uint32_t declared = ReadU32(bytes, 4);
    if (declared != bytes.size()) {
        SetError(error, "ILFF form size " + std::to_string(declared) + " does not match file size " +
                            std::to_string(bytes.size()));
        return false;
    }

    size_t pos = kIlffHeaderSize;
    while (pos + kChunkHeaderSize <= bytes.size()) {
        RawChunk chunk;
        std::memcpy(chunk.name, bytes.data() + pos, 4);
        chunk.size = ReadU32(bytes, pos + 4);
        const uint32_t skip = ReadU32(bytes, pos + 12);
        chunk.payload = pos + kChunkHeaderSize;
        if (chunk.payload + chunk.size > bytes.size()) {
            SetError(error, "chunk '" + std::string(chunk.name) + "' exceeds the file bounds");
            return false;
        }
        out->push_back(chunk);
        if (skip == 0)
            break;
        pos += skip;
    }
    return true;
}

const RawChunk* FindRaw(const std::vector<RawChunk>& chunks, const char* name, size_t ordinal = 0) {
    size_t seen = 0;
    for (const auto& chunk : chunks) {
        if (std::memcmp(chunk.name, name, 4) == 0) {
            if (seen == ordinal)
                return &chunk;
            ++seen;
        }
    }
    return nullptr;
}

bool ReadFileBytes(const std::string& path, std::vector<uint8_t>* out, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        SetError(error, "cannot open '" + path + "' for reading");
        return false;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0) {
        SetError(error, "cannot determine the size of '" + path + "'");
        return false;
    }
    in.seekg(0, std::ios::beg);
    out->resize(static_cast<size_t>(size));
    if (size > 0 && !in.read(reinterpret_cast<char*>(out->data()), size)) {
        SetError(error, "failed to read '" + path + "'");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Chunk assembly
// ---------------------------------------------------------------------------

struct OutChunk {
    char name[5] = {0, 0, 0, 0, 0};
    std::vector<uint8_t> payload;
};

void AddChunk(std::vector<OutChunk>* chunks, const char* name, std::vector<uint8_t> payload) {
    OutChunk chunk;
    std::memcpy(chunk.name, name, 4);
    chunk.payload = std::move(payload);
    chunks->push_back(std::move(chunk));
}

// ---------------------------------------------------------------------------
// Geometry helpers
// ---------------------------------------------------------------------------

struct Bounds {
    glm::vec3 min{0.f};
    glm::vec3 max{0.f};
    bool valid = false;
};

Bounds ComputeBounds(const std::vector<glm::vec3>& points) {
    Bounds bounds;
    for (const auto& p : points) {
        if (!bounds.valid) {
            bounds.min = p;
            bounds.max = p;
            bounds.valid = true;
            continue;
        }
        for (int i = 0; i < 3; ++i) {
            bounds.min[i] = std::min(bounds.min[i], p[i]);
            bounds.max[i] = std::max(bounds.max[i], p[i]);
        }
    }
    return bounds;
}

float MaxDistance(const std::vector<glm::vec3>& points, const glm::vec3& origin) {
    float best = 0.f;
    for (const auto& p : points)
        best = std::max(best, glm::length(p - origin));
    return best;
}

// Collision BVH node — RigidCollNode, 24 bytes.
//   src/magicobj/rigidcoll.c:26-33 (RigidCollNode)
//   walked by RigidDynCubeObj_CollLine HOOK 0x4C35F0, src/magicobj/rigidcoll.c:330-370:
//     *child is the index of the first child (0 = none), *next the index of the
//     next sibling (0 = none), and (firstTri, triCount) is a half-open range of
//     collision triangles.  Node 0 is the root and is not sphere-tested.
struct BvhNode {
    glm::vec3 center{0.f};
    float radius = 0.f;
    uint16_t firstTri = 0xFFFF;
    uint16_t triCount = 0;
    uint16_t child = 0;
    uint16_t next = 0;
};

class CollisionTreeBuilder {
public:
    CollisionTreeBuilder(const std::vector<glm::vec3>& verts,
                         const std::vector<MefCollFace>& faces)
        : verts_(verts), faces_(faces) {
        order_.resize(faces_.size());
        for (size_t i = 0; i < order_.size(); ++i)
            order_[i] = static_cast<uint32_t>(i);

        // Node indices are 16-bit.  Retail stores one triangle per leaf; keep
        // that shape whenever it fits.
        leafTris_ = 1;
        while (2u * ((order_.size() + leafTris_ - 1u) / leafTris_) + 2u > 0xFFF0u)
            leafTris_ *= 2u;
    }

    std::vector<BvhNode> Build() {
        nodes_.reserve(2 * ((order_.size() + leafTris_ - 1u) / leafTris_));
        BuildRange(0, order_.size());
        return nodes_;
    }

    // The triangle permutation that the tree's (firstTri, triCount) ranges index.
    // ECFC has to be emitted in this order: the engine reads ECFC[firstTri + t]
    // (rigidcoll.c:330-370), so a leaf's sphere and the triangles it names must
    // agree.  Retail holds that invariant for every static model measured.
    const std::vector<uint32_t>& Order() const { return order_; }

private:
    void NodeBounds(uint16_t node, size_t lo, size_t hi) {
        std::vector<glm::vec3> points;
        points.reserve((hi - lo) * 3);
        for (size_t i = lo; i < hi; ++i) {
            const MefCollFace& f = faces_[order_[i]];
            points.push_back(verts_[f.a]);
            points.push_back(verts_[f.b]);
            points.push_back(verts_[f.c]);
        }
        const Bounds bounds = ComputeBounds(points);
        glm::vec3 center(0.f);
        if (bounds.valid)
            center = (bounds.min + bounds.max) * 0.5f;
        nodes_[node].center = center;
        nodes_[node].radius = MaxDistance(points, center);
    }

    uint16_t BuildRange(size_t lo, size_t hi) {
        const uint16_t index = static_cast<uint16_t>(nodes_.size());
        nodes_.emplace_back();
        NodeBounds(index, lo, hi);

        if (hi - lo <= leafTris_) {
            nodes_[index].firstTri = static_cast<uint16_t>(lo);
            nodes_[index].triCount = static_cast<uint16_t>(hi - lo);
            return index;
        }

        // Split at the median along the widest axis of the triangle centroids.
        glm::vec3 cmin(0.f), cmax(0.f);
        for (size_t i = lo; i < hi; ++i) {
            const glm::vec3 c = Centroid(order_[i]);
            if (i == lo) {
                cmin = c;
                cmax = c;
            } else {
                for (int k = 0; k < 3; ++k) {
                    cmin[k] = std::min(cmin[k], c[k]);
                    cmax[k] = std::max(cmax[k], c[k]);
                }
            }
        }
        int axis = 0;
        if (cmax[1] - cmin[1] > cmax[axis] - cmin[axis])
            axis = 1;
        if (cmax[2] - cmin[2] > cmax[axis] - cmin[axis])
            axis = 2;

        const size_t mid = lo + (hi - lo) / 2;
        std::nth_element(order_.begin() + lo, order_.begin() + mid, order_.begin() + hi,
                         [&](uint32_t a, uint32_t b) { return Centroid(a)[axis] < Centroid(b)[axis]; });

        const uint16_t left = BuildRange(lo, mid);
        const uint16_t right = BuildRange(mid, hi);
        nodes_[index].firstTri = 0xFFFF;
        nodes_[index].triCount = 0;
        nodes_[index].child = left;
        nodes_[left].next = right;
        return index;
    }

    glm::vec3 Centroid(uint32_t face) const {
        const MefCollFace& f = faces_[face];
        return (verts_[f.a] + verts_[f.b] + verts_[f.c]) / 3.0f;
    }

    const std::vector<glm::vec3>& verts_;
    const std::vector<MefCollFace>& faces_;
    std::vector<uint32_t> order_;
    std::vector<BvhNode> nodes_;
    size_t leafTris_ = 1;
};

// ---------------------------------------------------------------------------
// OBJ parsing
// ---------------------------------------------------------------------------

std::string Trim(const std::string& s) {
    size_t b = 0;
    size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b])))
        ++b;
    while (e > b && (std::isspace(static_cast<unsigned char>(s[e - 1])) || s[e - 1] == '\r'))
        --e;
    return s.substr(b, e - b);
}

std::vector<std::string> SplitWhitespace(const std::string& s) {
    std::vector<std::string> parts;
    std::istringstream stream(s);
    std::string token;
    while (stream >> token)
        parts.push_back(token);
    return parts;
}

// OBJ indices are 1-based; a negative value counts back from the end of the
// list as it stands at that point in the file.  Returns false for index 0 or an
// out-of-range value.
bool ResolveObjIndex(const std::string& token, size_t count, uint32_t* out, bool* usedNegative) {
    if (token.empty())
        return false;
    int64_t value = 0;
    try {
        size_t consumed = 0;
        value = std::stoll(token, &consumed);
        if (consumed != token.size())
            return false;
    } catch (...) {
        return false;
    }
    if (value == 0)
        return false;
    if (value < 0) {
        if (usedNegative)
            *usedNegative = true;
        const int64_t resolved = static_cast<int64_t>(count) + value;
        if (resolved < 0 || static_cast<size_t>(resolved) >= count)
            return false;
        *out = static_cast<uint32_t>(resolved);
        return true;
    }
    if (static_cast<size_t>(value) > count)
        return false;
    *out = static_cast<uint32_t>(value - 1);
    return true;
}

struct CornerSpec {
    uint32_t v = kNoObjIndex;
    uint32_t vt = kNoObjIndex;
    uint32_t vn = kNoObjIndex;
};

bool ParseCorner(const std::string& token, const ObjMesh& mesh, CornerSpec* out, bool* usedNegative) {
    // Accepted forms: v, v/vt, v//vn, v/vt/vn (a trailing 4th field is ignored).
    std::vector<std::string> fields;
    std::string current;
    for (char c : token) {
        if (c == '/') {
            fields.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    fields.push_back(current);
    if (fields.empty() || fields.size() > 4)
        return false;

    if (!ResolveObjIndex(fields[0], mesh.positions.size(), &out->v, usedNegative))
        return false;
    if (fields.size() >= 2 && !fields[1].empty()) {
        if (!ResolveObjIndex(fields[1], mesh.texcoords.size(), &out->vt, usedNegative))
            return false;
    }
    if (fields.size() >= 3 && !fields[2].empty()) {
        if (!ResolveObjIndex(fields[2], mesh.normals.size(), &out->vn, usedNegative))
            return false;
    }
    return true;
}

float ParseFloatOr(const std::string& token, float fallback) {
    try {
        size_t consumed = 0;
        const float value = std::stof(token, &consumed);
        if (consumed == 0)
            return fallback;
        return value;
    } catch (...) {
        return fallback;
    }
}

// ---------------------------------------------------------------------------
// Vertex welding: one MEF vertex per distinct (v, vt, vn) OBJ triple.
// ---------------------------------------------------------------------------

struct CornerKey {
    uint32_t v = 0, vt = 0, vn = 0;
    bool operator==(const CornerKey& other) const {
        return v == other.v && vt == other.vt && vn == other.vn;
    }
};

struct CornerKeyHash {
    size_t operator()(const CornerKey& k) const {
        // FNV-1a is defined over 64 bits.  Accumulating in size_t would truncate
        // both constants on the 32-bit build (the shipping target) and change the
        // hash, so the accumulator is uint64_t and only the result narrows.
        uint64_t h = 1469598103934665603ull;
        auto mix = [&h](uint32_t x) {
            h ^= x;
            h *= 1099511628211ull;
        };
        mix(k.v);
        mix(k.vt);
        mix(k.vn);
        return static_cast<size_t>(h);
    }
};

// ---------------------------------------------------------------------------
// HSEM writer
// ---------------------------------------------------------------------------

// MeshHeader layout: src/model/modeltypes.h:9-28 (sizeof == 0x90).
// The HSEM *chunk* is 156 bytes in every retail file measured (corpus) and the
// editor's reader requires exactly 156; the last 12 bytes are beyond the
// modelled struct and are always zero in the corpus.  UNVERIFIED: what those 12
// bytes mean.
constexpr size_t kHsemChunkSize = 156;

std::vector<uint8_t> BuildHsem(const MefModel& model, uint32_t d3drSize, uint32_t dnerSize,
                               uint32_t xtrvSize) {
    std::vector<uint8_t> out;
    out.reserve(kHsemChunkSize);

    // version must be exactly 0.1f: Mesh_Load HOOK 0x4CF390, src/model/model.c:367
    //   if (*(uint32_t *)m->header != 0x3DCCCCCD) -> "wrong MEF version"
    PutF32(out, 0.1f);  // 0x3DCCCCCD

    // time[7] — MeshHeader.time; no reader of this field exists in the oracle,
    // so it is written as zero.  UNVERIFIED (nothing reads it).
    for (int i = 0; i < 7; ++i)
        PutI32(out, 0);

    // format — Direct3DRender_LoadModel HOOK 0x49FB10, src/bonemesh/d3drender.c:210
    // switches on arg->header->format (0 rigid, 1 bone, 3 lightmapped).
    PutI32(out, static_cast<int32_t>(model.format));

    // data / unk28 / coll — runtime pointers, overwritten by
    // Direct3DRender_LoadModel (src/bonemesh/d3drender.c:212,225,247-250).
    PutU32(out, 0);  // +0x24 data
    PutI32(out, 0);  // +0x28 unk28
    PutU32(out, 0);  // +0x2C coll

    // center / sphere.  sphere is the radius around center:
    //   ModelObj_GetLOD HOOK 0x4CFCB0, src/lod/mesh3d.c:213-227 uses
    //   (o->pos + h->center) as the distance reference and h->sphere as the
    //   bounding radius; Fog_SetRange(&obj->pos, h->sphere) in
    //   Direct3DRender_DrawRigidMesh HOOK 0x49DF60, src/render/render.c:930+.
    // Measured (corpus): sphere == max |vertex - center| in 1440/1440 non-empty
    // static retail models.
    std::vector<glm::vec3> points;
    points.reserve(model.vertices.size());
    for (const auto& v : model.vertices)
        points.push_back(v.pos);

    const Bounds bounds = ComputeBounds(points);
    glm::vec3 center = model.pivot;
    if (model.useBoundsCentrePivot && bounds.valid)
        center = (bounds.min + bounds.max) * 0.5f;
    // `center` is an authored pivot, not a derived quantity: measured (corpus)
    // it equals the bounding-box centre in only 144/1440 retail models and the
    // vertex centroid in 52/1440.  Its only oracle readers are the LOD distance
    // reference (src/lod/mesh3d.c:220-226) and the fog reference point, so the
    // bounding-box centre is a defensible default.  UNVERIFIED: the retail
    // authoring rule.
    const float sphere = MaxDistance(points, center);

    PutF32(out, center.x);
    PutF32(out, center.y);
    PutF32(out, center.z);
    PutF32(out, sphere);

    // unk40[0x20] — two more copies of (center, sphere) in 1017/1578 retail
    // files (measured).  Nothing in the oracle reads it.  UNVERIFIED.
    for (int repeat = 0; repeat < 2; ++repeat) {
        PutF32(out, center.x);
        PutF32(out, center.y);
        PutF32(out, center.z);
        PutF32(out, sphere);
    }

    // complexity — measured (corpus) to equal the triangle count in 1578/1578
    // retail files.  Read by lod_select, src/lod/mesh3d.c:196-198, only to
    // compare LOD levels against level 0.
    PutI32(out, static_cast<int32_t>(model.sourceTriangles));

    // unk64[0x14] — five 32-bit words.  Measured (corpus, 1578/1578):
    //   word 0 == vertex count
    //   word 1 == D3DR.size + DNER.size + XTRV.size  (1440/1440 non-empty files)
    //   words 2..4 == 0
    // Nothing in the oracle reads any of them.  UNVERIFIED beyond the
    // measurements above.
    PutU32(out, static_cast<uint32_t>(model.vertices.size()));
    PutU32(out, d3drSize + dnerSize + xtrvSize);
    PutU32(out, 0);
    PutU32(out, 0);
    PutU32(out, 0);

    // radius — measured (corpus) to equal max |vertex| about the model origin in
    // 1186/1440 non-empty retail models; the other 254 differ by more than 2%.
    // No reader of MeshHeader.radius exists in the oracle.  UNVERIFIED.
    PutF32(out, MaxDistance(points, glm::vec3(0.f)));

    // vertex_count / attach_count.  vertex_count is zero in 1444/1578 retail
    // files (measured); attach_count equals the ATTA record count in 1578/1578.
    // ModelLod_GetAttachCount reads attach_count (used by the attachment code
    // path in src/bonemesh/*).
    PutU16(out, 0);
    PutU16(out, static_cast<uint16_t>(
                    std::min<size_t>(model.attachmentBytes.size() / 68, 0x7FFFu)));

    // unk80[8] — zero in the corpus; unread by the oracle.  UNVERIFIED.
    PutU32(out, 0);
    PutU32(out, 0);

    // verts (MVTX) / attach (ATTA) — runtime pointers written by Mesh_Load
    // HOOK 0x4CF390, src/model/model.c:374-378.
    PutU32(out, 0);
    PutU32(out, 0);

    // Trailing 12 bytes of the 156-byte chunk: always zero in the corpus.
    while (out.size() < kHsemChunkSize)
        out.push_back(0);
    return out;
}

// ---------------------------------------------------------------------------
// D3DR writer
// ---------------------------------------------------------------------------

// RigidMesh  src/render/render.c:56-67   (format 0, 48-byte chunk)
// LmMesh     src/render/render.c:69-82   (format 3, 56-byte chunk)
//
// The chunk sizes are fixed by MeshBatch_Flush HOOK 0x49B4B0,
// src/dxinit/d3drender.c:179-211, which writes past the end of both structs:
//   case 0 writes m+0x2C  -> the chunk must be at least 0x30 = 48 bytes
//   case 3 writes m+0x34  -> the chunk must be at least 0x38 = 56 bytes
// and by the measured retail sizes (48 for every type-0 file, 56 for every
// type-3 file).
std::vector<uint8_t> BuildD3dr(uint32_t format, uint32_t triangleCount, uint32_t groupCount,
                               uint32_t vertexCount) {
    std::vector<uint8_t> out;
    const size_t chunkSize = (format == 3) ? 56 : 48;
    out.reserve(chunkSize);

    // primType: passed straight to DrawIndexedPrimitiveVB
    //   draw_group, src/render/render.c:526,531
    // Measured (corpus): 4 (D3DPT_TRIANGLELIST) in 1578/1578 retail files.
    PutU32(out, 4);

    if (format == 3) {
        // LmMesh+0x04 (unk4[0]).  Measured (corpus): 1 in 1158/1158 type-3
        // files.  UNVERIFIED: the field has no reader in the oracle.
        PutU32(out, 1);
    }

    // triangleCount / groupCount.  Measured (corpus) field order:
    //   format 0: +0x04 triangles, +0x08 groups
    //   format 3: +0x08 triangles, +0x0C groups
    // Nothing in the oracle reads these two; the group chain is walked by
    // `next` and terminated by 0xFFFF (src/render/render.c:487-489,1047).
    PutU32(out, triangleCount);
    PutU32(out, groupCount);

    // vertexCount — format 0: RigidMesh+0x0C, read by
    //   Direct3DRender_LoadModel src/bonemesh/d3drender.c:216 and
    //   MeshBatch_Flush src/dxinit/d3drender.c:188,193
    // format 3: LmMesh+0x10, read by
    //   Direct3DRender_LoadModel src/bonemesh/d3drender.c:230 and
    //   MeshBatch_Flush src/dxinit/d3drender.c:205,210
    PutU32(out, vertexCount);

    // Everything after that is either a runtime pointer filled in by
    // Direct3DRender_LoadModel (src/bonemesh/d3drender.c:214-215,227-229) or a
    // runtime list node filled in by MeshBatch_Flush
    // (src/dxinit/d3drender.c:181-185,198-202).  Zero on disk.
    while (out.size() < chunkSize)
        out.push_back(0);
    return out;
}

// ---------------------------------------------------------------------------
// DNER writer
// ---------------------------------------------------------------------------

// MeshGroup  src/render/rendertypes.h:6-19   (format 0/1 header, 28 bytes)
// LmGroup    src/render/render.c:40-54       (format 3 header, 32 bytes)
//
// Group advance:
//   format 0/1  g = (MeshGroup *)((char *)g + g->next + 0x1C)
//               Direct3DRender_DrawRigidMesh HOOK 0x49DF60, src/render/render.c:487-489
//   format 3    g = (LmGroup *)((char *)g + g->next + 0x20)
//               src/render/render.c:1047,1081,1110-1120
// `next` is therefore the byte length of this record's index array, and
// 0xFFFF marks the last record.  Measured (corpus): next == indexCount*2 in
// every record of all 1578 retail files.
//
// Indices are relative to `base`:
//   Direct3DRender_SplineNearZ HOOK 0x49BE10, src/render/render.c:479-481
//       ProjVert *a = &gProjVerts[idx[0] + g->base];
//   draw_group src/render/render.c:526  DrawIndexedPrimitiveVB(..., start + g->base, ...)
constexpr uint16_t kLastGroup = 0xFFFF;

// A record's index array length must fit in the 16-bit `next` field, and
// 0xFFFF is reserved as the terminator.
constexpr size_t kMaxIndicesPerRecord = 32766;  // 32766*2 = 65532 < 0xFFFF, and %3 == 0

struct DnerRecord {
    uint16_t indexCount = 0;
    uint16_t next = kLastGroup;
    uint16_t texture = 0xFFFF;
    uint16_t lightmap = 0xFFFF;
    uint16_t base = 0;
    uint16_t vertexCount = 0;
    uint16_t sorted = 0;
    uint16_t sorted3 = 0;  // format 3 stores `sorted` at +0x18 as well
    uint8_t unk1A[2] = {0, 0};
    uint8_t flags = 0;
    uint8_t unk1D[2] = {0, 0};
    uint8_t alpha = 0;
    std::vector<uint16_t> indices;
};

bool BuildDner(uint32_t format, const MefModel& model, std::vector<uint8_t>* out,
               std::string* error) {
    const size_t headerSize = (format == 3) ? 32 : 28;

    std::vector<DnerRecord> records;
    for (const MefGroup& group : model.groups) {
        if (group.indices.empty())
            continue;

        // Split a group whose index array cannot be described by the 16-bit
        // `next` field into consecutive records sharing its texture.
        for (size_t start = 0; start < group.indices.size(); start += kMaxIndicesPerRecord) {
            const size_t count = std::min(kMaxIndicesPerRecord, group.indices.size() - start);
            DnerRecord record;
            record.texture = (group.texture < 0) ? 0xFFFF : static_cast<uint16_t>(group.texture);
            record.lightmap = (group.lightmap < 0) ? 0xFFFF : static_cast<uint16_t>(group.lightmap);
            record.sorted = static_cast<uint16_t>(group.sorted);
            record.sorted3 = static_cast<uint16_t>(group.sorted);
            record.flags = static_cast<uint8_t>(group.flags);
            record.alpha = static_cast<uint8_t>(group.alpha);
            record.indices.reserve(count);

            uint32_t lo = std::numeric_limits<uint32_t>::max();
            uint32_t hi = 0;
            for (size_t i = 0; i < count; ++i) {
                const uint32_t index = group.indices[start + i];
                lo = std::min(lo, index);
                hi = std::max(hi, index);
            }
            if (hi > 0xFFFFu) {
                SetError(error, "vertex index " + std::to_string(hi) +
                                    " does not fit the 16-bit DNER index field");
                return false;
            }
            record.base = static_cast<uint16_t>(lo);
            const uint32_t span = hi - lo + 1;
            if (span > 0xFFFFu) {
                SetError(error, "a render group spans " + std::to_string(span) +
                                    " vertices, which does not fit the 16-bit `vertexCount` field");
                return false;
            }
            record.vertexCount = static_cast<uint16_t>(span);
            for (size_t i = 0; i < count; ++i)
                record.indices.push_back(static_cast<uint16_t>(group.indices[start + i] - lo));
            record.indexCount = static_cast<uint16_t>(record.indices.size());
            records.push_back(std::move(record));
        }
    }

    if (records.empty()) {
        SetError(error, "the mesh has no renderable triangles");
        return false;
    }

    out->clear();
    for (size_t i = 0; i < records.size(); ++i) {
        const DnerRecord& record = records[i];
        const bool last = (i + 1 == records.size());
        const size_t recordStart = out->size();
        // `next` = byte length of this record's index array (see citation above).
        const uint16_t next = last ? kLastGroup : static_cast<uint16_t>(record.indices.size() * 2);

        if (format == 3) {
            PutF32(*out, 0.f);  // center[3] — group centroid, used only when sorted
            PutF32(*out, 0.f);
            PutF32(*out, 0.f);
            PutU16(*out, record.indexCount);
            PutU16(*out, next);
            PutU16(*out, record.texture);
            PutU16(*out, record.lightmap);
            PutU16(*out, record.base);
            PutU16(*out, record.vertexCount);
            PutU16(*out, record.sorted);
            out->push_back(record.unk1A[0]);
            out->push_back(record.unk1A[1]);
            out->push_back(record.flags);
            out->push_back(record.unk1D[0]);
            out->push_back(record.unk1D[1]);
            out->push_back(record.alpha);
        } else {
            PutF32(*out, 0.f);  // center[3] — see above
            PutF32(*out, 0.f);
            PutF32(*out, 0.f);
            PutU16(*out, record.indexCount);
            PutU16(*out, next);
            PutU16(*out, record.texture);
            PutU16(*out, record.base);
            PutU16(*out, record.vertexCount);
            PutU16(*out, record.sorted);
            out->push_back(record.flags);
            out->push_back(record.unk1A[0]);
            out->push_back(record.unk1A[1]);
            out->push_back(record.alpha);
        }
        for (uint16_t index : record.indices)
            PutU16(*out, index);

        const size_t recordSize = headerSize + record.indices.size() * 2;
        if (out->size() - recordStart != recordSize) {
            SetError(error, "internal error: DNER record size mismatch");
            return false;
        }
    }
    return true;
}

}  // namespace

// ===========================================================================
// OBJ parsing
// ===========================================================================

bool ParseObjText(const std::string& text, ObjMesh* out, std::string* error) {
    if (!out) {
        SetError(error, "ParseObjText: null output");
        return false;
    }
    *out = ObjMesh();

    std::istringstream stream(text);
    std::string line;
    std::string currentMaterial;
    size_t lineNumber = 0;

    while (std::getline(stream, line)) {
        ++lineNumber;
        const std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#')
            continue;

        const std::vector<std::string> parts = SplitWhitespace(trimmed);
        if (parts.empty())
            continue;

        const std::string& keyword = parts[0];
        if (keyword == "v") {
            glm::vec3 p(0.f);
            for (size_t i = 1; i < parts.size() && i <= 3; ++i)
                p[static_cast<int>(i - 1)] = ParseFloatOr(parts[i], 0.f);
            out->positions.push_back(p);
        } else if (keyword == "vt") {
            glm::vec2 t(0.f);
            for (size_t i = 1; i < parts.size() && i <= 2; ++i)
                t[static_cast<int>(i - 1)] = ParseFloatOr(parts[i], 0.f);
            out->texcoords.push_back(t);
        } else if (keyword == "vn") {
            glm::vec3 n(0.f);
            for (size_t i = 1; i < parts.size() && i <= 3; ++i)
                n[static_cast<int>(i - 1)] = ParseFloatOr(parts[i], 0.f);
            out->normals.push_back(n);
        } else if (keyword == "f") {
            if (parts.size() < 4) {
                SetError(error, "OBJ line " + std::to_string(lineNumber) + ": a face needs at least 3 corners");
                return false;
            }
            std::vector<CornerSpec> corners;
            corners.reserve(parts.size() - 1);
            for (size_t i = 1; i < parts.size(); ++i) {
                CornerSpec corner;
                bool usedNegative = false;
                if (!ParseCorner(parts[i], *out, &corner, &usedNegative)) {
                    SetError(error, "OBJ line " + std::to_string(lineNumber) + ": cannot resolve corner '" +
                                        parts[i] + "'");
                    return false;
                }
                if (usedNegative)
                    ++out->negativeIndexCorners;
                corners.push_back(corner);
            }
            // Fan-triangulate around the first corner.
            for (size_t i = 1; i + 1 < corners.size(); ++i) {
                ObjFace face;
                const CornerSpec* tri[3] = {&corners[0], &corners[i], &corners[i + 1]};
                bool degenerate = false;
                for (int k = 0; k < 3; ++k) {
                    face.v[k] = tri[k]->v;
                    face.vt[k] = tri[k]->vt;
                    face.vn[k] = tri[k]->vn;
                }
                if (face.v[0] == face.v[1] || face.v[1] == face.v[2] || face.v[0] == face.v[2])
                    degenerate = true;
                if (degenerate) {
                    ++out->degenerateFaces;
                    continue;
                }
                out->faceMaterial.push_back(currentMaterial);
                out->faces.push_back(face);
            }
        } else if (keyword == "usemtl") {
            currentMaterial = (parts.size() >= 2) ? parts[1] : std::string();
            if (!currentMaterial.empty() &&
                std::find(out->materials.begin(), out->materials.end(), currentMaterial) ==
                    out->materials.end()) {
                out->materials.push_back(currentMaterial);
            }
        }
        // mtllib / g / o / s / others: no effect on the mesh data.
    }

    if (out->positions.empty()) {
        SetError(error, "the OBJ contains no vertices");
        return false;
    }
    if (out->faces.empty()) {
        SetError(error, "the OBJ contains no triangles");
        return false;
    }
    return true;
}

bool ParseObjFile(const std::string& path, ObjMesh* out, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        SetError(error, "cannot open OBJ '" + path + "'");
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (!ParseObjText(buffer.str(), out, error))
        return false;
    if (out)
        out->sourceName = path;
    return true;
}

// ===========================================================================
// MefModel construction
// ===========================================================================

bool BuildMefFromObj(const ObjMesh& obj, const WriteOptions& options, MefModel* out,
                     std::string* error) {
    if (!out) {
        SetError(error, "BuildMefFromObj: null output");
        return false;
    }
    if (options.format == 1) {
        SetError(error,
                 "format 1 (skeletal) output is not supported: an OBJ carries no skin weights or "
                 "bone indices, so a skeletal MEF cannot be written from it without inventing "
                 "skeleton data. Replace static models only.");
        return false;
    }
    if (options.format != 0 && options.format != 3) {
        SetError(error, "unsupported output format " + std::to_string(options.format) +
                            " (expected 0 or 3)");
        return false;
    }

    MefModel model;
    model.format = options.format;
    model.windingReversed = options.reverseWinding;
    model.useBoundsCentrePivot = options.useBoundsCentrePivot;
    model.pivot = options.pivot;

    // ---- weld corners into MEF vertices -----------------------------------
    std::unordered_map<CornerKey, uint32_t, CornerKeyHash> welded;
    welded.reserve(obj.faces.size() * 3);

    std::vector<std::array<uint32_t, 3>> triangles;
    triangles.reserve(obj.faces.size());
    std::vector<std::string> triangleMaterial;
    triangleMaterial.reserve(obj.faces.size());

    auto cornerVertex = [&](const ObjFace& face, int corner) -> uint32_t {
        CornerKey key;
        key.v = face.v[corner];
        key.vt = face.vt[corner];
        key.vn = face.vn[corner];
        const auto found = welded.find(key);
        if (found != welded.end())
            return found->second;

        MefVertex vertex;
        vertex.pos = obj.positions[key.v];
        if (key.vn != kNoObjIndex && key.vn < obj.normals.size())
            vertex.normal = obj.normals[key.vn];
        if (key.vt != kNoObjIndex && key.vt < obj.texcoords.size())
            vertex.uv = obj.texcoords[key.vt];

        const uint32_t index = static_cast<uint32_t>(model.vertices.size());
        model.vertices.push_back(vertex);
        welded.emplace(key, index);
        return index;
    };

    for (size_t i = 0; i < obj.faces.size(); ++i) {
        const ObjFace& face = obj.faces[i];
        uint32_t a = cornerVertex(face, 0);
        uint32_t b = cornerVertex(face, 1);
        uint32_t c = cornerVertex(face, 2);

        // Drop triangles with zero area: their face normal is undefined and the
        // engine would light them with a degenerate normal.
        const glm::vec3 e1 = model.vertices[b].pos - model.vertices[a].pos;
        const glm::vec3 e2 = model.vertices[c].pos - model.vertices[a].pos;
        if (glm::length(glm::cross(e1, e2)) == 0.f)
            continue;

        std::array<uint32_t, 3> triangle;
        if (options.reverseWinding) {
            // OBJ/Blender front faces are counter-clockwise seen from outside.
            // Retail packed DNER triangles are wound the other way: measured
            // (corpus) on 129,385 triangles, the geometric normal (b-a)x(c-a)
            // points *against* the vertex normal in 110,744 of them (86%),
            // while the engine culls counter-clockwise faces
            // (SRS(D3DRENDERSTATE_CULLMODE, D3DCULL_CCW), src/render/render.c:802).
            // {a,b,c} -> {a,c,b} makes an OBJ mesh match retail.
            triangle = {a, c, b};
        } else {
            triangle = {a, b, c};
        }
        triangles.push_back(triangle);
        triangleMaterial.push_back(i < obj.faceMaterial.size() ? obj.faceMaterial[i] : std::string());
    }

    if (triangles.empty()) {
        SetError(error, "every triangle in the OBJ is degenerate");
        return false;
    }
    model.sourceTriangles = triangles.size();

    if (model.vertices.size() > options.maxVertices) {
        SetError(error, "the OBJ welds to " + std::to_string(model.vertices.size()) +
                            " vertices, more than the " + std::to_string(options.maxVertices) +
                            " the 16-bit DNER group fields can address");
        return false;
    }

    // ---- fill in missing normals ------------------------------------------
    // D3DFVF_NORMAL is part of the format-0 vertex format
    // (gMeshFVF = {274, 530, 274}, src/dxinit/d3drender.c:356-357; 274 = 0x112 =
    // XYZ|NORMAL|TEX1) and the lit render state is selected in
    // Direct3DRender_DrawRigidMesh, src/render/render.c:975 (use_state(gD3DRenderStates, gLitIdx)),
    // so a zero normal would light the surface black.
    {
        std::vector<glm::vec3> accumulated(model.vertices.size(), glm::vec3(0.f));
        bool anyMissing = false;
        for (const auto& vertex : model.vertices) {
            if (glm::length(vertex.normal) == 0.f) {
                anyMissing = true;
                break;
            }
        }
        if (anyMissing) {
            for (const auto& triangle : triangles) {
                const glm::vec3& p0 = model.vertices[triangle[0]].pos;
                const glm::vec3& p1 = model.vertices[triangle[1]].pos;
                const glm::vec3& p2 = model.vertices[triangle[2]].pos;
                const glm::vec3 normal = glm::cross(p1 - p0, p2 - p0);  // area weighted
                for (uint32_t index : triangle)
                    accumulated[index] += normal;
            }
            for (size_t i = 0; i < model.vertices.size(); ++i) {
                if (glm::length(model.vertices[i].normal) != 0.f)
                    continue;
                const float len = glm::length(accumulated[i]);
                // The generated normal follows the stored winding, which was
                // reversed above, so flip it back to face the OBJ's front side.
                model.vertices[i].normal =
                    (len > 0.f) ? (accumulated[i] / len) * (options.reverseWinding ? -1.0f : 1.0f)
                                : glm::vec3(0.f, 0.f, 1.f);
            }
        }
    }

    // ---- group triangles by usemtl ----------------------------------------
    std::map<std::string, size_t> groupIndex;
    for (size_t i = 0; i < triangles.size(); ++i) {
        const std::string& material = triangleMaterial[i];
        auto found = groupIndex.find(material);
        if (found == groupIndex.end()) {
            MefGroup group;
            // The OBJ material name cannot be resolved to a texture slot here:
            // slots index the model's texture list (lod_texture(ml, g->texture),
            // src/render/render.c:525).  Assign slots in order of first use.
            group.texture = static_cast<int>(model.groups.size());
            found = groupIndex.emplace(material, model.groups.size()).first;
            model.groups.push_back(group);
        }
        MefGroup& group = model.groups[found->second];
        group.indices.push_back(triangles[i][0]);
        group.indices.push_back(triangles[i][1]);
        group.indices.push_back(triangles[i][2]);
    }

    // ---- collision ---------------------------------------------------------
    if (options.emitCollision) {
        model.hasCollision = true;
        model.collVertices.reserve(model.vertices.size());
        for (const auto& vertex : model.vertices) {
            MefCollVertex coll;
            coll.pos = vertex.pos;
            coll.bone = 0;  // measured (corpus): 0 in every static retail model
            model.collVertices.push_back(coll);
        }

        // One solid surface for the whole mesh.
        //
        // TAMC record (12 bytes), CollSurface / BoneCollFace:
        //   src/qasm/rigidhit.c:26-37, src/magicobj/rigidcoll.c:47-55,
        //   src/haze/bonecoll.c:31-42
        // `portal` (offset +4) must be 0xFFFF for the surface to be considered:
        //   rigidcoll.c:119  if (sf->unk4 != 0xFFFF) continue;
        // `material` == 0xFFFF blocks the surface completely:
        //   rigidcoll.c:216-219  if (sf->material == 0xFFFF) { q->visibility = 0; ... }
        //   bonecoll.c           same test on cm->faces[tri->face].material
        // The retail corpus contains exactly this record shape (dummy.mef TAMC,
        // 709_01_1.mef TAMC[6] = 0000803f ffffffff ffff0000).
        MefCollSurface surface;
        surface.opacity = 0x3F800000u;  // 1.0f, as in the retail all-blocking record
        surface.portal = 0xFFFF;
        surface.index = -1;
        surface.material = 0xFFFF;
        surface.unknown = 0;
        model.collSurfaces.push_back(surface);

        // ECFC record (8 bytes): RigidCollTri { uint16 v[3]; uint16 face; }
        //   src/qasm/rigidhit.c:11-24
        // `face` indexes the TAMC table (retail dummy.mef uses 0 and 1 for its
        // two TAMC records), so every triangle points at surface 0 here.
        for (const auto& triangle : triangles) {
            MefCollFace coll;
            coll.a = static_cast<uint16_t>(triangle[0]);
            coll.b = static_cast<uint16_t>(triangle[1]);
            coll.c = static_cast<uint16_t>(triangle[2]);
            coll.surface = 0;
            model.collFaces.push_back(coll);
        }
    }

    *out = std::move(model);
    return true;
}

bool BuildMefFromGeometry(const ParsedGeometry& geometry, const WriteOptions& options,
                          MefModel* out, std::string* error) {
    if (!out) {
        SetError(error, "BuildMefFromGeometry: null output");
        return false;
    }
    if (geometry.modelType == 1) {
        SetError(error,
                 "the reference MEF is a skeletal (format 1) model. Re-serialising it is refused: "
                 "the parsed geometry exposes no skin weights or bone indices, so writing it back "
                 "would silently drop the skeleton.");
        return false;
    }
    if (options.format == 1) {
        SetError(error, "format 1 (skeletal) output is not supported");
        return false;
    }
    if (geometry.vertices.empty() || geometry.triangles.empty()) {
        SetError(error, "the reference MEF has no renderable geometry");
        return false;
    }

    MefModel model;
    model.format = options.format;
    model.windingReversed = false;  // the parsed triangles are already retail order
    model.useBoundsCentrePivot = options.useBoundsCentrePivot;
    model.pivot = options.pivot;
    model.sourceTriangles = geometry.triangles.size();

    model.vertices.reserve(geometry.vertices.size());
    for (const auto& vertex : geometry.vertices) {
        MefVertex v;
        // rawPos is the unscaled XTRV position; MEF files store raw units.
        v.pos = vertex.rawPos;
        v.normal = vertex.normal;
        v.uv = vertex.uv;
        v.uv2 = vertex.uv2;
        model.vertices.push_back(v);
    }

    if (geometry.renderBlocks.empty()) {
        MefGroup group;
        group.texture = 0;
        for (const auto& triangle : geometry.triangles) {
            group.indices.push_back(triangle[0]);
            group.indices.push_back(triangle[1]);
            group.indices.push_back(triangle[2]);
        }
        model.groups.push_back(std::move(group));
    } else {
        for (const auto& block : geometry.renderBlocks) {
            if (block.triangleStart >= geometry.triangles.size())
                continue;
            const size_t end = std::min(geometry.triangles.size(),
                                        block.triangleStart + block.triangleCount);
            MefGroup group;
            group.texture = (block.materialSlot < 0) ? -1 : block.materialSlot;
            // ParsedGeometry exposes only the opacity bit of the group's sort
            // hint, so the round trip preserves `sorted` but not the original
            // alpha/flags bytes (mef_native.cpp is out of scope for this change).
            group.sorted = (block.opacity < 1.0f) ? 1 : 0;
            for (size_t t = block.triangleStart; t < end; ++t) {
                group.indices.push_back(geometry.triangles[t][0]);
                group.indices.push_back(geometry.triangles[t][1]);
                group.indices.push_back(geometry.triangles[t][2]);
            }
            if (!group.indices.empty())
                model.groups.push_back(std::move(group));
        }
    }

    if (model.groups.empty()) {
        SetError(error, "the reference MEF has no render groups");
        return false;
    }

    if (!geometry.xtvcVerts.empty() && !geometry.ecfcFaces.empty()) {
        model.hasCollision = true;
        for (const auto& vertex : geometry.xtvcVerts) {
            MefCollVertex coll;
            coll.pos = glm::vec3(vertex.px, vertex.py, vertex.pz);
            coll.bone = vertex.boneIndex;
            model.collVertices.push_back(coll);
        }
        for (const auto& face : geometry.ecfcFaces) {
            MefCollFace coll;
            coll.a = face.a;
            coll.b = face.b;
            coll.c = face.c;
            coll.surface = face.mat;
            model.collFaces.push_back(coll);
        }
        for (const auto& record : geometry.tamcRecords) {
            MefCollSurface surface;
            uint32_t opacityBits = 0;
            std::memcpy(&opacityBits, &record.opacity, sizeof(opacityBits));
            surface.opacity = opacityBits;
            surface.portal = record.portal;
            surface.index = record.diffuse;
            surface.material = static_cast<uint16_t>(record.matId);
            surface.unknown = record.unknown;
            model.collSurfaces.push_back(surface);
        }
    }

    *out = std::move(model);
    return true;
}

// ===========================================================================
// Serialisation
// ===========================================================================

bool SerialiseMef(const MefModel& model, std::vector<uint8_t>* out, std::string* error) {
    if (!out) {
        SetError(error, "SerialiseMef: null output");
        return false;
    }
    out->clear();

    if (model.format == 1) {
        SetError(error, "format 1 (skeletal) output is not supported");
        return false;
    }
    if (model.vertices.empty()) {
        SetError(error, "refusing to write a MEF with no vertices");
        return false;
    }
    if (model.vertices.size() > 0xFFFFu) {
        SetError(error, "vertex count " + std::to_string(model.vertices.size()) +
                            " exceeds the 16-bit DNER limit");
        return false;
    }

    // ---- XTRV -------------------------------------------------------------
    // Vertex strides come from MeshBatch_Flush HOOK 0x49B4B0,
    // src/dxinit/d3drender.c:188 (case 0: memcpy of vertexCount*0x20 from
    // RigidMesh+0x10) and :205 (case 3: memcpy of vertexCount*0x28 from
    // LmMesh+0x14).  Layout per vertex:
    //   format 0: pos[3] +12 normal[3] +24 uv[2]                       = 32 bytes
    //   format 3: pos[3] +12 normal[3] +24 uv[2] +32 uv2[2]            = 40 bytes
    // Field order is fixed by gMeshFVF = {274, 530, 274},
    // src/dxinit/d3drender.c:356-357: 274 = 0x112 = XYZ|NORMAL|TEX1,
    // 530 = 0x212 = XYZ|NORMAL|TEX2.
    std::vector<uint8_t> xtrv;
    xtrv.reserve(model.vertices.size() * (model.format == 3 ? 40 : 32));
    for (const auto& vertex : model.vertices) {
        PutF32(xtrv, vertex.pos.x);
        PutF32(xtrv, vertex.pos.y);
        PutF32(xtrv, vertex.pos.z);
        PutF32(xtrv, vertex.normal.x);
        PutF32(xtrv, vertex.normal.y);
        PutF32(xtrv, vertex.normal.z);
        PutF32(xtrv, vertex.uv.x);
        PutF32(xtrv, vertex.uv.y);
        if (model.format == 3) {
            PutF32(xtrv, vertex.uv2.x);
            PutF32(xtrv, vertex.uv2.y);
        }
    }

    // ---- DNER -------------------------------------------------------------
    std::vector<uint8_t> dner;
    if (!BuildDner(model.format, model, &dner, error))
        return false;

    size_t groupCount = 0;
    {
        // Count the records BuildDner emitted (a group may be split).
        size_t cursor = 0;
        const size_t headerSize = (model.format == 3) ? 32 : 28;
        while (cursor + headerSize <= dner.size()) {
            const uint16_t indexCount =
                static_cast<uint16_t>(dner[cursor + 12] | (dner[cursor + 13] << 8));
            const uint16_t next = static_cast<uint16_t>(dner[cursor + 14] | (dner[cursor + 15] << 8));
            ++groupCount;
            if (next == kLastGroup)
                break;
            cursor += headerSize + indexCount * 2u;
        }
    }

    // ---- D3DR -------------------------------------------------------------
    const std::vector<uint8_t> d3dr =
        BuildD3dr(model.format, static_cast<uint32_t>(model.sourceTriangles),
                  static_cast<uint32_t>(groupCount), static_cast<uint32_t>(model.vertices.size()));

    // ---- HSEM -------------------------------------------------------------
    const std::vector<uint8_t> hsem = BuildHsem(model, static_cast<uint32_t>(d3dr.size()),
                                                static_cast<uint32_t>(dner.size()),
                                                static_cast<uint32_t>(xtrv.size()));

    // ---- HSMC / XTVC / ECFC / TAMC / HPSC ---------------------------------
    //
    // HSMC is the 64-byte collision header read by Direct3DRender_LoadModel
    // HOOK 0x49FB10, src/bonemesh/d3drender.c:246-260: Chunk_Find(..., ID_CMSH)
    // ("HSMC"), then for i in 0..1 it fills part[i].verts/faces/mats/sphere from
    // the i-th XTVC/ECFC/TAMC/HPSC chunks.  Two 32-byte records follow the
    // 4 count words:
    //   part[i] = { int triCount; int vertCount; int matCount; int sphereCount;
    //               void *tris; void *verts; void *mats; void *sphere; }
    //   (BoneCollMesh src/haze/bonecoll.c:31-42, RigidCollMesh src/qasm/rigidhit.c:26-37,
    //    CollMesh src/magicobj/rigidcoll.c:47-55)
    // Counts are derived from the sibling chunk sizes: tris = ECFC.size/8,
    // verts = XTVC.size/16, mats = TAMC.size/12, sphere = HPSC.size/24 — which
    // reproduces the measured dummy.mef header (76, 40, 2, 151) exactly.
    std::vector<uint8_t> xtvc;
    std::vector<uint8_t> ecfc;
    std::vector<uint8_t> tamc;
    std::vector<uint8_t> hpsc;
    std::vector<uint8_t> hsmc;

    if (model.hasCollision && !model.collFaces.empty() && !model.collVertices.empty()) {
        // XTVC: RigidCollVert { float x, y, z; int unkC; }, 16 bytes
        //   src/qasm/rigidhit.c:11-14 (12-byte Vec3f + int), measured stride 16.
        xtvc.reserve(model.collVertices.size() * 16);
        for (const auto& vertex : model.collVertices) {
            PutF32(xtvc, vertex.pos.x);
            PutF32(xtvc, vertex.pos.y);
            PutF32(xtvc, vertex.pos.z);
            PutU32(xtvc, vertex.bone);
        }

        // HPSC: the sphere tree.  Built to satisfy the walk in
        // RigidDynCubeObj_CollLine HOOK 0x4C35F0, src/magicobj/rigidcoll.c:330-370.
        // It is built BEFORE ECFC on purpose: a leaf's (firstTri, triCount) is a
        // half-open range into the triangle array in the tree's own order, and the
        // engine reads ECFC[firstTri + t].  ECFC is therefore emitted in
        // tree.Order() below, so the ranges and the triangles cannot disagree.
        std::vector<glm::vec3> collPoints;
        collPoints.reserve(model.collVertices.size());
        for (const auto& vertex : model.collVertices)
            collPoints.push_back(vertex.pos);
        CollisionTreeBuilder tree(collPoints, model.collFaces);
        const std::vector<BvhNode> nodes = tree.Build();
        if (nodes.size() > 0xFFFFu) {
            SetError(error, "internal error: collision tree needs " + std::to_string(nodes.size()) +
                                " nodes, more than the 16-bit child/next fields allow");
            return false;
        }

        // ECFC: RigidCollTri { uint16 v[3]; uint16 face; }, 8 bytes
        //   src/qasm/rigidhit.c:11-24
        // Emitted in the tree's permutation, so ECFC[firstTri + t] really is the
        // triangle the node's sphere was built around.
        ecfc.reserve(model.collFaces.size() * 8);
        for (const uint32_t faceIndex : tree.Order()) {
            const MefCollFace& face = model.collFaces[faceIndex];
            PutU16(ecfc, face.a);
            PutU16(ecfc, face.b);
            PutU16(ecfc, face.c);
            PutU16(ecfc, face.surface);
        }

        // TAMC: CollSurface, 12 bytes — src/qasm/rigidhit.c:26-37
        tamc.reserve(model.collSurfaces.size() * 12);
        for (const auto& surface : model.collSurfaces) {
            PutU32(tamc, surface.opacity);
            PutU16(tamc, surface.portal);
            PutU16(tamc, static_cast<uint16_t>(surface.index));
            PutU16(tamc, surface.material);
            PutU16(tamc, surface.unknown);
        }

        hpsc.reserve(nodes.size() * 24);
        for (const auto& node : nodes) {
            PutF32(hpsc, node.center.x);
            PutF32(hpsc, node.center.y);
            PutF32(hpsc, node.center.z);
            PutF32(hpsc, node.radius);
            PutU16(hpsc, node.firstTri);
            PutU16(hpsc, node.triCount);
            PutU16(hpsc, node.child);
            PutU16(hpsc, node.next);
        }

        // HSMC: two 32-byte records, only the counts are meaningful on disk
        // (the four pointers are runtime addresses overwritten at load).
        for (int part = 0; part < 2; ++part) {
            const bool first = (part == 0);
            PutI32(hsmc, first ? static_cast<int32_t>(model.collFaces.size()) : 0);
            PutI32(hsmc, first ? static_cast<int32_t>(model.collVertices.size()) : 0);
            PutI32(hsmc, first ? static_cast<int32_t>(model.collSurfaces.size()) : 0);
            PutI32(hsmc, first ? static_cast<int32_t>(nodes.size()) : 0);
            PutU32(hsmc, 0);  // tris   pointer
            PutU32(hsmc, 0);  // verts  pointer
            PutU32(hsmc, 0);  // mats   pointer
            PutU32(hsmc, 0);  // sphere pointer
        }
    }

    // ---- assemble the chunk list ------------------------------------------
    // Chunk order mirrors retail (measured corpus): HSEM, ATTA, XTVM, TROP,
    // XVTP, CFTP, D3DR, DNER, XTRV, HSMC, XTVC, ECFC, TAMC, HPSC, then the
    // empty second collision set.  Only the order of HSEM/D3DR/DNER/XTRV is
    // load-bearing: the loader finds each chunk by id
    // (Chunk_Find HOOK 0x4B7590, src/res/chunk.c:74-81).
    std::vector<OutChunk> chunks;
    AddChunk(&chunks, "HSEM", hsem);
    AddChunk(&chunks, "ATTA", model.attachmentBytes);
    AddChunk(&chunks, "XTVM", {});
    AddChunk(&chunks, "TROP", {});
    AddChunk(&chunks, "XVTP", {});
    AddChunk(&chunks, "CFTP", {});
    AddChunk(&chunks, "D3DR", d3dr);
    AddChunk(&chunks, "DNER", dner);
    AddChunk(&chunks, "XTRV", xtrv);
    if (model.hasCollision) {
        AddChunk(&chunks, "HSMC", hsmc);
        AddChunk(&chunks, "XTVC", xtvc);
        AddChunk(&chunks, "ECFC", ecfc);
        AddChunk(&chunks, "TAMC", tamc);
        AddChunk(&chunks, "HPSC", hpsc);
        // Second collision set: empty in every retail file measured.
        AddChunk(&chunks, "XTVC", {});
        AddChunk(&chunks, "ECFC", {});
        AddChunk(&chunks, "TAMC", {});
        AddChunk(&chunks, "HPSC", {});
    }

    // ---- container --------------------------------------------------------
    // ILFF form header, 20 bytes:
    //   +0  "ILFF"
    //   +4  form size = whole file size
    //   +8  align = 4
    //   +12 next = 0
    //   +16 4-byte form id
    // Chunk_FirstChild HOOK 0x4B7490, src/res/chunk.c:17-32 puts the first child
    // at parent + ((align + 0x13) & ~(align - 1)) = parent + 20 for align 4, so
    // the 20-byte header is exactly what the game expects.  All 1578 retail
    // files use align 4, the form id bytes 4F 43 45 4D ("OCEM") and a chunk
    // payload at chunk+16 (Chunk_Data HOOK 0x4B74D0, src/res/chunk.c:34-49).
    size_t total = kIlffHeaderSize;
    for (const auto& chunk : chunks)
        total += Align4(kChunkHeaderSize + chunk.payload.size());

    out->reserve(total);
    out->push_back('I');
    out->push_back('L');
    out->push_back('F');
    out->push_back('F');
    PutU32(*out, static_cast<uint32_t>(total));
    PutU32(*out, 4);
    PutU32(*out, 0);
    out->push_back(0x4F);  // 'O'
    out->push_back(0x43);  // 'C'
    out->push_back(0x45);  // 'E'
    out->push_back(0x4D);  // 'M'

    for (size_t i = 0; i < chunks.size(); ++i) {
        const OutChunk& chunk = chunks[i];
        const size_t start = out->size();
        out->resize(start + 4);
        std::memcpy(out->data() + start, chunk.name, 4);
        PutU32(*out, static_cast<uint32_t>(chunk.payload.size()));
        PutU32(*out, 4);
        const bool last = (i + 1 == chunks.size());
        // `next` is the byte distance from this chunk's start to the next
        // chunk's start; 0 terminates the chain
        // (Chunk_Next HOOK 0x4B7470, src/res/chunk.c:9-15; the editor's reader
        // stops on skip == 0).
        PutU32(*out, last ? 0u : static_cast<uint32_t>(Align4(kChunkHeaderSize + chunk.payload.size())));
        out->insert(out->end(), chunk.payload.begin(), chunk.payload.end());
        while (out->size() < start + Align4(kChunkHeaderSize + chunk.payload.size()))
            out->push_back(0);
    }

    if (out->size() != total) {
        SetError(error, "internal error: wrote " + std::to_string(out->size()) +
                            " bytes but declared " + std::to_string(total));
        return false;
    }
    return true;
}

std::string DescribeMefModel(const MefModel& model) {
    std::ostringstream text;
    text << model.vertices.size() << " vertices, " << model.sourceTriangles << " triangles, "
         << model.groups.size() << " group" << (model.groups.size() == 1 ? "" : "s") << ", format "
         << model.format;
    if (model.hasCollision) {
        text << ", collision " << model.collVertices.size() << " verts/" << model.collFaces.size()
             << " faces/" << model.collSurfaces.size() << " surfaces";
    } else {
        text << ", no collision";
    }
    if (!model.attachmentBytes.empty())
        text << ", " << (model.attachmentBytes.size() / 68) << " attachments";
    if (model.windingReversed)
        text << ", winding reversed";
    return text.str();
}

// ===========================================================================
// File-level entry points
// ===========================================================================

namespace {

bool WriteBytesToFile(const std::string& path, const std::vector<uint8_t>& bytes,
                      std::string* error) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        SetError(error, "cannot open '" + path + "' for writing");
        return false;
    }
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    out.flush();
    if (!out) {
        out.close();
        std::remove(path.c_str());
        SetError(error, "failed to write '" + path + "'");
        return false;
    }
    out.close();
    if (!out) {
        std::remove(path.c_str());
        SetError(error, "failed to close '" + path + "'");
        return false;
    }
    return true;
}

// Copy the reference MEF's ATTA chunk and report whether the reference is a
// skeletal model (which makes the whole import a refusal).
bool LoadReference(const WriteOptions& options, MefModel* model, std::string* error) {
    if (options.sourceMefPath.empty())
        return true;

    std::vector<uint8_t> bytes;
    if (!ReadFileBytes(options.sourceMefPath, &bytes, error))
        return false;

    std::vector<RawChunk> chunks;
    if (!WalkIlff(bytes, &chunks, error))
        return false;

    const RawChunk* hsem = FindRaw(chunks, "HSEM");
    if (!hsem || hsem->size < 36) {
        SetError(error, "'" + options.sourceMefPath + "' has no usable HSEM chunk");
        return false;
    }
    const uint32_t format = ReadU32(bytes, hsem->payload + 32);
    if (format == 1) {
        SetError(error,
                 "the reference MEF '" + options.sourceMefPath +
                     "' is a skeletal (format 1) model. Refusing to import: an OBJ carries no skin "
                     "weights or bone indices, so writing it back would silently drop the skeleton. "
                     "Static models only (issue #29 scope).");
        return false;
    }

    if (!options.preserveAttachments)
        return true;

    const RawChunk* atta = FindRaw(chunks, "ATTA");
    if (atta && atta->size > 0) {
        if (atta->size % 68 != 0) {
            SetError(error, "the reference MEF's ATTA chunk is " + std::to_string(atta->size) +
                                " bytes, not a multiple of the 68-byte attachment record");
            return false;
        }
        model->attachmentBytes.assign(bytes.begin() + atta->payload,
                                     bytes.begin() + atta->payload + atta->size);
    }
    return true;
}

}  // namespace

bool WriteMefBytesFromObj(const std::string& objPath, const WriteOptions& options,
                          std::vector<uint8_t>* out, std::string* error) {
    ObjMesh obj;
    if (!ParseObjFile(objPath, &obj, error))
        return false;

    MefModel model;
    if (!BuildMefFromObj(obj, options, &model, error))
        return false;
    if (!LoadReference(options, &model, error))
        return false;
    return SerialiseMef(model, out, error);
}

bool WriteMefBytesFromGeometry(const ParsedGeometry& geometry, const WriteOptions& options,
                               std::vector<uint8_t>* out, std::string* error) {
    MefModel model;
    if (!BuildMefFromGeometry(geometry, options, &model, error))
        return false;
    if (!LoadReference(options, &model, error))
        return false;
    return SerialiseMef(model, out, error);
}

bool WriteMefFromObj(const std::string& objPath, const std::string& outMefPath,
                     const WriteOptions& options, std::string* error) {
    if (outMefPath.empty()) {
        SetError(error, "--import-mef needs an output .mef path");
        return false;
    }
    std::vector<uint8_t> bytes;
    if (!WriteMefBytesFromObj(objPath, options, &bytes, error))
        return false;
    return WriteBytesToFile(outMefPath, bytes, error);
}

bool WriteMefFromObj(const std::string& objPath, const std::string& outMefPath,
                     std::string* error) {
    return WriteMefFromObj(objPath, outMefPath, WriteOptions(), error);
}

// ===========================================================================
// CLI glue
// ===========================================================================

bool IsImportMefInvocation(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--import-mef") == 0)
            return true;
    }
    return false;
}

bool ParseImportMefRequest(int argc, char** argv, ImportMefRequest* out, std::string* error) {
    if (!out) {
        SetError(error, "ParseImportMefRequest: null output");
        return false;
    }
    *out = ImportMefRequest();

    std::string mefOperand;
    std::vector<std::string> importOperands;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--import-mef") {
            int collected = 0;
            while (i + 1 < argc && collected < 2) {
                const std::string next = argv[i + 1];
                if (!next.empty() && next[0] == '-')
                    break;
                importOperands.push_back(next);
                ++i;
                ++collected;
            }
        } else if (arg == "--mef" && i + 1 < argc) {
            mefOperand = argv[++i];
        } else if (arg == "--source-mef" && i + 1 < argc) {
            out->options.sourceMefPath = argv[++i];
        } else if (arg == "--mef-format" && i + 1 < argc) {
            const std::string value = argv[++i];
            if (value == "0") {
                out->options.format = 0;
            } else if (value == "3") {
                out->options.format = 3;
            } else {
                SetError(error, "--mef-format expects 0 or 3, got '" + value + "'");
                return false;
            }
        } else if (arg == "--keep-winding") {
            out->options.reverseWinding = false;
        } else if (arg == "--no-collision") {
            out->options.emitCollision = false;
        } else if (arg == "--pivot-origin") {
            out->options.useBoundsCentrePivot = false;
            out->options.pivot = glm::vec3(0.f);
        }
    }

    if (importOperands.size() == 2) {
        out->inputObjPath = importOperands[0];
        out->outputMefPath = importOperands[1];
    } else if (importOperands.size() == 1) {
        out->outputMefPath = importOperands[0];
        out->inputObjPath = mefOperand;
    } else if (importOperands.empty()) {
        out->inputObjPath = mefOperand;
    } else {
        SetError(error, "--import-mef takes at most an input .obj and an output .mef");
        return false;
    }

    if (out->inputObjPath.empty()) {
        SetError(error,
                 "no input OBJ. Use: igi1ed.exe --mef <input.obj> --import-mef <output.mef> "
                 "(or --import-mef <input.obj> <output.mef>)");
        return false;
    }
    if (out->outputMefPath.empty()) {
        SetError(error, "no output MEF. Use: igi1ed.exe --mef <input.obj> --import-mef <output.mef>");
        return false;
    }
    return true;
}

int RunImportMefCli(int argc, char** argv) {
    ImportMefRequest request;
    std::string error;
    if (!ParseImportMefRequest(argc, argv, &request, &error)) {
        std::cerr << "[MEF] " << error << std::endl;
        return 2;
    }

    std::cout << "[MEF] Importing OBJ '" << request.inputObjPath << "' -> '"
              << request.outputMefPath << "'" << std::endl;

    // Build everything before touching the disk, so a refusal (a skeletal
    // reference MEF, an unreadable OBJ, ...) never leaves a file behind.
    ObjMesh obj;
    if (!ParseObjFile(request.inputObjPath, &obj, &error)) {
        std::cerr << "[MEF] FAILED: " << error << std::endl;
        return 1;
    }
    MefModel model;
    if (!BuildMefFromObj(obj, request.options, &model, &error)) {
        std::cerr << "[MEF] FAILED: " << error << std::endl;
        return 1;
    }
    if (!LoadReference(request.options, &model, &error)) {
        std::cerr << "[MEF] FAILED: " << error << std::endl;
        return 1;
    }

    std::vector<uint8_t> bytes;
    if (!SerialiseMef(model, &bytes, &error)) {
        std::cerr << "[MEF] FAILED: " << error << std::endl;
        return 1;
    }

    if (!WriteBytesToFile(request.outputMefPath, bytes, &error)) {
        std::cerr << "[MEF] FAILED: " << error << std::endl;
        return 1;
    }

    std::cout << "[MEF] " << DescribeMefModel(model) << std::endl;
    std::cout << "[MEF] SUCCESS: wrote " << bytes.size() << " bytes to '"
              << request.outputMefPath << "'" << std::endl;
    return 0;
}

}  // namespace mef_writer
