#include <gtest/gtest.h>
#include "parsers/fnt_parser.h"
#include "parsers/res_parser.h"
#include "utils.h"
#include <fstream>
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

TEST(RetailMenuResourceTest, IngameMenuArchiveContainsUsableFont3) {
    namespace fs = std::filesystem;
    const fs::path archive = fs::path(Utils::GetIGIRootPath()) /
        "MENUSYSTEM" / "ingamemenu.res";
    if (!fs::exists(archive)) GTEST_SKIP() << "Missing retail menu archive: " << archive;

    const auto font_bytes = RES_Extract(archive.string(), "LOCAL:menusystem/font3.fnt");
    ASSERT_FALSE(font_bytes.empty()) << "LOCAL:menusystem/font3.fnt missing from " << archive;

    const fs::path staged = fs::temp_directory_path() / "igi_editor_test_ingamemenu_font3.fnt";
    {
        std::ofstream out(staged, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.is_open());
        out.write(reinterpret_cast<const char*>(font_bytes.data()),
                  static_cast<std::streamsize>(font_bytes.size()));
        ASSERT_TRUE(out.good());
    }
    const FntFont retail_font = FNT_Parse(staged.string());
    std::error_code ec;
    fs::remove(staged, ec);

    ASSERT_TRUE(retail_font.valid);
    EXPECT_GT(retail_font.lineHeight, 0);
    EXPECT_FALSE(retail_font.glyphs.empty());
}
