#include <gtest/gtest.h>
#include "../source/renderer/fnt_parser.h"
#include "../source/renderer/res_writer.h"
#include "support/temp_directory.h"
#include "utils.h"
#include <string>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <cstdint>
#include <vector>
#include <random>

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

TEST(FntParserMalformedTest, RejectsChunkThatCannotAdvance) {
    namespace fs = std::filesystem;
    test_support::TempDirectory temporary_directory;
    const fs::path path = temporary_directory.path() / "invalid-skip.fnt";
    std::vector<uint8_t> bytes(36, 0);
    auto put32 = [&](size_t offset, uint32_t value) {
        bytes[offset + 0] = static_cast<uint8_t>(value);
        bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
        bytes[offset + 2] = static_cast<uint8_t>(value >> 16);
        bytes[offset + 3] = static_cast<uint8_t>(value >> 24);
    };
    put32(0, 0x46464C49);   // ILFF
    put32(16, 0x544E4F46);  // FONT
    put32(20, 0x48544E46);  // FNTH
    put32(32, 8);           // invalid skip: smaller than the chunk header
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output.is_open());
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }

    const FntFont parsed = FNT_Parse(path.string());
    EXPECT_FALSE(parsed.valid);
}

TEST(FntParserMalformedTest, RejectsEveryShortHeaderWithoutReadingPastEnd) {
    for (size_t length = 0; length < 20; ++length) {
        const std::vector<uint8_t> bytes(length, 0);
        EXPECT_FALSE(FNT_ParseBytes(bytes).valid) << "length=" << length;
    }
}

namespace {
void AppendU16(std::vector<uint8_t>& bytes, uint16_t value) {
    bytes.push_back(static_cast<uint8_t>(value));
    bytes.push_back(static_cast<uint8_t>(value >> 8));
}

void AppendU32(std::vector<uint8_t>& bytes, uint32_t value) {
    bytes.push_back(static_cast<uint8_t>(value));
    bytes.push_back(static_cast<uint8_t>(value >> 8));
    bytes.push_back(static_cast<uint8_t>(value >> 16));
    bytes.push_back(static_cast<uint8_t>(value >> 24));
}

void AppendChunk(std::vector<uint8_t>& bytes, uint32_t fourcc, const std::vector<uint8_t>& data) {
    const uint32_t padded_length = static_cast<uint32_t>((data.size() + 3u) & ~3u);
    AppendU32(bytes, fourcc);
    AppendU32(bytes, static_cast<uint32_t>(data.size()));
    // align = 4, as in every shipped archive.  Chunk_Data (HOOK 0x4B74D0,
    // src/res/chunk.c:34-49) computes the payload as
    // `parent + ((align + off + 0xF) & ~(align - 1))`, which is meaningless at 0 --
    // this fixture used to write 0 and the engine-faithful reader rejects it.
    AppendU32(bytes, 4);
    AppendU32(bytes, 16u + padded_length);
    bytes.insert(bytes.end(), data.begin(), data.end());
    bytes.resize(bytes.size() + padded_length - data.size(), 0);
}

std::vector<uint8_t> MinimalRgb565Font() {
    std::vector<uint8_t> bytes;
    AppendU32(bytes, 0x46464C49); // ILFF
    AppendU32(bytes, 0); AppendU32(bytes, 0); AppendU32(bytes, 0);
    AppendU32(bytes, 0x544E4F46); // FONT

    std::vector<uint8_t> fnth(24, 0);
    fnth[4] = 1; fnth[12] = 7; fnth[16] = 5; fnth[20] = 9;
    AppendChunk(bytes, 0x48544E46, fnth); // FNTH
    std::vector<uint8_t> anmf(40, 0);
    anmf[22] = 2; anmf[24] = 1; anmf[26] = 3;
    AppendChunk(bytes, 0x464D4E41, anmf); // ANMF
    // TRAN is the engine's whole character map: 256 uint16 entries, which
    // src/res/sprite.c:197-199 walks two bytes at a time for 256 codes.  A 2-byte
    // chunk made the parser report a truncated map and reject the font outright.
    // Entry 0 = 1 selects frame 1, read 1-based (sprite.c:199-202 -> Sprite_SetFrame),
    // which is the single ANMF frame below.
    std::vector<uint8_t> tran(512, 0); tran[0] = 1;
    AppendChunk(bytes, 0x4E415254, tran); // TRAN
    // TexHeader: format@0, width@14, height@16, unk12@18, bitsPerPixel@22
    // (src/file/texture.c:38-49).  TexRes_Load derives the pitch as
    // `bitsPerPixel * unk12 / 8` (texture.c:110), so both must be set or the atlas is
    // rejected as zero-sized: 16 bpp * 2 / 8 = 4 bytes == width 2 * 2 bytes, which is
    // exactly the BODY below.
    std::vector<uint8_t> texh(24, 0);
    texh[0] = 2; texh[14] = 2; texh[16] = 1; texh[18] = 2; texh[22] = 16;
    AppendChunk(bytes, 0x48584554, texh); // TEXH
    const std::size_t body_header = bytes.size();
    std::vector<uint8_t> body;
    AppendU16(body, 0xffff); AppendU16(body, 0);
    AppendChunk(bytes, 0x59444F42, body); // BODY

    // The root form needs a real size and a power-of-two align: Chunk_FirstChild
    // (HOOK 0x4B7490, src/res/chunk.c:17-32) returns NULL when size == 0 or the align
    // is not a power of two, so a zeroed root header is an archive with no members at
    // all.  And Chunk_Next (0x4B7470, :9-15) ends the sibling walk on next == 0, so the
    // last chunk must not carry its own length there.
    const auto put32 = [&bytes](std::size_t at, uint32_t v) {
        bytes[at + 0] = static_cast<uint8_t>(v & 0xFFu);
        bytes[at + 1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
        bytes[at + 2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
        bytes[at + 3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
    };
    put32(4, static_cast<uint32_t>(bytes.size()));  // size: the whole form
    put32(8, 4u);                                   // align
    put32(body_header + 12, 0u);                    // chain terminator
    return bytes;
}
} // namespace

TEST(FntParserSyntheticTest, ParsesMinimalRgb565FontAndGlyphMap) {
    const FntFont font = FNT_ParseBytes(MinimalRgb565Font());
    ASSERT_TRUE(font.valid);
    EXPECT_EQ(font.texWidth, 2);
    EXPECT_EQ(font.texHeight, 1);
    EXPECT_EQ(font.lineHeight, 9);
    ASSERT_EQ(font.glyphs.size(), 1u);
    const FntGlyph& glyph = font.glyphs.at(0);
    EXPECT_EQ(glyph.width, 2);
    EXPECT_EQ(glyph.height, 1);
    EXPECT_EQ(glyph.advance, 3);
    ASSERT_EQ(font.rgba.size(), 8u);
    // Format 2 is ARGB1555 in the engine's own table (D3DTex_DownloadTexture
    // 0x50AD10, src/sysmisc/d3dtex.c:411-432), not RGB565: the alpha comes from the
    // low 5 bits and expands as 31 << 3 = 248, so an all-ones pixel is not fully
    // opaque.  The old expectation of 255 assumed RGB565.
    EXPECT_EQ(font.rgba[3], 248);
    EXPECT_EQ(font.rgba[7], 0);
}

TEST(FntParserMalformedTest, RejectsMinimalFontWithoutBody) {
    std::vector<uint8_t> bytes = MinimalRgb565Font();
    bytes.resize(bytes.size() - 20); // Remove BODY header and payload.
    EXPECT_FALSE(FNT_ParseBytes(bytes).valid);
}

TEST(FntParserFuzzSmokeTest, DeterministicMalformedBuffersNeverProduceValidFont) {
    // Fixed seed makes a failing input reproducible while exercising parser
    // bounds checks without creating one file per generated sample.
    std::mt19937 generator(0xF17F00Du);
    std::uniform_int_distribution<int> length(0, 512);
    std::uniform_int_distribution<int> byte(0, 255);
    for (int sample = 0; sample < 512; ++sample) {
        std::vector<uint8_t> input(static_cast<size_t>(length(generator)));
        for (uint8_t& value : input) value = static_cast<uint8_t>(byte(generator));
        EXPECT_FALSE(FNT_ParseBytes(input).valid) << "seed=0xF17F00D sample=" << sample;
    }
}

// ============================================================
//  The retail in-game menu's font: font3.fnt inside
//  menusystem/ingamemenu.res, the font the pause menu's style is
//  measured against (source/runtime/pause_menu_style.h).
// ============================================================

// The engine's own width for a string: `Sprite_StringWidth` (HOOK 0x4B7050,
// src/res/sprite.c:315-328) adds `frames[c - 1].advance` for a mapped character
// and `Sprite_Advance` for one the TRAN map leaves empty.
static int EngineStringWidth(const FntFont& font, const std::string& text) {
    int width = 0;
    for (char c : text) {
        const auto it = font.glyphs.find(static_cast<unsigned char>(c));
        width += it == font.glyphs.end() ? font.defaultAdvance : it->second.advance;
    }
    return width;
}

TEST(FntParserRetailMenuFontTest, Font3MetricsAreThePauseMenuStylesSource) {
    // `menusystem/ingamemenu.qvm` screen 900 names "font3.fnt" for its title and
    // for all five items; the engine resolves it through
    // `MenuManager_AddResource` (HOOK 0x417E20, src/menu/menumanager.c:260-296),
    // which turns the archive entry's "FNT" extension into a font.
    const std::filesystem::path archive =
        std::filesystem::path(Utils::GetIGIRootPath()) / "menusystem" / "ingamemenu.res";
    if (!std::filesystem::is_regular_file(archive)) {
        GTEST_SKIP() << "no retail in-game menu archive at " << archive;
    }

    const std::vector<uint8_t> bytes =
        RES_Extract(archive.string(), "LOCAL:menusystem/font3.fnt");
    ASSERT_FALSE(bytes.empty()) << "font3.fnt not found inside " << archive;
    const FntFont font = FNT_ParseBytes(bytes);
    ASSERT_TRUE(font.valid);

    // FontHeader::h is the line height the engine measures text with:
    // `Grid_Height` (HOOK 0x4B7C20, src/res/grid.c:209-213) is
    // `Sprite_Height(sprite) * rows` and `Text_New` builds one row
    // (HOOK 0x4B6EC0, src/res/text.c:76-93).  It is the pause menu's row height
    // before the frame's Y-Spacing is added.
    EXPECT_EQ(font.lineHeight, 21);
    // FontHeader::advance is the blank-cell advance (`Grid_UpdateRow`,
    // HOOK 0x4B7730, src/res/grid.c:93-105).
    EXPECT_EQ(font.defaultAdvance, 6);

    // The engine's own width for one of screen 900's labels, which pins the
    // per-glyph advances the layout and the renderer share to the real file.
    EXPECT_EQ(EngineStringWidth(font, "Resume Game"), 104);
    EXPECT_EQ(EngineStringWidth(font, " "), font.defaultAdvance);
}
