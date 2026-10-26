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

#include <gtest/gtest.h>

#include <algorithm>
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
        "texelsBelowPixels=%d texelsEqualPixels=%d\n",
        stats.archives, stats.files, stats.pages, stats.multi_page, stats.object_names.size(),
        stats.ref_indices.size(), stats.max_object_number, stats.max_ref_index,
        stats.count_histogram[1], stats.count_histogram[2], stats.count_histogram[3],
        stats.flags_01, stats.flags_10, stats.flags_00, stats.texels_below_pixels,
        stats.texels_equal_pixels);

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

}  // namespace
