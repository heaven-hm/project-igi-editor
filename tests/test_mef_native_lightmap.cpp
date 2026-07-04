#include <gtest/gtest.h>
#include "renderer/mef_native.h"
#include <filesystem>

// Real corpus fixture: 435_01_1 (WaterTower) is a known MODEL_LIGHTMAP
// (modelType 3) building model, confirmed via:
//   igi1conv lightmap resolve --model 435_01_1 \
//     --qsc D:\IGI1\missions\location0\level1\objects.qsc --task-id 1104
// which resolves successfully against this exact level/model pair.
static const char* kWaterTowerMef = "D:\\IGI1\\editor\\models\\level1\\435_01_1.mef";

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
