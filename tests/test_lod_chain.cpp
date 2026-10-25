#include <gtest/gtest.h>

#include "lod/lod_chain.h"

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

} // namespace
