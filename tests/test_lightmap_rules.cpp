// test_lightmap_rules.cpp - the retail object-lightmap rules, pure-logic port (#72).
//
// The oracle for every number here is IGI-RE (read-only). Key citations:
//   litmap/lightmap.c:89-101     Lightmap_GetDir 0x5210C0, Lightmap_GetResPath 0x5210E0
//   litmap/lightmap.c:186-190    LightmapInfo_ScanFile 0x523420 (the strstr, isDir test)
//   litmap/lightmap.c:231-257    LightmapInfo_MakeName 0x5235A0 (obj%05d allocation)
//   litmap/lightmap.c:195-227    LightmapInfo_Create 0x523450 (the defaults)
//   litmap/lightmap.c:277-318    LightmapInfo_Delete 0x5236C0 (the res unload)
//   litmap/litmaptypes.h:11-48   LightmapTex (0x2C) / Lightmaps (0x3C): the .olm layout
//   dirlight/lightmapbuild.c:26  LIGHTMAP_MAGIC 0x3DF5C28F
//   dirlight/lightmapbuild.c:44-100  the name format and the four-conjunct accept test
//   dirlight/lightmapinfo.c:75-200   kTexelScale, the bake scale, full/flags/unk
//   dirlight/lightmapinfo.c:433  kScaleRange {0.5f, 10.0f} in the DATA block
//   dirlight/dirlight.h:20-23    feq()
//   terredit/datetime.c:50-104   DateTime_Format / DateTime_Compare (text compare)
//   model/modeltypes.h:9-27      MeshHeader: version 0x00, time[7] 0x04, format 0x20
//
// The corpus half reads the real `MISSIONS/location0/level*/lightmaps/lightmaps.res`
// through the editor's own RES reader and validates every shipped `.olm` against
// the layout and the naming rule. It self-skips when the corpus is absent.

#include "../source/renderer/res_writer.h"
#include "../source/runtime/lightmap_rules.h"
#include "../source/lod/mef_mesh_header.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace igi::lightmap_rules;

fs::path GameRoot() {
    if (const char* env = std::getenv("IGI_GAME_PATH"); env != nullptr && *env != '\0')
        return fs::path(env);
    return fs::path("/Users/haseeb-mir/Downloads/IGI");
}

// ---------------------------------------------------------------------------
// Synthetic `.olm` builder, mirroring the layout litmaptypes.h:36-48 declares
// and the arithmetic lightmapbuild.c:72-109 writes. (IGI-RE's own test helper is
// tests/test_dirlight_lightmap.py:411-421 `olm_file`; this is the same shape.)
// ---------------------------------------------------------------------------

void PutU32(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) {
    b[at + 0] = static_cast<std::uint8_t>(v & 0xFF);
    b[at + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    b[at + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
    b[at + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
}

void PutI32(std::vector<std::uint8_t>& b, std::size_t at, std::int32_t v) {
    PutU32(b, at, static_cast<std::uint32_t>(v));
}

void PutI16(std::vector<std::uint8_t>& b, std::size_t at, std::int16_t v) {
    b[at + 0] = static_cast<std::uint8_t>(static_cast<std::uint16_t>(v) & 0xFF);
    b[at + 1] = static_cast<std::uint8_t>((static_cast<std::uint16_t>(v) >> 8) & 0xFF);
}

void PutF32(std::vector<std::uint8_t>& b, std::size_t at, float v) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    PutU32(b, at, bits);
}

std::vector<std::uint8_t> MakeOlm(const std::vector<std::pair<int, int>>& pages,
                                  std::uint32_t magic = kOlmMagic, float version = kOlmVersion,
                                  DateTime time = DateTime{2000, 11, 21, 7, 30, 13, 508},
                                  int from_resource = 0, int built = 1, int texels = 0,
                                  int texels2 = 0) {
    const int count = static_cast<int>(pages.size());
    std::size_t size = kOlmHeaderSize + static_cast<std::size_t>(count) * kOlmTexRecordSize;
    for (const auto& page : pages)
        size += static_cast<std::size_t>(page.first) * static_cast<std::size_t>(page.second) * 4;
    std::vector<std::uint8_t> buffer(size, 0);
    PutU32(buffer, kOlmMagicOffset, magic);
    PutF32(buffer, kOlmVersionOffset, version);
    PutI32(buffer, kOlmTimeOffset + 0, time.year);
    PutI32(buffer, kOlmTimeOffset + 4, time.month);
    PutI32(buffer, kOlmTimeOffset + 8, time.day);
    PutI32(buffer, kOlmTimeOffset + 12, time.hour);
    PutI32(buffer, kOlmTimeOffset + 16, time.minute);
    PutI32(buffer, kOlmTimeOffset + 20, time.second);
    PutI32(buffer, kOlmTimeOffset + 24, time.ms);
    PutI32(buffer, kOlmFromResourceOffset, from_resource);
    PutI32(buffer, kOlmBuiltOffset, built);
    PutI32(buffer, kOlmCountOffset, count);
    PutI32(buffer, kOlmTexelsOffset, texels);
    PutI32(buffer, kOlmTexels2Offset, texels2);
    for (int i = 0; i < count; ++i) {
        const std::size_t record = kOlmHeaderSize + static_cast<std::size_t>(i) * kOlmTexRecordSize;
        PutI16(buffer, record + kOlmTexSizeOffset, static_cast<std::int16_t>(pages[i].first));
        PutI16(buffer, record + kOlmTexSizeOffset + 2, static_cast<std::int16_t>(pages[i].second));
    }
    const std::size_t pixel =
        kOlmHeaderSize + static_cast<std::size_t>(count) * kOlmTexRecordSize;
    std::size_t n = 0;
    for (const auto& page : pages) {
        for (int i = 0; i < page.first * page.second; ++i, ++n) {
            PutU32(buffer, pixel + n * 4, 0xFF000000u | static_cast<std::uint32_t>(n & 0xFFFFFF));
        }
    }
    return buffer;
}

OlmFileHeader HeaderOf(const std::vector<std::uint8_t>& buffer) {
    OlmFileHeader header;
    const bool ok = ParseOlmHeader(buffer.data(), buffer.size(), &header);
    EXPECT_TRUE(ok);
    return header;
}

// ---------------------------------------------------------------------------
// Rule 1 - the resource names
// ---------------------------------------------------------------------------

TEST(LightmapRulesNameTest, ResourceNamesAreTheEngineLiterals) {
    // lightmap.c:91 and :98, with both format strings and both arguments stored in
    // the exe's DATA block (lightmap.c:433).
    EXPECT_STREQ(LightmapDir(), "MISSION:lightmaps");
    EXPECT_STREQ(LightmapResPath(), "MISSION:lightmaps/lightmaps.res");
}

TEST(LightmapRulesNameTest, OlmPathUsesTheEngineFormat) {
    // lightmapbuild.c:59: "%s/%s_%05d.olm" with Lightmap_GetDir() as %s.
    // `obj00006_00052.olm` is a real shipped name (level 4).
    EXPECT_EQ(OlmPath(LightmapDir(), "obj00006", 52), "MISSION:lightmaps/obj00006_00052.olm");
    EXPECT_EQ(OlmFileName("obj00006", 52), "obj00006_00052.olm");
    EXPECT_EQ(OlmPath("D", "N", 0), "D/N_00000.olm");
}

TEST(LightmapRulesNameTest, IndexFieldIsFiveDigitsZeroPadded) {
    // `%05d` is a MINIMUM width (lightmapbuild.c:59).
    EXPECT_EQ(OlmFileName("obj00000", 0), "obj00000_00000.olm");
    EXPECT_EQ(OlmFileName("obj00000", 1), "obj00000_00001.olm");
    EXPECT_EQ(OlmFileName("obj00000", 542), "obj00000_00542.olm");
    EXPECT_EQ(OlmFileName("obj00000", 99999), "obj00000_99999.olm");
    // Beyond 5 digits the field widens rather than truncating.
    EXPECT_EQ(OlmFileName("obj00000", 100000), "obj00000_100000.olm");
}

TEST(LightmapRulesNameTest, SeparatorAndExtensionAreExact) {
    const std::string name = OlmFileName("obj00001", 7);
    EXPECT_EQ(name, "obj00001_00007.olm");
    // One underscore before the digit field, and the extension is lower case.
    EXPECT_EQ(name.find('_'), 8u);
    EXPECT_EQ(name.find('_', 9), std::string::npos);
    EXPECT_EQ(name.compare(name.size() - 4, 4, ".olm"), 0);
    EXPECT_EQ(name.find(".OLM"), std::string::npos);
}

TEST(LightmapRulesNameTest, ObjectNameIsObjPlusFiveDigits) {
    // lightmap.c:240 and :255: crt_sprintf(name, "%s%05d", "obj", n).
    EXPECT_EQ(ObjectLightmapName(0), "obj00000");
    EXPECT_EQ(ObjectLightmapName(6), "obj00006");
    EXPECT_EQ(ObjectLightmapName(120), "obj00120");
    EXPECT_EQ(ObjectLightmapName(99999), "obj99999");
}

TEST(LightmapRulesNameTest, OlmFileNameRoundTrips) {
    for (int index : {0, 1, 7, 52, 68, 542, 99999}) {
        const OlmName parsed = ParseOlmFileName(OlmFileName("obj00006", index));
        ASSERT_TRUE(parsed.ok) << index;
        EXPECT_EQ(parsed.object_name, "obj00006");
        EXPECT_EQ(parsed.index, index);
    }
}

TEST(LightmapRulesNameTest, OlmNameParserRejectsForeignNames) {
    for (const char* bad : {"obj00006_00052.olm.bak", "obj00006_0052.olm", "obj00006_abcde.olm",
                            "obj00006.olm", "_00052.olm", "obj00006_00052", "obj00006_-0052.olm",
                            "obj00006_00052.OLM", ""}) {
        EXPECT_FALSE(ParseOlmFileName(bad).ok) << bad;
    }
}

// ---------------------------------------------------------------------------
// Rule 2 - the first-free `obj%05d` allocation (lightmap.c:231-257)
// ---------------------------------------------------------------------------

std::vector<ScanEntry> Files(const std::vector<std::string>& names) {
    std::vector<ScanEntry> entries;
    for (const std::string& name : names) entries.push_back(ScanEntry{name, false});
    return entries;
}

TEST(LightmapRulesMakeNameTest, FirstFreeIndexIgnoresDirectories) {
    // lightmap.c:188: `!e->isDir && crt_strstr(...)` -- a directory never matches,
    // so a directory called obj00000 does not consume index 0.
    std::vector<ScanEntry> entries{{"obj00000", true}, {"obj00001", true}};
    EXPECT_EQ(FindFreeObjectLightmapIndex({}, entries), 0);
}

TEST(LightmapRulesMakeNameTest, MatchIsSubstringNotEquality) {
    // lightmap.c:188 / :247 both use crt_strstr. A file that merely CONTAINS
    // `obj00000` consumes index 0, which is the engine's rule and is surprising
    // enough to pin here.
    EXPECT_EQ(FindFreeObjectLightmapIndex({}, Files({"xobj00000y.olm"})), 1);
    EXPECT_EQ(FindFreeObjectLightmapIndex({"pre-obj00000-post"}, {}), 1);
}

TEST(LightmapRulesMakeNameTest, SkipsTakenIndicesInBothSources) {
    // lightmap.c:236-253, the four cases IGI-RE's own test asserts
    // (tests/test_litmap_lightmap.py:121-122 CASES).
    EXPECT_EQ(FindFreeObjectLightmapIndex({}, {}), 0);
    EXPECT_EQ(FindFreeObjectLightmapIndex({"obj00000"}, {}), 1);
    EXPECT_EQ(FindFreeObjectLightmapIndex({}, Files({"obj00000_00001.olm"})), 1);
    EXPECT_EQ(FindFreeObjectLightmapIndex({"obj00000", "obj00002"},
                                          Files({"obj00001_00000.olm", "obj00003.olm"})),
              4);
}

TEST(LightmapRulesMakeNameTest, ScanFileCallbackRules) {
    // lightmap.c:186-190, HOOK 0x523420.
    EXPECT_TRUE(ScanNameMatches(ScanEntry{"obj00003_00001.olm", false}, "obj00003"));
    EXPECT_FALSE(ScanNameMatches(ScanEntry{"obj00003_00001.olm", true}, "obj00003"));
    EXPECT_TRUE(ScanNameMatches(ScanEntry{"obj00004.olm", false}, "obj00004"));
    EXPECT_FALSE(ScanNameMatches(ScanEntry{"obj00004.olm", false}, "obj00005"));
}

// ---------------------------------------------------------------------------
// The timestamp (datetime.c:50-104)
// ---------------------------------------------------------------------------

TEST(LightmapRulesDateTimeTest, FormatMatchesDateTimeFormat) {
    // datetime.c:78 + :84 + :90 joined by the two separators at :59 and :65.
    EXPECT_EQ(FormatDateTime(DateTime{2000, 11, 21, 7, 30, 13, 508}), "2000-11-21-07-30-13-0508");
    EXPECT_EQ(FormatDateTime(DateTime{2000, 11, 20, 23, 37, 41, 972}), "2000-11-20-23-37-41-0972");
    EXPECT_EQ(FormatDateTime(DateTime{0, 0, 0, 0, 0, 0, 0}), "0000-00-00-00-00-00-0000");
}

TEST(LightmapRulesDateTimeTest, CompareIsLexicographicOnTheText) {
    // datetime.c:94-104.
    const DateTime a{2000, 11, 21, 7, 30, 13, 508};
    EXPECT_EQ(DateTimeCompare(a, a), 0);
    EXPECT_EQ(DateTimeCompare(a, DateTime{2000, 11, 21, 7, 30, 13, 509}), -1);
    EXPECT_EQ(DateTimeCompare(a, DateTime{2000, 11, 21, 7, 30, 12, 999}), 1);
    // The year dominates: a later month in an earlier year is still earlier.
    EXPECT_EQ(DateTimeCompare(DateTime{2001, 1, 1, 0, 0, 0, 0}, DateTime{2000, 12, 31, 23, 59, 59, 999}),
              1);
    // Field-wise the text compare and a numeric compare agree, because every
    // field is fixed width.
    EXPECT_EQ(DateTimeCompare(DateTime{2000, 2, 1, 0, 0, 0, 0}, DateTime{2000, 10, 1, 0, 0, 0, 0}), -1);
}

// ---------------------------------------------------------------------------
// Rule 3 - the accept/reject test (lightmapbuild.c:53-88)
// ---------------------------------------------------------------------------

OlmAcceptInput MeshFor(const OlmFileHeader& file, bool full = false) {
    OlmAcceptInput mesh;
    mesh.mesh_version = file.version;
    mesh.mesh_time = file.time;
    mesh.mesh_tex_count = file.count;
    mesh.full = full;
    return mesh;
}

// ---------------------------------------------------------------------------
// The mesh side of the test, read out of a .mef
//
// A minimal ILFF container: the form, then a MESH ("HSEM") child whose payload is
// the 0x90-byte MeshHeader, then -- for format 3 -- the RD3D ("D3DR") child the
// engine's `mh->data` points at (Direct3DRender_LoadModel case 3, HOOK 0x49FB10,
// src/bonemesh/d3drender.c:224-230) and the LTMP ("PMTL") child that holds the
// `short[4]` page-size array `LightmapMesh.sizes` names
// (src/litmap/litmaptypes.h:145, d3drender.c:229). Laid out the way
// Chunk_FirstChild 0x4B7490 / Chunk_Next 0x4B7470 / Chunk_Data 0x4B74D0 walk it
// (src/res/chunk.c:9-49) with align 4, as every retail .mef uses: a child header
// sits at the parent + 0x10-aligned offset and its payload 0x10 bytes later.
// ---------------------------------------------------------------------------

constexpr std::size_t kMeshPayload = 0x90;   // MeshHeader, in memory 0x90
constexpr std::size_t kRd3dPayload = 0x38;   // MeshRender (src/bonemesh/bonemesh.h:18-31)
constexpr std::size_t kLtmpRecord = 8;       // short[4]

std::vector<std::uint8_t> MakeMef(int format, float version, const int time[7], int tex_count,
                                  int unk8, const std::vector<std::pair<int, int>>& sizes,
                                  bool with_rd3d = true) {
    const std::size_t hsem = 0x14;
    const std::size_t hsem_data = hsem + 0x10;
    const std::size_t rd3d = hsem_data + kMeshPayload;
    const std::size_t rd3d_data = rd3d + 0x10;
    const std::size_t ltmp = rd3d_data + kRd3dPayload;
    const std::size_t ltmp_data = ltmp + 0x10;
    const std::size_t total = ltmp_data + sizes.size() * kLtmpRecord;
    std::vector<std::uint8_t> b(total, 0);
    auto put_u32 = [&](std::size_t off, std::uint32_t v) { PutU32(b, off, v); };
    auto put_f32 = [&](std::size_t off, float v) { PutF32(b, off, v); };

    std::memcpy(b.data(), "ILFF", 4);
    put_u32(0x04, static_cast<std::uint32_t>(total));  // parent size
    put_u32(0x08, 4);                                  // parent align

    // MESH ("HSEM")
    std::memcpy(b.data() + hsem, "HSEM", 4);
    put_u32(hsem + 0x04, static_cast<std::uint32_t>(kMeshPayload));
    put_u32(hsem + 0x08, 4);
    put_u32(hsem + 0x0C, static_cast<std::uint32_t>(rd3d - hsem));  // next sibling
    put_f32(hsem_data + 0x00, version);                             // MeshHeader.version
    for (int i = 0; i < 7; ++i)
        put_u32(hsem_data + 0x04 + 4 * i, static_cast<std::uint32_t>(time[i]));
    put_u32(hsem_data + 0x20, static_cast<std::uint32_t>(format));

    // RD3D ("D3DR"): unk4 is LightmapMesh.texCount, unk8 the second build gate
    std::memcpy(b.data() + rd3d, "D3DR", 4);
    put_u32(rd3d + 0x04, static_cast<std::uint32_t>(with_rd3d ? kRd3dPayload : 0));
    put_u32(rd3d + 0x08, 4);
    put_u32(rd3d + 0x0C, static_cast<std::uint32_t>(ltmp - rd3d));
    put_u32(rd3d_data + 0x04, static_cast<std::uint32_t>(tex_count));
    put_u32(rd3d_data + 0x08, static_cast<std::uint32_t>(unk8));

    // LTMP ("PMTL"): `texCount` records of short[4]
    std::memcpy(b.data() + ltmp, "PMTL", 4);
    put_u32(ltmp + 0x04, static_cast<std::uint32_t>(sizes.size() * kLtmpRecord));
    put_u32(ltmp + 0x08, 4);
    put_u32(ltmp + 0x0C, 0);  // last sibling
    for (std::size_t i = 0; i < sizes.size(); ++i) {
        PutI16(b, ltmp_data + i * kLtmpRecord + 0, static_cast<std::int16_t>(sizes[i].first));
        PutI16(b, ltmp_data + i * kLtmpRecord + 2, static_cast<std::int16_t>(sizes[i].second));
    }
    return b;
}

TEST(LightmapRulesAcceptTest, MeshSideComesFromTheMeshHeaderAndTheRd3dChunk) {
    // MeshHeader.version +0x00, time[7] +0x04 (src/model/modeltypes.h:9-27) and
    // LightmapMesh.texCount = the RD3D payload's +0x04 (src/litmap/litmaptypes.h:139)
    // are the three mesh-side words of the accept test (lightmapbuild.c:63-64).
    const int time[7] = {2000, 11, 21, 7, 30, 13, 508};
    const std::vector<std::uint8_t> mef =
        MakeMef(3, kOlmVersion, time, /*tex_count=*/2, /*unk8=*/1, {{256, 16}, {256, 8}});

    const igi::lod::MefMeshHeader header = igi::lod::ReadMefMeshHeader(mef);
    ASSERT_TRUE(header.valid);
    EXPECT_EQ(header.format, 3);
    EXPECT_EQ(std::memcmp(&header.version, &kOlmVersionBits, 4), 0)
        << "version must be read as the raw float bits 0x3DCCCCCD";
    for (int i = 0; i < 7; ++i) EXPECT_EQ(header.time[i], time[i]) << "field " << i;
    EXPECT_TRUE(header.has_lightmap_mesh);
    EXPECT_EQ(header.lightmap_tex_count, 2);
    EXPECT_EQ(header.lightmap_unk8, 1);

    // The LTMP payload is the page-size array `mesh->sizes` (lightmapbuild.c:103).
    std::vector<std::array<std::int16_t, 2>> sizes;
    ASSERT_TRUE(igi::lod::ReadMefLightmapPageSizes(mef, &sizes));
    ASSERT_EQ(sizes.size(), 2u);
    EXPECT_EQ(sizes[0][0], 256);
    EXPECT_EQ(sizes[0][1], 16);
    EXPECT_EQ(sizes[1][0], 256);
    EXPECT_EQ(sizes[1][1], 8);

    // The input built from those words accepts a file whose page count and
    // version match and whose timestamp is later than the mesh's.
    const auto buffer = MakeOlm({{256, 16}, {256, 8}}, kOlmMagic, kOlmVersion,
                                DateTime{2000, 11, 21, 7, 30, 14, 0});
    const OlmFileHeader file = HeaderOf(buffer);
    const OlmAcceptInput mesh =
        MakeOlmAcceptInput(header.version, header.time, header.lightmap_tex_count);
    EXPECT_FALSE(mesh.full) << "the editor's load path is the engine's !full branch (:65)";
    EXPECT_TRUE(OlmHeaderAccepted(file, mesh));

    // ...and rejects the same file against the SAME mesh with a different page
    // count -- the conjunct that ties the file to the mesh that baked it (:64).
    const OlmAcceptInput other =
        MakeOlmAcceptInput(header.version, header.time, header.lightmap_tex_count + 1);
    EXPECT_FALSE(OlmHeaderAccepted(file, other));

    // A format-0 mesh has no LightmapMesh at all: `if (mh->format != 3) return
    // NULL;` (lightmapbuild.c:53-54), so the reader must not report one.
    const std::vector<std::uint8_t> rigid =
        MakeMef(0, kOlmVersion, time, 2, 1, {{256, 16}, {256, 8}});
    const igi::lod::MefMeshHeader rigid_header = igi::lod::ReadMefMeshHeader(rigid);
    ASSERT_TRUE(rigid_header.valid);
    EXPECT_EQ(rigid_header.format, 0);
    EXPECT_FALSE(rigid_header.has_lightmap_mesh);
}

TEST(LightmapRulesAcceptTest, MakeOlmAcceptInputMapsTheSevenTimeWordsInOrder) {
    // MeshHeader.time[7] and Lightmaps.time[7] are the same seven ints in
    // DateTime_Get order (datetime.c:16-26), which is what makes
    // DateTime_Compare(mh->time, file->time) meaningful (lightmapbuild.c:64).
    const int time[7] = {2001, 2, 3, 4, 5, 6, 7};
    const OlmAcceptInput mesh = MakeOlmAcceptInput(0.1f, time, 3, true);
    EXPECT_EQ(mesh.mesh_time.year, 2001);
    EXPECT_EQ(mesh.mesh_time.month, 2);
    EXPECT_EQ(mesh.mesh_time.day, 3);
    EXPECT_EQ(mesh.mesh_time.hour, 4);
    EXPECT_EQ(mesh.mesh_time.minute, 5);
    EXPECT_EQ(mesh.mesh_time.second, 6);
    EXPECT_EQ(mesh.mesh_time.ms, 7);
    EXPECT_FLOAT_EQ(mesh.mesh_version, 0.1f);
    EXPECT_EQ(mesh.mesh_tex_count, 3);
    EXPECT_TRUE(mesh.full);
    EXPECT_EQ(FormatDateTime(mesh.mesh_time), "2001-02-03-04-05-06-0007");
}

TEST(LightmapRulesAcceptTest, AllFourConjunctsMustHold) {
    const auto buffer = MakeOlm({{4, 4}});
    const OlmFileHeader file = HeaderOf(buffer);
    EXPECT_EQ(file.magic, kOlmMagic);
    EXPECT_FLOAT_EQ(file.version, kOlmVersion);
    EXPECT_EQ(file.count, 1);

    // A mesh whose bake is OLDER than the file: accepted (lightmapbuild.c:64).
    OlmAcceptInput mesh = MeshFor(file);
    mesh.mesh_time.ms = file.time.ms - 1;
    EXPECT_TRUE(OlmHeaderAccepted(file, mesh));

    // Equal timestamps are NOT accepted: the compare must be strictly < 0.
    EXPECT_FALSE(OlmHeaderAccepted(file, MeshFor(file)));

    // A NEWER mesh (the file is stale) is not accepted.
    OlmAcceptInput newer = MeshFor(file);
    newer.mesh_time.second = file.time.second + 1;
    EXPECT_FALSE(OlmHeaderAccepted(file, newer));
}

TEST(LightmapRulesAcceptTest, MagicVersionAndCountReject) {
    const auto buffer = MakeOlm({{4, 4}});
    const OlmFileHeader file = HeaderOf(buffer);
    OlmAcceptInput mesh = MeshFor(file);
    mesh.mesh_time.ms = file.time.ms - 1;
    ASSERT_TRUE(OlmHeaderAccepted(file, mesh));

    OlmFileHeader bad_magic = file;
    bad_magic.magic = 0x1234;  // IGI-RE's own test uses this value (:496)
    EXPECT_FALSE(OlmHeaderAccepted(bad_magic, mesh));

    OlmFileHeader bad_version = file;
    bad_version.version = 0.2f;
    EXPECT_FALSE(OlmHeaderAccepted(bad_version, mesh));

    OlmFileHeader bad_count = file;
    bad_count.count = 3;
    EXPECT_FALSE(OlmHeaderAccepted(bad_count, mesh));

    // The mesh version is the OTHER side of the same conjunct.
    OlmAcceptInput bad_mesh_version = mesh;
    bad_mesh_version.mesh_version = 0.2f;
    EXPECT_FALSE(OlmHeaderAccepted(file, bad_mesh_version));

    OlmAcceptInput bad_mesh_count = mesh;
    bad_mesh_count.mesh_tex_count = 2;
    EXPECT_FALSE(OlmHeaderAccepted(file, bad_mesh_count));
}

TEST(LightmapRulesAcceptTest, FullForcesARebuildEvenWhenAccepted) {
    // lightmapbuild.c:65 `if (!full) { ... return file; }` and :83-84: the
    // `return file` is inside the `!full` block, so `full != 0` rebuilds.
    const auto buffer = MakeOlm({{4, 4}});
    const OlmFileHeader file = HeaderOf(buffer);
    OlmAcceptInput mesh = MeshFor(file);
    mesh.mesh_time.ms = file.time.ms - 1;
    EXPECT_EQ(DecideOlmFile(file, mesh), OlmDecision::kLoadPage0Only);
    mesh.full = true;
    EXPECT_EQ(DecideOlmFile(file, mesh), OlmDecision::kRebuild);

    // A rejected file is a rebuild in either mode.
    OlmAcceptInput stale = MeshFor(file);
    EXPECT_EQ(DecideOlmFile(file, stale), OlmDecision::kRebuild);
    stale.full = true;
    EXPECT_EQ(DecideOlmFile(file, stale), OlmDecision::kRebuild);
}

TEST(LightmapRulesAcceptTest, FeqTreatsUnorderedAsEqual) {
    // dirlight.h:20-23: `!(a < b || a > b)`.
    EXPECT_TRUE(Feq(0.1f, 0.1f));
    EXPECT_TRUE(Feq(0.0f, -0.0f));
    EXPECT_FALSE(Feq(1.0f, 2.0f));
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_TRUE(Feq(nan, nan));   // the engine's compare sets ZF for unordered
    EXPECT_TRUE(Feq(nan, 1.0f));
}

TEST(LightmapRulesAcceptTest, NanVersionIsAcceptedBecauseFeqIsUnorderedEqual) {
    // A consequence of feq: a file whose version field is NaN compares EQUAL to
    // the mesh version. Reproduced, not sanitised (lightmapbuild.c:63).
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const auto buffer = MakeOlm({{4, 4}}, kOlmMagic, nan);
    const OlmFileHeader file = HeaderOf(buffer);
    OlmAcceptInput mesh = MeshFor(file);
    mesh.mesh_version = 0.1f;
    mesh.mesh_time.ms = file.time.ms - 1;
    EXPECT_TRUE(OlmHeaderAccepted(file, mesh));
}

// ---------------------------------------------------------------------------
// Reading the bytes (litmaptypes.h:11-48)
// ---------------------------------------------------------------------------

TEST(LightmapRulesLayoutTest, PixelOffsetAndExpectedSize) {
    // lightmapbuild.c:72 `px = (char *)&file->tex[file->count]`, and :76-78
    // `n = t->width * t->height; px += n * 4`.
    EXPECT_EQ(OlmPixelOffset(0), 0x3Cu);
    EXPECT_EQ(OlmPixelOffset(1), 0x3Cu + 0x2Cu);
    EXPECT_EQ(OlmPixelOffset(3), 0x3Cu + 3 * 0x2Cu);
    EXPECT_EQ(OlmExpectedSize({}), 0x3Cu);
    EXPECT_EQ(OlmExpectedSize({{256, 256}}), 0x3Cu + 0x2Cu + 256u * 256u * 4u);
    EXPECT_EQ(OlmExpectedSize({{256, 256}, {256, 64}}),
              0x3Cu + 2 * 0x2Cu + 256u * 256u * 4u + 256u * 64u * 4u);
}

TEST(LightmapRulesLayoutTest, ParseHeaderReadsEveryField) {
    const auto buffer = MakeOlm({{4, 4}}, kOlmMagic, kOlmVersion,
                                DateTime{2000, 11, 20, 23, 37, 42, 212}, /*from_resource=*/1,
                                /*built=*/0, /*texels=*/30, /*texels2=*/12);
    const OlmFileHeader file = HeaderOf(buffer);
    EXPECT_EQ(file.magic, kOlmMagic);
    EXPECT_FLOAT_EQ(file.version, kOlmVersion);
    EXPECT_EQ(FormatDateTime(file.time), "2000-11-20-23-37-42-0212");
    EXPECT_EQ(file.from_resource, 1);
    EXPECT_EQ(file.built, 0);
    EXPECT_EQ(file.count, 1);
    EXPECT_EQ(file.texels, 30);
    EXPECT_EQ(file.texels2, 12);
}

TEST(LightmapRulesLayoutTest, ReadPagesChecksTheWholeBuffer) {
    const auto buffer = MakeOlm({{256, 256}, {256, 64}});
    OlmFileHeader header;
    std::vector<OlmPage> pages;
    ASSERT_TRUE(ReadOlmPages(buffer.data(), buffer.size(), &header, &pages));
    ASSERT_EQ(pages.size(), 2u);
    EXPECT_EQ(pages[0].width, 256);
    EXPECT_EQ(pages[0].height, 256);
    EXPECT_EQ(pages[1].width, 256);
    EXPECT_EQ(pages[1].height, 64);
    EXPECT_EQ(header.count, 2);

    // One byte short, one byte long: both rejected.
    EXPECT_FALSE(ReadOlmPages(buffer.data(), buffer.size() - 1, &header, &pages));
    std::vector<std::uint8_t> longer = buffer;
    longer.push_back(0);
    EXPECT_FALSE(ReadOlmPages(longer.data(), longer.size(), &header, &pages));
}

TEST(LightmapRulesLayoutTest, ShortAndCorruptBuffersAreRejected) {
    OlmFileHeader header;
    EXPECT_FALSE(ParseOlmHeader(nullptr, 0, &header));
    const std::vector<std::uint8_t> empty;
    EXPECT_FALSE(ParseOlmHeader(empty.data(), 0, &header));

    // A header with zero pages is legal on its own: 0x3C bytes, count 0.
    auto zero_pages = MakeOlm({});
    ASSERT_EQ(zero_pages.size(), kOlmHeaderSize);
    ASSERT_TRUE(ParseOlmHeader(zero_pages.data(), zero_pages.size(), &header));
    EXPECT_EQ(header.count, 0);
    EXPECT_EQ(OlmPixelOffset(0), kOlmHeaderSize);

    // A count of 1 needs the record too, so a bare 0x3C-byte buffer is short.
    auto buffer = MakeOlm({{4, 4}});
    EXPECT_FALSE(ParseOlmHeader(buffer.data(), kOlmHeaderSize - 1, &header));
    EXPECT_FALSE(ParseOlmHeader(buffer.data(), kOlmHeaderSize, &header));
    EXPECT_TRUE(ParseOlmHeader(buffer.data(), OlmPixelOffset(1), &header));

    // A negative count cannot describe a file.
    PutI32(buffer, kOlmCountOffset, -1);
    EXPECT_FALSE(ParseOlmHeader(buffer.data(), buffer.size(), &header));
    // A count whose records do not fit is rejected by the port guard.
    PutI32(buffer, kOlmCountOffset, 1000);
    EXPECT_FALSE(ParseOlmHeader(buffer.data(), buffer.size(), &header));
}

// ---------------------------------------------------------------------------
// The STREAMING reader's plan: header + records only, then one span per page.
// This is the arithmetic `LoadOlmPages` (source/renderer/olm_texture.cpp) runs,
// so it is the part of the .olm fix that a host test can execute without a GL
// context. A shipped `.olm` carries 1..3 pages; the editor used to read page 0
// only, with an "88-byte header + 16-byte descriptor" layout that coincides with
// the engine's 0x3C/0x2C one exactly while count == 1.
// ---------------------------------------------------------------------------

TEST(LightmapRulesPageTableTest, RecordsParseWithoutThePixelBlock) {
    const auto buffer = MakeOlm({{256, 256}, {256, 64}, {256, 16}});
    const std::size_t prefix = OlmPixelOffset(3);
    ASSERT_LT(prefix, buffer.size()) << "the fixture must have a pixel block";

    std::vector<OlmPage> pages;
    ASSERT_TRUE(ParseOlmPageRecords(buffer.data(), prefix, 3, &pages));
    ASSERT_EQ(pages.size(), 3u);
    EXPECT_EQ(pages[0].width, 256);
    EXPECT_EQ(pages[0].height, 256);
    EXPECT_EQ(pages[1].height, 64);
    EXPECT_EQ(pages[2].height, 16);

    // One byte short of the record array, and a count one larger than the file
    // holds, are both rejected.
    EXPECT_FALSE(ParseOlmPageRecords(buffer.data(), prefix - 1, 3, &pages));
    EXPECT_FALSE(ParseOlmPageRecords(buffer.data(), prefix, 4, &pages));
    // A zero-sized page is not a page this reader will multiply.
    auto zero = MakeOlm({{0, 8}});
    EXPECT_FALSE(ParseOlmPageRecords(zero.data(), OlmPixelOffset(1), 1, &pages));
}

TEST(LightmapRulesPageTableTest, PlanGivesEveryPageInPageOrder) {
    const auto buffer = MakeOlm({{256, 256}, {256, 64}, {256, 16}});
    const std::size_t prefix = OlmPixelOffset(3);

    std::vector<OlmPageSpan> spans;
    ASSERT_TRUE(PlanOlmPages(buffer.data(), prefix, buffer.size(), &spans));
    ASSERT_EQ(spans.size(), 3u);

    EXPECT_EQ(spans[0].offset, OlmPixelOffset(3));
    EXPECT_EQ(spans[0].bytes, 256u * 256u * 4u);
    EXPECT_EQ(spans[1].offset, spans[0].offset + spans[0].bytes);
    EXPECT_EQ(spans[1].bytes, 256u * 64u * 4u);
    EXPECT_EQ(spans[2].offset, spans[1].offset + spans[1].bytes);
    EXPECT_EQ(spans[2].bytes, 256u * 16u * 4u);

    // The last page ends exactly at the end of the file, which is what the engine's
    // `px += n * 4` cursor does (lightmapbuild.c:76-78) and what the corpus asserts.
    EXPECT_EQ(spans[2].offset + spans[2].bytes, buffer.size());
    // Offsets ascend and every page is a distinct block, so page 1 is NOT page 0.
    EXPECT_LT(spans[0].offset, spans[1].offset);
    EXPECT_LT(spans[1].offset, spans[2].offset);

    // A file one byte short of the last page's block is rejected (port guard).
    EXPECT_FALSE(PlanOlmPages(buffer.data(), prefix, buffer.size() - 1, &spans));
    // ...and so is a prefix that does not even hold the records.
    EXPECT_FALSE(PlanOlmPages(buffer.data(), prefix - 1, buffer.size(), &spans));
}

TEST(LightmapRulesPageTableTest, PixelOffsetMatchesTheEngineCursor) {
    const std::vector<OlmPage> pages{{256, 256}, {256, 64}, {256, 16}};
    EXPECT_EQ(OlmPagePixelOffset(pages, 0), OlmPixelOffset(3));
    EXPECT_EQ(OlmPagePixelOffset(pages, 1), OlmPixelOffset(3) + 256u * 256u * 4u);
    EXPECT_EQ(OlmPagePixelOffset(pages, 2), OlmPixelOffset(3) + 256u * 256u * 4u + 256u * 64u * 4u);
    // Out of range has no page to point at.
    EXPECT_EQ(OlmPagePixelOffset(pages, -1), 0u);
    EXPECT_EQ(OlmPagePixelOffset(pages, 3), 0u);
    EXPECT_EQ(OlmPagePixelOffset({}, 0), 0u);
}

// ---------------------------------------------------------------------------
// Rule 4 - the bake parameters (lightmapinfo.c:97-200)
// ---------------------------------------------------------------------------

TEST(LightmapRulesBakeTest, ScaleIsLodTimesInfoScaleTimesTexelScale) {
    // lightmapinfo.c:150 with kTexelScale = 4096.0f (:75).
    EXPECT_FLOAT_EQ(BakeScale(0, 1.0f), 4096.0f);
    EXPECT_FLOAT_EQ(BakeScale(1, 1.0f), 8192.0f);
    EXPECT_FLOAT_EQ(BakeScale(2, 1.0f), 16384.0f);
    EXPECT_FLOAT_EQ(BakeScale(4, 1.0f), 65536.0f);
    EXPECT_FLOAT_EQ(BakeScale(0, 0.5f), 2048.0f);
    EXPECT_FLOAT_EQ(BakeScale(0, 2.0f), 8192.0f);
    // Out of the range a ref can carry (sector.c:557, VIRMODEL_LODS = 5).
    EXPECT_FLOAT_EQ(BakeScale(-1, 1.0f), 0.0f);
    EXPECT_FLOAT_EQ(BakeScale(31, 1.0f), 0.0f);
}

TEST(LightmapRulesBakeTest, UnkIsTheFloatBitPattern) {
    // lightmapinfo.c:151 `(int)float_bits(&info->unk94)`; float_bits is :80-86.
    EXPECT_EQ(BakeUnkFromFloatBits(100.0f), 0x42C80000);
    EXPECT_EQ(BakeUnkFromFloatBits(100.0f), 1120403456);
    EXPECT_EQ(BakeUnkFromFloatBits(1.0f), 0x3F800000);
    EXPECT_EQ(BakeUnkFromFloatBits(0.0f), 0);
    EXPECT_EQ(BakeUnkFromFloatBits(-0.0f), static_cast<int>(0x80000000u));
    EXPECT_EQ(BakeUnkFromFloatBits(0.5f), 0x3F000000);
}

TEST(LightmapRulesBakeTest, FullFlagsAndEdited) {
    // lightmapinfo.c:108, :149, :151.
    EXPECT_TRUE(BakeEdited(false, false, true));
    EXPECT_TRUE(BakeEdited(false, true, false));
    EXPECT_TRUE(BakeEdited(true, false, false));
    EXPECT_FALSE(BakeEdited(false, false, false));
    EXPECT_EQ(BakeFlags(true), 1);
    EXPECT_EQ(BakeFlags(false), 0);

    EXPECT_TRUE(BakeFullRebuild(true, 0, false));
    EXPECT_TRUE(BakeFullRebuild(false, 1, false));
    EXPECT_TRUE(BakeFullRebuild(false, 0, true));
    EXPECT_FALSE(BakeFullRebuild(false, 0, false));
}

TEST(LightmapRulesBakeTest, StaleFromScaleIsBitExact) {
    // lightmapinfo.c:106-107: `!unkCC && float_bits(lastScale) != 0x3F800000`.
    EXPECT_FALSE(BakeStaleFromScale(1, 0.5f));   // unkCC != 0 short-circuits
    EXPECT_FALSE(BakeStaleFromScale(0, 1.0f));
    EXPECT_TRUE(BakeStaleFromScale(0, 0.5f));
    // -0.0f has a different bit pattern from 1.0f, so it is "stale"; a plain
    // `lastScale != 1.0f` would say the same here, but the NaN case differs.
    EXPECT_TRUE(BakeStaleFromScale(0, -0.0f));
    EXPECT_TRUE(BakeStaleFromScale(0, std::numeric_limits<float>::quiet_NaN()));
}

TEST(LightmapRulesBakeTest, RefsMissingIsWhatMarksALoadedBakeStale) {
    // lightmapinfo.c:111-114: a ref with lightmaps but no info.
    // Lightmap_BuildLod's load path is what clears `info` (lightmapbuild.c:69).
    EXPECT_FALSE(BakeRefsMissing({}, {}));
    EXPECT_FALSE(BakeRefsMissing({true, false}, {true, true}));
    EXPECT_TRUE(BakeRefsMissing({true}, {false}));
    EXPECT_TRUE(BakeRefsMissing({true, true}, {true, false}));
    // A ref with no lightmaps at all is not "missing".
    EXPECT_FALSE(BakeRefsMissing({false}, {false}));
}

TEST(LightmapRulesBakeTest, EarlyOutNeedsEveryCondition) {
    // lightmapinfo.c:120.
    EXPECT_TRUE(BakeEarlyOut(true, 1.0f, 1.0f, false, 0));
    EXPECT_FALSE(BakeEarlyOut(false, 1.0f, 1.0f, false, 0));
    EXPECT_FALSE(BakeEarlyOut(true, 0.5f, 1.0f, false, 0));
    EXPECT_FALSE(BakeEarlyOut(true, 1.0f, 1.0f, true, 0));
    EXPECT_FALSE(BakeEarlyOut(true, 1.0f, 1.0f, false, 1));
    // feq: a NaN pair still counts as "unchanged".
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_TRUE(BakeEarlyOut(true, nan, nan, false, 0));
}

TEST(LightmapRulesBakeTest, RefSetReloadIsGatedButTheBuildLoopIsNot) {
    // lightmapinfo.c:137-138 vs :147-162.
    EXPECT_TRUE(BakeReloadsRefSet(true));
    EXPECT_FALSE(BakeReloadsRefSet(false));
}

TEST(LightmapRulesBakeTest, HemicubeSizes) {
    // lightmap.c:110-121 with the default 150 (lightmap.c:212, used at
    // lightmapinfo.c:180).
    EXPECT_EQ(HemicubeSize(kDefaultHemicubeSize), 150);
    EXPECT_EQ(HemicubeTopTexels(150), 22500u);
    EXPECT_EQ(HemicubeSideTexels(150), 75u * 150u);
    EXPECT_EQ(HemicubeTopTexels(8), 64u);
    EXPECT_EQ(HemicubeSideTexels(8), 32u);
    EXPECT_EQ(HemicubeTopTexels(0), 0u);
    EXPECT_EQ(HemicubeSideTexels(-4), 0u);
}

TEST(LightmapRulesBakeTest, PostRebuildBranches) {
    // lightmapinfo.c:177 (`unk6F || unk70`) and :197 (`unk71`).
    EXPECT_TRUE(BakeComputesVertexLights(true, false));
    EXPECT_TRUE(BakeComputesVertexLights(false, true));
    EXPECT_FALSE(BakeComputesVertexLights(false, false));
    EXPECT_TRUE(BakeCallsTaskNothing2(true));
    EXPECT_FALSE(BakeCallsTaskNothing2(false));
}

TEST(LightmapRulesBakeTest, DefaultsMatchLightmapInfoCreate) {
    // lightmap.c:211-223.
    EXPECT_FLOAT_EQ(kDefaultScale, 1.0f);
    EXPECT_FLOAT_EQ(kDefaultUnk94, 100.0f);
    EXPECT_EQ(kDefaultHemicubeSize, 150);
    EXPECT_EQ(kDefaultUnkA0, 1000);
    EXPECT_EQ(kDefaultUnkB4, 1);
    EXPECT_EQ(kDefaultUnkCC, 1);
    EXPECT_EQ(kDefaultUnkDC, 0);
    // The scale parameter's registered range is the first 8 bytes of the DATA
    // block at 0x54F050 (lightmapinfo.c:433, aliased at :434).
    EXPECT_FLOAT_EQ(kScaleRangeMin, 0.5f);
    EXPECT_FLOAT_EQ(kScaleRangeMax, 10.0f);
    // And the version a fresh bake carries (lightmapbuild.c:98).
    std::uint32_t version_bits = 0;
    std::memcpy(&version_bits, &kOlmVersion, 4);
    EXPECT_EQ(version_bits, kOlmVersionBits);
}

TEST(LightmapRulesResTest, ArchiveLoadAndUnloadAreLazyAndPaired) {
    // lightmap.c:200-201 and :315-316.
    EXPECT_TRUE(ShouldLoadLightmapResource(true, true));
    EXPECT_FALSE(ShouldLoadLightmapResource(false, true));
    EXPECT_FALSE(ShouldLoadLightmapResource(true, false));
    EXPECT_TRUE(ShouldUnloadLightmapResource(0, true));
    EXPECT_FALSE(ShouldUnloadLightmapResource(1, true));
    EXPECT_FALSE(ShouldUnloadLightmapResource(0, false));
}

// ---------------------------------------------------------------------------
// Corpus: every shipped `.olm` in MISSIONS/location0/level*/lightmaps/lightmaps.res
// ---------------------------------------------------------------------------

struct CorpusStats {
    int archives = 0;
    int files = 0;
    int pages = 0;
    int accepted_against_own_time_plus_1ms = 0;
    int rejected_against_own_time = 0;
    int flags_01 = 0;
    int flags_10 = 0;
    int flags_00 = 0;
    int texels_below_pixels = 0;
    int texels_equal_pixels = 0;
    int multi_page = 0;
    int walk_errors = 0;
    int max_ref_index = -1;
    int max_object_number = -1;
    // The streaming plan (PlanOlmPages) applied to the same bytes the reader sees:
    // one span per page, the last ending exactly at the file's end.
    int planned_files = 0;
    int planned_pages = 0;
    int plan_mismatches = 0;
    int multi_page_distinct_pixels = 0;
    std::set<std::string> object_names;
    std::set<int> ref_indices;
    std::map<int, int> count_histogram;
    std::string worst_file;
};

// Walks the retail archives. Returns false when no archive was found at all, so
// the caller can self-skip; a walk error is reported and counted instead, because
// GTEST_SKIP after a recorded failure would hide it.
bool WalkCorpus(CorpusStats* stats) {
    const fs::path root = GameRoot() / "MISSIONS" / "location0";
    for (int level = 1; level <= 14; ++level) {
        const fs::path res =
            root / ("level" + std::to_string(level)) / "lightmaps" / "lightmaps.res";
        if (!fs::exists(res)) continue;
        stats->archives++;

        std::string error;
        bool walked = RES_ForEachEntry(
            res.string(),
            [&](const std::string& name, const std::uint8_t* data, std::size_t size) {
                const std::size_t slash = name.find_last_of("/\\");
                const std::string bare =
                    slash == std::string::npos ? name : name.substr(slash + 1);
                if (bare.size() < 4 || bare.compare(bare.size() - 4, 4, ".olm") != 0) return;

                // Rule 2: the shipped name must be exactly what the format string
                // produces for its own numbers (lightmapbuild.c:59 + lightmap.c:240).
                const OlmName parsed = ParseOlmFileName(bare);
                ASSERT_TRUE(parsed.ok) << name;
                ASSERT_EQ(bare, OlmFileName(parsed.object_name, parsed.index)) << name;
                // The object half must be `obj%05d` and nothing else, and it must
                // round-trip through the same formatter (lightmap.c:240, :255).
                ASSERT_EQ(parsed.object_name.compare(0, 3, "obj"), 0) << name;
                ASSERT_EQ(parsed.object_name.size(), 8u) << name;
                const int object_number = std::stoi(parsed.object_name.substr(3));
                ASSERT_EQ(parsed.object_name, ObjectLightmapName(object_number)) << name;
                ASSERT_EQ(bare, OlmFileName(ObjectLightmapName(object_number), parsed.index)) << name;
                // The engine's own path form, with the directory it builds from.
                const std::string engine_path =
                    OlmPath(LightmapDir(), parsed.object_name, parsed.index);
                ASSERT_EQ(engine_path.compare(engine_path.size() - bare.size(), bare.size(), bare), 0)
                    << engine_path;

                stats->files++;
                stats->object_names.insert(parsed.object_name);
                stats->ref_indices.insert(parsed.index);
                stats->max_ref_index = std::max(stats->max_ref_index, parsed.index);
                stats->max_object_number = std::max(stats->max_object_number, object_number);

                // Rule 3's data half: the header, the page records and the exact
                // file size (litmaptypes.h:36-48, lightmapbuild.c:72-78).
                OlmFileHeader header;
                std::vector<OlmPage> pages;
                ASSERT_TRUE(ReadOlmPages(data, size, &header, &pages)) << name;
                EXPECT_EQ(header.magic, kOlmMagic) << name;
                std::uint32_t version_bits = 0;
                std::memcpy(&version_bits, &header.version, 4);
                EXPECT_EQ(version_bits, kOlmVersionBits) << name;
                EXPECT_GE(header.count, 1) << name;
                EXPECT_EQ(static_cast<int>(pages.size()), header.count) << name;
                stats->pages += header.count;
                stats->count_histogram[header.count]++;
                if (header.count > 1) stats->multi_page++;

                // The accept test against a mesh one millisecond older than the
                // file must pass; against the file's own time it must fail, because
                // DateTime_Compare must be strictly < 0 (lightmapbuild.c:64).
                OlmAcceptInput mesh;
                mesh.mesh_version = header.version;
                mesh.mesh_time = header.time;
                mesh.mesh_tex_count = header.count;
                EXPECT_FALSE(OlmHeaderAccepted(header, mesh)) << name;
                stats->rejected_against_own_time++;
                mesh.mesh_time.ms = header.time.ms - 1;
                if (OlmHeaderAccepted(header, mesh)) stats->accepted_against_own_time_plus_1ms++;

                if (header.from_resource == 0 && header.built == 1) stats->flags_01++;
                if (header.from_resource == 1 && header.built == 0) stats->flags_10++;
                if (header.from_resource == 0 && header.built == 0) stats->flags_00++;

                // `texels` (0x30) is the LIT texel count (lightmapbuild.c:374), not
                // the pixel count: the pixel block is 4*w*h per page (:76-78).
                std::size_t pixel_bytes = 0;
                for (const OlmPage& page : pages)
                    pixel_bytes += static_cast<std::size_t>(page.width) * page.height * 4u;
                const std::size_t texels_bytes = static_cast<std::size_t>(header.texels) * 4u;
                if (texels_bytes < pixel_bytes) stats->texels_below_pixels++;
                if (texels_bytes == pixel_bytes) stats->texels_equal_pixels++;

                // The streaming plan the editor's loader uses: the header+records
                // prefix and the REAL file size, so the spans are exactly what
                // LoadOlmPages hands to the GL upload. Every shipped file must plan
                // to its own end, and page 1 must not be a copy of page 0 -- that is
                // the data the old page-0-only reader dropped.
                std::vector<OlmPageSpan> spans;
                if (!PlanOlmPages(data, OlmPixelOffset(header.count), size, &spans)) {
                    ADD_FAILURE() << "PlanOlmPages failed on " << name;
                    stats->plan_mismatches++;
                } else {
                    stats->planned_files++;
                    stats->planned_pages += static_cast<int>(spans.size());
                    if (spans.size() != pages.size()) {
                        ADD_FAILURE() << "plan/read page count differ on " << name;
                        stats->plan_mismatches++;
                    }
                    if (spans.back().offset + spans.back().bytes != size) {
                        ADD_FAILURE() << "plan does not end at the file end on " << name;
                        stats->plan_mismatches++;
                    }
                    if (spans.size() > 1) {
                        const std::uint8_t* page0 = data + spans[0].offset;
                        const std::uint8_t* page1 = data + spans[1].offset;
                        if (std::memcmp(page0, page1, std::min(spans[0].bytes, spans[1].bytes)) != 0)
                            stats->multi_page_distinct_pixels++;
                    }
                }
            },
            error);
        if (!walked) {
            ADD_FAILURE() << "RES_ForEachEntry failed on " << res.string() << ": " << error;
            stats->walk_errors++;
        }
    }
    return stats->archives > 0;
}

TEST(LightmapRulesCorpusTest, EveryShippedOlmMatchesTheEngineLayoutAndName) {
    CorpusStats stats;
    if (!WalkCorpus(&stats)) {
        GTEST_SKIP() << "set IGI_GAME_PATH to an IGI install to enable the corpus test";
    }

    // Printed so the numbers are in the log rather than only asserted.
    std::printf(
        "[lightmap-rules] archives=%d files=%d pages=%d multiPage=%d objects=%zu refIndices=%zu "
        "maxObj=%d maxRef=%d count1=%d count2=%d count3=%d flags01=%d flags10=%d flags00=%d "
        "texelsBelowPixels=%d texelsEqualPixels=%d plannedFiles=%d plannedPages=%d "
        "planMismatches=%d multiPageDistinctPixels=%d\n",
        stats.archives, stats.files, stats.pages, stats.multi_page, stats.object_names.size(),
        stats.ref_indices.size(), stats.max_object_number, stats.max_ref_index,
        stats.count_histogram[1], stats.count_histogram[2], stats.count_histogram[3],
        stats.flags_01, stats.flags_10, stats.flags_00, stats.texels_below_pixels,
        stats.texels_equal_pixels, stats.planned_files, stats.planned_pages,
        stats.plan_mismatches, stats.multi_page_distinct_pixels);

    EXPECT_EQ(stats.archives, 14);
    EXPECT_EQ(stats.walk_errors, 0);
    EXPECT_GT(stats.files, 20000) << "the retail corpus is 25,337 .olm entries";
    EXPECT_GT(stats.pages, stats.files) << "some files carry more than one page";
    EXPECT_GT(stats.multi_page, 0) << "the page-count rule is only interesting when count > 1";
    // Every single file: the four-conjunct test against its own timestamp fails
    // (strictly-later required) and passes at one millisecond older.
    EXPECT_EQ(stats.rejected_against_own_time, stats.files);
    EXPECT_EQ(stats.accepted_against_own_time_plus_1ms, stats.files);
    // `texels` is a lit count, never the pixel count, in every shipped file.
    EXPECT_EQ(stats.texels_below_pixels + stats.texels_equal_pixels, stats.files);
    // The two header flags take only the two values the builder's paths set
    // (lightmapbuild.c:70-71 vs :93-94); both-zero also occurs, so they must NOT be
    // used as an accept predicate. The oracle does not test them (:63-64).
    EXPECT_EQ(stats.flags_01 + stats.flags_10 + stats.flags_00, stats.files);
    EXPECT_GT(stats.flags_01, 0);
    EXPECT_GT(stats.flags_10, 0);
    // The `%05d` index field never overflows in retail data.
    EXPECT_LT(stats.max_ref_index, 100000);
    EXPECT_GE(stats.max_ref_index, 0);
    // The streaming plan the editor's loader runs: every shipped file plans to its
    // own end, and every multi-page file's page 1 is different data from page 0.
    EXPECT_EQ(stats.planned_files, stats.files);
    EXPECT_EQ(stats.planned_pages, stats.pages);
    EXPECT_EQ(stats.plan_mismatches, 0);
    // 12 of the 13 multi-page files carry a later page that is NOT a copy of page 0
    // (the exception is LEVEL7's `obj00019_00068.olm`, whose pages 0 and 1 are
    // byte-identical 256x256 blocks; its third page, 256x16, is distinct). So the
    // old page-0-only reader dropped real lightmap data, not padding.
    EXPECT_GT(stats.multi_page_distinct_pixels, 0);
    EXPECT_LE(stats.multi_page_distinct_pixels, stats.multi_page);
}

TEST(LightmapRulesCorpusTest, RefIndicesAreTheSetPositionsOfTheShippedBakes) {
    CorpusStats stats;
    if (!WalkCorpus(&stats)) {
        GTEST_SKIP() << "set IGI_GAME_PATH to an IGI install to enable the corpus test";
    }
    // 543 distinct ref indices and 121 distinct `obj%05d` names across the corpus;
    // a ref index is a position in a SectorObject's set (sector.c:544-561), so the
    // numeric field is NOT a per-object counter and must not be renumbered.
    EXPECT_GT(stats.ref_indices.size(), stats.object_names.size())
        << "more ref indices than objects: each object owns a contiguous run";
    EXPECT_GT(stats.object_names.size(), 0u);
    EXPECT_EQ(*stats.ref_indices.begin(), 0) << "some object's LOD 0 ships a bake";
}

// ---------------------------------------------------------------------------
// Corpus: the four-conjunct accept test against the MESH side of the real install
//
// The live path now refuses an `.olm` whose four conjuncts fail
// (lightmapbuild.c:63-64). The mesh side comes from the LOD's own `.mef`, so this
// test reads the shipped `.mef` bundles and pairs every shipped `.olm` with the
// mesh that baked it. The pairing is the one the oracle gives:
//
//   `Lightmap_BuildLod` copies `mesh->sizes[i]` into the page's `t->width`/
//   `t->height` (lightmapbuild.c:103-107), so a shipped `.olm`'s page records ARE
//   that mesh's `short[4]` page-size array -- the LTMP chunk payload for a format-3
//   mesh (litmaptypes.h:145, d3drender.c:229). MEASURED: in all 2,051 format-3
//   .mef files the LTMP chunk size is exactly `texCount * 8`, and the pairing is
//   by (texCount, sizes) within the level's own model bundle plus the shared
//   common bundle -- the two archives `Renderer_Objects` resolves a model's `.mef`
//   from.
//
// The assertion is that NO shipped file is rejected: every shipped `.olm` has at
// least one mesh in its bundle with that page signature, and at least one of them
// passes all four conjuncts. A mesh whose timestamp is NOT strictly earlier than
// the file's is a mesh the engine would rebuild rather than reuse, and the corpus
// does contain such pairs (they are other meshes with the same page signature, so
// the timestamp conjunct is not vacuous here) -- the test counts them and requires
// every file to have a passing mesh.
//
// The refs BEYOND the owner's own LODs cannot be paired this way: they belong to
// the object's magic parts and rigid children, whose order needs
// `SectorObject_LoadLightmaps` (sector.c:641-680). Those entries are exactly the
// ones the editor does not test (`igi::lightmap::RefMeshIsIdentified`), so this
// test's "no shipped file is un-acceptable" claim covers what the live path can
// reject and no more.
// ---------------------------------------------------------------------------

struct MeshSideIndex {
    struct Mesh {
        std::uint32_t version_bits = 0;
        DateTime time;
        int tex_count = 0;
        std::vector<std::pair<int, int>> sizes;
    };
    std::map<std::string, std::vector<Mesh>> by_signature;
    int meshes = 0;
    int format3 = 0;
    int version_ok = 0;
    int tex_count_matches_ltmp = 0;
    int tex_count_mismatch = 0;
    int format3_zero_tex_count = 0;
};

std::string PageSignature(int count, const std::vector<std::pair<int, int>>& sizes) {
    std::string key = std::to_string(count);
    for (const auto& s : sizes)
        key += ":" + std::to_string(s.first) + "x" + std::to_string(s.second);
    return key;
}

void AddMeshSide(const std::uint8_t* data, std::size_t size, MeshSideIndex* index) {
    std::vector<std::uint8_t> bytes(data, data + size);
    const igi::lod::MefMeshHeader header = igi::lod::ReadMefMeshHeader(bytes);
    if (!header.valid) return;
    index->meshes++;
    std::uint32_t version_bits = 0;
    std::memcpy(&version_bits, &header.version, 4);
    if (version_bits == kOlmVersionBits) index->version_ok++;
    if (!header.has_lightmap_mesh) return;
    index->format3++;

    std::vector<std::array<std::int16_t, 2>> ltmp;
    const bool have_sizes = igi::lod::ReadMefLightmapPageSizes(bytes, &ltmp);
    if (have_sizes) {
        if (ltmp.size() == static_cast<std::size_t>(header.lightmap_tex_count))
            index->tex_count_matches_ltmp++;
        else
            index->tex_count_mismatch++;
    } else if (header.lightmap_tex_count == 0) {
        // A format-3 mesh with texCount 0 and no LTMP chunk at all: the engine
        // allocates a zero-page `Lightmaps` for it (lightmapbuild.c:90-91), so
        // "no page records" is the consistent reading, not a mismatch.
        index->tex_count_matches_ltmp++;
        index->format3_zero_tex_count++;
    } else {
        index->tex_count_mismatch++;
    }

    MeshSideIndex::Mesh mesh;
    mesh.version_bits = version_bits;
    mesh.tex_count = header.lightmap_tex_count;
    mesh.time = DateTime{header.time[0], header.time[1], header.time[2], header.time[3],
                         header.time[4], header.time[5], header.time[6]};
    if (have_sizes) {
        for (const auto& s : ltmp) mesh.sizes.emplace_back(s[0], s[1]);
    }
    index->by_signature[PageSignature(mesh.tex_count, mesh.sizes)].push_back(std::move(mesh));
}

TEST(LightmapRulesCorpusTest, EveryShippedOlmIsAcceptedAgainstItsMeshSide) {
    const fs::path root = GameRoot() / "MISSIONS" / "location0";
    if (!fs::exists(root)) {
        GTEST_SKIP() << "set IGI_GAME_PATH to an IGI install to enable the corpus test";
    }

    // The shared bundle every level can resolve a model from
    // (renderer_objects.cpp: GetModelFilePath / FindMeshData, and the engine's
    // "LOCAL:models/%s.mef", src/model/model.c:364).
    MeshSideIndex common;
    {
        const fs::path res = root / "common" / "models" / "location0.res";
        if (fs::exists(res)) {
            std::string error;
            const bool walked = RES_ForEachEntry(
                res.string(),
                [&](const std::string& name, const std::uint8_t* data, std::size_t size) {
                    if (name.size() > 4 && name.compare(name.size() - 4, 4, ".mef") == 0)
                        AddMeshSide(data, size, &common);
                },
                error);
            if (!walked) ADD_FAILURE() << "RES_ForEachEntry failed on " << res << ": " << error;
        }
    }

    int levels = 0;
    int files = 0;
    int files_with_candidate = 0;
    int files_accepted = 0;
    int candidate_pairs = 0;
    int pairs_time_fail = 0;
    int pairs_version_fail = 0;
    int pairs_count_fail = 0;
    int meshes = 0;
    int format3 = 0;
    int version_ok = 0;
    int tex_count_matches_ltmp = 0;
    int tex_count_mismatch = 0;
    int format3_zero_tex_count = 0;
    std::string worst;

    for (int level = 1; level <= 14; ++level) {
        const fs::path dir = root / ("level" + std::to_string(level));
        const fs::path models = dir / "models" / ("level" + std::to_string(level) + ".res");
        const fs::path archive = dir / "lightmaps" / "lightmaps.res";
        if (!fs::exists(models) || !fs::exists(archive)) continue;
        levels++;

        MeshSideIndex index;
        std::string error;
        bool walked = RES_ForEachEntry(
            models.string(),
            [&](const std::string& name, const std::uint8_t* data, std::size_t size) {
                if (name.size() > 4 && name.compare(name.size() - 4, 4, ".mef") == 0)
                    AddMeshSide(data, size, &index);
            },
            error);
        if (!walked) {
            ADD_FAILURE() << "RES_ForEachEntry failed on " << models << ": " << error;
            continue;
        }
        meshes += index.meshes;
        format3 += index.format3;
        version_ok += index.version_ok;
        tex_count_matches_ltmp += index.tex_count_matches_ltmp;
        tex_count_mismatch += index.tex_count_mismatch;
        format3_zero_tex_count += index.format3_zero_tex_count;

        walked = RES_ForEachEntry(
            archive.string(),
            [&](const std::string& name, const std::uint8_t* data, std::size_t size) {
                const std::size_t slash = name.find_last_of("/\\");
                const std::string bare =
                    slash == std::string::npos ? name : name.substr(slash + 1);
                if (bare.size() < 4 || bare.compare(bare.size() - 4, 4, ".olm") != 0) return;

                OlmFileHeader header;
                std::vector<OlmPage> pages;
                if (!ReadOlmPages(data, size, &header, &pages)) {
                    ADD_FAILURE() << "unreadable .olm " << name;
                    return;
                }
                files++;
                std::uint32_t file_version_bits = 0;
                std::memcpy(&file_version_bits, &header.version, 4);
                std::vector<std::pair<int, int>> dims;
                for (const OlmPage& p : pages) dims.emplace_back(p.width, p.height);
                const std::string key = PageSignature(header.count, dims);

                int candidates = 0, accepted = 0;
                for (const MeshSideIndex* table : {&index, &common}) {
                    const auto it = table->by_signature.find(key);
                    if (it == table->by_signature.end()) continue;
                    for (const MeshSideIndex::Mesh& mesh : it->second) {
                        candidates++;
                        candidate_pairs++;
                        float mesh_version = 0.0f;
                        std::memcpy(&mesh_version, &mesh.version_bits, 4);
                        int mesh_time[7] = {mesh.time.year, mesh.time.month, mesh.time.day,
                                            mesh.time.hour, mesh.time.minute, mesh.time.second,
                                            mesh.time.ms};
                        const OlmAcceptInput input = MakeOlmAcceptInput(
                            mesh_version, mesh_time, mesh.tex_count, /*full=*/false);
                        if (OlmHeaderAccepted(header, input)) {
                            accepted++;
                            continue;
                        }
                        if (mesh.version_bits != file_version_bits) pairs_version_fail++;
                        if (mesh.tex_count != header.count) pairs_count_fail++;
                        if (DateTimeCompare(mesh.time, header.time) >= 0) pairs_time_fail++;
                    }
                }
                if (candidates > 0) files_with_candidate++;
                if (accepted > 0) {
                    files_accepted++;
                } else if (worst.empty()) {
                    worst = name;
                }
            },
            error);
        if (!walked) ADD_FAILURE() << "RES_ForEachEntry failed on " << archive << ": " << error;
    }

    if (levels == 0) {
        GTEST_SKIP() << "no retail level bundles found under " << root;
    }

    std::printf("[lightmap-accept] levels=%d meshes=%d format3=%d versionOk=%d texCountMatchesLtmp=%d "
                "zeroTexCount=%d files=%d withCandidate=%d accepted=%d pairs=%d pairsTimeFail=%d "
                "pairsVersionFail=%d pairsCountFail=%d\n",
                levels, meshes, format3, version_ok, tex_count_matches_ltmp,
                format3_zero_tex_count, files, files_with_candidate, files_accepted, candidate_pairs,
                pairs_time_fail, pairs_version_fail, pairs_count_fail);
    if (!worst.empty()) std::printf("[lightmap-accept] first rejected file: %s\n", worst.c_str());

    EXPECT_EQ(levels, 14);
    EXPECT_GT(meshes, 2000) << "the retail model bundles hold thousands of .mef files";
    EXPECT_GT(format3, 2000) << "most retail meshes are lightmapped";
    // Every shipped MESH header carries the version the accept test compares
    // against (`feq(mh->version, file->version)`, lightmapbuild.c:63): 0.1f. The
    // engine refuses to even load a mesh whose version is not exactly 0.1f
    // (Mesh_Load, HOOK 0x4CF390, src/model/model.c:367-373), so this cannot differ.
    EXPECT_EQ(version_ok, meshes);
    // `texCount` is RD3D + 0x04 and the LTMP payload is `texCount` records of
    // short[4] -- the layout the mesh side and the page sizes both come from.
    EXPECT_EQ(tex_count_mismatch, 0);
    EXPECT_EQ(tex_count_matches_ltmp, format3);
    EXPECT_GT(files, 20000) << "the retail corpus is 25,337 .olm entries";
    // The claim the live path depends on: the engine's four-conjunct test rejects
    // NO shipped file.
    EXPECT_EQ(files_with_candidate, files) << "every shipped .olm has a mesh with its page signature";
    EXPECT_EQ(files_accepted, files) << "no shipped .olm may be rejected by the accept test";
    // ...and the timestamp conjunct is not vacuous: the corpus does contain
    // same-signature meshes that are NOT older than a bake, which the engine would
    // rebuild rather than reuse (:64, :86).
    EXPECT_GT(pairs_time_fail, 0);
    EXPECT_EQ(pairs_version_fail, 0) << "version cannot fail on retail data (all 0x3DCCCCCD)";
    EXPECT_EQ(pairs_count_fail, 0) << "the pairing is by page count, so it cannot differ";
}

}  // namespace
