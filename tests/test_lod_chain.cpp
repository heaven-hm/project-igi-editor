#include <gtest/gtest.h>

#include "lod/lod_chain.h"
#include "lod/lod_qvm_table.h"
#include "lod/lod_settings.h"
#include "renderer/res_writer.h"
#include "level/qvm_parser.h"
#include "level/qvm_decompiler.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

// Unit tests for the engine's LOD virtual-model chain rule.
// Oracle: src/model/model.c:145-163 (HOOK(0x4CED50, VirModel_InitLods)) in IGI-RE.
// See docs/igi-re-parity/LOD_CHAINS.md for the full trace and the corpus numbers.

namespace {

using igi::lod::BuildLodChain;
using igi::lod::IncrementLodName;
using igi::lod::kVirModelLods;
using igi::lod::LodChain;
using igi::lod::LodDistance;

std::set<std::string> MakeSet(std::initializer_list<const char*> names) {
    std::set<std::string> s;
    for (const char* n : names) s.insert(n);
    return s;
}

LodChain ChainIn(const std::set<std::string>& names, const std::string& start) {
    return BuildLodChain(start, [&names](const std::string& n) { return names.count(n) > 0; });
}

// --------------------------------------------------------------------------
// The increment rule (model.c:160)
// --------------------------------------------------------------------------

TEST(LodChainTest, IncrementIsCharacterArithmeticOnTheLastCharacter) {
    std::string digit = "435_01_1";
    IncrementLodName(digit);
    EXPECT_EQ(digit, "435_01_2");

    // A non-digit last character is incremented as a character, not rejected and
    // not treated as a missing suffix. Measured in retail: `waypoint` -> `waypoinu`,
    // `dummy` -> `dummz`, `colbox` -> `colboy`, `joint_fixer` -> `joint_fixes`,
    // `cylinder` -> `cylindes`.
    std::string letter = "waypoint";
    IncrementLodName(letter);
    EXPECT_EQ(letter, "waypoinu");

    std::string underscore = "model_";
    IncrementLodName(underscore);
    EXPECT_EQ(underscore, "model`");  // '_' + 1 == '`'
}

TEST(LodChainTest, IncrementLeavesAnEmptyNameAlone) {
    std::string empty;
    IncrementLodName(empty);  // engine would write name[-1]; unreachable in retail
    EXPECT_TRUE(empty.empty());
}

TEST(LodChainTest, NonDigitEndingStillChainsWhenTheIncrementedNameExists) {
    // The rule is not "append a numeric suffix": a name ending in a letter chains
    // to the next LETTER. Nothing in the retail corpus exercises this (all 18
    // non-digit-ending names terminate at length 1), so it is pinned here.
    auto names = MakeSet({"waypoint", "waypoinu", "waypoinv"});
    LodChain chain = ChainIn(names, "waypoint");
    ASSERT_EQ(chain.count, 3);
    EXPECT_EQ(chain.names[0], "waypoint");
    EXPECT_EQ(chain.names[1], "waypoinu");
    EXPECT_EQ(chain.names[2], "waypoinv");
}

TEST(LodChainTest, NumericSuffixIsNeverInvented) {
    // A numeric-parse implementation would find `435_01_2` from `435_01`. The
    // engine does not: it increments the last character, so `435_01` looks for
    // `435_02`.
    auto names = MakeSet({"435_01", "435_01_2", "435_02"});
    LodChain chain = ChainIn(names, "435_01");
    ASSERT_EQ(chain.count, 2);
    EXPECT_EQ(chain.names[1], "435_02");
}

// --------------------------------------------------------------------------
// Distances and the level cap (model.c:154-155)
// --------------------------------------------------------------------------

TEST(LodChainTest, DistancesAreIntegerOneHundredOverLevelSquared) {
    EXPECT_EQ(LodDistance(1), 100);
    EXPECT_EQ(LodDistance(2), 25);
    EXPECT_EQ(LodDistance(3), 11);  // 100/9 truncated
    EXPECT_EQ(LodDistance(4), 6);   // 100/16 truncated
    EXPECT_EQ(LodDistance(5), 4);   // 100/25
}

TEST(LodChainTest, ChainStopsAtFiveLevelsEvenWhenMoreExist) {
    auto names = MakeSet({"m1", "m2", "m3", "m4", "m5", "m6", "m7"});
    LodChain chain = ChainIn(names, "m1");
    ASSERT_EQ(chain.count, kVirModelLods);
    EXPECT_EQ(chain.count, 5);
    EXPECT_EQ(chain.names[4], "m5");
    EXPECT_EQ(chain.distance[4], 4);
}

// --------------------------------------------------------------------------
// The terminator (model.c:157-158)
// --------------------------------------------------------------------------

TEST(LodChainTest, MissingVariantEndsTheChain) {
    // Measured retail case: LEVEL1 `210_01_1` chains to `210_01_2`, `210_01_3`,
    // then `210_01_4` is absent and the chain is 3.
    auto names = MakeSet({"210_01_1", "210_01_2", "210_01_3"});
    LodChain chain = ChainIn(names, "210_01_1");
    EXPECT_EQ(chain.count, 3);
}

TEST(LodChainTest, AGapIsNotSkippedOver) {
    // `_2` missing, `_3` present: the engine stops at 1 and never looks at `_3`.
    auto names = MakeSet({"m_1", "m_3", "m_4"});
    LodChain chain = ChainIn(names, "m_1");
    EXPECT_EQ(chain.count, 1);
    EXPECT_EQ(chain.names[0], "m_1");
    EXPECT_TRUE(chain.names[1].empty());
}

TEST(LodChainTest, AModelWhoseOwnNameIsAbsentHasNoChain) {
    // VirModel_Get reports lod_count == 0 as "not found" (model.c:118-139).
    auto names = MakeSet({"m_2", "m_3"});
    LodChain chain = ChainIn(names, "m_1");
    EXPECT_EQ(chain.count, 0);
    EXPECT_TRUE(chain.names[0].empty());
}

TEST(LodChainTest, AChainStartedAtAVariantCoversOnlyTheHigherNumbers) {
    // The chain is built from whatever name was asked for, so asking for `_3`
    // never reaches back to `_1` or `_2` (model.c:146-152).
    auto names = MakeSet({"435_01_1", "435_01_2", "435_01_3", "435_01_4", "435_01_5"});
    LodChain chain = ChainIn(names, "435_01_3");
    ASSERT_EQ(chain.count, 3);
    EXPECT_EQ(chain.names[0], "435_01_3");
    EXPECT_EQ(chain.names[2], "435_01_5");
}

// --------------------------------------------------------------------------
// The issue's own example, on the real bundle
// --------------------------------------------------------------------------

TEST(LodChainTest, IssueExampleWaterTowerChainsThroughFiveLevels) {
    // Issue #62: "435_01_1 pulls in 435_01_2 ... 435_01_5, stopping at the first
    // name that fails to resolve or at five levels."
    auto names = MakeSet({"435_01_1", "435_01_2", "435_01_3", "435_01_4", "435_01_5",
                          "435_02_1"});
    LodChain chain = ChainIn(names, "435_01_1");
    ASSERT_EQ(chain.count, 5);
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(chain.names[i], "435_01_" + std::to_string(i + 1));
    }
    EXPECT_EQ(chain.distance[0], 100);
    EXPECT_EQ(chain.distance[4], 4);
}

// --------------------------------------------------------------------------
// Corpus measurement, re-run as an assertion
// --------------------------------------------------------------------------

// Minimal MTP reader: "FORM" + BE size + "MTP ", then 4-byte fourcc + BE size
// chunks. VNAM is `u32 count, count * u32 offsets, packed NUL-terminated names`
// (src/model/model.c:274-275); MODS is `u32 count, packed names`.
struct MtpNames {
    std::vector<std::string> vnam;
    std::vector<std::string> mods;
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

bool ReadMtp(const std::string& path, MtpNames& out) {
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
        } else if (std::strcmp(fourcc, "MODS") == 0) {
            out.mods = ReadNames(chunk, size, 0);
        }
        off += 8 + size;
    }
    return !out.vnam.empty();
}

std::string InstallRoot() {
    // IGI_GAME_PATH is what scripts/run-host-tests.sh exports for the detected install.
    for (const char* name : {"IGI_LOD_ROOT", "IGI_GAME_PATH", "IGI_MEF_CORPUS"}) {
        const char* root = std::getenv(name);
        if (root != nullptr && *root != '\0') return root;
    }
    return std::string();
}

TEST(LodChainCorpusTest, EveryRetailLevelBundleMatchesTheMeasuredChainHistogram) {
    const std::string root = InstallRoot();
    if (root.empty()) {
        GTEST_SKIP() << "set IGI_GAME_PATH (or IGI_LOD_ROOT) to an IGI install to enable";
    }

    int names = 0, multi = 0, nonDigitEndings = 0, gaps = 0, maxChain = 0;
    std::map<int, int> hist;
    int levels = 0;
    std::vector<std::string> mismatchedMods;

    for (int level = 1; level <= 14; ++level) {
        const std::string path = root + "/MISSIONS/location0/LEVEL" + std::to_string(level) +
                                 "/LEVEL" + std::to_string(level) + ".MTP";
        if (!std::filesystem::exists(path)) continue;
        MtpNames mtp;
        ASSERT_TRUE(ReadMtp(path, mtp)) << "failed to read " << path;
        levels++;
        std::set<std::string> set(mtp.vnam.begin(), mtp.vnam.end());
        // ModelLod.name comes from VNAM (model.c:274-289); the editor's DAT view is
        // MODS. Measured: the two sets are equal in all 14 retail bundles.
        std::set<std::string> mods(mtp.mods.begin(), mtp.mods.end());
        if (set != mods) mismatchedMods.push_back(std::to_string(level));

        for (const std::string& name : set) {
            names++;
            LodChain chain = BuildLodChain(name, [&set](const std::string& n) {
                return set.count(n) > 0;
            });
            hist[chain.count]++;
            if (chain.count > 1) multi++;
            if (chain.count > maxChain) maxChain = chain.count;
            if (!name.empty() && !(name.back() >= '0' && name.back() <= '9')) nonDigitEndings++;
            // A gap is a variant that exists past the terminator: the engine must
            // not reach it.
            if (chain.count < kVirModelLods) {
                std::string probe = name;
                for (int i = 1; i <= chain.count; ++i) probe[probe.size() - 1]++;
                for (int i = chain.count + 1; i <= kVirModelLods; ++i) {
                    probe[probe.size() - 1]++;
                    if (set.count(probe) > 0) { gaps++; break; }
                }
            }
        }
    }

    ASSERT_EQ(levels, 14) << "expected all 14 retail level bundles";
    EXPECT_TRUE(mismatchedMods.empty())
        << "VNAM/MODS name sets differ in levels: " << mismatchedMods.size();

    // Measured on the retail install (see docs/igi-re-parity/LOD_CHAINS.md §3).
    EXPECT_EQ(names, 6605);
    EXPECT_EQ(multi, 3981);
    EXPECT_EQ(maxChain, 5);
    EXPECT_EQ(gaps, 0) << "the engine's terminator must not skip a missing variant";
    EXPECT_EQ(nonDigitEndings, 18);
    EXPECT_EQ(hist[1], 2624);
    EXPECT_EQ(hist[2], 1598);
    EXPECT_EQ(hist[3], 1326);
    EXPECT_EQ(hist[4], 728);
    EXPECT_EQ(hist[5], 329);

    std::printf("[lod] bundles=%d names=%d chain>1=%d (%.1f%%) hist 1:%d 2:%d 3:%d 4:%d 5:%d gaps=%d nonDigitEndings=%d\n",
                levels, names, multi, 100.0 * multi / names, hist[1], hist[2], hist[3],
                hist[4], hist[5], gaps, nonDigitEndings);
}

// --------------------------------------------------------------------------
// VirModel_InitLods: the distance array is written BEFORE the find
// (src/model/model.c:153-158)
// --------------------------------------------------------------------------

TEST(LodChainTest, DistanceIsWrittenAtTheIndexThatTerminatedTheLoop) {
    // Chain of exactly 2, so the loop breaks on i == 3. The engine still writes
    // lod_dist[2] = 100/(3*3) = 11 before breaking, and leaves lod_dist[3] = 0
    // and lod_dist[4] = 0 from the zeroed VirModel.
    const auto names = MakeSet({"a_1", "a_2"});
    const LodChain c = ChainIn(names, "a_1");

    ASSERT_EQ(c.count, 2);
    EXPECT_EQ(c.distance[0], 100);  // 100/(1*1)
    EXPECT_EQ(c.distance[1], 25);   // 100/(2*2)
    EXPECT_EQ(c.distance[2], 11);   // 100/(3*3) — the terminating iteration
    EXPECT_EQ(c.distance[3], 0);    // never reached, VirModel was zeroed
    EXPECT_EQ(c.distance[4], 0);
}

TEST(LodChainTest, FullChainWritesAllFiveDistances) {
    const auto names = MakeSet({"m_1", "m_2", "m_3", "m_4", "m_5"});
    const LodChain c = ChainIn(names, "m_1");

    ASSERT_EQ(c.count, 5);
    EXPECT_EQ(c.distance[0], 100);
    EXPECT_EQ(c.distance[1], 25);
    EXPECT_EQ(c.distance[2], 11);
    EXPECT_EQ(c.distance[3], 6);   // 100/16
    EXPECT_EQ(c.distance[4], 4);   // 100/25
}

TEST(LodChainTest, MissOnTheFirstStepLeavesEveryDistanceAtZero) {
    // The engine breaks on i == 1, after writing lod_dist[0]. So distance[0] is
    // set even though lod_count is 0.
    const auto names = MakeSet({"other"});
    const LodChain c = ChainIn(names, "absent");

    EXPECT_EQ(c.count, 0);
    EXPECT_EQ(c.distance[0], 100);
    EXPECT_EQ(c.distance[1], 0);
}

// --------------------------------------------------------------------------
// ModelLODSettings_Changed: monotonize, then scale by k4096
// (src/animtrig/lodsettings.c:157-171)
// --------------------------------------------------------------------------

TEST(LodSettingsTest, MonotonizeRaisesEachDistanceToItsPredecessor) {
    float d[5] = {10.0f, 5.0f, 30.0f, 20.0f, 25.0f};
    igi::lod::MonotonizeDistances(d);

    EXPECT_FLOAT_EQ(d[0], 10.0f);  // i == 0 is never compared
    EXPECT_FLOAT_EQ(d[1], 10.0f);  // 5 raised to 10
    EXPECT_FLOAT_EQ(d[2], 30.0f);  // already above
    EXPECT_FLOAT_EQ(d[3], 30.0f);  // 20 raised to 30
    EXPECT_FLOAT_EQ(d[4], 30.0f);  // 25 raised to 30
}

TEST(LodSettingsTest, MonotonizeLeavesAnAscendingTableUntouched) {
    float d[5] = {0.0f, 70.0f, 170.0f, 300.0f, 500.0f};
    igi::lod::MonotonizeDistances(d);

    EXPECT_FLOAT_EQ(d[0], 0.0f);
    EXPECT_FLOAT_EQ(d[1], 70.0f);
    EXPECT_FLOAT_EQ(d[2], 170.0f);
    EXPECT_FLOAT_EQ(d[3], 300.0f);
    EXPECT_FLOAT_EQ(d[4], 500.0f);
}

TEST(LodSettingsTest, DistancesAreScaledBy4096ForTheModel) {
    const float d[5] = {0.0f, 70.0f, 170.0f, 300.0f, 500.0f};
    float out[5] = {};
    igi::lod::ScaleDistancesToWorldUnits(d, out);

    EXPECT_FLOAT_EQ(out[0], 0.0f);
    EXPECT_FLOAT_EQ(out[1], 70.0f * 4096.0f);
    EXPECT_FLOAT_EQ(out[4], 500.0f * 4096.0f);
}

// --------------------------------------------------------------------------
// lod_select, authored-table branch (src/lod/mesh3d.c:172-184)
// --------------------------------------------------------------------------

TEST(LodSelectTest, PicksTheFirstBucketWhoseDistanceCoversTheViewDepth) {
    // Table in world units, ascending, as ModelLODSettings_Changed would leave it.
    const float dists[5] = {0.0f, 70.0f * 4096.0f, 170.0f * 4096.0f,
                            300.0f * 4096.0f, 500.0f * 4096.0f};
    const float fov = 1.0f, k = 1.0f, radius = 0.0f;

    // d == dists[0] == 0 is not > dists[0], and dists[0] == 0 takes the special
    // path: i = min(n-1, 1) = 1, then 0 > dists[1] is false, so the result is 1.
    EXPECT_EQ(igi::lod::SelectAuthoredLod(dists, 5, fov, 0.0f, radius, k), 1);

    EXPECT_EQ(igi::lod::SelectAuthoredLod(dists, 5, fov, 70.0f * 4096.0f, radius, k), 1);
    EXPECT_EQ(igi::lod::SelectAuthoredLod(dists, 5, fov, 80.0f * 4096.0f, radius, k), 2);
    EXPECT_EQ(igi::lod::SelectAuthoredLod(dists, 5, fov, 200.0f * 4096.0f, radius, k), 3);
    EXPECT_EQ(igi::lod::SelectAuthoredLod(dists, 5, fov, 400.0f * 4096.0f, radius, k), 4);
}

TEST(LodSelectTest, PastTheCutoffIsNotDrawn) {
    const float dists[5] = {0.0f, 70.0f * 4096.0f, 170.0f * 4096.0f,
                            300.0f * 4096.0f, 500.0f * 4096.0f};

    EXPECT_EQ(igi::lod::SelectAuthoredLod(dists, 5, 1.0f, 500.0f * 4096.0f, 0.0f, 1.0f), 4);
    EXPECT_EQ(igi::lod::SelectAuthoredLod(dists, 5, 1.0f, 501.0f * 4096.0f, 0.0f, 1.0f), -1);
}

TEST(LodSelectTest, ANonZeroFirstDistanceStartsTheScanAtLodZero) {
    // dists[0] != 0, so the special path is skipped and the scan starts at 0.
    const float dists[5] = {10.0f, 20.0f, 30.0f, 40.0f, 100.0f};

    EXPECT_EQ(igi::lod::SelectAuthoredLod(dists, 5, 1.0f, 5.0f, 0.0f, 1.0f), 0);
    EXPECT_EQ(igi::lod::SelectAuthoredLod(dists, 5, 1.0f, 15.0f, 0.0f, 1.0f), 1);
    EXPECT_EQ(igi::lod::SelectAuthoredLod(dists, 5, 1.0f, 45.0f, 0.0f, 1.0f), 4);
}

TEST(LodSelectTest, ShortChainsClampToTheLastAvailableLod) {
    // lod_count 1: i = n-1 = 0, the `i > 1` guard does not apply, and the scan
    // loop cannot run because i < n-1 is false. A zero first distance therefore
    // still selects 0 rather than 1.
    const float dists[5] = {0.0f, 20.0f, 30.0f, 40.0f, 100.0f};
    EXPECT_EQ(igi::lod::SelectAuthoredLod(dists, 1, 1.0f, 5.0f, 0.0f, 1.0f), 0);

    // lod_count 2: i = n-1 = 1, the guard leaves it at 1.
    EXPECT_EQ(igi::lod::SelectAuthoredLod(dists, 2, 1.0f, 5.0f, 0.0f, 1.0f), 1);
}

TEST(LodSelectTest, RadiusIsSubtractedBeforeTheFovAndKScale) {
    const float dists[5] = {0.0f, 100.0f, 200.0f, 300.0f, 1000.0f};
    // d = fov * ((dist - r) * k). dists[0] == 0, so the scan starts at i = 1.
    //
    // r = 50: d = 1 * ((150 - 50) * 2) = 200 -> 200 > 100 advances to i = 2,
    //         200 > 200 stops. Result 2.
    EXPECT_EQ(igi::lod::SelectAuthoredLod(dists, 5, 1.0f, 150.0f, 50.0f, 2.0f), 2);
    // If the radius were dropped the same call would give d = 300 and land on 3,
    // so this pair is what pins the subtraction to the right side of the scale.
    EXPECT_EQ(igi::lod::SelectAuthoredLod(dists, 5, 1.0f, 150.0f, 0.0f, 2.0f), 3);

    // r = 50 at dist 160: d = 1 * ((160 - 50) * 2) = 220 -> 220 > 200 advances
    // to i = 3, then 220 > 300 stops. Result 3.
    EXPECT_EQ(igi::lod::SelectAuthoredLod(dists, 5, 1.0f, 160.0f, 50.0f, 2.0f), 3);
}

TEST(LodSelectTest, NoChainIsNeverDrawn) {
    const float dists[5] = {0.0f, 20.0f, 30.0f, 40.0f, 100.0f};
    EXPECT_EQ(igi::lod::SelectAuthoredLod(dists, 0, 1.0f, 5.0f, 0.0f, 1.0f), -1);
}

// --------------------------------------------------------------------------
// The editor's loaded model set is a faithful ModelLod_Find set
// --------------------------------------------------------------------------
//
// The renderer's chain resolver asks whether an exact `<name>.mef` exists in the
// loaded .res model archives (Renderer_Objects::MeshNameResolves,
// source/renderer/renderer_objects_mesh.cpp) -- the editor's equivalent of the
// engine's strcmp over every LOD of every loaded ModelFile
// (ModelLod_Find, src/model/model.c:165-178). These tests prove that this set
// resolves the SAME chain as the engine's VNAM-derived ModelLod.name set on the
// retail corpus, so the wiring cannot invent or drop a level.

// Exact `.mef` stems of a packed level model archive.
//
// The reader's streaming pass is used rather than RES_BuildIndex: the index is keyed
// by the engine's resource identity, which is upper-cased and backslash-folded
// (String_ToUpper 0x4B5AF0, src/res/resource.c:98-104; Resource_IsLoaded compares
// those at :136-138), while this test compares against the MTP's VNAM names in the
// archive's own spelling.  Using the index made 19 lowercase model names (colbox66
// and friends) look like names that exist in only one of the two sets.
std::set<std::string> ResMefStems(const std::string& resPath) {
    std::set<std::string> stems;
    std::string error;
    RES_ForEachEntry(
        resPath,
        [&stems](const std::string& raw_name, const uint8_t*, std::size_t) {
            const size_t slash = raw_name.find_last_of("\\/");
            const std::string base =
                (slash == std::string::npos) ? raw_name : raw_name.substr(slash + 1);
            if (base.size() < 4) return;
            std::string ext = base.substr(base.size() - 4);
            for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (ext != ".mef") return;
            stems.insert(base.substr(0, base.size() - 4));
        },
        error);
    return stems;
}

std::map<int, int> ChainHistogram(const std::set<std::string>& names) {
    std::map<int, int> hist;
    for (const std::string& name : names) {
        hist[BuildLodChain(name, [&names](const std::string& n) {
            return names.count(n) > 0;
        }).count]++;
    }
    return hist;
}

TEST(LodChainCorpusTest, ThePackedModelArchiveResolvesTheEngineVnamChainExactly) {
    const std::string root = InstallRoot();
    if (root.empty()) {
        GTEST_SKIP() << "set IGI_GAME_PATH (or IGI_LOD_ROOT) to an IGI install to enable";
    }

    int levels = 0, resTotal = 0, vnamTotal = 0, onlyVnam = 0, onlyRes = 0, differingChains = 0;
    for (int level = 1; level <= 14; ++level) {
        const std::string resPath = root + "/missions/location0/level" + std::to_string(level) +
                                    "/models/level" + std::to_string(level) + ".res";
        const std::string mtpPath = root + "/MISSIONS/location0/LEVEL" + std::to_string(level) +
                                    "/LEVEL" + std::to_string(level) + ".MTP";
        if (!std::filesystem::exists(resPath) || !std::filesystem::exists(mtpPath)) continue;
        levels++;

        const std::set<std::string> resNames = ResMefStems(resPath);
        MtpNames mtp;
        ASSERT_TRUE(ReadMtp(mtpPath, mtp)) << "failed to read " << mtpPath;
        const std::set<std::string> vnamNames(mtp.vnam.begin(), mtp.vnam.end());
        ASSERT_FALSE(resNames.empty()) << resPath;
        resTotal += static_cast<int>(resNames.size());
        vnamTotal += static_cast<int>(vnamNames.size());

        for (const std::string& n : vnamNames) if (!resNames.count(n)) onlyVnam++;
        for (const std::string& n : resNames) if (!vnamNames.count(n)) onlyRes++;

        // The strong claim: every name the engine can resolve produces the same
        // chain under the editor's archive lookup as under the VNAM set.
        for (const std::string& n : vnamNames) {
            const LodChain overRes = BuildLodChain(n, [&resNames](const std::string& q) {
                return resNames.count(q) > 0;
            });
            const LodChain overVnam = BuildLodChain(n, [&vnamNames](const std::string& q) {
                return vnamNames.count(q) > 0;
            });
            if (overRes.count != overVnam.count) { differingChains++; continue; }
            for (int i = 0; i < overRes.count; ++i) {
                if (overRes.names[i] != overVnam.names[i]) { differingChains++; break; }
            }
        }
        EXPECT_EQ(ChainHistogram(resNames), ChainHistogram(vnamNames))
            << "level " << level << " chain histogram differs";
    }

    ASSERT_EQ(levels, 14) << "expected all 14 retail level bundles";
    EXPECT_EQ(differingChains, 0)
        << "the archive name set must resolve the engine's chain exactly";
    std::printf("[lod-res] levels=%d resMefNames=%d vnamNames=%d onlyVnam=%d onlyRes=%d differingChains=%d\n",
                levels, resTotal, vnamTotal, onlyVnam, onlyRes, differingChains);
}

// --------------------------------------------------------------------------
// The authored table from the retail lod.qvm feeds the chain
// --------------------------------------------------------------------------
//
// The renderer parses the level's `ModelLODSettings` tasks out of lod.qvm
// (source/renderer/renderer_objects_metadata.cpp -> igi::lod::ParseAuthoredLodEntries),
// monotonizes and scales each table exactly as ModelLODSettings_Changed does
// (IGI-RE src/animtrig/lodsettings.c:157-171), and hands it to lod_select's
// authored branch (IGI-RE src/lod/mesh3d.c:172-184). This test runs that whole
// pipeline on the shipped data.

TEST(LodChainCorpusTest, AuthoredLodTablesFromRetailLodQvmSelectInsideTheResolvedChain) {
    const std::string root = InstallRoot();
    if (root.empty()) {
        GTEST_SKIP() << "set IGI_GAME_PATH (or IGI_LOD_ROOT) to an IGI install to enable";
    }
    const std::string qvmPath = root + "/LOD.QVM";
    if (!std::filesystem::exists(qvmPath)) {
        GTEST_SKIP() << "lod.qvm not present at " << qvmPath;
    }

    QVMFile qvm = QVM_Parse(qvmPath);
    ASSERT_TRUE(qvm.valid) << "failed to parse " << qvmPath;
    const std::string decompiled = QVM_DecompileToString(qvm);
    ASSERT_FALSE(decompiled.empty());

    const std::vector<igi::lod::AuthoredLodEntry> entries =
        igi::lod::ParseAuthoredLodEntries(decompiled);
    ASSERT_FALSE(entries.empty()) << "no ModelLODSettings entries in " << qvmPath;

    // Every decompiled line carrying both markers, to show the parser drops none
    // of them (a dropped line would silently lose an authored table).
    int markerLines = 0;
    {
        std::istringstream lines(decompiled);
        std::string line;
        while (std::getline(lines, line)) {
            if (line.find("Task_New") != std::string::npos &&
                line.find("\"ModelLODSettings\"") != std::string::npos) markerLines++;
        }
    }
    EXPECT_EQ(markerLines, static_cast<int>(entries.size()));

    // Index every level's packed model names once. The editor's loaded set is
    // the level's own model archive PLUS the shared common archive
    // (Renderer_Objects::LoadResCache indexes both).
    std::set<std::string> commonStems = ResMefStems(
        root + "/missions/location0/common/models/location0.res");
    std::vector<std::set<std::string>> perLevel(15);
    for (int level = 1; level <= 14; ++level) {
        const std::string resPath = root + "/missions/location0/level" + std::to_string(level) +
                                    "/models/level" + std::to_string(level) + ".res";
        if (std::filesystem::exists(resPath)) perLevel[level] = ResMefStems(resPath);
        perLevel[level].insert(commonStems.begin(), commonStems.end());
    }

    int trailingOne = 0, resolved = 0, monotonizeChanged = 0, selected = 0, culled = 0;
    std::set<std::string> distinctModels;
    std::vector<std::string> unresolvedExamples;
    for (const igi::lod::AuthoredLodEntry& e : entries) {
        if (e.model.size() >= 2 && e.model.compare(e.model.size() - 2, 2, "_1") == 0) trailingOne++;
        distinctModels.insert(e.model);

        // ModelLODSettings_Changed: clamp non-decreasing, then x4096.
        float raw[5];
        for (int i = 0; i < 5; ++i) raw[i] = e.distance[static_cast<size_t>(i)];
        float before[5];
        for (int i = 0; i < 5; ++i) before[i] = raw[i];
        igi::lod::MonotonizeDistances(raw);
        for (int i = 0; i < 5; ++i) {
            if (raw[i] != before[i]) { monotonizeChanged++; break; }
        }
        float scaled[5];
        igi::lod::ScaleDistancesToWorldUnits(raw, scaled);

        // The chain comes from the same exact archive lookup the renderer wires.
        int level = 0;
        for (int l = 1; l <= 14 && !level; ++l) {
            if (perLevel[l].count(e.model)) level = l;
        }
        if (!level) {  // authored entry for a model not in a retail bundle
            if (unresolvedExamples.size() < 8) unresolvedExamples.push_back(e.model);
            continue;
        }
        resolved++;
        const LodChain chain = BuildLodChain(e.model, [&perLevel, level](const std::string& n) {
            return perLevel[level].count(n) > 0;
        });
        ASSERT_GT(chain.count, 0) << "authored model " << e.model << " has no chain";
        // Probe inside the table: one world unit above the first bucket, clamped
        // to the authored cutoff (a fully flat table has dists[0] == dists[4],
        // and lod_select culls only on `d > dists[4]`).
        const float insideDist = std::min(scaled[0] + 1.0f, scaled[4]);
        const int inside = igi::lod::SelectAuthoredLod(scaled, chain.count, 1.0f,
                                                       insideDist, 0.0f, 1.0f);
        EXPECT_GE(inside, 0) << e.model << " dist=" << insideDist << " cutoff=" << scaled[4];
        EXPECT_LT(inside, chain.count) << e.model;
        selected++;
        const int past = igi::lod::SelectAuthoredLod(scaled, chain.count, 1.0f,
                                                     scaled[4] + 1.0f, 0.0f, 1.0f);
        EXPECT_EQ(past, -1) << e.model << " must be culled past its authored cutoff";
        if (past == -1) culled++;
    }

    EXPECT_EQ(entries.size(), 462u) << "retail LOD.QVM authored ModelLODSettings entries";
    EXPECT_EQ(distinctModels.size(), 450u) << "authored entries cover 450 distinct models";
    EXPECT_EQ(trailingOne, static_cast<int>(entries.size()))
        << "every authored entry is named with a trailing _1";
    EXPECT_EQ(resolved, 431) << "authored models present in a level+common bundle";
    EXPECT_EQ(culled, selected);
    // 31 authored entries name a model no shipped bundle contains. The engine
    // hands those to the "missing" fallback model (ModelLODSettings_Changed
    // calls VirModel_Find, src/animtrig/lodsettings.c:161-171, and VirModel_Find
    // substitutes "missing", src/model/model.c:107-116); the editor has no
    // substitute, so such a name simply has no chain and is never LOD-selected.
    std::printf("[lod-qvm] markerLines=%d authoredEntries=%zu distinctModels=%zu trailing_1=%d "
                "resolvedInBundles=%d commonArchiveNames=%zu monotonizeChanged=%d selected=%d culled=%d\n",
                markerLines, entries.size(), distinctModels.size(), trailingOne, resolved,
                commonStems.size(), monotonizeChanged, selected, culled);
    if (!unresolvedExamples.empty()) {
        std::printf("[lod-qvm] unresolved example(s):");
        for (const std::string& n : unresolvedExamples) std::printf(" %s", n.c_str());
        std::printf("\n");
    }
}

} // namespace
