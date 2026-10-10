#include <gtest/gtest.h>
#include "../source/renderer/tex_writer.h"
#include "../source/renderer/res_writer.h"
#include "utils.h"
#include <string>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cctype>

// ============================================================
//  TEX Parser — first .tex extracted from level1.res
//
//  Extracts the first .tex entry from level1.res into a temp
//  file then parses it with TEX_Parse.
// ============================================================

static std::string ResPath() {
    return Utils::GetIGIRootPath() +
           "\\missions\\location0\\level1\\textures\\level1.res";
}
static std::string TempTexPath() {
    return Utils::GetExeDirectory() + "\\fixtures\\_tmp_test.tex";
}

static bool ExtractFirstTex(const std::string& resPath, const std::string& outPath) {
    RESFile res = RES_Parse(resPath);
    if (!res.valid) return false;
    for (const auto& e : res.entries) {
        std::string name = e.name;
        std::string ext = name.size() >= 4 ? name.substr(name.size() - 4) : "";
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c){ return (char)std::tolower(c); });
        if (ext == ".tex") {
            std::ofstream f(outPath, std::ios::binary);
            if (!f) return false;
            f.write(reinterpret_cast<const char*>(e.data.data()), (std::streamsize)e.data.size());
            return true;
        }
    }
    return false;
}

class TexParserTest : public ::testing::Test {
protected:
    TEXFile tex;
    void SetUp() override {
        bool ok = ExtractFirstTex(ResPath(), TempTexPath());
        ASSERT_TRUE(ok) << "Could not extract a .tex entry from " << ResPath();
        tex = TEX_Parse(TempTexPath());
    }
    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove(TempTexPath(), ec);
    }
};

TEST_F(TexParserTest, ParsesValid) {
    ASSERT_TRUE(tex.valid) << "TEX parse failed: " << tex.error;
}

TEST_F(TexParserTest, VersionIsKnown) {
    EXPECT_TRUE(tex.version == 2 || tex.version == 7 ||
                tex.version == 9 || tex.version == 11)
        << "Unexpected TEX version: " << tex.version;
}

TEST_F(TexParserTest, HasImages) {
    EXPECT_GT(tex.images.size(), 0u);
}

TEST_F(TexParserTest, FirstImageHasPositiveDimensions) {
    ASSERT_FALSE(tex.images.empty());
    EXPECT_GT(tex.images[0].width,  0u);
    EXPECT_GT(tex.images[0].height, 0u);
}

TEST_F(TexParserTest, PixelDataSizeMatchesDimensions) {
    ASSERT_FALSE(tex.images.empty());
    const auto& img = tex.images[0];
    size_t bytesPerPixel = (img.mode == 2) ? 2u : 4u;
    size_t expected = (size_t)img.width * img.height * bytesPerPixel;
    EXPECT_EQ(img.pixels.size(), expected)
        << "mode=" << img.mode << " w=" << img.width << " h=" << img.height;
}
