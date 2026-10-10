#include <gtest/gtest.h>
#include "../source/renderer/fnt_parser.h"
#include "utils.h"
#include <string>
#include <filesystem>
#include <algorithm>
#include <cctype>

// ============================================================
//  FNT Parser — first .fnt found under game root
// ============================================================

static std::string FindFirstFile(const std::string& root, const std::string& ext) {
    namespace fs = std::filesystem;
    std::error_code ec;
    for (auto& e : fs::recursive_directory_iterator(root, ec)) {
        if (ec) { ec.clear(); continue; }
        if (!e.is_regular_file(ec)) { ec.clear(); continue; }
        std::string fext = e.path().extension().string();
        std::transform(fext.begin(), fext.end(), fext.begin(),
                       [](unsigned char c){ return (char)std::tolower(c); });
        if (fext == ext) return e.path().string();
    }
    return "";
}

class FntParserTest : public ::testing::Test {
protected:
    FntFont font;
    std::string fntPath;
    void SetUp() override {
        fntPath = FindFirstFile(Utils::GetIGIRootPath(), ".fnt");
        ASSERT_FALSE(fntPath.empty())
            << "No .fnt file found under: " << Utils::GetIGIRootPath();
        font = FNT_Parse(fntPath);
    }
};

TEST_F(FntParserTest, ParsesValid) {
    ASSERT_TRUE(font.valid) << "FNT parse failed\nPath: " << fntPath;
}

TEST_F(FntParserTest, LineHeightIsPositive) {
    EXPECT_GT(font.lineHeight, 0);
}

TEST_F(FntParserTest, TextureDimensionsArePositive) {
    EXPECT_GT(font.texWidth,  0);
    EXPECT_GT(font.texHeight, 0);
}

TEST_F(FntParserTest, HasGlyphs) {
    EXPECT_GT(font.glyphs.size(), 0u);
}

TEST_F(FntParserTest, AtlasPixelDataSizeMatchesDimensions) {
    size_t expected = (size_t)font.texWidth * font.texHeight * 4;
    EXPECT_EQ(font.rgba.size(), expected)
        << "texWidth=" << font.texWidth << " texHeight=" << font.texHeight;
}
