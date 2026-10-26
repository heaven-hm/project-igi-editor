// Regression coverage for IGI-RE parity in the binary MEF parser:
// render partitions come from the REND/DNER group chain, walked by the record's
// own `next` link (0xFFFF = last), NOT from the RD3D header count field and NOT
// from the collision chunks XTVC/ECFC/TAMC.
//
// Oracle: /Users/haseeb-mir/Documents/Code/IGI-RE
//   src/bonemesh/d3drender.c:204-261  Direct3DRender_LoadModel (chunk ids, collision parts)
//   src/bonemesh/d3drender.c:163-183  format-1 draw walk: g += sizeof(MeshGroup) + g->next
//   src/render/render.c:487-489       format-0 walk: g += g->next + 0x1C
//   src/render/render.c:1047,1081     format-3 walk: g += g->next + 0x20
//   src/render/rendertypes.h:6-18     MeshGroup layout
//   src/render/render.c:40-54,151     LmGroup layout (format 3)
//   src/dxinit/d3drender.c:226-313    BoneMesh_Skin: extra influences at mesh+0xC,
//                                     base vertices at mesh+0x10, XTRV at mesh+0x18
//
// The fixtures below are synthetic: no retail 001_02_1.mef is available in this
// checkout, so these tests pin the parser to the decompiled game algorithm
// rather than to a captured asset.

#include <gtest/gtest.h>
#include "renderer/mef_native.h"

#include <cstring>
#include <string>
#include <vector>

namespace {

void PutU16(std::vector<uint8_t>& b, size_t off, uint16_t v) {
    std::memcpy(b.data() + off, &v, sizeof(v));
}

void PutU32(std::vector<uint8_t>& b, size_t off, uint32_t v) {
    std::memcpy(b.data() + off, &v, sizeof(v));
}

void PutF32(std::vector<uint8_t>& b, size_t off, float v) {
    std::memcpy(b.data() + off, &v, sizeof(v));
}

struct ChunkSpec {
    std::string name;
    std::vector<uint8_t> data;
};

// Builds an ILFF container the same way the editor's ParseIlffChunks expects it:
// 20-byte file header (magic, total size, align=4, skip=0), then chunk records of
// {id, size, align, next} + data, where `next` is the byte offset to the following
// chunk (0 = last chunk, which is how the game's Chunk_Next terminates).
std::vector<uint8_t> BuildMef(const std::vector<ChunkSpec>& specs) {
    std::vector<size_t> starts;
    size_t pos = 20;
    for (const auto& s : specs) {
        starts.push_back(pos);
        pos += 16 + s.data.size();
    }

    std::vector<uint8_t> out(pos, 0);
    std::memcpy(out.data(), "ILFF", 4);
    PutU32(out, 4, static_cast<uint32_t>(pos));
    PutU32(out, 8, 4);
    PutU32(out, 12, 0);

    for (size_t i = 0; i < specs.size(); ++i) {
        const size_t p = starts[i];
        std::memcpy(out.data() + p, specs[i].name.data(), 4);
        PutU32(out, p + 4, static_cast<uint32_t>(specs[i].data.size()));
        PutU32(out, p + 8, 4);
        PutU32(out, p + 12, (i + 1 < specs.size())
                                ? static_cast<uint32_t>(starts[i + 1] - p)
                                : 0u);
        std::memcpy(out.data() + p + 16, specs[i].data.data(), specs[i].data.size());
    }
    return out;
}

// HSEM must be exactly 156 bytes (mef_native.cpp:625) and carries the model type
// at +32 (ReadModelType, mef_native.cpp:173-179).
//
// The version word at +0 is not optional: Mesh_Load (HOOK 0x4CF390,
// src/model/model.c:367-373) refuses a mesh whose version is not 0.10, so a
// fixture that leaves it zero is a file the engine would reject.  Retail .mef
// files carry 0.1f there (mef_writer.cpp writes it, and all 2212 static retail
// models round-trip through it).
std::vector<uint8_t> MakeHsem(uint32_t modelType) {
    std::vector<uint8_t> d(156, 0);
    PutF32(d, 0, 0.1f);
    PutU32(d, 32, modelType);
    return d;
}

// Format-1 RD3D = MeshRender (bonemesh/bonemesh.h:21-31):
//   +0xC  extra influence count  (BoneMesh_Skin: *(int *)(mesh + 0xC))
//   +0x10 base vertex count      (BoneMesh_Skin: *(int *)(mesh + 0x10))
//   +0x14 numVerts
//   +0x18/+0x1C are the XTRV/REND pointers, patched in at load time.
std::vector<uint8_t> MakeD3dr(uint32_t extraInfluences, uint32_t baseVerts,
                              uint32_t numVerts, uint32_t headerCountField) {
    std::vector<uint8_t> d(32, 0);
    PutU32(d, 4, 0);                       // unread by the game
    PutU32(d, 8, headerCountField);        // unread by the game (was the editor's bound)
    PutU32(d, 12, extraInfluences);
    PutU32(d, 16, baseVerts);
    PutU32(d, 20, numVerts);
    return d;
}

// Format-1 XTRV = array of SkinVertex (src/dxinit/d3drender.c:37-52, 0x28 bytes):
//   p[3] @0, n[3] @12, uv[2] @24, w @32, target @36, bone @38
std::vector<uint8_t> MakeXtrv(size_t baseCount, size_t influenceCount) {
    std::vector<uint8_t> d((baseCount + influenceCount) * 40, 0);
    for (size_t i = 0; i < baseCount + influenceCount; ++i) {
        const size_t o = i * 40;
        PutF32(d, o + 0, static_cast<float>(i) * 10.0f);
        PutF32(d, o + 4, 0.0f);
        PutF32(d, o + 8, 0.0f);
        PutF32(d, o + 12, 0.0f);
        PutF32(d, o + 16, 1.0f);
        PutF32(d, o + 20, 0.0f);
        PutF32(d, o + 24, 0.25f);
        PutF32(d, o + 28, 0.75f);
        PutF32(d, o + 32, 1.0f);
        PutU16(d, o + 36, static_cast<uint16_t>(i)); // target
        PutU16(d, o + 38, 0);                        // bone
    }
    if (influenceCount > 0) {
        const size_t o = baseCount * 40;
        PutF32(d, o + 32, 0.5f);            // weight
        PutU16(d, o + 36, 0);               // accumulates into base vertex 0
    }
    return d;
}

// One REND/DNER record = MeshGroup (src/render/rendertypes.h:6-18):
//   indexCount@12, next@14, texture@16, base@18, vertexCount@20, indices@0x1C.
// `next` is the distance from the start of the indices to the next record, so it
// includes any alignment padding (game: g += g->next + 0x1C).
std::vector<uint8_t> MakeGroup(uint16_t indexCount, uint16_t next, uint16_t texture,
                               uint16_t base, uint16_t vertexCount,
                               const std::vector<uint16_t>& indices, size_t paddingBytes) {
    std::vector<uint8_t> d(28 + indexCount * 2 + paddingBytes, 0);
    PutU16(d, 12, indexCount);
    PutU16(d, 14, next);
    PutU16(d, 16, texture);
    PutU16(d, 18, base);
    PutU16(d, 20, vertexCount);
    for (size_t i = 0; i < indices.size(); ++i) {
        PutU16(d, 28 + i * 2, indices[i]);
    }
    return d;
}

std::vector<uint8_t> Concat(std::vector<std::vector<uint8_t>> parts) {
    std::vector<uint8_t> out;
    for (auto& p : parts) out.insert(out.end(), p.begin(), p.end());
    return out;
}

// A format-1 model with `numGroups` DNER records. The RD3D count field is set to
// `headerCountField` so a test can prove the parser does not depend on it.
std::vector<uint8_t> MakeType1Mef(uint32_t headerCountField,
                                  const std::vector<std::vector<uint8_t>>& groups,
                                  std::vector<ChunkSpec> extraChunks = {}) {
    std::vector<ChunkSpec> specs;
    specs.push_back({"HSEM", MakeHsem(1)});
    specs.push_back({"D3DR", MakeD3dr(1, 3, 4, headerCountField)});
    specs.push_back({"XTRV", MakeXtrv(3, 1)});
    specs.push_back({"DNER", Concat(groups)});
    for (auto& c : extraChunks) specs.push_back(c);
    return BuildMef(specs);
}

} // namespace

// The game's walk ends at the record whose `next` is 0xFFFF; the RD3D header
// field the parser used to bound the walk with is never read by the game
// (no reference to it exists in IGI-RE). With the header field deliberately
// smaller than the chain, every group must still be reported.
TEST(MefDualSetTest, RenderGroupChainIsNotBoundedByRd3dHeaderCount) {
    const std::vector<uint8_t> dner = Concat({
        MakeGroup(3, 6, 5, 0, 3, {0, 1, 2}, 0),   // texture 5
        MakeGroup(3, 0xFFFF, 9, 1, 3, {0, 1, 2}, 0), // texture 9, base 1
    });

    const ParsedGeometry geo = ParseMefFileFromMemory(MakeType1Mef(1, {dner}), "001_02_1");

    ASSERT_EQ(geo.renderBlocks.size(), 2u)
        << "both DNER records must be reported; the RD3D count field is not the chain length";
    EXPECT_EQ(geo.renderBlocks[0].materialSlot, 5);
    EXPECT_EQ(geo.renderBlocks[1].materialSlot, 9);

    // Triangles are addressed as group.base + raw index (game:
    // DrawIndexedPrimitiveVB(..., g->base + base, ...)).
    ASSERT_EQ(geo.triangles.size(), 2u);
    EXPECT_EQ(geo.triangles[0][0], 0u);
    EXPECT_EQ(geo.triangles[0][1], 1u);
    EXPECT_EQ(geo.triangles[0][2], 2u);
    EXPECT_EQ(geo.triangles[1][0], 1u);
    EXPECT_EQ(geo.triangles[1][1], 2u);
    EXPECT_EQ(geo.triangles[1][2], 3u);
}

// `next` is the distance from the end of a record's indices to the next record,
// so it carries alignment padding. Stepping by indexCount*2 instead desynchronises
// the walk and binds the following groups to the wrong material.
TEST(MefDualSetTest, RenderGroupChainFollowsPaddedNextLinks) {
    const std::vector<uint8_t> dner = Concat({
        MakeGroup(3, 8, 5, 0, 3, {0, 1, 2}, 2),      // next = 3*2 + 2 pad bytes
        MakeGroup(3, 0xFFFF, 9, 0, 3, {2, 1, 0}, 0),
    });

    const ParsedGeometry geo = ParseMefFileFromMemory(MakeType1Mef(7, {dner}), "001_02_1");

    ASSERT_EQ(geo.renderBlocks.size(), 2u);
    EXPECT_EQ(geo.renderBlocks[0].materialSlot, 5);
    EXPECT_EQ(geo.renderBlocks[1].materialSlot, 9);
    EXPECT_EQ(geo.renderBlockCount, 2u);
    ASSERT_EQ(geo.triangles.size(), 2u);
    EXPECT_EQ(geo.triangles[1][0], 2u);
    EXPECT_EQ(geo.triangles[1][1], 1u);
    EXPECT_EQ(geo.triangles[1][2], 0u);
}

// Dual-vertex-set format-1 models split XTRV into base vertices plus extra
// influences. The counts come from the RD3D header fields BoneMesh_Skin reads
// (mesh+0xC and mesh+0x10), and the two runs must partition the vertex array.
TEST(MefDualSetTest, DualVertexSetCountsFollowBoneMeshSkinHeaderFields) {
    const std::vector<uint8_t> dner =
        MakeGroup(3, 0xFFFF, 3, 0, 3, {0, 1, 2}, 0);

    const ParsedGeometry geo = ParseMefFileFromMemory(MakeType1Mef(1, {dner}), "001_02_1");

    ASSERT_EQ(geo.modelType, 1u);
    ASSERT_EQ(geo.vertices.size(), 4u);
    EXPECT_EQ(geo.baseVertexCount, 3u);       // RD3D + 0x10
    EXPECT_EQ(geo.extraInfluenceCount, 1u);   // RD3D + 0x0C
    EXPECT_EQ(geo.baseVertexCount + geo.extraInfluenceCount,
              static_cast<uint32_t>(geo.vertices.size()));
}

// IGI-RE Direct3DRender_LoadModel (bonemesh/d3drender.c:234-261) fills exactly two
// COLLISION parts from XTVC/ECFC/TAMC/HSPC after reading the HSMC collision chunk.
// They are not render partitions: the render blocks, and therefore the material
// selection, must keep coming from DNER.
TEST(MefDualSetTest, CollisionChunksDoNotDriveRenderPartitions) {
    std::vector<uint8_t> xtvc(2 * 16, 0);          // 2 collision vertices
    PutF32(xtvc, 0, 1.0f);
    PutF32(xtvc, 16, 2.0f);

    std::vector<uint8_t> ecfc(8, 0);               // 1 collision face
    PutU16(ecfc, 0, 0);
    PutU16(ecfc, 2, 1);
    PutU16(ecfc, 4, 0);
    PutU16(ecfc, 6, 7);                            // collision material index 7

    std::vector<uint8_t> tamc(12, 0);              // 1 collision material
    PutF32(tamc, 0, 1.0f);
    PutU16(tamc, 8, 7);                            // matId

    const std::vector<uint8_t> dner = Concat({
        MakeGroup(3, 6, 5, 0, 3, {0, 1, 2}, 0),
        MakeGroup(3, 0xFFFF, 9, 0, 3, {1, 2, 3}, 0),
    });

    const ParsedGeometry geo = ParseMefFileFromMemory(
        MakeType1Mef(1, {dner},
                     {{"HSMC", std::vector<uint8_t>(64, 0)},
                      {"XTVC", xtvc},
                      {"ECFC", ecfc},
                      {"TAMC", tamc}}),
        "001_02_1");

    ASSERT_EQ(geo.renderBlocks.size(), 2u);
    EXPECT_EQ(geo.renderBlocks[0].materialSlot, 5);
    EXPECT_EQ(geo.renderBlocks[1].materialSlot, 9);
    for (const auto& block : geo.renderBlocks) {
        EXPECT_NE(block.materialSlot, 7)
            << "the collision TAMC material index must not appear as a render material";
    }

    // The collision sets are still parsed, but only as collision/export data.
    ASSERT_EQ(geo.xtvcVerts.size(), 2u);
    ASSERT_EQ(geo.ecfcFaces.size(), 1u);
    ASSERT_EQ(geo.ecfcFaces[0].mat, 7u);
    ASSERT_EQ(geo.tamcRecords.size(), 1u);
    EXPECT_EQ(geo.tamcRecords[0].matId, 7);
}
