#include <gtest/gtest.h>

#include "lod/lod_chain.h"
#include "lod/lod_settings.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
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

} // namespace
