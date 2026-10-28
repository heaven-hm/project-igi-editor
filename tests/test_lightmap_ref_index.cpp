#include <gtest/gtest.h>

#include "renderer/lightmap_ref_index.h"
#include "renderer/mef_native.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

// Unit tests for the engine's lightmap ref-index rule.
// Oracle: src/sector/sector.c:546-562 (fill_refs) and
//         src/dirlight/lightmapbuild.c:44-59 (Lightmap_BuildLod) in IGI-RE.
// See docs/igi-re-parity/LIGHTMAP_REFS.md.

namespace {

using igi::lightmap::OlmFileName;
using igi::lightmap::OlmFileNameForFirstLod;
using igi::lightmap::RefIndexForLod;
using igi::lightmap::RefLayout;
using igi::lightmap::RefMeshIsIdentified;
using igi::lightmap::ReserveRefs;

// --------------------------------------------------------------------------
// Rule 1 + 3: one ref per LOD, and an object with no LODs contributes nothing
// (sector.c:548 `for (i = 0; i < lod_count; i++)`, :556 `(*n)++`)
// --------------------------------------------------------------------------

TEST(LightmapRefIndexTest, EachObjectContributesOneRefPerLod) {
    int cursor = 0;
    const RefLayout a = ReserveRefs(3, cursor);
    const RefLayout b = ReserveRefs(1, cursor);
    const RefLayout c = ReserveRefs(5, cursor);

    EXPECT_EQ(a.base, 0);
    EXPECT_EQ(a.lod_count, 3);
    EXPECT_EQ(b.base, 3);
    EXPECT_EQ(b.lod_count, 1);
    EXPECT_EQ(c.base, 4);
    EXPECT_EQ(c.lod_count, 5);
    EXPECT_EQ(cursor, 9);
}

TEST(LightmapRefIndexTest, AnObjectWithNoLodsDoesNotAdvanceTheCursor) {
    // The failure this guards: a model whose name does not resolve has
    // lod_count == 0 (VirModel_InitLods, src/model/model.c:145-163). Reserving a
    // slot for it would shift every later object's lightmap by one.
    int cursor = 0;
    const RefLayout a = ReserveRefs(2, cursor);
    const RefLayout none = ReserveRefs(0, cursor);
    const RefLayout b = ReserveRefs(2, cursor);

    EXPECT_EQ(a.base, 0);
    EXPECT_EQ(none.base, -1) << "an object with no LODs has no first ref";
    EXPECT_EQ(none.lod_count, 0);
    EXPECT_EQ(b.base, 2) << "the cursor must not have moved";
    EXPECT_EQ(cursor, 4);
}

TEST(LightmapRefIndexTest, ANegativeLodCountIsTreatedAsNoLods) {
    // The engine's loop bound is a signed int comparison; a negative count runs
    // zero iterations, same as zero.
    int cursor = 7;
    const RefLayout none = ReserveRefs(-1, cursor);
    EXPECT_EQ(none.base, -1);
    EXPECT_EQ(none.lod_count, 0);
    EXPECT_EQ(cursor, 7);
}

// --------------------------------------------------------------------------
// Rule 2: the ref index for a LOD is base + lodIndex, and only 0..lod_count-1
// exist (sector.c:552-555)
// --------------------------------------------------------------------------

TEST(LightmapRefIndexTest, RefIndexIsBasePlusLodIndexWithinRange) {
    int cursor = 0;
    ReserveRefs(2, cursor);  // an earlier object, base 0
    const RefLayout layout = ReserveRefs(3, cursor);

    EXPECT_EQ(RefIndexForLod(layout, 0), 2);
    EXPECT_EQ(RefIndexForLod(layout, 1), 3);
    EXPECT_EQ(RefIndexForLod(layout, 2), 4);
    EXPECT_EQ(RefIndexForLod(layout, 3), -1) << "lodIndex 3 does not exist";
    EXPECT_EQ(RefIndexForLod(layout, -1), -1);
}

TEST(LightmapRefIndexTest, RefIndexIsMinusOneWhenTheObjectHasNoLods) {
    int cursor = 0;
    const RefLayout none = ReserveRefs(0, cursor);
    EXPECT_EQ(RefIndexForLod(none, 0), -1);
}

TEST(LightmapRefIndexTest, HigherLodsOfOneObjectGetDistinctRefIndexes) {
    // This is what a page-0-only lookup cannot express: the same object's LOD 0
    // and LOD 1 live in different `.olm` files, because the file name carries the
    // ref index (lightmapbuild.c:59).
    int cursor = 0;
    const RefLayout layout = ReserveRefs(4, cursor);

    const std::string lod0 = OlmFileName("435_01_1", RefIndexForLod(layout, 0));
    const std::string lod1 = OlmFileName("435_01_1", RefIndexForLod(layout, 1));
    EXPECT_EQ(lod0, "435_01_1_00000.olm");
    EXPECT_EQ(lod1, "435_01_1_00001.olm");
    EXPECT_NE(lod0, lod1);
}

// --------------------------------------------------------------------------
// The file name (lightmapbuild.c:59 `"%s/%s_%05d.olm"`)
// --------------------------------------------------------------------------

TEST(LightmapRefIndexTest, OlmNameIsPercentZeroFiveD) {
    EXPECT_EQ(OlmFileName("watertower", 0), "watertower_00000.olm");
    EXPECT_EQ(OlmFileName("watertower", 7), "watertower_00007.olm");
    EXPECT_EQ(OlmFileName("watertower", 12345), "watertower_12345.olm");
    // A six-digit index is not truncated: %05d is a minimum width.
    EXPECT_EQ(OlmFileName("watertower", 123456), "watertower_123456.olm");
    EXPECT_TRUE(OlmFileName("watertower", -1).empty());
}

TEST(LightmapRefIndexTest, FirstLodNameHelperMatchesTheExplicitForm) {
    // One earlier object with a single LOD occupies ref 0, so this object's refs
    // start at 1 and its first LOD is ref 1 -- not 3, which is its third LOD.
    int cursor = 0;
    ReserveRefs(1, cursor);
    const RefLayout layout = ReserveRefs(3, cursor);
    EXPECT_EQ(layout.base, 1);
    EXPECT_EQ(OlmFileNameForFirstLod("x", layout), OlmFileName("x", 1));
    EXPECT_EQ(OlmFileNameForFirstLod("x", layout), "x_00001.olm");
    // ...and its last LOD is ref 3, a different file.
    EXPECT_EQ(OlmFileName("x", RefIndexForLod(layout, 2)), "x_00003.olm");
}

// --------------------------------------------------------------------------
// Rule 5: the page INSIDE the file is the drawn group's own `LmGroup.lightmap`
// (src/render/render.c:1033 `set->tex[g->lightmap]`), with 0xFFFF meaning "no
// page" (render.c:1098, :1114). The editor stores it on SubMesh.lightmapPage as
// -1 when there is none, so PageIndexForGroup must accept both spellings.
// --------------------------------------------------------------------------

TEST(LightmapRefIndexTest, GroupPageIndexIsTheRawGroupField) {
    EXPECT_EQ(igi::lightmap::PageIndexForGroup(0), 0);
    EXPECT_EQ(igi::lightmap::PageIndexForGroup(1), 1);
    EXPECT_EQ(igi::lightmap::PageIndexForGroup(2), 2);
    EXPECT_EQ(igi::lightmap::PageIndexForGroup(17), 17);
}

TEST(LightmapRefIndexTest, GroupPageIndexMapsBothNoPageSpellingsToMinusOne) {
    // The engine's own sentinel (render.c:1098/:1114) and the editor's normalised
    // form (SubMesh.lightmapPage, source/renderer/model.h).
    EXPECT_EQ(igi::lightmap::kNoLightmapPage, 0xFFFF);
    EXPECT_EQ(igi::lightmap::PageIndexForGroup(igi::lightmap::kNoLightmapPage), -1);
    // A non-format-3 render block has no lightmap field at all; mef_native stores
    // -1 for those (ParsedGeometry::RenderBlock::lightmapSlot).
    EXPECT_EQ(igi::lightmap::PageIndexForGroup(-1), -1);
}

// --------------------------------------------------------------------------
// Which entries of a resolved `.olm` list the engine's accept test can be run on
// --------------------------------------------------------------------------

TEST(LightmapRefIndexTest, OnlyTheOwnersOwnLodsHaveAnIdentifiableMesh) {
    // fill_refs (src/sector/sector.c:544-563) gives the set's owner refs
    // 0..lod_count-1, one per LOD, and every later ref belongs to the object's
    // magic parts or rigid children, whose order the editor does not walk. So only
    // `entry < owner_lod_count` names a mesh the editor can read the accept test's
    // `mh->version`/`mh->time`/`mesh->texCount` from (lightmapbuild.c:63-64).
    EXPECT_TRUE(RefMeshIsIdentified(/*owner_binding=*/true, /*owner_lod_count=*/3, 0));
    EXPECT_TRUE(RefMeshIsIdentified(true, 3, 2));
    EXPECT_FALSE(RefMeshIsIdentified(true, 3, 3)) << "ref 3 is a magic/child ref";
    EXPECT_FALSE(RefMeshIsIdentified(true, 3, 400));
    EXPECT_FALSE(RefMeshIsIdentified(true, 0, 0)) << "lod_count 0 contributes no refs";
    EXPECT_FALSE(RefMeshIsIdentified(true, 3, -1));
}

TEST(LightmapRefIndexTest, APositionResolvedListIsNotThisObjectsOwnBinding) {
    // A list resolved by authored position (LightmapResolveByPos) belongs to an
    // ANCESTOR's lightmap binding -- the object's model is merely referenced inside
    // that tree -- so its entries are not this object's LODs and no mesh may be
    // assumed for them. Testing one against this object's mesh would reject a valid
    // file whenever the page counts differ.
    EXPECT_FALSE(RefMeshIsIdentified(/*owner_binding=*/false, /*owner_lod_count=*/5, 0));
    EXPECT_FALSE(RefMeshIsIdentified(false, 5, 4));
}

// --------------------------------------------------------------------------
// Corpus differential: the rule applied to every model of every retail level
// --------------------------------------------------------------------------

struct MtpNames {
    std::vector<std::string> vnam;
};

uint32_t ReadU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}
uint32_t ReadU32BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

std::vector<std::string> ReadNames(const uint8_t* d, uint32_t size, uint32_t skipBytes) {
    std::vector<std::string> out;
    if (size < 4) return out;
    uint32_t count = ReadU32LE(d);
    uint32_t off = 4 + skipBytes;
    for (uint32_t i = 0; i < count && off < size; ++i) {
        size_t maxLen = size - off, len = 0;
        while (len < maxLen && d[off + len] != 0) ++len;
        out.emplace_back(reinterpret_cast<const char*>(d + off), len);
        off += static_cast<uint32_t>(len) + 1;
    }
    return out;
}

bool ReadMtpVnam(const std::string& path, MtpNames& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (data.size() < 20 || std::memcmp(data.data(), "FORM", 4) != 0) return false;
    size_t off = 12;  // FORM + u32 size + "MTP "
    while (off + 8 <= data.size()) {
        char fourcc[5] = {};
        std::memcpy(fourcc, data.data() + off, 4);
        uint32_t size = ReadU32BE(data.data() + off + 4);
        if (off + 8 + size > data.size()) break;
        const uint8_t* chunk = data.data() + off + 8;
        if (std::strcmp(fourcc, "VNAM") == 0) {
            out.vnam = ReadNames(chunk, size, ReadU32LE(chunk) * 4);
        }
        off += 8 + size;
    }
    return !out.vnam.empty();
}

std::string InstallRoot() {
    for (const char* name : {"IGI_LIGHTMAP_ROOT", "IGI_GAME_PATH", "IGI_LOD_ROOT", "IGI_MEF_CORPUS"}) {
        const char* root = std::getenv(name);
        if (root != nullptr && *root != '\0') return root;
    }
    return std::string();
}

// Walks every model of every retail level bundle in VNAM order, lays the refs
// out with the rule, and checks the invariants the engine's loop guarantees.
TEST(LightmapRefIndexCorpusTest, RetailLevelsLayOutContiguouslyWithRealLodCounts) {
    const std::string root = InstallRoot();
    if (root.empty()) {
        GTEST_SKIP() << "set IGI_GAME_PATH (or IGI_LIGHTMAP_ROOT) to an IGI install to enable";
    }

    int levels = 0, objects = 0, multiLod = 0, maxLod = 0, totalRefs = 0;
    int distinctOlmNames = 0;

    for (int level = 1; level <= 14; ++level) {
        const std::string path = root + "/MISSIONS/location0/LEVEL" + std::to_string(level) +
                                 "/LEVEL" + std::to_string(level) + ".MTP";
        if (!std::filesystem::exists(path)) continue;
        MtpNames mtp;
        ASSERT_TRUE(ReadMtpVnam(path, mtp)) << "failed to read " << path;
        levels++;

        const std::set<std::string> names(mtp.vnam.begin(), mtp.vnam.end());

        int cursor = 0;
        int previousBase = -1;
        for (const std::string& name : mtp.vnam) {
            // The chain length is the object's lod_count (VirModel_InitLods).
            int lodCount = 0;
            {
                std::string current = name;
                for (int i = 1; i <= 5; ++i) {
                    if (names.count(current) == 0) break;
                    lodCount = i;
                    if (i < 5) {
                        current[current.size() - 1] =
                            static_cast<char>(current[current.size() - 1] + 1);
                    }
                }
            }

            const RefLayout layout = ReserveRefs(lodCount, cursor);
            objects++;
            if (lodCount > 1) multiLod++;
            if (lodCount > maxLod) maxLod = lodCount;

            if (lodCount == 0) {
                EXPECT_EQ(layout.base, -1);
                continue;
            }
            // Bases advance by exactly the previous object's lod_count, which is
            // what `(*n)++` inside the loop produces.
            EXPECT_EQ(layout.base, previousBase < 0 ? 0 : previousBase + 1)
                << "level " << level << " name " << name;
            previousBase = layout.base + lodCount - 1;

            for (int i = 0; i < lodCount; ++i) {
                const int ref = RefIndexForLod(layout, i);
                EXPECT_EQ(ref, layout.base + i);
                EXPECT_GE(ref, 0);
                EXPECT_LT(ref, cursor);
                const std::string olm = OlmFileName(name, ref);
                ASSERT_FALSE(olm.empty());
                // Round-trip the index out of the name: the numeric field is
                // `%05d` (lightmapbuild.c:59), a minimum width, so parse it back
                // and require it to equal the ref index rather than rebuilding the
                // expected string from a different format.
                ASSERT_GT(olm.size(), 4u) << olm;
                EXPECT_EQ(olm.compare(0, name.size(), name), 0) << olm;
                EXPECT_EQ(olm.compare(olm.size() - 4, 4, ".olm"), 0) << olm;
                const size_t digitEnd = olm.size() - 4;
                const size_t digitStart = olm.rfind('_', digitEnd);
                ASSERT_NE(digitStart, std::string::npos) << olm;
                ASSERT_EQ(digitEnd - digitStart - 1, 5u)
                    << olm << " should carry a 5-digit field for ref " << ref;
                const std::string digits = olm.substr(digitStart + 1, 5);
                for (char ch : digits) EXPECT_TRUE(ch >= '0' && ch <= '9') << olm;
                EXPECT_EQ(std::stoi(digits), ref) << olm;
                if (i > 0) distinctOlmNames++;
            }
        }
        totalRefs += cursor;
    }

    if (levels == 0) {
        GTEST_SKIP() << "no retail level bundles found under " << root;
    }

    // Printed so the numbers are in the log rather than only asserted.
    std::printf("[lightmap-refs] levels=%d objects=%d multiLod=%d maxLod=%d totalRefs=%d "
                "higherLodRefs=%d\n",
                levels, objects, multiLod, maxLod, totalRefs, distinctOlmNames);

    EXPECT_GT(levels, 0);
    EXPECT_GT(objects, 0);
    EXPECT_GT(totalRefs, objects) << "some retail model must have more than one LOD";
    EXPECT_GT(multiLod, 0) << "the rule is only interesting when chains are longer than one";
    EXPECT_LE(maxLod, 5) << "VirModel_InitLods caps at VIRMODEL_LODS = 5 (src/model/model.h:11)";
}

// --------------------------------------------------------------------------
// Rule 5 measured on the shipped meshes: every format-3 render group carries a
// page index (there is no 0xFFFF in the retail corpus), and at least one names a
// page other than 0 -- which is why a page-0-only reader is wrong, and why
// `LmGroup.lightmap` has to reach SubMesh.lightmapPage.
// --------------------------------------------------------------------------

TEST(LightmapRefIndexCorpusTest, Format3GroupsNameTheirOwnOlmPage) {
    const std::string root = InstallRoot();
    if (root.empty()) {
        GTEST_SKIP() << "set IGI_GAME_PATH (or IGI_LIGHTMAP_ROOT) to an IGI install to enable";
    }

    int mefs = 0, format3 = 0, blocks = 0, blocks_on_page_0 = 0, blocks_off_page_0 = 0;
    int blocks_with_no_page = 0, max_page = -1;
    std::set<std::string> models_off_page_0;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".mef") continue;
        mefs++;
        ParsedGeometry geometry;
        try {
            geometry = ParseMefFile(entry.path().string());
        } catch (...) {
            continue;  // a model the parser rejects is not this test's subject
        }
        if (geometry.modelType != 3) continue;
        format3++;
        for (const auto& block : geometry.renderBlocks) {
            blocks++;
            const int page = igi::lightmap::PageIndexForGroup(block.lightmapSlot);
            if (page < 0) {
                blocks_with_no_page++;
                continue;
            }
            if (page == 0) {
                blocks_on_page_0++;
            } else {
                blocks_off_page_0++;
                models_off_page_0.insert(entry.path().filename().string());
            }
            if (page > max_page) max_page = page;
        }
    }

    if (mefs == 0) {
        GTEST_SKIP() << "no .mef files found under " << root;
    }

    // Printed so the numbers are in the log rather than only asserted.
    std::printf("[lightmap-pages] mefs=%d format3=%d blocks=%d page0=%d offPage0=%d "
                "noPage=%d maxPage=%d modelsOffPage0=%zu\n",
                mefs, format3, blocks, blocks_on_page_0, blocks_off_page_0,
                blocks_with_no_page, max_page, models_off_page_0.size());

    EXPECT_GT(format3, 0) << "the corpus must contain lightmapped models";
    EXPECT_GT(blocks, 0);
    // The engine reads `set->tex[g->lightmap]` without checking 0xFFFF for a group
    // that has a texture (render.c:1033), so a sentinel there would be an
    // out-of-range read; retail data never produces one.
    EXPECT_EQ(blocks_with_no_page, 0);
    // The page index is real data: at least one group in the corpus samples a page
    // other than 0, so binding page 0 for every group draws the wrong atlas.
    EXPECT_GT(blocks_off_page_0, 0)
        << "if this ever reads 0, the multi-page .olm fix has no retail case";
    EXPECT_GE(max_page, 1);
}

}  // namespace
