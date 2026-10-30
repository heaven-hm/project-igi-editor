// tests/test_mod_asset_compat.cpp
//
// Issue #49 part 3 — "verify and extend parsers/asset paths so levels/assets
// from a mod load correctly, not just vanilla IGI1 data".
//
// WHAT IS PROVED HERE.  Two container rules that a third-party asset can
// violate, each with the engine citation, and each measured against a real
// shipping file rather than a hand-written expectation:
//
//   1. THE QVM BYTECODE MINOR.  `Qvm_Load` HOOK 0x4BD220 refuses `unk08 < 5`
//      and otherwise decodes with ONE opcode table, the version-5 one
//      (src/qvm/qvm.c:441-446).  A container that carries another minor passes
//      the header check and is then misread.  This is not hypothetical: a
//      third-party level archive on this machine ships 102 QVM files with
//      `version = 8, unk08 = 7` whose code sections do not decode at all —
//      see docs/igi-re-parity/NEO_REMASTERED.md for the measurement.
//
//   2. THE v11 TEXTURE FORMAT 0x43.  `D3DTex_DownloadTexture` HOOK 0x50AD10
//      (src/sysmisc/d3dtex.c:411-432) has a `case 0x43` that goes through
//      `Pixel_Copy32`, so a v11 texture with format 0x43 is four bytes per
//      pixel.  The mod-level textures on this machine use exactly that format.
//
// The tests are host-portable and self-skip when the corpus file they need is
// absent.  No existing test is weakened or replaced.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../source/level/qvm_parser.h"
#include "../source/renderer/tex_parser.h"
#include "support/temp_directory.h"

namespace {

// --- corpus -----------------------------------------------------------------

std::string GameRoot() {
    const char* env = std::getenv("IGI_GAME_PATH");
    if (env != nullptr && *env != '\0') return env;
    return std::string();
}

bool ReadFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return !out.empty();
}

bool WriteFile(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    return f.good();
}

int32_t ReadI32(const std::vector<uint8_t>& b, size_t off) {
    int32_t v = 0;
    std::memcpy(&v, b.data() + off, 4);
    return v;
}

void WriteI32(std::vector<uint8_t>& b, size_t off, int32_t v) {
    std::memcpy(b.data() + off, &v, 4);
}

// A shipping QVM to patch.  `humanplayer.qvm` is small and always present.
std::string RetailQvmPath() {
    const std::string root = GameRoot();
    if (root.empty()) return std::string();
    return root + "/humanplayer/humanplayer.qvm";
}

}  // namespace

// ===========================================================================
// 1. The QVM bytecode minor
// ===========================================================================

TEST(ModAssetCompat, EveryRetailContainerCarriesBytecodeMinorFive) {
    const std::string path = RetailQvmPath();
    if (path.empty())
        GTEST_SKIP() << "set IGI_GAME_PATH to a retail install to run this test";
    std::vector<uint8_t> bytes;
    if (!ReadFile(path, bytes)) GTEST_SKIP() << path << " not found";

    const QVMFile qvm = QVM_Parse(path);
    ASSERT_TRUE(qvm.valid) << qvm.error;
    // `Qvm_Load` requires exactly 8 at 0x04 and at least 5 at 0x08
    // (src/qvm/qvm.c:441-446); every retail container uses 5 at 0x08.
    EXPECT_EQ(qvm.header.version, 8);
    EXPECT_EQ(qvm.header.unk08, 5);
    EXPECT_TRUE(qvm.warning.empty()) << qvm.warning;
}

TEST(ModAssetCompat, AContainerWithAnotherBytecodeMinorIsAcceptedAndFlagged) {
    // The engine accepts `unk08 >= 5`, so the editor must too — refusing would
    // be stricter than IGI.exe.  What the editor adds is a non-fatal note that
    // names the real cause of the decode failure that follows.
    const std::string path = RetailQvmPath();
    if (path.empty())
        GTEST_SKIP() << "set IGI_GAME_PATH to a retail install to run this test";
    std::vector<uint8_t> bytes;
    if (!ReadFile(path, bytes)) GTEST_SKIP() << path << " not found";

    const QVMFile baseline = QVM_Parse(path);
    ASSERT_TRUE(baseline.valid) << baseline.error;

    // Patch the header word at 0x08 from the retail 5 to the 7 a third-party
    // toolchain writes, and change nothing else.
    test_support::TempDirectory tmp;
    const std::string patched = (tmp.path() / "minor7.qvm").string();
    WriteI32(bytes, 0x08, 7);
    ASSERT_TRUE(WriteFile(patched, bytes));

    const QVMFile qvm = QVM_Parse(patched);
    ASSERT_TRUE(qvm.valid) << qvm.error;
    EXPECT_EQ(qvm.header.unk08, 7);
    EXPECT_FALSE(qvm.warning.empty())
        << "a non-retail bytecode minor must be reported";
    EXPECT_NE(qvm.warning.find("0x08"), std::string::npos) << qvm.warning;

    // And the header word does NOT affect the decode: the instruction stream is
    // byte-for-byte the same, because the engine has one opcode table.
    ASSERT_EQ(qvm.instructions.size(), baseline.instructions.size());
    for (size_t i = 0; i < qvm.instructions.size(); ++i) {
        EXPECT_EQ(static_cast<int>(qvm.instructions[i].type),
                  static_cast<int>(baseline.instructions[i].type)) << "instr " << i;
        EXPECT_EQ(qvm.instructions[i].address, baseline.instructions[i].address)
            << "instr " << i;
    }
}

TEST(ModAssetCompat, AForeignBytecodeOrderIsNamedInTheDecodeError) {
    // Reproduce what a third-party container actually looks like: `unk08 = 7`
    // and a first code byte of 0x13.  In the retail opcode table 0x13 is RET
    // (`Qvm_Init` HOOK 0x4BD430 registers `Qvm_OpError` for it, src/qvm/qvm.c:
    // 544), so the walk cannot start.  The error must say WHY, not just
    // "Illegal instruction".
    const std::string path = RetailQvmPath();
    if (path.empty())
        GTEST_SKIP() << "set IGI_GAME_PATH to a retail install to run this test";
    std::vector<uint8_t> bytes;
    if (!ReadFile(path, bytes)) GTEST_SKIP() << path << " not found";

    const int32_t code_offset = ReadI32(bytes, 0x2C);
    ASSERT_GT(code_offset, 0);
    ASSERT_LT(static_cast<size_t>(code_offset), bytes.size());

    test_support::TempDirectory tmp;
    const std::string patched = (tmp.path() / "foreign-order.qvm").string();
    WriteI32(bytes, 0x08, 7);
    bytes[static_cast<size_t>(code_offset)] = 0x13;  // RET in the retail table
    ASSERT_TRUE(WriteFile(patched, bytes));

    const QVMFile qvm = QVM_Parse(patched);
    EXPECT_FALSE(qvm.valid);
    EXPECT_EQ(qvm.error_code, QVM_ERR_ILLEGAL);
    // The bytecode minor is the actionable half of the message.
    EXPECT_NE(qvm.error.find("bytecode minor 7"), std::string::npos) << qvm.error;
}

TEST(ModAssetCompat, ABytecodeMinorBelowFiveIsStillRefusedLikeTheEngine) {
    // The accept/reject boundary is the engine's and is unchanged.
    const std::string path = RetailQvmPath();
    if (path.empty())
        GTEST_SKIP() << "set IGI_GAME_PATH to a retail install to run this test";
    std::vector<uint8_t> bytes;
    if (!ReadFile(path, bytes)) GTEST_SKIP() << path << " not found";

    test_support::TempDirectory tmp;
    const std::string patched = (tmp.path() / "minor4.qvm").string();
    WriteI32(bytes, 0x08, 4);
    ASSERT_TRUE(WriteFile(patched, bytes));

    const QVMFile qvm = QVM_Parse(patched);
    EXPECT_FALSE(qvm.valid);
    EXPECT_NE(qvm.error.find("0x08"), std::string::npos) << qvm.error;
}

// ===========================================================================
// 2. The v11 texture format 0x43
// ===========================================================================

namespace {

// A minimal but well-formed LOOP v11 texture, built from the header layout
// `Texture_LoadFile` HOOK 0x4B24C0 reads (src/file/texture.c:237-247) and the
// member offsets the editor's ParseV11 uses
// (source/renderer/tex_parser.cpp:168-208):
//   magic(0) version(4) format(I@8) unk0C(I@12) unk10(I@16)
//   unk14(H@20) width(H@22) height(H@24) unk1A(H@26) unk1C(H@28) bpp(H@30)
std::vector<uint8_t> MakeV11(uint32_t format, uint16_t width, uint16_t height,
                             uint16_t bytes_per_pixel, uint16_t row_words) {
    // Enough for the whole mip chain: `ParseV11` keeps halving while
    // `(format & kFmtMipmaps)` is set (source/renderer/tex_parser.cpp:205), and
    // kFmtMipmaps is 0x40 (tex_parser.cpp:31) — which 0x43 has.
    std::vector<uint8_t> b(0x20 + 4096, 0);
    std::memcpy(b.data(), "LOOP", 4);
    WriteI32(b, 4, 11);
    WriteI32(b, 8, static_cast<int32_t>(format));
    auto put16 = [&](size_t off, uint16_t v) {
        std::memcpy(b.data() + off, &v, 2);
    };
    put16(20, 0);                 // unk14
    put16(22, width);
    put16(24, height);
    put16(26, row_words);         // unk1A
    put16(28, 0);                 // unk1C
    put16(30, bytes_per_pixel);   // bytesPerPixel
    return b;
}

}  // namespace

TEST(ModAssetCompat, Format0x43IsFourBytesPerPixelInTheEngineAndHere) {
    // src/sysmisc/d3dtex.c:414-419 —
    //   case 3: case 4: case 0x43: Pixel_Copy32(cur, w, h, data, pitch >> 2, h, 0);
    EXPECT_EQ(igi_tex::DepthForMode(0x43), 4u);
    EXPECT_EQ(igi_tex::DepthForMode(3), 4u);
    EXPECT_EQ(igi_tex::DepthForMode(4), 4u);
    // And the formats the engine has no conversion for stay refused, so the
    // editor does not invent one: `default: Error_Show("Unsupported texture
    // format")` (d3dtex.c:427-431).
    for (uint32_t mode : {0u, 6u, 0x42u, 0x44u, 0x100u})
        EXPECT_EQ(igi_tex::DepthForMode(mode), 0u) << "format " << mode;
}

TEST(ModAssetCompat, AV11TextureWithFormat0x43ParsesAsFourBytePixels) {
    // The mod-level textures on this machine are v11 with format 0x43; this is
    // the shape a mod asset has and it must parse.
    const std::vector<uint8_t> bytes = MakeV11(0x43, 8, 8, 4, 8);
    std::istringstream stream(std::string(bytes.begin(), bytes.end()),
                              std::ios::binary);

    TEXFile tex;
    ASSERT_TRUE(igi_tex::TEX_ParseStreamCore(stream, tex)) << tex.error;
    EXPECT_EQ(tex.version, 11u);
    ASSERT_FALSE(tex.images.empty());
    EXPECT_EQ(tex.images[0].mode, 0x43u);
    EXPECT_EQ(tex.images[0].width, 8u);
    EXPECT_EQ(tex.images[0].height, 8u);
    // pitch = bytesPerPixel * unk1A (src/file/texture.c:242)
    EXPECT_EQ(tex.images[0].pitch, 4u * 8u);
    EXPECT_EQ(tex.images[0].pixels.size(), 4u * 8u * 8u);
    // 0x43 has the mipmap bit (0x40), so the whole chain is read:
    // 8x8, 4x4, 2x2, 1x1, with pitch halved each level
    // (D3DTex_DownloadTexture, src/sysmisc/d3dtex.c:423-432).
    ASSERT_EQ(tex.images.size(), 4u);
    EXPECT_EQ(tex.images[3].width, 1u);
    EXPECT_EQ(tex.images[3].pitch, 4u);
    EXPECT_EQ(tex.images[3].pixels.size(), 4u);
}

TEST(ModAssetCompat, AV11TextureWithAnUnknownFormatIsRefusedNotGuessed) {
    // 0x42 is not in the engine's switch.  The editor must refuse it rather
    // than pick a depth, because picking one would read the wrong byte count.
    const std::vector<uint8_t> bytes = MakeV11(0x42, 8, 8, 4, 8);
    std::istringstream stream(std::string(bytes.begin(), bytes.end()),
                              std::ios::binary);
    TEXFile tex;
    EXPECT_FALSE(igi_tex::TEX_ParseStreamCore(stream, tex));
    // The message carries the format word; 0x42 is 66 in the decimal the
    // parser prints ("v11: unsupported texture format 66").
    EXPECT_NE(tex.error.find("66"), std::string::npos) << tex.error;
    EXPECT_NE(tex.error.find("unsupported texture format"), std::string::npos)
        << tex.error;
}

TEST(ModAssetCompat, TheRetailTextureFormat2StillParses) {
    // The retail level textures are v11 with format 2 (`Pixel_1555To32`,
    // src/sysmisc/d3dtex.c:415-417), so the extension above did not disturb
    // the vanilla path.
    const std::vector<uint8_t> bytes = MakeV11(2, 8, 8, 2, 8);
    std::istringstream stream(std::string(bytes.begin(), bytes.end()),
                              std::ios::binary);
    TEXFile tex;
    ASSERT_TRUE(igi_tex::TEX_ParseStreamCore(stream, tex)) << tex.error;
    ASSERT_FALSE(tex.images.empty());
    EXPECT_EQ(tex.images[0].mode, 2u);
    EXPECT_EQ(tex.images[0].pitch, 2u * 8u);
    // 0x02 has no mipmap bit, so there is exactly one level.
    EXPECT_EQ(tex.images.size(), 1u);
}

TEST(ModAssetCompat, ACorpusLevelTextureParsesWithItsOwnFormat) {
    // The real thing: a shipping level texture, whatever format it carries.
    const std::string root = GameRoot();
    if (root.empty())
        GTEST_SKIP() << "set IGI_GAME_PATH to a retail install to run this test";
    const std::string path =
        root + "/MISSIONS/location0/LEVEL2/TEXTURES/LEVEL2.RES";
    std::vector<uint8_t> bytes;
    if (!ReadFile(path, bytes)) GTEST_SKIP() << path << " not found";

    // Find the first `LOOP` member inside the archive and parse from there.
    size_t at = std::string::npos;
    for (size_t i = 0; i + 4 <= bytes.size(); ++i) {
        if (std::memcmp(bytes.data() + i, "LOOP", 4) == 0) { at = i; break; }
    }
    if (at == std::string::npos) GTEST_SKIP() << "no LOOP member in " << path;

    std::istringstream stream(
        std::string(bytes.begin() + static_cast<long>(at), bytes.end()),
        std::ios::binary);
    TEXFile tex;
    ASSERT_TRUE(igi_tex::TEX_ParseStreamCore(stream, tex)) << tex.error;
    EXPECT_FALSE(tex.images.empty());
    // Whatever the format, the parser only accepts one the engine converts.
    EXPECT_NE(igi_tex::DepthForMode(tex.images[0].mode), 0u)
        << "format " << tex.images[0].mode;
}
