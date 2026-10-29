// OBJ -> MEF importer suite (issue #29).
//
// Two halves:
//
//   * MefImport*        — asset-free.  A hand-written OBJ is imported, the
//                         resulting .MEF is re-parsed with the shipping reader
//                         (ParseMefFile) and the geometry is compared.  The
//                         ILFF container, the render chunks and the collision
//                         chunks are each checked against the layout derived
//                         from IGI-RE (see docs/igi-re-parity/MEF_IMPORT.md).
//
//   * MefImportCorpus*  — enabled by pointing IGI_MEF_CORPUS or IGI_GAME_PATH
//                         at a retail IGI install.  Every static retail model
//                         is re-serialised by the writer and re-parsed, and the
//                         geometry must survive unchanged.  Without the
//                         environment variable the suite skips, so the host
//                         lane and the cross-builds stay portable.
//
// Unit convention: OBJ `v` coordinates are RAW MEF units (the same units the
// reader exposes as RenderVertex::rawPos), so a MEF -> OBJ -> MEF round trip is
// lossless.  The editor's kMefNativeScale = 1/40.96 is applied to
// RenderVertex::pos for display only and is not part of the file format.

#include <gtest/gtest.h>

#include "renderer/mef_native.h"
#include "renderer/mef_writer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// ---------------------------------------------------------------------------
// A deliberately independent, minimal ILFF builder.
//
// The tests must not validate the writer using the writer's own chunk helpers,
// so this builds reference .MEF files from scratch.  Layout: 20-byte form
// header ("ILFF", size, align 4, next 0, "OCEM"), then 16-byte chunk headers
// with the payload at +16 and `next` = align4(16 + size).
// ---------------------------------------------------------------------------

void PutU32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFFu));
}

uint32_t GetU32(const std::vector<uint8_t>& in, size_t at) {
    return static_cast<uint32_t>(in[at]) | (static_cast<uint32_t>(in[at + 1]) << 8) |
           (static_cast<uint32_t>(in[at + 2]) << 16) | (static_cast<uint32_t>(in[at + 3]) << 24);
}

float GetF32(const std::vector<uint8_t>& in, size_t at) {
    const uint32_t bits = GetU32(in, at);
    float value = 0.f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void PutF32(std::vector<uint8_t>& out, float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    PutU32(out, bits);
}

size_t Align4(size_t n) { return (n + 3u) & ~static_cast<size_t>(3u); }

std::vector<uint8_t> BuildIlff(
    const std::vector<std::pair<std::string, std::vector<uint8_t>>>& chunks) {
    size_t total = 20;
    for (const auto& chunk : chunks)
        total += Align4(16 + chunk.second.size());

    std::vector<uint8_t> out;
    out.insert(out.end(), {'I', 'L', 'F', 'F'});
    PutU32(out, static_cast<uint32_t>(total));
    PutU32(out, 4);
    PutU32(out, 0);
    out.insert(out.end(), {'O', 'C', 'E', 'M'});

    for (size_t i = 0; i < chunks.size(); ++i) {
        const size_t start = out.size();
        out.insert(out.end(), chunks[i].first.begin(), chunks[i].first.begin() + 4);
        PutU32(out, static_cast<uint32_t>(chunks[i].second.size()));
        PutU32(out, 4);
        const bool last = (i + 1 == chunks.size());
        PutU32(out, last ? 0u : static_cast<uint32_t>(Align4(16 + chunks[i].second.size())));
        out.insert(out.end(), chunks[i].second.begin(), chunks[i].second.end());
        while (out.size() < start + Align4(16 + chunks[i].second.size()))
            out.push_back(0);
    }
    return out;
}

// A 156-byte HSEM chunk with only the version and format words meaningful.
std::vector<uint8_t> MakeHsem(uint32_t format) {
    std::vector<uint8_t> hsem(156, 0);
    std::memcpy(hsem.data(), "\xcd\xcc\xcc\x3d", 4);  // 0.1f, the only version the game loads
    hsem[32] = static_cast<uint8_t>(format & 0xFF);
    hsem[33] = static_cast<uint8_t>((format >> 8) & 0xFF);
    hsem[34] = static_cast<uint8_t>((format >> 16) & 0xFF);
    hsem[35] = static_cast<uint8_t>((format >> 24) & 0xFF);
    return hsem;
}

struct ChunkView {
    std::string name;
    uint32_t size = 0;
    size_t payload = 0;
    size_t start = 0;
    uint32_t next = 0;
};

std::vector<ChunkView> WalkChunks(const std::vector<uint8_t>& bytes) {
    std::vector<ChunkView> chunks;
    if (bytes.size() < 20 || std::memcmp(bytes.data(), "ILFF", 4) != 0)
        return chunks;
    size_t pos = 20;
    while (pos + 16 <= bytes.size()) {
        ChunkView view;
        view.start = pos;
        view.name.assign(reinterpret_cast<const char*>(bytes.data() + pos), 4);
        view.size = GetU32(bytes, pos + 4);
        view.next = GetU32(bytes, pos + 12);
        view.payload = pos + 16;
        chunks.push_back(view);
        if (view.next == 0)
            break;
        pos += view.next;
    }
    return chunks;
}

const ChunkView* FindChunk(const std::vector<ChunkView>& chunks, const std::string& name,
                           size_t ordinal = 0) {
    size_t seen = 0;
    for (const auto& chunk : chunks) {
        if (chunk.name == name) {
            if (seen == ordinal)
                return &chunk;
            ++seen;
        }
    }
    return nullptr;
}

fs::path TempDir() {
    static const fs::path dir = [] {
        const fs::path base = fs::temp_directory_path() / "igi_mef_import_test";
        std::error_code ec;
        fs::create_directories(base, ec);
        return base;
    }();
    return dir;
}

std::string WriteTempFile(const std::string& name, const std::vector<uint8_t>& bytes) {
    const fs::path path = TempDir() / name;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    out.close();
    return path.string();
}

std::string WriteTempText(const std::string& name, const std::string& text) {
    const fs::path path = TempDir() / name;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
    out.close();
    return path.string();
}

// A quad in the XY plane: 4 positions, 1 normal, 4 UVs, 2 triangles.
const char* const kQuadObj =
    "# unit quad, raw MEF units\n"
    "v 0 0 0\n"
    "v 10 0 0\n"
    "v 10 10 0\n"
    "v 0 10 0\n"
    "vn 0 0 1\n"
    "vt 0 0\n"
    "vt 1 0\n"
    "vt 1 1\n"
    "vt 0 1\n"
    "usemtl brick\n"
    "f 1/1/1 2/2/1 3/3/1\n"
    "f 1/1/1 3/3/1 4/4/1\n";

const glm::vec3 kQuadPositions[4] = {{0.f, 0.f, 0.f}, {10.f, 0.f, 0.f}, {10.f, 10.f, 0.f}, {0.f, 10.f, 0.f}};

// A strip of `count` separate triangles listed in DECREASING x order.  The
// collision builder splits at the median of the triangle centroids, so a fixture
// whose input order is already spatial is left in place and the leaf-range
// invariant would hold by accident.  Listing them backwards forces the builder to
// permute the triangle array, which is the condition that test needs to exercise.
std::string MakeReversedStripObj(size_t count) {
    std::ostringstream out;
    for (size_t i = 0; i < count; ++i) {
        const double x = static_cast<double>((count - 1 - i) * 10);
        out << "v " << x << " 0 0\n"
            << "v " << (x + 1.0) << " 0 0\n"
            << "v " << x << " 0 1\n";
    }
    out << "usemtl strip\n";
    for (size_t i = 0; i < count; ++i) {
        const size_t base = i * 3 + 1;
        out << "f " << base << " " << (base + 1) << " " << (base + 2) << "\n";
    }
    return out.str();
}
const glm::vec2 kQuadUvs[4] = {{0.f, 0.f}, {1.f, 0.f}, {1.f, 1.f}, {0.f, 1.f}};

void ExpectVec3Near(const glm::vec3& actual, const glm::vec3& expected, float tolerance,
                    const std::string& what) {
    for (int i = 0; i < 3; ++i) {
        ASSERT_FALSE(std::isnan(actual[i])) << what << " component " << i << " is NaN";
        ASSERT_NEAR(actual[i], expected[i], tolerance) << what << " component " << i;
    }
}

void ExpectVec2Near(const glm::vec2& actual, const glm::vec2& expected, float tolerance,
                    const std::string& what) {
    for (int i = 0; i < 2; ++i) {
        ASSERT_FALSE(std::isnan(actual[i])) << what << " component " << i << " is NaN";
        ASSERT_NEAR(actual[i], expected[i], tolerance) << what << " component " << i;
    }
}

// Walk the collision sphere tree exactly the way the game does:
//   RigidDynCubeObj_CollLine HOOK 0x4C35F0, src/magicobj/rigidcoll.c:331-357
// and collect the (firstTri, triCount) ranges it reaches.  Node 0 is the root
// and is not sphere-tested; `child` is the first child index, `next` the next
// sibling index, both 0 for "none".
std::vector<std::pair<uint16_t, uint16_t>> WalkCollisionTree(
    const std::vector<uint8_t>& nodes) {
    std::vector<std::pair<uint16_t, uint16_t>> visited;
    const size_t nodeCount = nodes.size() / 24;
    if (nodeCount == 0)
        return visited;

    auto field = [&](size_t node, size_t offset) {
        return static_cast<uint16_t>(nodes[node * 24 + offset] |
                                     (nodes[node * 24 + offset + 1] << 8));
    };
    // RigidCollNode (24 bytes, src/magicobj/rigidcoll.c:26-33):
    //   +0 center[3], +12 radius, +16 firstTri, +18 triCount, +20 child, +22 next
    auto firstTri = [&](size_t node) { return field(node, 16); };
    auto triCount = [&](size_t node) { return field(node, 18); };
    auto child = [&](size_t node) { return field(node, 20); };
    auto next = [&](size_t node) { return field(node, 22); };

    std::vector<size_t> stack;
    stack.push_back(0);
    size_t n = 0;
    bool first = true;
    for (;;) {
        if (!first) {
            if (next(n) == 0) {
                stack.pop_back();
                if (stack.empty())
                    break;
                n = stack.back();
                continue;
            }
            n = next(n);
            stack.back() = n;
        }
        first = false;
        while (child(n) != 0) {
            n = child(n);
            stack.push_back(n);
        }
        if (triCount(n) != 0)
            visited.emplace_back(firstTri(n), triCount(n));
    }
    return visited;
}

std::vector<std::string> FindMefFiles(const fs::path& root) {
    std::vector<std::string> out;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, ec), end; it != end; it.increment(ec)) {
        if (ec)
            break;
        if (!it->is_regular_file(ec))
            continue;
        std::string ext = it->path().extension().string();
        for (char& c : ext)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".mef")
            out.push_back(it->path().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

const char* CorpusRoot() {
    for (const char* name : {"IGI_MEF_CORPUS", "IGI_GAME_PATH"}) {
        const char* value = std::getenv(name);
        if (value != nullptr && *value != '\0')
            return value;
    }
    return nullptr;
}

}  // namespace

// ===========================================================================
// OBJ parsing
// ===========================================================================

TEST(MefImportObjTest, ParsesPositionsNormalsUvsAndTriangulates) {
    mef_writer::ObjMesh obj;
    std::string error;
    ASSERT_TRUE(mef_writer::ParseObjText(kQuadObj, &obj, &error)) << error;

    EXPECT_EQ(obj.positions.size(), 4u);
    EXPECT_EQ(obj.normals.size(), 1u);
    EXPECT_EQ(obj.texcoords.size(), 4u);
    EXPECT_EQ(obj.faces.size(), 2u);
    ASSERT_EQ(obj.materials.size(), 1u);
    EXPECT_EQ(obj.materials[0], "brick");
    EXPECT_EQ(obj.faceMaterial.size(), 2u);
    ExpectVec3Near(obj.positions[2], glm::vec3(10.f, 10.f, 0.f), 1e-6f, "v3");
}

TEST(MefImportObjTest, FansPolygonsWithMoreThanThreeCorners) {
    const char* obj =
        "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nv 2 0 0\n"
        "f 1 2 3 4 5\n";
    mef_writer::ObjMesh mesh;
    std::string error;
    ASSERT_TRUE(mef_writer::ParseObjText(obj, &mesh, &error)) << error;
    EXPECT_EQ(mesh.faces.size(), 3u);  // a 5-gon fans into 3 triangles
}

TEST(MefImportObjTest, ResolvesNegativeIndices) {
    const char* obj =
        "v 0 0 0\nv 1 0 0\nv 1 1 0\n"
        "vt 0 0\nvt 1 0\nvt 1 1\n"
        "f -3/-3 -2/-2 -1/-1\n";
    mef_writer::ObjMesh mesh;
    std::string error;
    ASSERT_TRUE(mef_writer::ParseObjText(obj, &mesh, &error)) << error;
    ASSERT_EQ(mesh.faces.size(), 1u);
    EXPECT_EQ(mesh.faces[0].v[0], 0u);
    EXPECT_EQ(mesh.faces[0].v[1], 1u);
    EXPECT_EQ(mesh.faces[0].v[2], 2u);
    EXPECT_EQ(mesh.faces[0].vt[2], 2u);
    EXPECT_EQ(mesh.negativeIndexCorners, 3u);
}

TEST(MefImportObjTest, RejectsEmptyAndVertexlessInput) {
    mef_writer::ObjMesh mesh;
    std::string error;
    EXPECT_FALSE(mef_writer::ParseObjText("", &mesh, &error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(mef_writer::ParseObjText("v 0 0 0\nv 1 0 0\n", &mesh, &error));
}

// ===========================================================================
// Synthetic OBJ -> MEF -> ParseMefFile round trip
// ===========================================================================

TEST(MefImportRoundTripTest, GeometrySurvivesTheRoundTrip) {
    mef_writer::ObjMesh obj;
    std::string error;
    ASSERT_TRUE(mef_writer::ParseObjText(kQuadObj, &obj, &error)) << error;

    mef_writer::WriteOptions options;  // format 0, winding reversed, collision on
    mef_writer::MefModel model;
    ASSERT_TRUE(mef_writer::BuildMefFromObj(obj, options, &model, &error)) << error;
    EXPECT_EQ(model.vertices.size(), 4u);
    EXPECT_EQ(model.groups.size(), 1u);
    EXPECT_EQ(model.sourceTriangles, 2u);

    std::vector<uint8_t> bytes;
    ASSERT_TRUE(mef_writer::SerialiseMef(model, &bytes, &error)) << error;

    const std::string path = WriteTempFile("quad_roundtrip.mef", bytes);
    ParsedGeometry parsed;
    ASSERT_NO_THROW(parsed = ParseMefFile(path));

    ASSERT_EQ(parsed.vertices.size(), 4u);
    ASSERT_EQ(parsed.triangles.size(), 2u);
    EXPECT_EQ(parsed.modelType, 0u);
    EXPECT_TRUE(parsed.fromRenderMesh);
    ASSERT_EQ(parsed.renderBlocks.size(), 1u);
    EXPECT_EQ(parsed.renderBlocks[0].materialSlot, 0);

    for (size_t i = 0; i < 4; ++i) {
        ExpectVec3Near(parsed.vertices[i].rawPos, kQuadPositions[i], 1e-6f, "position");
        ExpectVec3Near(parsed.vertices[i].normal, glm::vec3(0.f, 0.f, 1.f), 1e-6f, "normal");
        ExpectVec2Near(parsed.vertices[i].uv, kQuadUvs[i], 1e-6f, "uv");
        // The reader's display position is the raw position scaled; the file
        // stores raw units.
        ExpectVec3Near(parsed.vertices[i].pos, kQuadPositions[i] * kMefNativeScale, 1e-5f,
                       "scaled position");
    }

    // Winding: retail packed DNER triangles are wound clockwise seen from
    // outside (measured on the shipped corpus), so the importer emits {a,c,b}
    // for every OBJ face {a,b,c}.
    EXPECT_EQ(parsed.triangles[0][0], 0u);
    EXPECT_EQ(parsed.triangles[0][1], 2u);
    EXPECT_EQ(parsed.triangles[0][2], 1u);
    EXPECT_EQ(parsed.triangles[1][0], 0u);
    EXPECT_EQ(parsed.triangles[1][1], 3u);
    EXPECT_EQ(parsed.triangles[1][2], 2u);
}

TEST(MefImportRoundTripTest, KeepWindingPreservesTheObjOrder) {
    mef_writer::ObjMesh obj;
    std::string error;
    ASSERT_TRUE(mef_writer::ParseObjText(kQuadObj, &obj, &error)) << error;

    mef_writer::WriteOptions options;
    options.reverseWinding = false;
    mef_writer::MefModel model;
    ASSERT_TRUE(mef_writer::BuildMefFromObj(obj, options, &model, &error)) << error;

    std::vector<uint8_t> bytes;
    ASSERT_TRUE(mef_writer::SerialiseMef(model, &bytes, &error)) << error;
    const std::string path = WriteTempFile("quad_keep_winding.mef", bytes);

    ParsedGeometry parsed;
    ASSERT_NO_THROW(parsed = ParseMefFile(path));
    ASSERT_EQ(parsed.triangles.size(), 2u);
    EXPECT_EQ(parsed.triangles[0][0], 0u);
    EXPECT_EQ(parsed.triangles[0][1], 1u);
    EXPECT_EQ(parsed.triangles[0][2], 2u);
}

TEST(MefImportRoundTripTest, MissingNormalsAreGeneratedAndUnitLength) {
    const char* obj =
        "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
        "f 1 2 3\n";
    mef_writer::ObjMesh mesh;
    std::string error;
    ASSERT_TRUE(mef_writer::ParseObjText(obj, &mesh, &error)) << error;

    mef_writer::MefModel model;
    ASSERT_TRUE(mef_writer::BuildMefFromObj(mesh, mef_writer::WriteOptions(), &model, &error))
        << error;
    ASSERT_EQ(model.vertices.size(), 3u);
    for (const auto& vertex : model.vertices) {
        EXPECT_NEAR(glm::length(vertex.normal), 1.f, 1e-5f);
        // The OBJ triangle is counter-clockwise seen from +Z; after the retail
        // winding reversal the generated normal must still face +Z.
        EXPECT_GT(vertex.normal.z, 0.9f);
    }
}

TEST(MefImportRoundTripTest, WritesTheIlffContainerTheEngineExpects) {
    mef_writer::ObjMesh obj;
    std::string error;
    ASSERT_TRUE(mef_writer::ParseObjText(kQuadObj, &obj, &error)) << error;

    mef_writer::MefModel model;
    ASSERT_TRUE(mef_writer::BuildMefFromObj(obj, mef_writer::WriteOptions(), &model, &error))
        << error;
    std::vector<uint8_t> bytes;
    ASSERT_TRUE(mef_writer::SerialiseMef(model, &bytes, &error)) << error;

    ASSERT_GE(bytes.size(), 20u);
    EXPECT_EQ(std::memcmp(bytes.data(), "ILFF", 4), 0);
    EXPECT_EQ(GetU32(bytes, 4), bytes.size());   // form size == whole file
    EXPECT_EQ(GetU32(bytes, 8), 4u);             // align
    EXPECT_EQ(GetU32(bytes, 12), 0u);            // next
    EXPECT_EQ(std::memcmp(bytes.data() + 16, "OCEM", 4), 0);

    const std::vector<ChunkView> chunks = WalkChunks(bytes);
    ASSERT_GE(chunks.size(), 5u);

    // Every chunk is 4-aligned and points at the next one, and the last one
    // terminates the chain exactly at the end of the file.
    for (size_t i = 0; i < chunks.size(); ++i) {
        const ChunkView& chunk = chunks[i];
        EXPECT_EQ(GetU32(bytes, chunk.start + 8), 4u) << chunk.name << " align";
        EXPECT_LE(chunk.payload + chunk.size, bytes.size()) << chunk.name << " bounds";
        if (i + 1 < chunks.size()) {
            EXPECT_NE(chunk.next, 0u) << chunk.name << " next";
            EXPECT_EQ(chunk.start + chunk.next, chunks[i + 1].start) << chunk.name << " link";
        } else {
            EXPECT_EQ(chunk.next, 0u) << "last chunk must terminate the chain";
            EXPECT_EQ(chunk.payload + chunk.size, bytes.size()) << "trailing bytes";
        }
    }

    const ChunkView* hsem = FindChunk(chunks, "HSEM");
    ASSERT_NE(hsem, nullptr);
    EXPECT_EQ(hsem->size, 156u);
    EXPECT_EQ(GetU32(bytes, hsem->payload), 0x3DCCCCCDu);  // version 0.1f, exact
    EXPECT_EQ(GetU32(bytes, hsem->payload + 32), 0u);      // format 0
    // complexity == triangle count (measured: 1578/1578 retail files)
    EXPECT_EQ(GetU32(bytes, hsem->payload + 96), 2u);
    // unk64[0] == vertex count, unk64[1] == D3DR.size + DNER.size + XTRV.size
    EXPECT_EQ(GetU32(bytes, hsem->payload + 100), 4u);

    const ChunkView* d3dr = FindChunk(chunks, "D3DR");
    ASSERT_NE(d3dr, nullptr);
    EXPECT_EQ(d3dr->size, 48u);  // format 0 chunk size
    EXPECT_EQ(GetU32(bytes, d3dr->payload + 0), 4u);   // D3DPT_TRIANGLELIST
    EXPECT_EQ(GetU32(bytes, d3dr->payload + 4), 2u);   // triangles
    EXPECT_EQ(GetU32(bytes, d3dr->payload + 8), 1u);   // groups
    EXPECT_EQ(GetU32(bytes, d3dr->payload + 12), 4u);  // vertices

    const ChunkView* dner = FindChunk(chunks, "DNER");
    ASSERT_NE(dner, nullptr);
    const uint16_t indexCount = static_cast<uint16_t>(bytes[dner->payload + 12] |
                                                      (bytes[dner->payload + 13] << 8));
    const uint16_t next = static_cast<uint16_t>(bytes[dner->payload + 14] |
                                                (bytes[dner->payload + 15] << 8));
    EXPECT_EQ(indexCount, 6u);
    EXPECT_EQ(next, 0xFFFFu);  // single (last) record
    EXPECT_EQ(GetU32(bytes, dner->payload + 16), 0u);  // texture slot 0

    const ChunkView* xtrv = FindChunk(chunks, "XTRV");
    ASSERT_NE(xtrv, nullptr);
    EXPECT_EQ(xtrv->size, 4u * 32u);  // format 0 stride
    EXPECT_EQ(GetU32(bytes, hsem->payload + 104),
              static_cast<uint32_t>(d3dr->size + dner->size + xtrv->size));
}

TEST(MefImportRoundTripTest, WritesSelfConsistentCollisionChunks) {
    mef_writer::ObjMesh obj;
    std::string error;
    ASSERT_TRUE(mef_writer::ParseObjText(kQuadObj, &obj, &error)) << error;

    mef_writer::MefModel model;
    ASSERT_TRUE(mef_writer::BuildMefFromObj(obj, mef_writer::WriteOptions(), &model, &error))
        << error;
    std::vector<uint8_t> bytes;
    ASSERT_TRUE(mef_writer::SerialiseMef(model, &bytes, &error)) << error;

    const std::vector<ChunkView> chunks = WalkChunks(bytes);

    const ChunkView* hsmc = FindChunk(chunks, "HSMC");
    ASSERT_NE(hsmc, nullptr);
    EXPECT_EQ(hsmc->size, 64u);

    const ChunkView* xtvc = FindChunk(chunks, "XTVC", 0);
    const ChunkView* ecfc = FindChunk(chunks, "ECFC", 0);
    const ChunkView* tamc = FindChunk(chunks, "TAMC", 0);
    const ChunkView* hpsc = FindChunk(chunks, "HPSC", 0);
    ASSERT_NE(xtvc, nullptr);
    ASSERT_NE(ecfc, nullptr);
    ASSERT_NE(tamc, nullptr);
    ASSERT_NE(hpsc, nullptr);

    EXPECT_EQ(xtvc->size, 4u * 16u);  // one collision vertex per mesh vertex
    EXPECT_EQ(ecfc->size, 2u * 8u);   // one collision triangle per render triangle
    EXPECT_EQ(tamc->size, 12u);
    EXPECT_EQ(hpsc->size % 24u, 0u);

    // HSMC header counts are derived from the sibling chunk sizes, which is
    // what the retail files contain (dummy.mef: 76, 40, 2, 151).
    EXPECT_EQ(GetU32(bytes, hsmc->payload + 0), 2u);
    EXPECT_EQ(GetU32(bytes, hsmc->payload + 4), 4u);
    EXPECT_EQ(GetU32(bytes, hsmc->payload + 8), 1u);
    EXPECT_EQ(GetU32(bytes, hsmc->payload + 12), hpsc->size / 24u);

    // The second collision set is empty in every retail file measured.
    for (const char* name : {"XTVC", "ECFC", "TAMC", "HPSC"}) {
        const ChunkView* second = FindChunk(chunks, name, 1);
        ASSERT_NE(second, nullptr) << name;
        EXPECT_EQ(second->size, 0u) << name;
    }

    // ECFC must carry exactly the collision triangles, all on the only surface.
    // It is emitted in the sphere tree's permutation (see the leaf-sphere test
    // below), so this compares as a multiset rather than position by position.
    std::vector<std::array<uint16_t, 3>> emitted;
    for (size_t i = 0; i < ecfc->size / 8u; ++i) {
        const size_t at = ecfc->payload + i * 8;
        const uint16_t a = static_cast<uint16_t>(bytes[at] | (bytes[at + 1] << 8));
        const uint16_t b = static_cast<uint16_t>(bytes[at + 2] | (bytes[at + 3] << 8));
        const uint16_t c = static_cast<uint16_t>(bytes[at + 4] | (bytes[at + 5] << 8));
        const uint16_t surface = static_cast<uint16_t>(bytes[at + 6] | (bytes[at + 7] << 8));
        EXPECT_EQ(surface, 0u);
        emitted.push_back({a, b, c});
    }
    std::vector<std::array<uint16_t, 3>> expected;
    for (const auto& face : model.collFaces)
        expected.push_back({face.a, face.b, face.c});
    std::sort(emitted.begin(), emitted.end());
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(emitted, expected);

    // TAMC[0] must be a tested, fully blocking surface:
    //   rigidcoll.c:120  if (sf->unk4 != 0xFFFF) continue;
    //   rigidcoll.c:216  if (sf->material == 0xFFFF) -> visibility 0
    const uint16_t portal = static_cast<uint16_t>(bytes[tamc->payload + 4] |
                                                  (bytes[tamc->payload + 5] << 8));
    const uint16_t material = static_cast<uint16_t>(bytes[tamc->payload + 8] |
                                                    (bytes[tamc->payload + 9] << 8));
    EXPECT_EQ(portal, 0xFFFFu);
    EXPECT_EQ(material, 0xFFFFu);

    // The sphere tree must reach every collision triangle when walked the way
    // the game walks it.
    const std::vector<uint8_t> nodeBytes(bytes.begin() + hpsc->payload,
                                        bytes.begin() + hpsc->payload + hpsc->size);
    const auto visited = WalkCollisionTree(nodeBytes);
    ASSERT_FALSE(visited.empty());
    std::vector<bool> covered(model.collFaces.size(), false);
    for (const auto& range : visited) {
        for (uint16_t t = 0; t < range.second; ++t) {
            const size_t index = static_cast<size_t>(range.first) + t;
            ASSERT_LT(index, model.collFaces.size()) << "HPSC range leaves the ECFC array";
            covered[index] = true;
        }
    }
    for (size_t i = 0; i < covered.size(); ++i)
        EXPECT_TRUE(covered[i]) << "collision triangle " << i << " unreachable in the HPSC tree";
}

// A HPSC node's (firstTri, triCount) is a half-open range into ECFC, and the game
// sphere-tests the node before testing those triangles (rigidcoll.c:330-370).  So
// every triangle a node names must lie inside that node's sphere.  Retail satisfies
// this for every static model measured.  The builder splits a *permuted* triangle
// array, so this test fails if ECFC is ever emitted in the unpermuted order again --
// which is the defect it was added for: the ranges would then name triangles the
// sphere was not built around, and every leaf would sphere-reject its own geometry.
TEST(MefImportRoundTripTest, HpscLeafSpheresContainTheTrianglesTheirRangesName) {
    // Eight triangles, listed in reverse spatial order, so the builder's median
    // split has to permute them -- otherwise this test cannot fail.
    const std::string objText = MakeReversedStripObj(8);
    mef_writer::ObjMesh obj;
    std::string error;
    ASSERT_TRUE(mef_writer::ParseObjText(objText, &obj, &error)) << error;

    mef_writer::MefModel model;
    ASSERT_TRUE(mef_writer::BuildMefFromObj(obj, mef_writer::WriteOptions(), &model, &error))
        << error;
    std::vector<uint8_t> bytes;
    ASSERT_TRUE(mef_writer::SerialiseMef(model, &bytes, &error)) << error;

    const std::vector<ChunkView> chunks = WalkChunks(bytes);
    const ChunkView* xtvc = FindChunk(chunks, "XTVC", 0);
    const ChunkView* ecfc = FindChunk(chunks, "ECFC", 0);
    const ChunkView* hpsc = FindChunk(chunks, "HPSC", 0);
    ASSERT_NE(xtvc, nullptr);
    ASSERT_NE(ecfc, nullptr);
    ASSERT_NE(hpsc, nullptr);

    const size_t vertexCount = xtvc->size / 16u;
    const size_t triangleCount = ecfc->size / 8u;
    ASSERT_GT(triangleCount, 0u);
    ASSERT_EQ(triangleCount, model.collFaces.size());

    auto cornerIndex = [&](size_t tri, size_t corner) {
        const size_t at = ecfc->payload + tri * 8u + corner * 2u;
        return static_cast<size_t>(bytes[at] | (bytes[at + 1] << 8));
    };

    size_t leafNodes = 0;
    std::vector<size_t> inLeafRanges(triangleCount, 0);
    for (size_t at = hpsc->payload; at + 24u <= hpsc->payload + hpsc->size; at += 24u) {
        const float cx = GetF32(bytes, at + 0);
        const float cy = GetF32(bytes, at + 4);
        const float cz = GetF32(bytes, at + 8);
        const float radius = GetF32(bytes, at + 12);
        const uint16_t firstTri = static_cast<uint16_t>(bytes[at + 16] | (bytes[at + 17] << 8));
        const uint16_t triCount = static_cast<uint16_t>(bytes[at + 18] | (bytes[at + 19] << 8));
        if (triCount == 0)
            continue;  // interior node: sphere-tested, but names no triangles
        ++leafNodes;
        for (uint16_t t = 0; t < triCount; ++t) {
            const size_t tri = static_cast<size_t>(firstTri) + t;
            ASSERT_LT(tri, triangleCount) << "HPSC range leaves the ECFC array";
            ++inLeafRanges[tri];
            for (size_t corner = 0; corner < 3; ++corner) {
                const size_t vi = cornerIndex(tri, corner);
                ASSERT_LT(vi, vertexCount);
                const size_t vat = xtvc->payload + vi * 16u;
                const float dx = GetF32(bytes, vat + 0) - cx;
                const float dy = GetF32(bytes, vat + 4) - cy;
                const float dz = GetF32(bytes, vat + 8) - cz;
                const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
                EXPECT_LE(distance, radius + 1e-3f)
                    << "node " << (at - hpsc->payload) / 24u << " (radius " << radius
                    << ") names triangle " << tri << ", whose corner " << corner << " is "
                    << distance << " from the node centre";
            }
        }
    }

    EXPECT_GT(leafNodes, 0u);
    // The leaf ranges partition the triangle array: each triangle exactly once.
    for (size_t i = 0; i < inLeafRanges.size(); ++i)
        EXPECT_EQ(inLeafRanges[i], 1u) << "triangle " << i << " appears in " << inLeafRanges[i]
                                       << " leaf ranges";
}

TEST(MefImportRoundTripTest, NoCollisionOptionOmitsTheCollisionChunks) {
    mef_writer::ObjMesh obj;
    std::string error;
    ASSERT_TRUE(mef_writer::ParseObjText(kQuadObj, &obj, &error)) << error;

    mef_writer::WriteOptions options;
    options.emitCollision = false;
    mef_writer::MefModel model;
    ASSERT_TRUE(mef_writer::BuildMefFromObj(obj, options, &model, &error)) << error;
    std::vector<uint8_t> bytes;
    ASSERT_TRUE(mef_writer::SerialiseMef(model, &bytes, &error)) << error;

    const std::vector<ChunkView> chunks = WalkChunks(bytes);
    EXPECT_EQ(FindChunk(chunks, "HSMC"), nullptr);
    EXPECT_EQ(FindChunk(chunks, "HPSC"), nullptr);
    EXPECT_EQ(FindChunk(chunks, "XTVC"), nullptr);
}

TEST(MefImportRoundTripTest, SplitsGroupsTooLargeForTheSixteenBitNextField) {
    // A DNER record's `next` is a 16-bit byte count, and 0xFFFF is the
    // terminator, so an index array longer than 32766 entries must be split
    // across consecutive records (src/render/render.c:487-489).
    const int kGrid = 106;  // 106*106 = 11236 vertices, 105*105*2 = 22050 triangles
    std::ostringstream obj;
    obj << std::setprecision(9);
    for (int j = 0; j < kGrid; ++j)
        for (int i = 0; i < kGrid; ++i)
            obj << "v " << i << ' ' << j << " 0\n";
    auto index = [&](int i, int j) { return j * kGrid + i + 1; };
    for (int j = 0; j + 1 < kGrid; ++j) {
        for (int i = 0; i + 1 < kGrid; ++i) {
            obj << "f " << index(i, j) << ' ' << index(i + 1, j) << ' ' << index(i + 1, j + 1) << "\n";
            obj << "f " << index(i, j) << ' ' << index(i + 1, j + 1) << ' ' << index(i, j + 1) << "\n";
        }
    }

    mef_writer::ObjMesh mesh;
    std::string error;
    ASSERT_TRUE(mef_writer::ParseObjText(obj.str(), &mesh, &error)) << error;
    ASSERT_EQ(mesh.positions.size(), static_cast<size_t>(kGrid * kGrid));
    ASSERT_EQ(mesh.faces.size(), 22050u);

    mef_writer::MefModel model;
    ASSERT_TRUE(mef_writer::BuildMefFromObj(mesh, mef_writer::WriteOptions(), &model, &error))
        << error;
    EXPECT_EQ(model.vertices.size(), static_cast<size_t>(kGrid * kGrid));

    std::vector<uint8_t> bytes;
    ASSERT_TRUE(mef_writer::SerialiseMef(model, &bytes, &error)) << error;

    const std::vector<ChunkView> chunks = WalkChunks(bytes);
    const ChunkView* dner = FindChunk(chunks, "DNER");
    ASSERT_NE(dner, nullptr);
    const ChunkView* d3dr = FindChunk(chunks, "D3DR");
    ASSERT_NE(d3dr, nullptr);

    // 22050 triangles * 3 = 66150 indices -> three records.
    std::vector<uint16_t> indexCounts, nexts;
    size_t cursor = 0;
    while (cursor + 28 <= dner->size) {
        const size_t at = dner->payload + cursor;
        const uint16_t indexCount = static_cast<uint16_t>(bytes[at + 12] | (bytes[at + 13] << 8));
        const uint16_t next = static_cast<uint16_t>(bytes[at + 14] | (bytes[at + 15] << 8));
        indexCounts.push_back(indexCount);
        nexts.push_back(next);
        if (next == 0xFFFF)
            break;
        ASSERT_EQ(next, indexCount * 2u) << "next must be the index array length";
        cursor += 28 + indexCount * 2u;
    }
    ASSERT_EQ(indexCounts.size(), 3u);
    EXPECT_EQ(indexCounts[0], 32766u);
    EXPECT_EQ(indexCounts[1], 32766u);
    EXPECT_EQ(indexCounts[2], 22050u * 3u - 32766u * 2u);
    EXPECT_EQ(nexts[0], 65532u);
    EXPECT_EQ(nexts[1], 65532u);
    EXPECT_EQ(nexts[2], 0xFFFFu);
    EXPECT_EQ(GetU32(bytes, d3dr->payload + 8), 3u) << "D3DR group count must follow the split";

    const std::string path = WriteTempFile("split_groups.mef", bytes);
    ParsedGeometry parsed;
    ASSERT_NO_THROW(parsed = ParseMefFile(path));
    EXPECT_EQ(parsed.vertices.size(), static_cast<size_t>(kGrid * kGrid));
    EXPECT_EQ(parsed.triangles.size(), 22050u);
}

TEST(MefImportRoundTripTest, GeneratedNormalsAreAreaWeightedAndSmoothed) {
    // An OBJ with no `vn` gets the area-weighted sum of the incident triangle
    // normals at each shared vertex, which is a *smoothed* normal rather than an
    // axis-aligned face normal.  This is the repository's own cube fixture
    // (tools/e2e/fixtures/camera-box.obj) inlined so the test stays asset-free.
    // The corner (-1,-1,-1) is touched by five triangles: two on the -Z face
    // ((0,0,-4) each), two on the -Y face ((0,-4,0) each) and one on the -X face
    // ((-4,0,0)); the sum (-4,-8,-8) normalises to (-1,-2,-2)/3.
    const char* cube =
        "v -1 -1 -1\nv 1 -1 -1\nv 1 1 -1\nv -1 1 -1\n"
        "v -1 -1 1\nv 1 -1 1\nv 1 1 1\nv -1 1 1\n"
        "f 1 3 2\nf 1 4 3\nf 5 6 7\nf 5 7 8\nf 1 2 6\nf 1 6 5\n"
        "f 2 3 7\nf 2 7 6\nf 3 4 8\nf 3 8 7\nf 4 1 5\nf 4 5 8\n";

    mef_writer::ObjMesh mesh;
    std::string error;
    ASSERT_TRUE(mef_writer::ParseObjText(cube, &mesh, &error)) << error;
    ASSERT_EQ(mesh.faces.size(), 12u);
    EXPECT_EQ(mesh.normals.size(), 0u);

    mef_writer::MefModel model;
    ASSERT_TRUE(mef_writer::BuildMefFromObj(mesh, mef_writer::WriteOptions(), &model, &error))
        << error;
    ASSERT_EQ(model.vertices.size(), 8u);

    const glm::vec3 expected = glm::normalize(glm::vec3(-1.f, -2.f, -2.f));
    ExpectVec3Near(model.vertices[0].normal, expected, 1e-5f, "smoothed corner normal");
    for (const auto& vertex : model.vertices)
        EXPECT_NEAR(glm::length(vertex.normal), 1.f, 1e-5f);

    // Every generated normal must point away from the cube's centre, i.e. the
    // winding reversal must not have flipped the shading.
    for (const auto& vertex : model.vertices)
        EXPECT_GT(glm::dot(vertex.normal, glm::normalize(vertex.pos)), 0.f);

    // The fixture is consistently wound, so all twelve triangles must be kept.
    EXPECT_EQ(model.sourceTriangles, 12u);
}

TEST(MefImportRoundTripTest, WritesFormatThreeWithTenFloatVertices) {
    mef_writer::ObjMesh obj;
    std::string error;
    ASSERT_TRUE(mef_writer::ParseObjText(kQuadObj, &obj, &error)) << error;

    mef_writer::WriteOptions options;
    options.format = 3;
    mef_writer::MefModel model;
    ASSERT_TRUE(mef_writer::BuildMefFromObj(obj, options, &model, &error)) << error;
    std::vector<uint8_t> bytes;
    ASSERT_TRUE(mef_writer::SerialiseMef(model, &bytes, &error)) << error;

    const std::vector<ChunkView> chunks = WalkChunks(bytes);
    const ChunkView* d3dr = FindChunk(chunks, "D3DR");
    const ChunkView* dner = FindChunk(chunks, "DNER");
    const ChunkView* xtrv = FindChunk(chunks, "XTRV");
    ASSERT_NE(d3dr, nullptr);
    ASSERT_NE(dner, nullptr);
    ASSERT_NE(xtrv, nullptr);
    EXPECT_EQ(d3dr->size, 56u);
    EXPECT_EQ(GetU32(bytes, d3dr->payload + 4), 1u);   // measured constant
    EXPECT_EQ(GetU32(bytes, d3dr->payload + 8), 2u);   // triangles
    EXPECT_EQ(GetU32(bytes, d3dr->payload + 12), 1u);  // groups
    EXPECT_EQ(GetU32(bytes, d3dr->payload + 16), 4u);  // vertices
    EXPECT_EQ(xtrv->size, 4u * 40u);
    // format 3 DNER header is 32 bytes: indexCount at +12, base at +20.
    const uint16_t indexCount = static_cast<uint16_t>(bytes[dner->payload + 12] |
                                                      (bytes[dner->payload + 13] << 8));
    EXPECT_EQ(indexCount, 6u);

    const std::string path = WriteTempFile("quad_format3.mef", bytes);
    ParsedGeometry parsed;
    ASSERT_NO_THROW(parsed = ParseMefFile(path));
    EXPECT_EQ(parsed.modelType, 3u);
    ASSERT_EQ(parsed.vertices.size(), 4u);
    ASSERT_EQ(parsed.triangles.size(), 2u);
    for (size_t i = 0; i < 4; ++i)
        ExpectVec3Near(parsed.vertices[i].rawPos, kQuadPositions[i], 1e-6f, "position");
}

// HSEM center[3] / sphere are the model's bounding sphere, and the engine reads
// BOTH of them:
//
//   ModelObj_GetLOD      HOOK 0x4CFCB0, src/lod/mesh3d.c:209-226
//       x = o->pos[0] - c->pos.x + h->center.x;
//       dist = ... + h->center.z/y ...;
//       return lod_select(vm, dist, max_scale(o->scale) * h->sphere, ...);
//   Mesh3D_ExcludeRigid  HOOK 0x4D0510, src/lod/mesh3d.c:321-334
//       x = m[2]*h->center.z + m[1]*h->center.y + m[0]*h->center.x; ...
//       return Mesh3D_Exclude(v, max_scale(o->scale) * h->sphere, clip);
//
// and lod_select itself (src/lod/mesh3d.c:161-207) turns the radius into the
// cull threshold `e = (gLodRange - r) * (1 - gLodBlend) + r`, returning -1
// (culled) once `cull * e >= fov * q`.  So a written sphere that does not bound
// the mesh about `center` makes a replaced model disappear early or linger, and
// moves its LOD switch distance.
//
// The fixture sits far from the origin so that the two candidate radii are far
// apart: a writer that put `radius` (max |v|, HSEM +120) into `sphere` would be
// caught rather than passing by coincidence.
TEST(MefImportRoundTripTest, WritesTheBoundingSphereTheLodAndCullPathsRead) {
    const char* const kOffsetQuadObj =
        "v 100 0 0\n"
        "v 110 0 0\n"
        "v 110 10 0\n"
        "v 100 10 0\n"
        "vn 0 0 1\n"
        "usemtl wall\n"
        "f 1 2 3\n"
        "f 1 3 4\n";
    const glm::vec3 kExpectedCenter(105.f, 5.f, 0.f);
    const float kExpectedSphere = std::sqrt(5.f * 5.f + 5.f * 5.f);   // ~7.07107
    const float kExpectedRadius = std::sqrt(110.f * 110.f + 10.f * 10.f);  // ~110.4536

    mef_writer::ObjMesh obj;
    std::string error;
    ASSERT_TRUE(mef_writer::ParseObjText(kOffsetQuadObj, &obj, &error)) << error;

    mef_writer::MefModel model;
    ASSERT_TRUE(mef_writer::BuildMefFromObj(obj, mef_writer::WriteOptions(), &model, &error))
        << error;
    std::vector<uint8_t> bytes;
    ASSERT_TRUE(mef_writer::SerialiseMef(model, &bytes, &error)) << error;

    const std::vector<ChunkView> chunks = WalkChunks(bytes);
    const ChunkView* hsem = FindChunk(chunks, "HSEM");
    ASSERT_NE(hsem, nullptr);
    ASSERT_GE(hsem->size, 156u) << "MeshHeader is 0x90 bytes plus a 12-byte tail";

    // MeshHeader offsets (src/model/modeltypes.h:9-29): center at +48, sphere at
    // +60, radius at +120.  The writer's HSEM payload starts at MeshHeader +0.
    ExpectVec3Near(glm::vec3(GetF32(bytes, hsem->payload + 48), GetF32(bytes, hsem->payload + 52),
                             GetF32(bytes, hsem->payload + 56)),
                   kExpectedCenter, 1e-4f, "HSEM center");
    const float sphere = GetF32(bytes, hsem->payload + 60);
    const float radius = GetF32(bytes, hsem->payload + 120);
    EXPECT_NEAR(sphere, kExpectedSphere, 1e-4f) << "sphere must be max |v - center|";
    EXPECT_NEAR(radius, kExpectedRadius, 1e-4f) << "radius is max |v| about the model origin";
    EXPECT_GT(std::fabs(sphere - radius), 100.f)
        << "fixture must keep sphere and radius far apart for this test to mean anything";

    // The contract the engine relies on: every vertex is inside the sphere, and
    // the sphere is tight (some vertex sits on it).  Checked on the vertices the
    // shipping reader returns, not on the writer's own bookkeeping.
    const std::string path = WriteTempFile("offset_quad_sphere.mef", bytes);
    ParsedGeometry parsed;
    ASSERT_NO_THROW(parsed = ParseMefFile(path));
    ASSERT_EQ(parsed.vertices.size(), 4u);
    float furthest = 0.f;
    for (const auto& vertex : parsed.vertices) {
        const float d = glm::length(vertex.rawPos - kExpectedCenter);
        EXPECT_LE(d, sphere + 1e-4f) << "a vertex lies outside the written bounding sphere";
        furthest = std::max(furthest, d);
    }
    EXPECT_NEAR(furthest, sphere, 1e-4f) << "the sphere must be tight around the mesh";

    // --pivot-origin: the caller overrides the centre, so the sphere is measured
    // about the origin instead.
    mef_writer::WriteOptions originOptions;
    originOptions.useBoundsCentrePivot = false;
    originOptions.pivot = glm::vec3(0.f);
    mef_writer::MefModel originModel;
    ASSERT_TRUE(mef_writer::BuildMefFromObj(obj, originOptions, &originModel, &error)) << error;
    std::vector<uint8_t> originBytes;
    ASSERT_TRUE(mef_writer::SerialiseMef(originModel, &originBytes, &error)) << error;
    const ChunkView* originHsem = FindChunk(WalkChunks(originBytes), "HSEM");
    ASSERT_NE(originHsem, nullptr);
    ExpectVec3Near(glm::vec3(GetF32(originBytes, originHsem->payload + 48),
                             GetF32(originBytes, originHsem->payload + 52),
                             GetF32(originBytes, originHsem->payload + 56)),
                   glm::vec3(0.f), 1e-6f, "HSEM center under --pivot-origin");
    EXPECT_NEAR(GetF32(originBytes, originHsem->payload + 60), kExpectedRadius, 1e-4f)
        << "with the origin as centre the sphere must equal max |v|";
}

// ===========================================================================
// Refusals and reference-MEF handling
// ===========================================================================

TEST(MefImportRefusalTest, SkeletalOutputFormatIsRefused) {
    mef_writer::ObjMesh obj;
    std::string error;
    ASSERT_TRUE(mef_writer::ParseObjText(kQuadObj, &obj, &error)) << error;

    mef_writer::WriteOptions options;
    options.format = 1;
    mef_writer::MefModel model;
    EXPECT_FALSE(mef_writer::BuildMefFromObj(obj, options, &model, &error));
    EXPECT_NE(error.find("skeletal"), std::string::npos) << error;
}

TEST(MefImportRefusalTest, SkeletalReferenceMefIsRefusedAndLeavesNoFile) {
    // A reference MEF that claims format 1 must abort the whole import rather
    // than silently dropping the skeleton.
    std::vector<std::pair<std::string, std::vector<uint8_t>>> chunks;
    chunks.emplace_back("HSEM", MakeHsem(1));
    chunks.emplace_back("ATTA", std::vector<uint8_t>(68, 0xAB));
    const std::string reference = WriteTempFile("skeletal_reference.mef", BuildIlff(chunks));

    const std::string objPath = WriteTempText("skeletal_ref.obj", kQuadObj);
    const fs::path outPath = TempDir() / "skeletal_ref_out.mef";
    std::error_code ec;
    fs::remove(outPath, ec);

    mef_writer::WriteOptions options;
    options.sourceMefPath = reference;
    std::string error;
    EXPECT_FALSE(mef_writer::WriteMefFromObj(objPath, outPath.string(), options, &error));
    EXPECT_NE(error.find("skeletal"), std::string::npos) << error;
    EXPECT_FALSE(fs::exists(outPath)) << "a refused import must not leave an output file";
}

TEST(MefImportRefusalTest, SkeletalParsedGeometryIsRefused) {
    ParsedGeometry geometry;
    geometry.modelType = 1;
    geometry.vertices.resize(3);
    geometry.triangles.push_back({0, 1, 2});
    mef_writer::MefModel model;
    std::string error;
    EXPECT_FALSE(mef_writer::BuildMefFromGeometry(geometry, mef_writer::WriteOptions(), &model,
                                                 &error));
    EXPECT_NE(error.find("skeletal"), std::string::npos) << error;
}

TEST(MefImportRefusalTest, EmptyParsedGeometryIsRefused) {
    ParsedGeometry geometry;
    geometry.modelType = 0;
    mef_writer::MefModel model;
    std::string error;
    EXPECT_FALSE(
        mef_writer::BuildMefFromGeometry(geometry, mef_writer::WriteOptions(), &model, &error));
    EXPECT_FALSE(error.empty());
}

// The largest mesh the format can carry, and the first one it cannot.
//
// A DNER group's vertex window is 16-bit in BOTH formats:
//   MeshGroup  src/render/rendertypes.h:6-18   uint16_t base; uint16_t vertexCount;
//   LmGroup    src/render/render.c:40-54       uint16_t base; uint16_t vertexCount;
// and draw_group hands those fields straight to the draw call
// (src/render/render.c:526-531,
//  IDirect3DDevice7_DrawIndexedPrimitiveVB(DEV, d->primType, vb, start + g->base,
//                                          g->vertexCount, (WORD *)g->indices, ...)),
// with a 16-bit index array behind them.  A group therefore cannot address more
// than 0xFFFF vertices, and because `base` is 16-bit as well there is no way to
// chain groups past that either: 65535 is a hard ceiling on a static mesh, not a
// policy choice of the writer.
//
// This is the case the issue is about -- a high-poly replacement -- so the
// boundary is pinned from both sides, through the OBJ text parser and the
// shipping reader.
namespace {

mef_writer::ObjMesh MakeFanMesh(size_t vertexCount) {
    mef_writer::ObjMesh mesh;
    mesh.positions.resize(vertexCount);
    // A zig-zag in y so that ANY three consecutive vertices form a triangle with
    // non-zero area: the writer drops degenerate faces before it counts vertices,
    // and a fixture on a straight line would be filtered out and never reach the
    // limit being tested.
    for (size_t i = 0; i < vertexCount; ++i)
        mesh.positions[i] = glm::vec3(static_cast<float>(i), (i % 3 == 0) ? 0.f : 1.f, 0.f);
    mesh.normals.push_back(glm::vec3(0.f, 0.f, 1.f));
    for (size_t i = 0; i + 2 < vertexCount; i += 3) {
        mef_writer::ObjFace face;
        for (int k = 0; k < 3; ++k) {
            face.v[k] = static_cast<uint32_t>(i + k);
            face.vn[k] = 0;
        }
        mesh.faces.push_back(face);
        mesh.faceMaterial.push_back("wall");
    }
    // The 3-at-a-time loop above leaves `vertexCount % 3` positions unreferenced,
    // and the writer only emits welded, referenced vertices.  Close the mesh so
    // the welded count is exactly `vertexCount`.
    const size_t covered = mesh.faces.size() * 3;
    if (vertexCount > covered) {
        mef_writer::ObjFace face;
        face.v[0] = static_cast<uint32_t>(covered);
        face.v[1] = static_cast<uint32_t>(covered + 1 < vertexCount ? covered + 1 : 1u);
        face.v[2] = 0u;
        for (int k = 0; k < 3; ++k)
            face.vn[k] = 0;
        mesh.faces.push_back(face);
        mesh.faceMaterial.push_back("wall");
    }
    return mesh;
}

}  // namespace

TEST(MefImportRefusalTest, AcceptsTheLargestMeshTheSixteenBitGroupFieldsAddress) {
    const mef_writer::ObjMesh mesh = MakeFanMesh(0xFFFFu);

    mef_writer::MefModel model;
    std::string error;
    ASSERT_TRUE(mef_writer::BuildMefFromObj(mesh, mef_writer::WriteOptions(), &model, &error))
        << error;
    ASSERT_EQ(model.vertices.size(), 0xFFFFu);

    std::vector<uint8_t> bytes;
    ASSERT_TRUE(mef_writer::SerialiseMef(model, &bytes, &error)) << error;

    const std::string path = WriteTempFile("vertex_limit_65535.mef", bytes);
    ParsedGeometry parsed;
    ASSERT_NO_THROW(parsed = ParseMefFile(path));
    EXPECT_EQ(parsed.vertices.size(), 0xFFFFu);
    EXPECT_EQ(parsed.triangles.size(), 0xFFFFu / 3u);
    // Every index must survive the 16-bit index arrays.
    for (const auto& triangle : parsed.triangles)
        for (int k = 0; k < 3; ++k)
            ASSERT_LT(triangle[k], 0xFFFFu);
}

TEST(MefImportRefusalTest, RefusesMoreVerticesThanTheSixteenBitGroupFieldsAddress) {
    const mef_writer::ObjMesh mesh = MakeFanMesh(0x10000u);

    mef_writer::MefModel model;
    std::string error;
    EXPECT_FALSE(mef_writer::BuildMefFromObj(mesh, mef_writer::WriteOptions(), &model, &error));
    EXPECT_NE(error.find("65535"), std::string::npos) << error;
    EXPECT_NE(error.find("16-bit"), std::string::npos) << error;

    // And through the file entry point, so a refusal cannot leave a truncated
    // .mef behind for the game to load.
    const std::string objPath = WriteTempText("too_many_vertices.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    const fs::path outPath = TempDir() / "too_many_vertices.mef";
    std::error_code ec;
    fs::remove(outPath, ec);

    mef_writer::WriteOptions options;
    options.maxVertices = 2;  // keep the fixture small; the rule under test is the same one
    std::string fileError;
    EXPECT_FALSE(mef_writer::WriteMefFromObj(objPath, outPath.string(), options, &fileError));
    EXPECT_FALSE(fileError.empty());
    EXPECT_FALSE(fs::exists(outPath)) << "a refused import must not leave an output file";
}

TEST(MefImportReferenceTest, StaticReferenceAttachmentsAreCopiedVerbatim) {
    std::vector<uint8_t> atta(2 * 68, 0);
    for (size_t record = 0; record < 2; ++record) {
        const std::string name = (record == 0) ? "muzzle" : "hand_r";
        std::memcpy(atta.data() + record * 68, name.c_str(), name.size());
        for (size_t i = 0; i < 68; ++i)
            atta[record * 68 + i] ^= static_cast<uint8_t>(record + 1);
    }
    std::vector<std::pair<std::string, std::vector<uint8_t>>> chunks;
    chunks.emplace_back("HSEM", MakeHsem(0));
    chunks.emplace_back("ATTA", atta);
    const std::string reference = WriteTempFile("static_reference.mef", BuildIlff(chunks));

    mef_writer::ObjMesh obj;
    std::string error;
    ASSERT_TRUE(mef_writer::ParseObjText(kQuadObj, &obj, &error)) << error;

    mef_writer::WriteOptions options;
    options.sourceMefPath = reference;
    mef_writer::MefModel model;
    ASSERT_TRUE(mef_writer::BuildMefFromObj(obj, options, &model, &error)) << error;

    // Write through the OBJ path so the reference is lifted.
    const std::string objPath = WriteTempText("attach_ref.obj", kQuadObj);
    std::vector<uint8_t> bytes;
    ASSERT_TRUE(mef_writer::WriteMefBytesFromObj(objPath, options, &bytes, &error)) << error;

    const std::vector<ChunkView> written = WalkChunks(bytes);
    const ChunkView* writtenAtta = FindChunk(written, "ATTA");
    ASSERT_NE(writtenAtta, nullptr);
    ASSERT_EQ(writtenAtta->size, atta.size());
    EXPECT_EQ(std::memcmp(bytes.data() + writtenAtta->payload, atta.data(), atta.size()), 0);

    // attach_count in HSEM must equal the attachment record count.
    const ChunkView* hsem = FindChunk(written, "HSEM");
    ASSERT_NE(hsem, nullptr);
    const uint16_t attachCount = static_cast<uint16_t>(bytes[hsem->payload + 126] |
                                                       (bytes[hsem->payload + 127] << 8));
    EXPECT_EQ(attachCount, 2u);
}

// ===========================================================================
// CLI argument parsing (source/cli/cli_handler.cpp is not in the host lane's
// source pool, so the parsing is exercised here).
// ===========================================================================

namespace {

mef_writer::ImportMefRequest ParseArgs(std::vector<std::string> args, std::string* error) {
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>("igi1ed.exe"));
    for (auto& arg : args)
        argv.push_back(const_cast<char*>(arg.c_str()));
    mef_writer::ImportMefRequest request;
    EXPECT_TRUE(mef_writer::ParseImportMefRequest(static_cast<int>(argv.size()), argv.data(),
                                                 &request, error))
        << *error;
    return request;
}

}  // namespace

TEST(MefImportCliTest, AcceptsTheIssueCommandForm) {
    std::string error;
    const mef_writer::ImportMefRequest request =
        ParseArgs({"--mef", "building01_hp.obj", "--import-mef", "building01_new.mef"}, &error);
    EXPECT_EQ(request.inputObjPath, "building01_hp.obj");
    EXPECT_EQ(request.outputMefPath, "building01_new.mef");
    EXPECT_EQ(request.options.format, 0u);
    EXPECT_TRUE(request.options.reverseWinding);
    EXPECT_TRUE(request.options.emitCollision);
}

TEST(MefImportCliTest, AcceptsTwoImportOperands) {
    std::string error;
    const mef_writer::ImportMefRequest request =
        ParseArgs({"--import-mef", "in.obj", "out.mef"}, &error);
    EXPECT_EQ(request.inputObjPath, "in.obj");
    EXPECT_EQ(request.outputMefPath, "out.mef");
}

TEST(MefImportCliTest, AcceptsTheOptionalSwitches) {
    std::string error;
    const mef_writer::ImportMefRequest request = ParseArgs(
        {"--mef", "in.obj", "--import-mef", "out.mef", "--source-mef", "ref.mef", "--mef-format",
         "3", "--keep-winding", "--no-collision", "--pivot-origin"},
        &error);
    EXPECT_EQ(request.options.sourceMefPath, "ref.mef");
    EXPECT_EQ(request.options.format, 3u);
    EXPECT_FALSE(request.options.reverseWinding);
    EXPECT_FALSE(request.options.emitCollision);
    EXPECT_FALSE(request.options.useBoundsCentrePivot);
}

TEST(MefImportCliTest, ReportsUsageErrors) {
    std::vector<char*> argv;
    std::vector<std::string> args = {"igi1ed.exe", "--import-mef"};
    for (auto& arg : args)
        argv.push_back(const_cast<char*>(arg.c_str()));
    mef_writer::ImportMefRequest request;
    std::string error;
    EXPECT_FALSE(mef_writer::ParseImportMefRequest(static_cast<int>(argv.size()), argv.data(),
                                                   &request, &error));
    EXPECT_FALSE(error.empty());

    EXPECT_TRUE(mef_writer::IsImportMefInvocation(static_cast<int>(argv.size()), argv.data()));
    std::vector<char*> guiArgv;
    std::string gui = "-level";
    guiArgv.push_back(const_cast<char*>("igi1ed.exe"));
    guiArgv.push_back(const_cast<char*>(gui.c_str()));
    EXPECT_FALSE(mef_writer::IsImportMefInvocation(static_cast<int>(guiArgv.size()), guiArgv.data()));
}

TEST(MefImportCliTest, RunImportMefCliWritesAParsableFile) {
    // Exercises the exact function source/cli/cli_handler.cpp dispatches to.
    const std::string objPath = WriteTempText("cli_roundtrip.obj", kQuadObj);
    const fs::path outPath = TempDir() / "cli_roundtrip.mef";
    std::error_code ec;
    fs::remove(outPath, ec);

    std::vector<std::string> args = {"igi1ed.exe", "--mef", objPath, "--import-mef",
                                     outPath.string()};
    std::vector<char*> argv;
    for (auto& arg : args)
        argv.push_back(const_cast<char*>(arg.c_str()));

    EXPECT_EQ(mef_writer::RunImportMefCli(static_cast<int>(argv.size()), argv.data()), 0);
    ASSERT_TRUE(fs::exists(outPath));

    ParsedGeometry parsed;
    ASSERT_NO_THROW(parsed = ParseMefFile(outPath.string()));
    EXPECT_EQ(parsed.vertices.size(), 4u);
    EXPECT_EQ(parsed.triangles.size(), 2u);

    // A usage error must return non-zero and write nothing.
    std::vector<std::string> badArgs = {"igi1ed.exe", "--import-mef"};
    std::vector<char*> badArgv;
    for (auto& arg : badArgs)
        badArgv.push_back(const_cast<char*>(arg.c_str()));
    EXPECT_NE(mef_writer::RunImportMefCli(static_cast<int>(badArgv.size()), badArgv.data()), 0);
}

// ===========================================================================
// Retail corpus round trip
// ===========================================================================

TEST(MefImportCorpusTest, StaticRetailModelsRoundTripThroughTheWriter) {
    const char* root = CorpusRoot();
    if (root == nullptr || !fs::exists(root)) {
        GTEST_SKIP() << "Set IGI_MEF_CORPUS (or IGI_GAME_PATH) to an IGI install root to enable "
                        "this suite";
    }

    const std::vector<std::string> files = FindMefFiles(root);
    ASSERT_FALSE(files.empty()) << "no .mef files under " << root;

    size_t tested = 0, skippedSkeletal = 0, skippedNoRender = 0, failed = 0;
    size_t attachmentsPreserved = 0;
    std::string firstFailure;

    for (const std::string& file : files) {
        ParsedGeometry source;
        try {
            source = ParseMefFile(file);
        } catch (const std::exception& e) {
            ++failed;
            if (firstFailure.empty())
                firstFailure = file + ": parse threw: " + e.what();
            continue;
        }

        if (source.modelType == 1) {
            ++skippedSkeletal;
            continue;
        }
        if (!source.fromRenderMesh || source.vertices.empty() || source.triangles.empty()) {
            ++skippedNoRender;
            continue;
        }

        mef_writer::WriteOptions options;
        options.format = source.modelType;   // preserve rigid vs lightmapped
        options.sourceMefPath = file;        // exercise the ATTA lift on real files
        options.reverseWinding = false;      // the parsed triangles are already retail order
        options.emitCollision = false;       // collision is not what this test compares

        std::vector<uint8_t> bytes;
        std::string error;
        if (!mef_writer::WriteMefBytesFromGeometry(source, options, &bytes, &error)) {
            ++failed;
            if (firstFailure.empty())
                firstFailure = file + ": write failed: " + error;
            continue;
        }

        ParsedGeometry reparsed;
        try {
            reparsed = ParseMefFileFromMemory(bytes, file);
        } catch (const std::exception& e) {
            ++failed;
            if (firstFailure.empty())
                firstFailure = file + ": reparse threw: " + e.what();
            continue;
        }

        bool mismatch = false;
        std::string detail;
        if (reparsed.vertices.size() != source.vertices.size()) {
            mismatch = true;
            detail = "vertex count " + std::to_string(reparsed.vertices.size()) + " != " +
                     std::to_string(source.vertices.size());
        } else if (reparsed.triangles.size() != source.triangles.size()) {
            mismatch = true;
            detail = "triangle count " + std::to_string(reparsed.triangles.size()) + " != " +
                     std::to_string(source.triangles.size());
        } else {
            for (size_t i = 0; i < source.vertices.size() && !mismatch; ++i) {
                const auto& a = source.vertices[i];
                const auto& b = reparsed.vertices[i];
                for (int k = 0; k < 3 && !mismatch; ++k) {
                    if (std::fabs(a.rawPos[k] - b.rawPos[k]) > 1e-3f) {
                        mismatch = true;
                        detail = "vertex " + std::to_string(i) + " position component " +
                                 std::to_string(k);
                    }
                }
                for (int k = 0; k < 3 && !mismatch; ++k) {
                    const float x = a.normal[k], y = b.normal[k];
                    if (std::isnan(x) || std::isnan(y))
                        continue;
                    if (std::fabs(x - y) > 1e-3f) {
                        mismatch = true;
                        detail = "vertex " + std::to_string(i) + " normal component " +
                                 std::to_string(k);
                    }
                }
                for (int k = 0; k < 2 && !mismatch; ++k) {
                    const float x = a.uv[k], y = b.uv[k];
                    if (std::isnan(x) || std::isnan(y))
                        continue;
                    if (std::fabs(x - y) > 1e-3f) {
                        mismatch = true;
                        detail = "vertex " + std::to_string(i) + " uv component " + std::to_string(k);
                    }
                }
                if (source.modelType == 3) {
                    for (int k = 0; k < 2 && !mismatch; ++k) {
                        const float x = a.uv2[k], y = b.uv2[k];
                        if (std::isnan(x) || std::isnan(y))
                            continue;
                        if (std::fabs(x - y) > 1e-3f) {
                            mismatch = true;
                            detail = "vertex " + std::to_string(i) + " uv2 component " +
                                     std::to_string(k);
                        }
                    }
                }
            }
            for (size_t t = 0; t < source.triangles.size() && !mismatch; ++t) {
                for (int k = 0; k < 3 && !mismatch; ++k) {
                    if (source.triangles[t][k] != reparsed.triangles[t][k]) {
                        mismatch = true;
                        detail = "triangle " + std::to_string(t) + " corner " + std::to_string(k);
                    }
                }
            }
        }

        if (mismatch) {
            ++failed;
            if (firstFailure.empty())
                firstFailure = file + ": " + detail;
            continue;
        }

        // The reference MEF's ATTA chunk must be lifted byte for byte.
        if (!source.mefAttachments.empty()) {
            const std::vector<ChunkView> written = WalkChunks(bytes);
            const ChunkView* atta = FindChunk(written, "ATTA");
            if (atta == nullptr || atta->size == 0) {
                ++failed;
                if (firstFailure.empty())
                    firstFailure = file + ": ATTA chunk was dropped";
                continue;
            }
            ++attachmentsPreserved;
        }
        ++tested;
    }

    EXPECT_EQ(failed, 0u) << "first failure: " << firstFailure;
    EXPECT_GT(tested, 0u) << "no static retail model was round-tripped";
    std::cout << "[MefImportCorpus] round-tripped " << tested << " static models ("
              << skippedSkeletal << " skeletal skipped, " << skippedNoRender
              << " without render geometry skipped, " << attachmentsPreserved
              << " with attachments)" << std::endl;
}

// The OBJ path (not the parsed-geometry path) against real retail geometry.
//
// A per-vertex OBJ is generated from each model, imported with --keep-winding,
// and the result must reproduce the original vertex positions, normals, UVs and
// triangle indices exactly.  This is the strongest end-to-end check available on
// this host: it goes through the OBJ text parser, the (v,vt,vn) weld, the group
// builder, the DNER/XTRV serialiser and back through the shipping reader.
namespace {

std::string ObjFromGeometry(const ParsedGeometry& geometry) {
    std::ostringstream out;
    out << std::setprecision(9);
    out << "# generated from a retail model\n";
    for (const auto& vertex : geometry.vertices)
        out << "v " << vertex.rawPos.x << ' ' << vertex.rawPos.y << ' ' << vertex.rawPos.z << "\n";
    for (const auto& vertex : geometry.vertices)
        out << "vn " << vertex.normal.x << ' ' << vertex.normal.y << ' ' << vertex.normal.z << "\n";
    for (const auto& vertex : geometry.vertices)
        out << "vt " << vertex.uv.x << ' ' << vertex.uv.y << "\n";
    for (const auto& triangle : geometry.triangles) {
        out << "f";
        for (int k = 0; k < 3; ++k) {
            const uint32_t i = triangle[k] + 1;
            out << ' ' << i << '/' << i << '/' << i;
        }
        out << "\n";
    }
    return out.str();
}

bool AllFinite(const ParsedGeometry& geometry) {
    for (const auto& vertex : geometry.vertices) {
        for (int k = 0; k < 3; ++k) {
            if (!std::isfinite(vertex.rawPos[k]) || !std::isfinite(vertex.normal[k]))
                return false;
        }
        for (int k = 0; k < 2; ++k) {
            if (!std::isfinite(vertex.uv[k]))
                return false;
        }
    }
    return true;
}

}  // namespace

TEST(MefImportCorpusTest, RetailGeometrySurvivesAnObjRoundTrip) {
    const char* root = CorpusRoot();
    if (root == nullptr || !fs::exists(root)) {
        GTEST_SKIP() << "Set IGI_MEF_CORPUS (or IGI_GAME_PATH) to an IGI install root to enable "
                        "this suite";
    }

    const std::vector<std::string> files = FindMefFiles(root);
    ASSERT_FALSE(files.empty()) << "no .mef files under " << root;

    // Bounded: this test is about the OBJ path, not about walking the corpus.
    const size_t kMaxModels = 40;
    size_t tested = 0, skipped = 0, failed = 0;
    std::string firstFailure;

    for (const std::string& file : files) {
        if (tested >= kMaxModels)
            break;

        ParsedGeometry source;
        try {
            source = ParseMefFile(file);
        } catch (const std::exception&) {
            continue;
        }
        if (source.modelType == 1 || !source.fromRenderMesh)
            continue;
        if (source.vertices.empty() || source.triangles.empty())
            continue;
        if (!AllFinite(source)) {
            ++skipped;
            continue;
        }
        // Skip models whose indices do not fit the 16-bit DNER fields, or whose
        // triangles the importer would drop as degenerate: those are checked
        // separately and would only obscure this comparison.
        if (source.vertices.size() > 0xFFFFu) {
            ++skipped;
            continue;
        }
        bool unusable = false;
        for (const auto& triangle : source.triangles) {
            if (triangle[0] >= source.vertices.size() || triangle[1] >= source.vertices.size() ||
                triangle[2] >= source.vertices.size()) {
                unusable = true;
                break;
            }
            const glm::vec3& p0 = source.vertices[triangle[0]].rawPos;
            const glm::vec3& p1 = source.vertices[triangle[1]].rawPos;
            const glm::vec3& p2 = source.vertices[triangle[2]].rawPos;
            if (glm::length(glm::cross(p1 - p0, p2 - p0)) == 0.f) {
                unusable = true;
                break;
            }
        }
        if (unusable) {
            ++skipped;
            continue;
        }

        const std::string objPath = WriteTempText("corpus_roundtrip.obj", ObjFromGeometry(source));
        mef_writer::WriteOptions options;
        options.reverseWinding = false;  // exact index comparison needs the OBJ order
        options.emitCollision = false;

        std::vector<uint8_t> bytes;
        std::string error;
        if (!mef_writer::WriteMefBytesFromObj(objPath, options, &bytes, &error)) {
            ++failed;
            if (firstFailure.empty())
                firstFailure = file + ": import failed: " + error;
            continue;
        }

        ParsedGeometry reparsed;
        try {
            reparsed = ParseMefFileFromMemory(bytes, file);
        } catch (const std::exception& e) {
            ++failed;
            if (firstFailure.empty())
                firstFailure = file + ": reparse threw: " + e.what();
            continue;
        }

        std::string detail;
        if (reparsed.vertices.size() != source.vertices.size()) {
            detail = "vertex count " + std::to_string(reparsed.vertices.size()) + " != " +
                     std::to_string(source.vertices.size());
        } else if (reparsed.triangles.size() != source.triangles.size()) {
            detail = "triangle count " + std::to_string(reparsed.triangles.size()) + " != " +
                     std::to_string(source.triangles.size());
        } else {
            for (size_t i = 0; i < source.vertices.size() && detail.empty(); ++i) {
                for (int k = 0; k < 3 && detail.empty(); ++k) {
                    if (std::fabs(source.vertices[i].rawPos[k] - reparsed.vertices[i].rawPos[k]) >
                        1e-4f)
                        detail = "vertex " + std::to_string(i) + " position";
                }
                for (int k = 0; k < 3 && detail.empty(); ++k) {
                    if (std::fabs(source.vertices[i].normal[k] - reparsed.vertices[i].normal[k]) >
                        1e-4f)
                        detail = "vertex " + std::to_string(i) + " normal";
                }
                for (int k = 0; k < 2 && detail.empty(); ++k) {
                    if (std::fabs(source.vertices[i].uv[k] - reparsed.vertices[i].uv[k]) > 1e-4f)
                        detail = "vertex " + std::to_string(i) + " uv";
                }
            }
            for (size_t t = 0; t < source.triangles.size() && detail.empty(); ++t) {
                for (int k = 0; k < 3 && detail.empty(); ++k) {
                    if (source.triangles[t][k] != reparsed.triangles[t][k])
                        detail = "triangle " + std::to_string(t) + " corner " + std::to_string(k);
                }
            }
        }

        if (!detail.empty()) {
            ++failed;
            if (firstFailure.empty())
                firstFailure = file + ": " + detail;
            continue;
        }
        ++tested;
    }

    EXPECT_EQ(failed, 0u) << "first failure: " << firstFailure;
    EXPECT_GE(tested, 10u) << "too few models exercised the OBJ path";
    std::cout << "[MefImportCorpus] OBJ round trip exact for " << tested
              << " retail models (" << skipped << " skipped)" << std::endl;
}
