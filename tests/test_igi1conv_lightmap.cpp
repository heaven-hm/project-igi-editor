#include <gtest/gtest.h>
#include "utils_igi1conv.h"

// ============================================================
//  ParseLightmapResolveStdout — pure stdout parsing, no process spawn.
//  Sample text captured from the real bundled igi1conv.exe:
//    igi1conv lightmap resolve --model 435_01_1 \
//      --qsc D:\IGI1\missions\location0\level1\objects.qsc --task-id 1104
// ============================================================

TEST(Igi1convLightmapTest, ParsesRealResolveOutput) {
    const std::string sample =
        "lightmap: resolved   task 1104 \"WaterTower\" -> obj00000 @ (2.46585e+07, -5.59572e+07, 1.74412e+08)\r\n"
        "lightmap: 11 .olm file(s):\r\n"
        "  D:\\IGI1\\missions\\location0\\level1\\lightmaps\\lightmaps_unpacked\\obj00000_00000.olm\r\n"
        "  D:\\IGI1\\missions\\location0\\level1\\lightmaps\\lightmaps_unpacked\\obj00000_00001.olm\r\n"
        "  D:\\IGI1\\missions\\location0\\level1\\lightmaps\\lightmaps_unpacked\\obj00000_00002.olm\r\n";
    std::string err;
    std::vector<std::string> paths = igi1conv::ParseLightmapResolveStdout(sample, err);
    ASSERT_EQ(paths.size(), 3u) << "err=" << err;
    EXPECT_EQ(paths[0], "D:\\IGI1\\missions\\location0\\level1\\lightmaps\\lightmaps_unpacked\\obj00000_00000.olm");
    EXPECT_EQ(paths[2], "D:\\IGI1\\missions\\location0\\level1\\lightmaps\\lightmaps_unpacked\\obj00000_00002.olm");
}

TEST(Igi1convLightmapTest, EmptyOutputReturnsErrorNoPaths) {
    std::string err;
    std::vector<std::string> paths = igi1conv::ParseLightmapResolveStdout("", err);
    EXPECT_TRUE(paths.empty());
    EXPECT_FALSE(err.empty());
}

TEST(Igi1convLightmapTest, StopsAtNextNonIndentedLine) {
    const std::string sample =
        "lightmap: resolved   task 1 \"X\" -> obj00000 @ (0, 0, 0)\r\n"
        "lightmap: 2 .olm file(s):\r\n"
        "  C:\\a.olm\r\n"
        "  C:\\b.olm\r\n"
        "some trailing unrelated line\r\n"
        "  C:\\should_not_be_included.olm\r\n";
    std::string err;
    std::vector<std::string> paths = igi1conv::ParseLightmapResolveStdout(sample, err);
    ASSERT_EQ(paths.size(), 2u);
    EXPECT_EQ(paths[1], "C:\\b.olm");
}
