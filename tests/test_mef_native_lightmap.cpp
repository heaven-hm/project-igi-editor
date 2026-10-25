#include <gtest/gtest.h>
#include "renderer/mef_native.h"
#include <cstdlib>
#include <filesystem>
#include <string>

// Real corpus fixture: 435_01_1 (WaterTower) is a known MODEL_LIGHTMAP
// (modelType 3) building model, confirmed via:
//   igi1conv lightmap resolve --model 435_01_1 \
//     --qsc D:\IGI1\missions\location0\level1\objects.qsc --task-id 1104
// which resolves successfully against this exact level/model pair.
//
// The Windows paths below are the original fixtures. On a host without the game
// install the same assets can be supplied through IGI_MEF_WATERTOWER /
// IGI_MEF_SNIPER. 001_02_1 is not loose on disk in the retail install — it lives
// in LEVEL7.RES / LEVEL8.RES and can be extracted with the editor's own
// RES_Extract, e.g.:
//   RES_Extract LEVEL7.RES "LOCAL:models/001_02_1.mef" 001_02_1.mef
//   IGI_MEF_SNIPER=$PWD/001_02_1.mef <test binary>
static std::string ResolveMefAsset(const char* envVar, const char* fallback) {
    if (const char* p = std::getenv(envVar); p != nullptr && *p != '\0') {
        if (std::filesystem::exists(p)) return p;
    }
    return fallback;
}

// A model that is loose in the retail install can also be resolved from the
// install root scripts/run-host-tests.sh exports as IGI_GAME_PATH, so the case
// runs in the standard gate instead of skipping.
static std::string ResolveLooseCorpusMef(const char* relativePath) {
    for (const char* name : {"IGI_GAME_PATH", "IGI_MEF_CORPUS"}) {
        const char* root = std::getenv(name);
        if (root == nullptr || *root == '\0') continue;
        std::string candidate = std::string(root) + "/" + relativePath;
        if (std::filesystem::exists(candidate)) return candidate;
    }
    return std::string();
}

static const std::string kWaterTowerMef = [] {
    const std::string fromCorpus =
        ResolveLooseCorpusMef("MISSIONS/location0/LEVEL1/MODELS/models/435_01_1.mef");
    if (!fromCorpus.empty()) return fromCorpus;
    return ResolveMefAsset("IGI_MEF_WATERTOWER", "D:\\IGI1\\editor\\models\\level1\\435_01_1.mef");
}();
static const std::string kSniperMef =
    ResolveMefAsset("IGI_MEF_SNIPER", "D:\\IGI1\\content\\models\\level2\\001_02_1.mef");

TEST(MefNativeLightmapTest, ModelType3PopulatesDistinctUv2Channel) {
    if (!std::filesystem::exists(kWaterTowerMef)) {
        GTEST_SKIP() << "Real IGI1 corpus not present at: " << kWaterTowerMef;
    }

    ParsedGeometry geo = ParseMefFile(kWaterTowerMef);
    ASSERT_EQ(geo.modelType, 3u)
        << "435_01_1 (WaterTower) is expected to be MODEL_LIGHTMAP (type 3)";
    ASSERT_FALSE(geo.vertices.empty());

    // The lightmap atlas UV is a distinct mapping from the diffuse UV for
    // every type-3 model — if uv2 were still unread (all zero / equal to uv)
    // this would fail.
    bool anyVertexHasDistinctUv2 = false;
    for (const auto& v : geo.vertices) {
        if (v.uv2 != v.uv) { anyVertexHasDistinctUv2 = true; break; }
    }
    EXPECT_TRUE(anyVertexHasDistinctUv2)
        << "Expected at least one vertex whose uv2 (lightmap atlas UV) "
           "differs from uv (diffuse UV) on a type-3 model";
}

TEST(MefNativeRenderLayoutTest, SkinnedModelsUsePackedDrawRecords) {
    if (!std::filesystem::exists(kSniperMef)) {
        GTEST_SKIP() << "Real IGI1 corpus not present at: " << kSniperMef;
    }

    const ParsedGeometry geo = ParseMefFile(kSniperMef);
    ASSERT_EQ(geo.modelType, 1u);
    EXPECT_EQ(geo.renderLayout, "type1 packed DNER");
    EXPECT_FALSE(geo.renderBlocks.empty());
    EXPECT_FALSE(geo.triangles.empty());
    EXPECT_GT(geo.baseVertexCount, 0u);
    EXPECT_GT(geo.extraInfluenceCount, 0u);
    EXPECT_EQ(geo.baseVertexCount + geo.extraInfluenceCount, geo.vertices.size());

    // Packed DNER material slots are the ordered instance-material indices.
    for (size_t i = 0; i < geo.renderBlocks.size(); ++i) {
        EXPECT_EQ(geo.renderBlocks[i].materialSlot, static_cast<int>(i));
    }
}
