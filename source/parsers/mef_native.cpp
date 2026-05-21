#include "mef_native.h"

#include <glm/gtc/type_ptr.hpp>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>
#include "renderer/hardcoded_bones.h"

// ---------------------------------------------------------------------------
// Internal types
// ---------------------------------------------------------------------------

namespace {

struct ChunkInfo {
    std::string name;
    uint32_t size  = 0;
    uint32_t align = 0;
    uint32_t skip  = 0;
    size_t   start = 0;
    size_t   data  = 0;
};

struct D3drInfo {
    uint32_t numFaces  = 0;
    uint32_t numMeshes = 0;
    uint32_t verts0    = 0;
    uint32_t verts1    = 0;
    uint32_t numVerts  = 0;
    uint32_t rawSize   = 0;
    bool     valid     = false;
};

constexpr uint32_t kIlffHeaderSize  = 20;
constexpr uint32_t kChunkHeaderSize = 16;

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------

template <typename T>
T ReadValue(const std::vector<uint8_t>& bytes, size_t offset) {
    if (offset + sizeof(T) > bytes.size()) {
        throw std::runtime_error("Unexpected end of MEF stream");
    }
    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return value;
}

std::vector<uint8_t> ReadWholeFile(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open MEF file: " + filepath);
    }

    const std::streamsize size = file.tellg();
    if (size < 0) {
        throw std::runtime_error("Failed to read MEF file size: " + filepath);
    }

    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    if (!bytes.empty() && !file.read(reinterpret_cast<char*>(bytes.data()), size)) {
        throw std::runtime_error("Failed to read MEF file bytes: " + filepath);
    }

    return bytes;
}

// ---------------------------------------------------------------------------
// ILFF chunk parsing
// ---------------------------------------------------------------------------

std::vector<ChunkInfo> ParseIlffChunks(const std::vector<uint8_t>& bytes, const std::string& filepath) {
    if (bytes.size() < kIlffHeaderSize) {
        throw std::runtime_error("MEF file too small for ILFF header: " + filepath);
    }

    if (std::memcmp(bytes.data(), "ILFF", 4) != 0) {
        throw std::runtime_error("Invalid MEF magic header (expected ILFF): " + filepath);
    }

    const uint32_t declaredSize = ReadValue<uint32_t>(bytes, 4);
    const uint32_t align        = ReadValue<uint32_t>(bytes, 8);
    const uint32_t skip         = ReadValue<uint32_t>(bytes, 12);

    if (declaredSize != bytes.size()) {
        throw std::runtime_error("ILFF size mismatch in: " + filepath);
    }
    if (align != 4) {
        throw std::runtime_error("Unsupported ILFF alignment in: " + filepath);
    }
    if (skip != 0) {
        throw std::runtime_error("Unsupported ILFF skip field in: " + filepath);
    }

    std::vector<ChunkInfo> chunks;
    size_t pos = kIlffHeaderSize;

    while (pos + kChunkHeaderSize <= bytes.size()) {
        ChunkInfo chunk;
        chunk.start = pos;
        chunk.name.assign(reinterpret_cast<const char*>(bytes.data() + pos), 4);
        chunk.size  = ReadValue<uint32_t>(bytes, pos + 4);
        chunk.align = ReadValue<uint32_t>(bytes, pos + 8);
        chunk.skip  = ReadValue<uint32_t>(bytes, pos + 12);
        chunk.data  = pos + kChunkHeaderSize;

        if (chunk.data + chunk.size > bytes.size()) {
            throw std::runtime_error("MEF chunk exceeds file bounds in: " + filepath);
        }

        chunks.push_back(chunk);

        if (chunk.skip == 0) {
            pos = chunk.data + chunk.size;
            break;
        }

        const size_t nextPos = chunk.start + chunk.skip;
        if (nextPos <= chunk.start || nextPos + kChunkHeaderSize > bytes.size()) {
            throw std::runtime_error("Invalid ILFF chunk link in: " + filepath);
        }

        pos = nextPos;
    }

    if (chunks.empty()) {
        throw std::runtime_error("MEF file contains no ILFF chunks: " + filepath);
    }

    return chunks;
}

std::string DescribeChunks(const std::vector<ChunkInfo>& chunks) {
    std::string text;
    for (size_t i = 0; i < chunks.size(); ++i) {
        const auto& chunk = chunks[i];
        if (!text.empty()) {
            text += " | ";
        }
        text += chunk.name +
            "(size=" + std::to_string(chunk.size) +
            ", skip=" + std::to_string(chunk.skip) +
            ", data=" + std::to_string(chunk.data) + ")";
    }
    return text;
}

const ChunkInfo* FindChunk(const std::vector<ChunkInfo>& chunks, std::string_view name, size_t ordinal = 0) {
    size_t seen = 0;
    for (const auto& chunk : chunks) {
        if (chunk.name == name) {
            if (seen == ordinal) {
                return &chunk;
            }
            ++seen;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// HSEM / D3DR header parsers
// ---------------------------------------------------------------------------

uint32_t ReadModelType(const std::vector<uint8_t>& bytes, const std::vector<ChunkInfo>& chunks) {
    const ChunkInfo* hsem = FindChunk(chunks, "HSEM");
    if (!hsem || hsem->size < 36) {
        return 0;
    }
    return ReadValue<uint32_t>(bytes, hsem->data + 32);
}

D3drInfo ReadD3drInfo(const std::vector<uint8_t>& bytes, const std::vector<ChunkInfo>& chunks, uint32_t modelType) {
    D3drInfo info;
    const ChunkInfo* d3dr = FindChunk(chunks, "D3DR");
    if (!d3dr) {
        return info;
    }

    info.rawSize = d3dr->size;
    if (d3dr->size < 16) {
        return info;
    }

    info.valid = true;
    if (modelType == 1) {
        if (d3dr->size < 24) {
            info.valid = false;
            return info;
        }
        info.numFaces  = ReadValue<uint32_t>(bytes, d3dr->data + 4);
        info.numMeshes = ReadValue<uint32_t>(bytes, d3dr->data + 8);
        info.verts0    = ReadValue<uint32_t>(bytes, d3dr->data + 12);
        info.verts1    = ReadValue<uint32_t>(bytes, d3dr->data + 16);
        info.numVerts  = ReadValue<uint32_t>(bytes, d3dr->data + 20);
    } else if (modelType == 3) {
        if (d3dr->size < 20) {
            info.valid = false;
            return info;
        }
        info.numFaces  = ReadValue<uint32_t>(bytes, d3dr->data + 8);
        info.numMeshes = ReadValue<uint32_t>(bytes, d3dr->data + 12);
        info.numVerts  = ReadValue<uint32_t>(bytes, d3dr->data + 16);
    } else {
        info.numFaces  = ReadValue<uint32_t>(bytes, d3dr->data + 4);
        info.numMeshes = ReadValue<uint32_t>(bytes, d3dr->data + 8);
        info.numVerts  = ReadValue<uint32_t>(bytes, d3dr->data + 12);
    }

    return info;
}

// ---------------------------------------------------------------------------
// XTRV vertex parser
// ---------------------------------------------------------------------------

std::vector<RenderVertex> ParseRenderVertices(const std::vector<uint8_t>& bytes, const ChunkInfo& chunk, uint32_t modelType) {
    uint32_t vertexSize = 0;
    uint32_t uvOffset   = 24; // default: pos(12) + normal(12) + uv(8)
    switch (modelType) {
    case 0:
        vertexSize = 32;
        uvOffset   = 24;  // pos(12) + normal(12) + uv(8)
        break;
    case 1:
        vertexSize = 40;
        uvOffset   = 24;  // pos(12) + normal(12) + uv0(8) + w(4) + vn(2) + bn(2)
        break;
    case 3:
        vertexSize = 40;
        uvOffset   = 24;  // pos(12) + normal(12) + uv(8) + ...
        break;
    default:
        throw std::runtime_error("Unsupported MEF modelType in XTRV");
    }

    const size_t count = chunk.size / vertexSize;
    std::vector<RenderVertex> vertices(count);

    for (size_t i = 0; i < count; ++i) {
        const size_t base = chunk.data + i * vertexSize;

        glm::vec3 rawPos(
            ReadValue<float>(bytes, base + 0),
            ReadValue<float>(bytes, base + 4),
            ReadValue<float>(bytes, base + 8)
        );
        vertices[i].rawPos = rawPos;
        vertices[i].pos = rawPos * kMefNativeScale;

        // Normal is present for all model types at +12..+23
        vertices[i].normal = glm::vec3(
            ReadValue<float>(bytes, base + 12),
            ReadValue<float>(bytes, base + 16),
            ReadValue<float>(bytes, base + 20)
        );

        vertices[i].uv = glm::vec2(
            ReadValue<float>(bytes, base + uvOffset),
            ReadValue<float>(bytes, base + uvOffset + 4)
        );

        if (modelType == 1) {
            vertices[i].weight        = ReadValue<float>   (bytes, base + 32);
            vertices[i].localVertexId = ReadValue<uint16_t>(bytes, base + 36);
            vertices[i].boneIndex     = ReadValue<uint16_t>(bytes, base + 38);
        }
    }

    return vertices;
}

// ---------------------------------------------------------------------------
// DNER/ECAF triangle parsers
// ---------------------------------------------------------------------------

std::vector<std::array<uint32_t, 3>> ParsePackedRenderTriangles(
    const std::vector<uint8_t>& bytes,
    const ChunkInfo& chunk,
    uint32_t modelType,
    std::vector<ParsedGeometry::RenderBlock>& outBlocks,
    size_t& outBlockCount)
{
    const size_t headerSize = (modelType == 3) ? 32 : 28;
    std::vector<std::array<uint32_t, 3>> triangles;
    size_t cursor    = 0;
    size_t blockCount = 0;

    while (cursor + headerSize <= chunk.size) {
        const size_t base          = chunk.data + cursor;
        const uint16_t indexCount   = ReadValue<uint16_t>(bytes, base + 12);
        const int16_t  nextoffs    = ReadValue<int16_t> (bytes, base + 14);
        const int16_t  materialSlot = (modelType == 3)
            ? ReadValue<int16_t>(bytes, base + 16)
            : static_cast<int16_t>(blockCount);
        const uint16_t vertsOffset = (modelType == 3)
            ? ReadValue<uint16_t>(bytes, base + 20)
            : ReadValue<uint16_t>(bytes, base + 18);
        const uint16_t vertsCount  = (modelType == 3)
            ? ReadValue<uint16_t>(bytes, base + 22)
            : ReadValue<uint16_t>(bytes, base + 20);
        (void)vertsCount; // used for diagnostics only
        const size_t indexBytes    = static_cast<size_t>(indexCount) * sizeof(uint16_t);

        if (cursor + headerSize + indexBytes > chunk.size) {
            break;
        }

        const size_t facesBase         = base + headerSize;
        const size_t blockTriangleStart = triangles.size();

        for (uint32_t i = 0; i + 2 < indexCount; i += 3) {
            const uint16_t a = ReadValue<uint16_t>(bytes, facesBase + static_cast<size_t>(i + 0) * sizeof(uint16_t));
            const uint16_t b = ReadValue<uint16_t>(bytes, facesBase + static_cast<size_t>(i + 1) * sizeof(uint16_t));
            const uint16_t c = ReadValue<uint16_t>(bytes, facesBase + static_cast<size_t>(i + 2) * sizeof(uint16_t));
            triangles.push_back({
                static_cast<uint32_t>(vertsOffset) + a,
                static_cast<uint32_t>(vertsOffset) + b,
                static_cast<uint32_t>(vertsOffset) + c
            });
        }

        const size_t blockTriangleCount = triangles.size() - blockTriangleStart;
        if (blockTriangleCount > 0) {
            outBlocks.push_back({ blockTriangleStart, blockTriangleCount, materialSlot });
        }

        ++blockCount;
        cursor += headerSize + indexBytes;
        // modelType 3 (buildings): base+14 is NOT a linked-list nextoffs field —
        // the ASCII-export path never reads it. Rely solely on the while-loop
        // boundary to stop. For other model types keep the -1 sentinel.
        if (modelType != 3 && nextoffs == -1) {
            break;
        }
    }

    outBlockCount = blockCount;
    return triangles;
}

std::vector<std::array<uint32_t, 3>> ParseSplitBoneTriangles(
    const std::vector<uint8_t>& bytes,
    const ChunkInfo& dnerChunk,
    const ChunkInfo& ecafChunk,
    const D3drInfo& d3drInfo,
    std::vector<ParsedGeometry::RenderBlock>& outBlocks,
    size_t& outBlockCount)
{
    // Bone DNER records in IGI 1 are consistently 32 bytes for models using ECAF.
    const size_t kBoneRecordSize = 32;
    const size_t blockCount      = d3drInfo.numMeshes;

    if (dnerChunk.size < blockCount * kBoneRecordSize) {
        return {};
    }

    std::vector<std::array<uint32_t, 3>> triangles;
    const size_t totalIndicesInECAF = ecafChunk.size / sizeof(uint16_t);

    for (size_t block = 0; block < blockCount; ++block) {
        const size_t base = dnerChunk.data + block * kBoneRecordSize;

        // IGI 1 Bone DNER Layout:
        //  0-11: floats
        // 12-13: numFace (embedded indices count, usually 0)
        // 14-15: nextOffs
        // 16-17: indexOffset (into ECAF chunk, in indices)
        // 18-19: triangleCount
        // 20-21: vertsOffset
        // 22-23: vertsCount

        const uint16_t u12         = ReadValue<uint16_t>(bytes, base + 12);
        const uint16_t u14         = ReadValue<uint16_t>(bytes, base + 14);
        const uint16_t indexOffset = ReadValue<uint16_t>(bytes, base + 16);
        const uint16_t u18         = ReadValue<uint16_t>(bytes, base + 18);
        const uint16_t vertsOffset = ReadValue<uint16_t>(bytes, base + 20);

        // IGI 1 bone models have inconsistent field usage for face/triangle counts.
        size_t triangleCount = 0;
        if (u12 > 0)      triangleCount = u12 / 3;
        else if (u18 > 0) triangleCount = u18;
        else if (u14 > 0) triangleCount = u14;

        const size_t blockTriangleStart = triangles.size();

        for (size_t tri = 0; tri < triangleCount; ++tri) {
            const size_t firstIndex = static_cast<size_t>(indexOffset) + tri * 3;
            if (firstIndex + 2 >= totalIndicesInECAF) break;

            const size_t indexBase = ecafChunk.data + firstIndex * sizeof(uint16_t);
            const uint16_t a = ReadValue<uint16_t>(bytes, indexBase + 0);
            const uint16_t b = ReadValue<uint16_t>(bytes, indexBase + 2);
            const uint16_t c = ReadValue<uint16_t>(bytes, indexBase + 4);

            // Winding order: IGI 1 bone models use CW winding in raw data.
            // Swap to {a, c, b} to match reference dconv OBJ export and OpenGL CCW expectation.
            triangles.push_back({
                static_cast<uint32_t>(a + vertsOffset),
                static_cast<uint32_t>(c + vertsOffset),
                static_cast<uint32_t>(b + vertsOffset)
            });
        }

        // Read td (diffuse texture index) from bone DNER record at base+24
        // This determines which texture from the DAT file this submesh uses.
        const int16_t td = ReadValue<int16_t>(bytes, base + 24);
        // Use td as the material slot when valid; fall back to sequential block index.
        const int materialSlot = (td >= 0) ? static_cast<int>(td) : static_cast<int>(block);

        if (triangles.size() > blockTriangleStart) {
            outBlocks.push_back({ blockTriangleStart, triangles.size() - blockTriangleStart, materialSlot });
        }
    }

    outBlockCount = outBlocks.size();
    return triangles;
}

// ---------------------------------------------------------------------------
// Collision geometry fallback
// ---------------------------------------------------------------------------

ParsedGeometry ParseCollisionGeometry(const std::vector<uint8_t>& bytes, const std::vector<ChunkInfo>& chunks) {
    ParsedGeometry geometry;

    const ChunkInfo* xtvc = FindChunk(chunks, "XTVC");
    const ChunkInfo* ecfc = FindChunk(chunks, "ECFC");
    if (!xtvc || !ecfc) {
        return geometry;
    }

    const size_t vertexCount = xtvc->size / 16;
    geometry.vertices.resize(vertexCount);

    for (size_t i = 0; i < vertexCount; ++i) {
        const size_t base = xtvc->data + i * 16;
        const float x = ReadValue<float>(bytes, base + 0);
        const float y = ReadValue<float>(bytes, base + 4);
        const float z = ReadValue<float>(bytes, base + 8);
        geometry.vertices[i].pos = glm::vec3(x, y, z) * kMefNativeScale;
        geometry.vertices[i].uv  = glm::vec2(x * 0.1f, z * 0.1f);
    }

    const size_t faceCount = ecfc->size / 8;
    geometry.triangles.reserve(faceCount);
    for (size_t i = 0; i < faceCount; ++i) {
        const size_t base = ecfc->data + i * 8;
        geometry.triangles.push_back({
            ReadValue<uint16_t>(bytes, base + 0),
            ReadValue<uint16_t>(bytes, base + 2),
            ReadValue<uint16_t>(bytes, base + 4)
        });
    }

    geometry.collisionVertexCount = vertexCount;
    geometry.collisionFaceCount   = faceCount;

    return geometry;
}

// ---------------------------------------------------------------------------
// REIH bone hierarchy — returns vector<BoneInfo>
// ---------------------------------------------------------------------------

std::vector<BoneInfo> ParseBoneHierarchy(const std::vector<uint8_t>& bytes, const std::vector<ChunkInfo>& chunks) {
    const ChunkInfo* reih = FindChunk(chunks, "REIH");
    if (!reih || reih->size < 1) return {};

    const size_t sz       = reih->size;
    const size_t align    = sz % 13;
    const size_t numBones = (sz - align) / 13;
    const size_t p2Offset = reih->data + numBones + align;

    if (numBones == 0 || p2Offset + numBones * 12 > bytes.size()) return {};

    std::vector<BoneInfo> bones(numBones);
    std::vector<uint8_t> numChildArr(numBones);
    for (size_t i = 0; i < numBones; ++i) {
        numChildArr[i]      = bytes[reih->data + i];
        bones[i].numChild   = numChildArr[i];
        bones[i].pivot.x    = ReadValue<float>(bytes, p2Offset + i * 12 + 0);
        bones[i].pivot.y    = ReadValue<float>(bytes, p2Offset + i * 12 + 4);
        bones[i].pivot.z    = ReadValue<float>(bytes, p2Offset + i * 12 + 8);
    }

    // Reconstruct parent indices using breadth-first queue logic:
    // Bones are stored in level-order (BFS) traversal. Each node's children are
    // assigned to the next consecutive bone IDs in the flat array.
    bones[0].parent = -1;
    {
        // queue stores (boneId, remainingChildren)
        std::vector<std::pair<int,int>> bfsQueue;
        bfsQueue.reserve(numBones);
        bfsQueue.push_back({ 0, static_cast<int>(numChildArr[0]) });
        size_t head = 0;
        size_t nextBone = 1;
        while (head < bfsQueue.size() && nextBone < numBones) {
            auto& [parentId, remaining] = bfsQueue[head];
            if (remaining == 0) {
                ++head;
                continue;
            }
            --remaining;
            bones[nextBone].parent = parentId;
            bfsQueue.push_back({ static_cast<int>(nextBone),
                                  static_cast<int>(numChildArr[nextBone]) });
            ++nextBone;
        }
    }

    return bones;
}

// ---------------------------------------------------------------------------
// Helper: compute world positions from BoneInfo (for vertex baking)
// ---------------------------------------------------------------------------

std::vector<glm::vec3> ComputeBoneWorldPositions(const std::vector<BoneInfo>& bones) {
    const size_t numBones = bones.size();
    std::vector<glm::vec3> worldPos(numBones);
    for (size_t i = 0; i < numBones; ++i) {
        glm::vec3 acc = bones[i].pivot;
        int p = bones[i].parent;
        while (p != -1 && p < static_cast<int>(numBones)) { acc += bones[p].pivot; p = bones[p].parent; }
        worldPos[i] = acc;
    }
    return worldPos;
}

// ---------------------------------------------------------------------------
// MANB bone names
// ---------------------------------------------------------------------------

std::vector<std::string> ParseManbNames(const std::vector<uint8_t>& bytes, const std::vector<ChunkInfo>& chunks) {
    const ChunkInfo* manb = FindChunk(chunks, "MANB");
    if (!manb || manb->size == 0) return {};

    const size_t count = manb->size / 16;
    std::vector<std::string> names(count);
    for (size_t i = 0; i < count; ++i) {
        const size_t base = manb->data + i * 16;
        size_t len = 0;
        while (len < 16 && bytes[base + len] != 0) ++len;
        names[i] = std::string(reinterpret_cast<const char*>(bytes.data() + base), len);
    }
    return names;
}

// ---------------------------------------------------------------------------
// ATTA attachment records (72 bytes each)
// ---------------------------------------------------------------------------

std::vector<Attachment> ParseAttachments(const std::vector<uint8_t>& bytes, const std::vector<ChunkInfo>& chunks) {
    const ChunkInfo* atta = FindChunk(chunks, "ATTA");
    if (!atta || atta->size == 0) return {};

    const size_t stride = 72;
    const size_t count  = atta->size / stride;
    std::vector<Attachment> result(count);
    for (size_t i = 0; i < count; ++i) {
        const size_t base = atta->data + i * stride;

        size_t len = 0;
        while (len < 16 && bytes[base + len] != 0) ++len;
        result[i].name = std::string(reinterpret_cast<const char*>(bytes.data() + base), len);

        result[i].pos.x  = ReadValue<float>  (bytes, base + 16);
        result[i].pos.y  = ReadValue<float>  (bytes, base + 20);
        result[i].pos.z  = ReadValue<float>  (bytes, base + 24);
        result[i].boneId = ReadValue<int32_t>(bytes, base + 68);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Main geometry orchestrator
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// New parsers: XTVC, ECFC, ECAF, DNER records, TAMC
// ---------------------------------------------------------------------------

std::vector<MefAttachment> ParseMefAttachments(const std::vector<uint8_t>& bytes, const ChunkInfo& chunk) {
    const size_t stride = 72;
    const size_t count  = chunk.size / stride;
    std::vector<MefAttachment> attas(count);
    for (size_t i = 0; i < count; ++i) {
        const size_t base = chunk.data + i * stride;
        std::memcpy(attas[i].name, &bytes[base + 0], 16);
        attas[i].px        = ReadValue<float>   (bytes, base + 16);
        attas[i].py        = ReadValue<float>   (bytes, base + 20);
        attas[i].pz        = ReadValue<float>   (bytes, base + 24);
        attas[i].r00       = ReadValue<float>   (bytes, base + 28);
        attas[i].r01       = ReadValue<float>   (bytes, base + 32);
        attas[i].r02       = ReadValue<float>   (bytes, base + 36);
        attas[i].r03       = ReadValue<float>   (bytes, base + 40);
        attas[i].r04       = ReadValue<float>   (bytes, base + 44);
        attas[i].r05       = ReadValue<float>   (bytes, base + 48);
        attas[i].r06       = ReadValue<float>   (bytes, base + 52);
        attas[i].r07       = ReadValue<float>   (bytes, base + 56);
        attas[i].r08       = ReadValue<float>   (bytes, base + 60);
        attas[i].unknown09 = ReadValue<uint32_t>(bytes, base + 64);
        attas[i].boneId    = ReadValue<int32_t> (bytes, base + 68);
    }
    return attas;
}

std::vector<XtvmVertex> ParseXtvmVerts(const std::vector<uint8_t>& bytes, const ChunkInfo& chunk) {
    const size_t stride = 16;
    const size_t count  = chunk.size / stride;
    std::vector<XtvmVertex> verts(count);
    for (size_t i = 0; i < count; ++i) {
        const size_t base = chunk.data + i * stride;
        verts[i].px      = ReadValue<float>  (bytes, base + 0);
        verts[i].py      = ReadValue<float>  (bytes, base + 4);
        verts[i].pz      = ReadValue<float>  (bytes, base + 8);
        verts[i].unknown = ReadValue<int32_t>(bytes, base + 12);
    }
    return verts;
}

std::vector<XtvcVertex> ParseXtvcVerts(const std::vector<uint8_t>& bytes, const ChunkInfo& chunk, bool isIgi1) {
    if (isIgi1) {
        // XTVC IGI 1: 16 bytes per vertex
        const size_t stride = 16;
        const size_t count  = chunk.size / stride;
        std::vector<XtvcVertex> verts(count);
        for (size_t i = 0; i < count; ++i) {
            const size_t base = chunk.data + i * stride;
            verts[i].px        = ReadValue<float>   (bytes, base + 0);
            verts[i].py        = ReadValue<float>   (bytes, base + 4);
            verts[i].pz        = ReadValue<float>   (bytes, base + 8);
            verts[i].boneIndex = ReadValue<uint32_t>(bytes, base + 12);
            verts[i].reserved  = 0;
        }
        return verts;
    } else {
        // XTVC IGI 2: 20 bytes per vertex
        const size_t stride = 20;
        const size_t count  = chunk.size / stride;
        std::vector<XtvcVertex> verts(count);
        for (size_t i = 0; i < count; ++i) {
            const size_t base = chunk.data + i * stride;
            verts[i].px        = ReadValue<float>   (bytes, base + 0);
            verts[i].py        = ReadValue<float>   (bytes, base + 4);
            verts[i].pz        = ReadValue<float>   (bytes, base + 8);
            verts[i].boneIndex = ReadValue<uint32_t>(bytes, base + 12);
            verts[i].reserved  = ReadValue<uint32_t>(bytes, base + 16);
        }
        return verts;
    }
}

std::vector<EcfcFace> ParseEcfcFaces(const std::vector<uint8_t>& bytes, const ChunkInfo& chunk, bool isIgi1) {
    // ECFC: 8 bytes per face in IGI 1, 12 bytes per face in IGI 2
    const size_t stride = isIgi1 ? 8 : 12;
    const size_t count  = chunk.size / stride;
    std::vector<EcfcFace> faces(count);
    for (size_t i = 0; i < count; ++i) {
        const size_t base = chunk.data + i * stride;
        faces[i].a   = ReadValue<uint16_t>(bytes, base + 0);
        faces[i].b   = ReadValue<uint16_t>(bytes, base + 2);
        faces[i].c   = ReadValue<uint16_t>(bytes, base + 4);
        faces[i].mat = ReadValue<uint16_t>(bytes, base + 6);
        if (!isIgi1) {
            faces[i].lmp = ReadValue<uint16_t>(bytes, base + 8);
            faces[i].vrt = ReadValue<uint16_t>(bytes, base + 10);
        } else {
            faces[i].lmp = 0;
            faces[i].vrt = 0;
        }
    }
    return faces;
}

std::vector<EcafFace> ParseEcafFaces(const std::vector<uint8_t>& bytes, const ChunkInfo& chunk) {
    // ECAF: 6 bytes per face
    const size_t stride = 6;
    const size_t count  = chunk.size / stride;
    std::vector<EcafFace> faces(count);
    for (size_t i = 0; i < count; ++i) {
        const size_t base = chunk.data + i * stride;
        faces[i].a = ReadValue<uint16_t>(bytes, base + 0);
        faces[i].b = ReadValue<uint16_t>(bytes, base + 2);
        faces[i].c = ReadValue<uint16_t>(bytes, base + 4);
    }
    return faces;
}

std::vector<DnerRecord> ParseDnerRecords(const std::vector<uint8_t>& bytes, const ChunkInfo& chunk) {
    // DNER type0/type1: 32 bytes per record
    const size_t stride = 32;
    const size_t count  = chunk.size / stride;
    std::vector<DnerRecord> recs(count);
    for (size_t i = 0; i < count; ++i) {
        const size_t base = chunk.data + i * stride;
        recs[i].opacity     = ReadValue<uint8_t> (bytes, base + 0);
        recs[i].mshine      = ReadValue<uint8_t> (bytes, base + 1);
        recs[i].scolor      = ReadValue<uint8_t> (bytes, base + 2);
        recs[i].opacitd     = ReadValue<uint8_t> (bytes, base + 3);
        recs[i].px          = ReadValue<float>   (bytes, base + 4);
        recs[i].py          = ReadValue<float>   (bytes, base + 8);
        recs[i].pz          = ReadValue<float>   (bytes, base + 12);
        recs[i].offsetIndex = ReadValue<uint16_t>(bytes, base + 16);
        recs[i].numFace     = ReadValue<uint16_t>(bytes, base + 18);
        recs[i].offVerts    = ReadValue<uint16_t>(bytes, base + 20);
        recs[i].numVerts    = ReadValue<uint16_t>(bytes, base + 22);
        recs[i].td          = ReadValue<int16_t> (bytes, base + 24);
        recs[i].tb          = ReadValue<int16_t> (bytes, base + 26);
        recs[i].tr          = ReadValue<int16_t> (bytes, base + 28);
        recs[i].trd         = ReadValue<uint8_t> (bytes, base + 30);
        recs[i].tbd         = ReadValue<uint8_t> (bytes, base + 31);
    }
    return recs;
}

std::vector<TamcRecord> ParseTamcRecords(const std::vector<uint8_t>& bytes, const ChunkInfo& chunk, bool isIgi1) {
    if (isIgi1) {
        // TAMC IGI 1: 12 bytes per record
        const size_t stride = 12;
        const size_t count  = chunk.size / stride;
        std::vector<TamcRecord> recs(count);
        for (size_t i = 0; i < count; ++i) {
            const size_t base = chunk.data + i * stride;
            recs[i].opacity  = ReadValue<float>   (bytes, base + 0);
            recs[i].portal   = ReadValue<uint16_t>(bytes, base + 4);
            recs[i].diffuse  = ReadValue<int16_t> (bytes, base + 6);
            recs[i].matId    = ReadValue<int16_t> (bytes, base + 8);
            recs[i].unknown  = ReadValue<uint16_t>(bytes, base + 10);
            recs[i].unknown0 = 0;
            recs[i].unknown1 = 0;
        }
        return recs;
    } else {
        // TAMC IGI 2: 16 bytes per record
        const size_t stride = 16;
        const size_t count  = chunk.size / stride;
        std::vector<TamcRecord> recs(count);
        for (size_t i = 0; i < count; ++i) {
            const size_t base = chunk.data + i * stride;
            recs[i].opacity  = ReadValue<float>   (bytes, base + 0);
            recs[i].portal   = ReadValue<uint16_t>(bytes, base + 4);
            recs[i].diffuse  = ReadValue<int16_t> (bytes, base + 6);
            recs[i].unknown0 = ReadValue<uint16_t>(bytes, base + 8);
            recs[i].unknown1 = ReadValue<uint16_t>(bytes, base + 10);
            recs[i].matId    = ReadValue<int16_t> (bytes, base + 12);
            recs[i].unknown  = ReadValue<uint16_t>(bytes, base + 14);
        }
        return recs;
    }
}

ParsedGeometry ParseMefGeometry(const std::vector<uint8_t>& bytes, const std::vector<ChunkInfo>& chunks, const std::string& filepath = "") {
    ParsedGeometry geometry;
    const ChunkInfo* hsem = FindChunk(chunks, "HSEM");
    const bool isIgi1 = hsem && (hsem->size == 156);
    geometry.isIgi1 = isIgi1;

    const uint32_t modelType = ReadModelType(bytes, chunks);
    geometry.modelType = modelType;
    const D3drInfo d3drInfo = ReadD3drInfo(bytes, chunks, modelType);

    const ChunkInfo* xtrv = FindChunk(chunks, "XTRV");
    const ChunkInfo* dner = FindChunk(chunks, "DNER");
    const ChunkInfo* ecaf = FindChunk(chunks, "ECAF");

    if (xtrv && dner) {
        geometry.vertices = ParseRenderVertices(bytes, *xtrv, modelType);

        if (modelType == 1) {
            if (isIgi1) {
                std::string modelName;
                if (!filepath.empty()) {
                    std::string filename = filepath;
                    size_t lastSlash = filename.find_last_of("\\/");
                    if (lastSlash != std::string::npos) filename = filename.substr(lastSlash + 1);
                    size_t lastDot = filename.find_last_of(".");
                    modelName = (lastDot != std::string::npos) ? filename.substr(0, lastDot) : filename;
                }
                uint32_t maxBoneIdx = 0;
                for (const auto& v : geometry.vertices) {
                    if (v.boneIndex > maxBoneIdx) maxBoneIdx = v.boneIndex;
                }
                geometry.bones = GetIgi1HardcodedBones(modelName, maxBoneIdx);
            } else {
                geometry.bones = ParseBoneHierarchy(bytes, chunks);
                const std::vector<std::string> names = ParseManbNames(bytes, chunks);
                for (size_t i = 0; i < geometry.bones.size() && i < names.size(); ++i) {
                    geometry.bones[i].name = names[i];
                }
            }
            geometry.attachments = ParseAttachments(bytes, chunks);

            // Bake world-space bone offsets into vertex positions
            const std::vector<glm::vec3> boneWorldPos = ComputeBoneWorldPositions(geometry.bones);
            if (!boneWorldPos.empty()) {
                for (auto& v : geometry.vertices) {
                    if (v.boneIndex < static_cast<uint16_t>(boneWorldPos.size())) {
                        v.pos += boneWorldPos[v.boneIndex] * kMefNativeScale;
                    }
                }
            }
        }

        if (modelType == 1 && ecaf && d3drInfo.valid && d3drInfo.numMeshes > 0) {
            geometry.triangles = ParseSplitBoneTriangles(bytes, *dner, *ecaf, d3drInfo, geometry.renderBlocks, geometry.renderBlockCount);
            geometry.renderLayout = "type1 split ECAF/DNER";
            if (geometry.triangles.empty()) {
                // Split path failed, fall back to packed DNER parsing
                geometry.triangles = ParsePackedRenderTriangles(bytes, *dner, modelType, geometry.renderBlocks, geometry.renderBlockCount);
                geometry.renderLayout = "type1 packed DNER (split fallback)";
            }
        } else {
            geometry.triangles = ParsePackedRenderTriangles(bytes, *dner, modelType, geometry.renderBlocks, geometry.renderBlockCount);
            geometry.renderLayout = (modelType == 1) ? "type1 packed DNER" : "packed DNER";
        }

        geometry.fromRenderMesh = !geometry.vertices.empty() && !geometry.triangles.empty();
    }

    if (!geometry.fromRenderMesh) {
        geometry = ParseCollisionGeometry(bytes, chunks);
        geometry.renderLayout = "collision fallback";
    }

    // ---- Parse ASCII-export-specific chunks ----
    if (isIgi1) {
        if (dner) {
            // Parse inline DNER & ECAF for IGI 1
            size_t cursor = 0;
            uint32_t accumulatedIndicesCount = 0;
            while (cursor < dner->size) {
                if (modelType == 3) {
                    if (cursor + 32 > dner->size) break;
                    const size_t base = dner->data + cursor;
                    
                    DnerRecord rec;
                    rec.px = ReadValue<float>(bytes, base + 0);
                    rec.py = ReadValue<float>(bytes, base + 4);
                    rec.pz = ReadValue<float>(bytes, base + 8);
                    rec.numFace = ReadValue<uint16_t>(bytes, base + 12);
                    rec.td = ReadValue<int16_t>(bytes, base + 16);
                    rec.tb = ReadValue<int16_t>(bytes, base + 18); // _lmap
                    rec.offVerts = ReadValue<uint16_t>(bytes, base + 20);
                    rec.numVerts = ReadValue<uint16_t>(bytes, base + 22);
                    rec.rawOpacity = ReadValue<uint16_t>(bytes, base + 24);
                    rec.eflame = ReadValue<uint8_t>(bytes, base + 28);
                    rec.mshine = ReadValue<uint8_t>(bytes, base + 29);
                    rec.scolor = ReadValue<uint8_t>(bytes, base + 30); // _mcolor (stored in scolor)
                    rec.opacitd = ReadValue<uint8_t>(bytes, base + 31);
                    
                    uint8_t op = 0;
                    if (rec.rawOpacity != 0) op |= 4;
                    if (rec.scolor != 0)     op |= 2;
                    if (rec.eflame != 0)     op |= 1;
                    rec.opacity = op;
                    
                    rec.offsetIndex = accumulatedIndicesCount;
                    geometry.dnerRecords.push_back(rec);
                    
                    cursor += 32;
                    
                    if (cursor + rec.numFace * 2 > dner->size) break;
                    
                    for (uint16_t i = 0; i + 2 < rec.numFace; i += 3) {
                        EcafFace face;
                        face.a = ReadValue<uint16_t>(bytes, dner->data + cursor + i * 2) + rec.offVerts;
                        face.b = ReadValue<uint16_t>(bytes, dner->data + cursor + (i + 1) * 2) + rec.offVerts;
                        face.c = ReadValue<uint16_t>(bytes, dner->data + cursor + (i + 2) * 2) + rec.offVerts;
                        geometry.ecafFaces.push_back(face);
                    }
                    
                    accumulatedIndicesCount += rec.numFace;
                    cursor += rec.numFace * 2;
                } else {
                    if (cursor + 28 > dner->size) break;
                    const size_t base = dner->data + cursor;
                    
                    DnerRecord rec;
                    rec.px = ReadValue<float>(bytes, base + 0);
                    rec.py = ReadValue<float>(bytes, base + 4);
                    rec.pz = ReadValue<float>(bytes, base + 8);
                    rec.numFace = ReadValue<uint16_t>(bytes, base + 12);
                    rec.td = ReadValue<int16_t>(bytes, base + 16);
                    rec.offVerts = ReadValue<uint16_t>(bytes, base + 18);
                    rec.numVerts = ReadValue<uint16_t>(bytes, base + 20);
                    rec.rawOpacity = ReadValue<uint16_t>(bytes, base + 22);
                    rec.eflame = ReadValue<uint8_t>(bytes, base + 24);
                    rec.mshine = ReadValue<uint8_t>(bytes, base + 25);
                    rec.scolor = ReadValue<uint8_t>(bytes, base + 26); // _Scolor
                    rec.opacitd = ReadValue<uint8_t>(bytes, base + 27);
                    
                    uint8_t op = 0;
                    if (rec.rawOpacity != 0) op |= 4;
                    if (rec.scolor != 0)     op |= 2;
                    if (rec.eflame != 0)     op |= 1;
                    rec.opacity = op;
                    
                    rec.offsetIndex = accumulatedIndicesCount;
                    geometry.dnerRecords.push_back(rec);
                    
                    cursor += 28;
                    
                    if (cursor + rec.numFace * 2 > dner->size) break;
                    
                    for (uint16_t i = 0; i + 2 < rec.numFace; i += 3) {
                        EcafFace face;
                        face.a = ReadValue<uint16_t>(bytes, dner->data + cursor + i * 2) + rec.offVerts;
                        face.b = ReadValue<uint16_t>(bytes, dner->data + cursor + (i + 1) * 2) + rec.offVerts;
                        face.c = ReadValue<uint16_t>(bytes, dner->data + cursor + (i + 2) * 2) + rec.offVerts;
                        geometry.ecafFaces.push_back(face);
                    }
                    
                    accumulatedIndicesCount += rec.numFace;
                    cursor += rec.numFace * 2;
                }
            }
        }
    } else {
        if (dner) {
            geometry.dnerRecords = ParseDnerRecords(bytes, *dner);
        }
        if (ecaf) {
            geometry.ecafFaces = ParseEcafFaces(bytes, *ecaf);
        }
    }

    // XTVC0, ECFC0, TAMC0 (set 0), XTVC1, ECFC1, TAMC1 (set 1)
    const ChunkInfo* xtvc0 = FindChunk(chunks, "XTVC", 0);
    const ChunkInfo* ecfc0 = FindChunk(chunks, "ECFC", 0);
    const ChunkInfo* tamc0 = FindChunk(chunks, "TAMC", 0);
    const ChunkInfo* xtvc1 = FindChunk(chunks, "XTVC", 1);
    const ChunkInfo* ecfc1 = FindChunk(chunks, "ECFC", 1);
    const ChunkInfo* tamc1 = FindChunk(chunks, "TAMC", 1);
    const ChunkInfo* xtvm  = FindChunk(chunks, "XTVM", 0);
    const ChunkInfo* atta  = FindChunk(chunks, "ATTA", 0);

    if (xtvc0) geometry.xtvcVerts  = ParseXtvcVerts (bytes, *xtvc0, isIgi1);
    if (ecfc0) geometry.ecfcFaces  = ParseEcfcFaces (bytes, *ecfc0, isIgi1);
    if (tamc0) geometry.tamcRecords= ParseTamcRecords(bytes, *tamc0, isIgi1);
    if (xtvc1) geometry.xtvcVerts1 = ParseXtvcVerts (bytes, *xtvc1, isIgi1);
    if (ecfc1) geometry.ecfcFaces1 = ParseEcfcFaces (bytes, *ecfc1, isIgi1);
    if (tamc1) geometry.tamcRecords1= ParseTamcRecords(bytes, *tamc1, isIgi1);
    if (xtvm)  geometry.xtvmVerts  = ParseXtvmVerts (bytes, *xtvm);
    if (atta)  geometry.mefAttachments = ParseMefAttachments(bytes, *atta);

    return geometry;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

ParsedGeometry ParseMefFile(const std::string& filepath) {
    const std::vector<uint8_t> bytes  = ReadWholeFile(filepath);
    const std::vector<ChunkInfo> chunks = ParseIlffChunks(bytes, filepath);
    return ParseMefGeometry(bytes, chunks, filepath);
}
