/******************************************************************************
 * @file    test_character_texture_resolution.cpp
 * @brief   Character-model texture resolution order (issue #47 follow-on).
 *
 * IGI-RE is the oracle for the archive a texture must come from. The game
 * resolves a model's textures from exactly one archive, the one of the level the
 * model file belongs to:
 *
 *   src/model/model.c:233-235  ModelFile_Load: sprintf("%s%s.mtp", gModelDir, name)
 *   src/model/model.c:251,261  ModelFile_Setup, HOOK(0x4CF080):
 *                              sprintf("%stextures/%s.res", gModelDir, name);
 *                              Resource_Load(path, 0); ModelFile_LoadTextures(f);
 *   src/model/model.c:450-461  ModelFile_LoadTextures: MTP_TEXF -> f->textures
 *   src/model/model.c:300-306  tex = f->textures + t * 0x24      (t < 0x2000)
 *                              tex = first->textures + (t - 0x2000) * 0x24
 *
 * There is no cross-level search and no name-based fallback: the level's own
 * archive is authoritative. That matters because retail archives really do carry
 * different images under one texture id (the measured table is in
 * docs/igi-re-parity/CHARACTER_TEXTURE_RESOLUTION.md), so whichever archive the
 * editor happened to index first used to decide the look.
 *
 * The suite is host-native and needs no game install for the synthetic cases.
 * The retail cases run only when a root is given through IGI_TEX_ROOT (or
 * IGI_MEF_CORPUS) and skip otherwise, so the suite stays portable.
 *****************************************************************************/
#include "../source/renderer/model_texture_resolution.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

// In-memory stand-in for a .res archive: RES_BuildIndex/RES_WriteEntries live in
// non-portable translation units, so the synthetic cases index entries directly.
std::map<std::string, std::vector<uint8_t>>& FakeEntries() {
    static std::map<std::string, std::vector<uint8_t>> entries;
    return entries;
}

std::string FakeKey(const std::string& resPath, size_t offset) {
    return resPath + "#" + std::to_string(offset);
}

ModelSourceArchiveIndex MakeArchive(
    const std::string& resPath,
    const std::vector<std::pair<std::string, std::vector<uint8_t>>>& entries) {
    ModelSourceArchiveIndex archive;
    archive.resPath = resPath;
    size_t offset = 0;
    for (const auto& entry : entries) {
        // The reader's index is keyed by the engine's own resource identity: the full
        // "LOCAL:textures/<id>.tex" member name (ModelFile_LoadTextures,
        // src/model/model.c:467, HOOK 0x4CF790), upper-cased and backslash-folded
        // (String_ToUpper 0x4B5AF0, src/res/resource.c:98-104, compared by
        // Resource_IsLoaded at :136-138).  These fixtures used to key by the bare leaf,
        // which no engine lookup ever produces.
        archive.entries[igi_res::NormalizeResourceName("LOCAL:textures/" + entry.first)] =
            ResEntryInfo{offset, entry.second.size()};
        FakeEntries()[FakeKey(resPath, offset)] = entry.second;
        offset += entry.second.size() + 1;
    }
    return archive;
}

std::vector<uint8_t> ReadFake(const std::string& resPath, const ResEntryInfo& info) {
    const auto found = FakeEntries().find(FakeKey(resPath, info.data_offset));
    return found == FakeEntries().end() ? std::vector<uint8_t>{} : found->second;
}

std::vector<uint8_t> ReadFromDisk(const std::string& resPath, const ResEntryInfo& info) {
    return RES_ReadEntry(resPath, info);
}

// ".tex" files are a 32-byte header then pixel data. Reading the dimensions back
// proves two same-named copies really are different images rather than the same
// image stored twice.
struct TexHeader {
    bool valid = false;
    int width = 0;
    int height = 0;
};

TexHeader ReadTexHeader(const std::vector<uint8_t>& bytes) {
    TexHeader header;
    if (bytes.size() < 32) return header;
    if (std::string(bytes.begin(), bytes.begin() + 4) != "LOOP") return header;
    header.width = bytes[22] | (bytes[23] << 8);
    header.height = bytes[24] | (bytes[25] << 8);
    // Self-validate the offsets against the 16-bit pixel payload.
    header.valid = header.width > 0 && header.height > 0 &&
        bytes.size() == 32u + static_cast<size_t>(header.width) * header.height * 2u;
    return header;
}

const char* InstallRoot() {
    // IGI_GAME_PATH is what scripts/run-host-tests.sh exports for the detected
    // install, so the retail cases run in the standard gate without extra setup.
    for (const char* name : {"IGI_TEX_ROOT", "IGI_GAME_PATH", "IGI_MEF_CORPUS"}) {
        const char* root = std::getenv(name);
        if (root != nullptr && *root != '\0') return root;
    }
    return nullptr;
}

const char* const kCommonRes = "/igi/missions/location0/common/textures/location0.res";
const char* const kLevel1Res = "/igi/missions/location0/level1/textures/level1.res";
const char* const kLevel2Res = "/igi/missions/location0/level2/textures/level2.res";
const char* const kLevel7Res = "/igi/missions/location0/level7/textures/level7.res";

}  // namespace

// The archive that owns a texture must be searched before any archive the
// session indexed earlier, including the shared common archive. Before the fix
// the editor searched the session order as-is and bound the common copy.
TEST(CharacterTextureResolution, OwnerLevelArchiveBeatsEarlierSessionArchives) {
    const std::vector<uint8_t> commonBytes{0, 0, 0, 0, 1, 2, 3, 4};
    const std::vector<uint8_t> ownerBytes{0, 0, 0, 0, 9, 9, 9, 9};

    const auto level2 = MakeArchive(kLevel2Res, {{"unrelated.tex", {7, 7, 7}}});
    const auto common = MakeArchive(kCommonRes, {{"001_02_1.tex", commonBytes}});
    const auto level7 = MakeArchive(kLevel7Res, {{"001_02_1.tex", ownerBytes}});

    // What LoadResCache(2) leaves indexed for a level-2 session.
    std::vector<ModelSourceArchiveIndex> session{level2, common};

    // The defect, pinned: a level-2 session alone resolves 001_02_1 to the
    // shared common archive's unrelated image.
    EXPECT_EQ(FindPreviewTextureEntry(session, "001_02_1", ReadFake), commonBytes);

    // The fix: the owning level's archive is hoisted in front of the session
    // order, so the level-7 model keeps its level-7 image.
    session.push_back(level7);
    const auto ordered = OrderTextureArchivesByOwner(session, kLevel7Res);
    ASSERT_EQ(ordered.size(), session.size());
    EXPECT_EQ(ordered.front().resPath, kLevel7Res);
    EXPECT_EQ(FindPreviewTextureEntry(ordered, "001_02_1", ReadFake), ownerBytes);
}

// A "_argb8888" id with no exact entry must fall back to the stripped name in the
// owning archive, not to an unrelated archive that happens to hold that name.
TEST(CharacterTextureResolution, FormatSuffixedIdFallsBackInsideOwnerArchive) {
    const std::vector<uint8_t> ownerBase{1, 1, 1, 1};
    const std::vector<uint8_t> otherBase{2, 2, 2, 2};
    const std::vector<uint8_t> ownerExact{3, 3, 3, 3};

    const auto level1 = MakeArchive(kLevel1Res,
        {{"009_09_1.tex", otherBase}, {"004_13_1.tex", otherBase}});
    const auto common = MakeArchive(kCommonRes, {{"unrelated.tex", {0}}});
    const auto level7 = MakeArchive(kLevel7Res,
        {{"009_09_1.tex", ownerBase},
         {"009_09_1_argb8888.tex", ownerExact},
         {"004_13_1.tex", ownerBase}});

    const std::vector<ModelSourceArchiveIndex> archives{level1, common, level7};

    // The exact suffixed entry wins while it exists, even though the owning
    // archive also holds the base name with different bytes.
    EXPECT_EQ(FindPreviewTextureEntry(OrderTextureArchivesByOwner(archives, kLevel7Res),
                                      "009_09_1_argb8888", ReadFake),
              ownerExact);

    // No archive defines the exact suffixed id, so the stripped name is used. It
    // must come from the owning archive, not from the unrelated level-1 image
    // that a plain session-order search reaches first.
    EXPECT_EQ(FindPreviewTextureEntry(archives, "004_13_1_argb8888", ReadFake), otherBase);
    EXPECT_EQ(FindPreviewTextureEntry(OrderTextureArchivesByOwner(archives, kLevel7Res),
                                      "004_13_1_argb8888", ReadFake),
              ownerBase);
}

// Hoisting an owner that is not indexed must not reorder or drop anything.
TEST(CharacterTextureResolution, UnknownOwnerArchiveLeavesSearchOrderUntouched) {
    const auto level2 = MakeArchive(kLevel2Res, {{"a.tex", {1}}});
    const auto common = MakeArchive(kCommonRes, {{"b.tex", {2}}});
    const std::vector<ModelSourceArchiveIndex> session{level2, common};

    const auto unchanged = OrderTextureArchivesByOwner(session, "");
    ASSERT_EQ(unchanged.size(), 2u);
    EXPECT_EQ(unchanged[0].resPath, kLevel2Res);
    EXPECT_EQ(unchanged[1].resPath, kCommonRes);

    const auto missing = OrderTextureArchivesByOwner(session, kLevel7Res);
    ASSERT_EQ(missing.size(), 2u);
    EXPECT_EQ(missing[0].resPath, kLevel2Res);
    EXPECT_EQ(missing[1].resPath, kCommonRes);

    // An indexed owner moves to the front exactly once.
    auto withOwner = session;
    withOwner.push_back(MakeArchive(kLevel7Res, {{"c.tex", {3}}}));
    const auto ordered = OrderTextureArchivesByOwner(withOwner, kLevel7Res);
    ASSERT_EQ(ordered.size(), 3u);
    EXPECT_EQ(ordered[0].resPath, kLevel7Res);
    EXPECT_EQ(ordered[1].resPath, kLevel2Res);
    EXPECT_EQ(ordered[2].resPath, kCommonRes);
}

// Retail evidence: the same id is a different image in different archives, and
// the owning level's copy is the one the game binds.
TEST(CharacterTextureResolution, RetailArchivesCarryDifferentImagesUnderOneId) {
    const char* root = InstallRoot();
    if (root == nullptr) {
        GTEST_SKIP() << "set IGI_TEX_ROOT to an IGI install to run the retail cases";
    }
    const std::string igi = root;
    const std::string commonRes = igi + "/missions/location0/common/textures/location0.res";
    const std::string level2Res = igi + "/missions/location0/level2/textures/level2.res";
    const std::string level7Res = igi + "/missions/location0/level7/textures/level7.res";

    // Skip rather than fail when the root is not a usable install.
    if (!std::filesystem::exists(commonRes) || !std::filesystem::exists(level2Res) ||
        !std::filesystem::exists(level7Res)) {
        GTEST_SKIP() << "no retail texture archives under " << igi;
    }

    std::vector<ModelSourceArchiveIndex> all(3);
    all[0].resPath = level2Res;
    all[1].resPath = commonRes;
    all[2].resPath = level7Res;
    for (auto& archive : all) {
        std::string error;
        ASSERT_TRUE(RES_BuildIndex(archive.resPath, archive.entries, error))
            << archive.resPath << ": " << error;
    }
    const auto& level2 = all[0];
    const auto& common = all[1];
    const auto& level7 = all[2];

    // 001_02_1 lives in levels 7 and 8; a level-2 session has neither indexed.
    const std::vector<ModelSourceArchiveIndex> session{level2, common};
    const auto commonCopy = FindPreviewTextureEntry(session, "001_02_1", ReadFromDisk);
    ASSERT_FALSE(commonCopy.empty());
    const TexHeader commonHeader = ReadTexHeader(commonCopy);

    const auto ownerCopy = FindPreviewTextureEntry(
        OrderTextureArchivesByOwner(all, level7Res), "001_02_1", ReadFromDisk);
    ASSERT_FALSE(ownerCopy.empty());
    const TexHeader ownerHeader = ReadTexHeader(ownerCopy);

    ASSERT_TRUE(commonHeader.valid) << "unexpected .tex header in " << commonRes;
    ASSERT_TRUE(ownerHeader.valid) << "unexpected .tex header in " << level7Res;
    // Measured on the retail install: the common copy is a 128x32 strip, the
    // level-7 copy the 128x128 sheet the character is UV-mapped for.
    EXPECT_EQ(commonHeader.width, 128);
    EXPECT_EQ(commonHeader.height, 32);
    EXPECT_EQ(ownerHeader.width, 128);
    EXPECT_EQ(ownerHeader.height, 128);
    EXPECT_NE(commonCopy, ownerCopy);

    // The level-7 model's own texture table, in MTP order, from LEVEL7.DAT.
    const std::vector<std::string> ids{
        "018_03_1", "007_06_1", "001_06_1", "001_07_1", "001_02_1", "015_13_1", "001_05_1",
        "004_13_1", "000_09_1", "000_10_1", "001_04_1", "001_01_1", "001_03_1"};

    std::size_t ownedByLevel7 = 0;
    std::size_t resolvedInLevel7 = 0;
    const auto ordered = OrderTextureArchivesByOwner(all, level7Res);
    for (const auto& id : ids) {
        const auto bytes = FindPreviewTextureEntry(ordered, id, ReadFromDisk);
        ASSERT_FALSE(bytes.empty()) << id;
        const TexHeader header = ReadTexHeader(bytes);
        std::printf("[tex] 001_02_1 -> %-10s bytes=%-6zu %dx%d\n", id.c_str(), bytes.size(),
                    header.valid ? header.width : -1, header.valid ? header.height : -1);
        // The archive index is keyed by the engine's own member identity, not by the
        // bare leaf (RES_BuildIndex / ModelFile_LoadTextures,
        // src/model/model.c:467, HOOK 0x4CF790), so the counter below is only
        // meaningful with the "LOCAL:textures/<id>.tex" key.  With the bare
        // "<id>.tex" it matched nothing and the assertion was vacuous (0 == 0).
        const auto inLevel7 = level7.entries.find(
            igi_res::NormalizeResourceName("LOCAL:textures/" + id + ".tex"));
        if (inLevel7 == level7.entries.end()) continue;
        ++ownedByLevel7;
        if (RES_ReadEntry(level7Res, inLevel7->second) == bytes) ++resolvedInLevel7;
    }
    std::printf("[tex] 001_02_1: %zu of 13 ids defined by level7, %zu resolved there\n",
                ownedByLevel7, resolvedInLevel7);
    EXPECT_EQ(resolvedInLevel7, ownedByLevel7);
}

// The archive a model's bytes were read from is the archive its textures must be
// searched in. One gModelDir supplies both in the engine: Model_SetDir copies the
// level's directory into gModelDir before that level's models are loaded
// (src/game/game.c:411-418; HOOK(0x4CFBD0) src/lod/modelfile.c:153-157), and
// ModelFile_Setup reads "models/<name>.res" and "textures/<name>.res" from it
// (src/model/model.c:267, :261, HOOK 0x4CF080). The texture id's own DAT owner is
// only the fallback for a mesh read from the shared/common archive.
TEST(CharacterTextureResolution, TheModelsOwnArchiveDecidesTheTextureArchive) {
    EXPECT_EQ(TextureOwnerLevelForModel(7, 1), 7);
    EXPECT_EQ(TextureOwnerLevelForModel(4, 1), 4);
    EXPECT_EQ(TextureOwnerLevelForModel(12, 7), 12);
    // No level archive (shared common, or a loose .mef): the editor's texture-id
    // owner stands, and 0 when even that is unknown.
    EXPECT_EQ(TextureOwnerLevelForModel(0, 1), 1);
    EXPECT_EQ(TextureOwnerLevelForModel(0, 0), 0);
}

TEST(CharacterTextureResolution, ModelArchiveOwnerLevelReadsTheArchivePath) {
    EXPECT_EQ(ModelArchiveOwnerLevel(
                  "Z:\\IGI\\missions\\location0\\level7\\models\\level7.res"), 7);
    EXPECT_EQ(ModelArchiveOwnerLevel(
                  "/igi/missions/location0/LEVEL12/models/LEVEL12.RES"), 12);
    EXPECT_EQ(ModelArchiveOwnerLevel(
                  "Z:\\IGI\\missions\\location0\\common\\models\\location0.res"), 0);
    EXPECT_EQ(ModelArchiveOwnerLevel(
                  "Z:\\IGI\\missions\\location0\\level7\\textures\\level7.res"), 0);
    EXPECT_EQ(ModelArchiveOwnerLevel(""), 0);
}

// Retail evidence for the rule. `020_01_1` is read from level4's models archive, and
// one of the ids its own record lists (018_03_1) is defined by BOTH level4's and
// level7's texture archives with different bytes (4128 B and 65568 B; measured, and
// the same id/archives table as docs/igi-re-parity/CHARACTER_TEXTURE_RESOLUTION.md
// section 2). The old texture-id-owner rule hoisted a level listed first by the DATs
// (level1, whose copy is byte-identical to level7's); the engine's rule hoists the
// archive the model came from, so level4's copy is the one that is bound.
TEST(CharacterTextureResolution, RetailModelArchiveBeatsAnotherArchivesCopy) {
    const char* root = InstallRoot();
    if (root == nullptr) {
        GTEST_SKIP() << "set IGI_TEX_ROOT to an IGI install to run the retail cases";
    }
    const std::string igi = root;
    const std::string level4Models = igi + "/missions/location0/level4/models/level4.res";
    const std::string level4Res = igi + "/missions/location0/level4/textures/level4.res";
    const std::string level7Res = igi + "/missions/location0/level7/textures/level7.res";
    if (!std::filesystem::exists(level4Models) || !std::filesystem::exists(level4Res) ||
        !std::filesystem::exists(level7Res)) {
        GTEST_SKIP() << "no retail level4/level7 archives under " << igi;
    }

    // The model really is in that archive, and the archive path names the level.
    ModelSourceArchiveIndex models;
    models.resPath = level4Models;
    std::string error;
    ASSERT_TRUE(RES_BuildIndex(level4Models, models.entries, error)) << error;
    const std::string member =
        igi_res::NormalizeResourceName(igi_res::ModelMemberName("020_01_1"));
    EXPECT_NE(models.entries.find(member), models.entries.end());
    const int meshLevel = ModelArchiveOwnerLevel(level4Models);
    EXPECT_EQ(meshLevel, 4);
    // The DAT owner of this id is level1; the rule must still choose the model's
    // own archive.
    EXPECT_EQ(TextureOwnerLevelForModel(meshLevel, /*textureIdOwnerLevel=*/1), 4);

    // And the two archives really do hold different images under the one id.
    std::vector<ModelSourceArchiveIndex> all(2);
    all[0].resPath = level4Res;
    all[1].resPath = level7Res;
    for (auto& archive : all) {
        ASSERT_TRUE(RES_BuildIndex(archive.resPath, archive.entries, error))
            << archive.resPath << ": " << error;
    }
    const std::string key =
        igi_res::NormalizeResourceName("LOCAL:textures/018_03_1.tex");
    const auto level4Entry = all[0].entries.find(key);
    const auto level7Entry = all[1].entries.find(key);
    ASSERT_NE(level4Entry, all[0].entries.end());
    ASSERT_NE(level7Entry, all[1].entries.end());
    const std::vector<uint8_t> level4Bytes = RES_ReadEntry(level4Res, level4Entry->second);
    const std::vector<uint8_t> level7Bytes = RES_ReadEntry(level7Res, level7Entry->second);
    ASSERT_FALSE(level4Bytes.empty());
    ASSERT_FALSE(level7Bytes.empty());
    EXPECT_NE(level4Bytes, level7Bytes);

    // The model's own archive wins; another level's copy is what the other order
    // binds, which is the defect.
    EXPECT_EQ(FindPreviewTextureEntry(OrderTextureArchivesByOwner(all, level4Res),
                                      "018_03_1", ReadFromDisk),
              level4Bytes);
    EXPECT_EQ(FindPreviewTextureEntry(OrderTextureArchivesByOwner(all, level7Res),
                                      "018_03_1", ReadFromDisk),
              level7Bytes);
}
