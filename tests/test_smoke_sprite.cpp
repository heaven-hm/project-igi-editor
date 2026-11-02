// test_smoke_sprite.cpp - the retail sprite a Smoke particle is drawn with
// (issue #43, item 1: "the .spr texels are NOT loaded" was a named divergence).
//
// Two kinds of proof, both against the engine's own code and the shipped data:
//
//   1. CORPUS. The four sprites Smoke_Init interns (src/fxobj/smoke.c:189-192)
//      are read out of <IGI>/common/sprites/sprites.res -- the pack
//      Game_LoadSprites mounts (src/game/game.c:536-547) -- and decoded with
//      SmokeSpriteParse, i.e. Sprite_Load HOOK 0x4B6720 plus
//      D3DTex_DownloadTexture's format dispatch HOOK 0x50AD90. Every expected
//      texel below was computed from the file bytes with the engine's formulas
//      (Pixel_Copy32 0x517F00, Pixel_1555To32 0x517C90), NOT recorded from this
//      implementation's output.
//
//   2. SYNTHETIC. Hand-built LOOP v9 images pin each conversion arm and the two
//      failure modes the engine treats as fatal, so the decoder's behaviour is
//      checked even on a host with no installed game.
//
// The char-map checks pin particle.c:271-272 (`last = map->cols - 1`) and
// particle.c:204-205 (`frame = cells[p->frame] - 1`), which is what makes the
// shipped smoke1/smoke2/fireball draw frame 0 forever and blood1.spr animate
// one frame per tick.
//
// The archive is walked IN THIS FILE with the header-only chunk helpers
// (source/renderer/res_chunks.h) rather than through RES_Extract, because
// RES_Extract's translation unit resolves the game root through
// Utils::GetIGIRootPath(), which the host lane cannot link. The walk is the
// engine's (Chunk_FirstChild 0x4B7490 / Chunk_Next 0x4B7470 / Chunk_Data
// 0x4B74D0 arithmetic), the same one tests/test_pause_menu_input.cpp uses.
//
// Corpus: $IGI_GAME_PATH (scripts/run-host-tests.sh exports it, default
// /Users/haseeb-mir/Downloads/IGI). The corpus tests SKIP when it is absent;
// every synthetic test always runs.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "renderer/res_chunks.h"
#include "runtime/smoke.h"
#include "runtime/smoke_sim.h"
#include "runtime/smoke_sprite.h"

namespace {

// ── corpus access ───────────────────────────────────────────────────────────

std::filesystem::path CorpusRoot() {
    if (const char* env = std::getenv("IGI_GAME_PATH"); env != nullptr && *env != '\0') {
        return std::filesystem::path(env);
    }
    return std::filesystem::path("/Users/haseeb-mir/Downloads/IGI");
}

std::filesystem::path SpritePack() {
    // igi_res::LocalDevicePath(root, "LOCAL:common/sprites/sprites.res") without
    // the Utils dependency: the LOCAL device strips the prefix and the CRT opens
    // the rest (LocalDevice_Open, src/sysmisc/localdev.c:162-205).
    return CorpusRoot() / "common" / "sprites" / "sprites.res";
}

bool CorpusAvailable() {
    std::error_code ec;
    return std::filesystem::is_regular_file(SpritePack(), ec);
}

std::vector<uint8_t> ReadWholeFile(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f),
                                std::istreambuf_iterator<char>());
}

igi_res::ChunkHeader HeaderAt(const std::vector<uint8_t>& b, std::size_t off) {
    igi_res::ChunkHeader h;
    if (off + 16 > b.size()) return h;
    auto u32 = [&](std::size_t o) {
        return static_cast<uint32_t>(b[o]) | (static_cast<uint32_t>(b[o + 1]) << 8) |
               (static_cast<uint32_t>(b[o + 2]) << 16) |
               (static_cast<uint32_t>(b[o + 3]) << 24);
    };
    h.id = u32(off);
    h.size = u32(off + 4);
    h.align = u32(off + 8);
    h.next = u32(off + 12);
    h.form = (off + 20 <= b.size()) ? u32(off + 16) : 0;
    return h;
}

// The BODY bytes of one resource, matched by the engine's resource identity
// (upper-cased, '/' separated -- Resource_Create 0x4B59F0).
std::vector<uint8_t> FindMember(const std::vector<uint8_t>& b, const std::string& wanted) {
    const std::string want = igi_res::NormalizeResourceName(wanted);
    std::string pending;
    std::size_t off = 20;  // past "ILFF" + size/align/next + "IRES"
    std::size_t guard = 0;
    while (off + 16 <= b.size() && guard++ < 100000) {
        const igi_res::ChunkHeader h = HeaderAt(b, off);
        if (h.id == igi_res::kChunkIres) {
            off += 4;  // the form-type word that opens an ILFF body
            continue;
        }
        const std::size_t body = off + 16;
        if (h.id == igi_res::kChunkName && body + h.size <= b.size()) {
            pending.assign(reinterpret_cast<const char*>(b.data() + body), h.size);
            const std::size_t nul = pending.find('\0');
            if (nul != std::string::npos) pending.resize(nul);
        } else if ((h.id == igi_res::kChunkBody || h.id == igi_res::kChunkCstr ||
                    h.id == igi_res::kChunkPath) &&
                   body + h.size <= b.size()) {
            if (igi_res::NormalizeResourceName(pending) == want) {
                return std::vector<uint8_t>(b.begin() + static_cast<std::ptrdiff_t>(body),
                                            b.begin() + static_cast<std::ptrdiff_t>(body + h.size));
            }
        }
        if (h.next != 0 && off + h.next < b.size()) {
            off += h.next;
        } else if (h.size >= 16 && off + 16 + h.size <= b.size()) {
            off += 16 + h.size;
        } else {
            break;
        }
    }
    return {};
}

// The whole pack, parsed once per test binary.
const std::vector<uint8_t>& PackBytes() {
    static const std::vector<uint8_t> bytes = ReadWholeFile(SpritePack());
    return bytes;
}

// One shipped sprite decoded, or an empty (invalid) one when the corpus is
// missing.
igi::SmokeSprite LoadShipped(const char* file) {
    igi::SmokeSprite sprite;
    const std::vector<uint8_t>& pack = PackBytes();
    if (pack.empty()) return sprite;
    const std::vector<uint8_t> bytes = FindMember(pack, igi::SmokeSpriteMemberName(file));
    if (bytes.empty()) return sprite;
    igi::SmokeSpriteParse(bytes.data(), bytes.size(), file, &sprite);
    return sprite;
}

// The RGBA at (x, y) of a frame, asserting the frame is big enough.
std::array<uint8_t, 4> Texel(const igi::SmokeSpriteFrame& f, int x, int y) {
    std::array<uint8_t, 4> out{0, 0, 0, 0};
    if (x < 0 || y < 0 || x >= f.width || y >= f.height) return out;
    const std::size_t o = (static_cast<std::size_t>(y) * f.width + x) * 4u;
    for (int i = 0; i < 4; ++i) out[i] = f.rgba[o + i];
    return out;
}

// ── synthetic LOOP v9 builder ───────────────────────────────────────────────

// Builds a one-frame LOOP v9 sprite whose single frame is `texels` bytes with
// the given format/pitch/width/height, optionally followed by a char map of
// `cols`x1 cells. Layouts: LoopHeader 52 bytes (src/res/sprite.c:113-127),
// LoopFrame9 32 bytes (:141-150), so the frame record is at 52 and the texels
// start at 84 unless `char_map` pushes them later; the char map goes after the
// texels like the shipped files lay it out.
struct SyntheticSprite {
    std::vector<uint8_t> bytes;
};

void PutU16(std::vector<uint8_t>& b, std::size_t off, uint16_t v) {
    b[off + 0] = static_cast<uint8_t>(v & 0xFF);
    b[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

void PutU32(std::vector<uint8_t>& b, std::size_t off, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[off + i] = static_cast<uint8_t>((v >> (8 * i)) & 0xFF);
}

SyntheticSprite MakeSprite(std::uint32_t format, std::uint32_t pitch, std::uint32_t w,
                           std::uint32_t h, const std::vector<std::uint8_t>& frame_texels,
                           const std::vector<std::uint8_t>& palette_bgra = {},
                           const std::vector<std::uint8_t>& cells = {}, int nframes = 1) {
    const std::size_t kHeader = 52;
    const std::size_t kFrame9 = 32;
    const std::size_t records = kHeader + kFrame9 * static_cast<std::size_t>(nframes);
    const std::size_t palette_at = palette_bgra.empty() ? 0 : records + frame_texels.size();
    const std::size_t charmap_at =
        cells.empty() ? 0 : records + frame_texels.size() + (palette_bgra.empty() ? 0u : 0x410u);
    const std::size_t total = records + frame_texels.size() +
                              (palette_bgra.empty() ? 0u : 0x410u) +
                              (cells.empty() ? 0u : 36u + cells.size() * 16u);
    SyntheticSprite s;
    s.bytes.assign(total, 0u);
    std::vector<uint8_t>& b = s.bytes;

    std::memcpy(b.data(), "LOOP", 4);
    PutU32(b, 4, 9);                 // version
    PutU16(b, 16, 0);                // unk10
    PutU16(b, 18, 5);                // unk12 (passed to Texture_Create)
    PutU32(b, 24, static_cast<uint32_t>(palette_at));
    PutU32(b, 28, static_cast<uint32_t>(charmap_at));
    PutU32(b, 32, static_cast<uint32_t>(nframes));
    PutU32(b, 40, w);                // header w
    PutU32(b, 44, h);                // header h
    PutU32(b, 48, format);           // header format

    // LoopFrame9 (src/res/sprite.c:141-150): pixels is an ABSOLUTE offset.
    const std::size_t per_frame = frame_texels.size() / static_cast<std::size_t>(nframes);
    for (int i = 0; i < nframes; ++i) {
        const std::size_t at = kHeader + kFrame9 * static_cast<std::size_t>(i);
        PutU32(b, at + 0, static_cast<uint32_t>(records + per_frame * static_cast<std::size_t>(i)));
        PutU32(b, at + 4, format);
        PutU16(b, at + 8, static_cast<uint16_t>(pitch));
        PutU16(b, at + 10, static_cast<uint16_t>(w));
        PutU16(b, at + 12, static_cast<uint16_t>(h));
    }
    std::memcpy(b.data() + records, frame_texels.data(), frame_texels.size());

    if (!palette_bgra.empty()) {
        // The word at palette + 0xC is what Loop_Palette hands back as the
        // texture format (src/res/sprite.c:145-151); the 256 BGRA entries start
        // at +0x10 (D3DTex_AllocPalette 0x50A670 copies exactly 0x400 bytes).
        PutU32(b, palette_at + 0x0C, 1);   // format 1: 8 bit palettised
        std::memcpy(b.data() + palette_at + 0x10, palette_bgra.data(),
                    std::min<std::size_t>(palette_bgra.size(), 0x400u));
    }
    if (!cells.empty()) {
        // CharMap: 4 ints, cols, rows, 3 ints, then 16-byte cells whose first
        // byte is a 1-BASED frame number (Grid_LoadCharMap 0x4B75C0).
        PutU32(b, charmap_at + 16, static_cast<uint32_t>(cells.size()));
        PutU32(b, charmap_at + 20, 1);
        for (std::size_t i = 0; i < cells.size(); ++i) {
            b[charmap_at + 36 + i * 16] = cells[i];
        }
    }
    return s;
}

// The single-frame case, which is what three of the four shipped sprites are.
SyntheticSprite MakeOneFrame(std::uint32_t format, std::uint32_t pitch, std::uint32_t w,
                             std::uint32_t h, const std::vector<std::uint8_t>& texels,
                             const std::vector<std::uint8_t>& palette_bgra = {},
                             const std::vector<std::uint8_t>& cells = {}) {
    return MakeSprite(format, pitch, w, h, texels, palette_bgra, cells, 1);
}

// ── simulation helper: one emitter with the given sprite slot ───────────────

igi::SmokeParams Bloodish(int sprite_index, double rate, int count) {
    igi::SmokeParams p;
    p.count = count;
    p.life_time = 4.0f;   // delay 120 ticks
    p.fade_time = 4.0f;
    p.intensity = 1.0f;
    p.generate_factor = rate;
    p.radius = 0.0f;
    p.spread = 0.0f;
    p.sprite_index = sprite_index;
    return p;
}

}  // namespace

// ─── the names the engine resolves ──────────────────────────────────────────

// Game_LoadSprites (HOOK 0x4161D0, src/game/game.c:536-547) builds
// "LOCAL:common/sprites/" + the name Game_AddSprite registered (smoke.c:189-192).
TEST(SmokeSprite, MemberNamesAreTheEngines) {
    EXPECT_EQ(igi::SmokeSpriteMemberName("smoke1.spr"), "LOCAL:common/sprites/smoke1.spr");
    EXPECT_EQ(igi::SmokeSpriteMemberName("blood1.spr"), "LOCAL:common/sprites/blood1.spr");
    EXPECT_EQ(std::string(igi::kSmokeSpritePackName), "LOCAL:common/sprites/sprites.res");
    // SmokeSpriteFile's four names are exactly the four slots.
    EXPECT_STREQ(igi::SmokeSpriteFile(0), "fireball.spr");
    EXPECT_STREQ(igi::SmokeSpriteFile(1), "blood1.spr");
    EXPECT_STREQ(igi::SmokeSpriteFile(2), "smoke2.spr");
    EXPECT_STREQ(igi::SmokeSpriteFile(3), "smoke1.spr");
    EXPECT_STREQ(igi::SmokeSpriteFile(99), "smoke1.spr");  // the default arm
}

// ─── the shipped sprites ───────────────────────────────────────────────────

TEST(SmokeSprite, Smoke1IsTheShipped128Sprite) {
    if (!CorpusAvailable()) GTEST_SKIP() << "no retail install at " << CorpusRoot();
    const igi::SmokeSprite s = LoadShipped("smoke1.spr");
    ASSERT_TRUE(s.valid) << s.error;
    EXPECT_EQ(s.version, 9u);
    EXPECT_EQ(s.format, 3u);          // 32 bit, D3DTex DownloadTexture case 3/4
    ASSERT_EQ(s.frames.size(), 1u);
    EXPECT_EQ(s.frames[0].width, 128);
    EXPECT_EQ(s.frames[0].height, 128);
    EXPECT_EQ(s.pitch, 128 * 4);

    // format 3 is Pixel_Copy32 (0x517F00): the file's dword is B,G,R,A in
    // memory, so the decoded texel is the same four bytes with R and B swapped
    // into RGBA order.  smoke1.spr at (64,64) is 94 94 94 bd.
    const auto centre = Texel(s.frames[0], 64, 64);
    EXPECT_EQ(centre[0], 148u);
    EXPECT_EQ(centre[1], 148u);
    EXPECT_EQ(centre[2], 148u);
    EXPECT_EQ(centre[3], 189u);
    // The corner is 5f 5f 5f 00: grey texels, fully transparent edge. That
    // baked-in falloff is exactly what the procedural blob was standing in for.
    const auto corner = Texel(s.frames[0], 0, 0);
    EXPECT_EQ(corner[0], 95u);
    EXPECT_EQ(corner[1], 95u);
    EXPECT_EQ(corner[2], 95u);
    EXPECT_EQ(corner[3], 0u);
}

TEST(SmokeSprite, FireballAndSmoke2UseArgb1555) {
    if (!CorpusAvailable()) GTEST_SKIP() << "no retail install at " << CorpusRoot();
    const igi::SmokeSprite fireball = LoadShipped("fireball.spr");
    ASSERT_TRUE(fireball.valid) << fireball.error;
    EXPECT_EQ(fireball.format, 2u);   // Pixel_1555To32 0x517C90
    ASSERT_EQ(fireball.frames.size(), 1u);
    EXPECT_EQ(fireball.frames[0].width, 32);
    EXPECT_EQ(fireball.frames[0].height, 32);
    // ARGB1555: R = (p>>7)&0xF8, G = (p>>2)&0xF8, B = (p&0x1F)<<3, A = 0x8000.
    // The measured centre word decodes to an orange-white core.
    const auto centre = Texel(fireball.frames[0], 16, 16);
    EXPECT_EQ(centre[0], 232u);
    EXPECT_EQ(centre[1], 232u);
    EXPECT_EQ(centre[2], 128u);
    EXPECT_EQ(centre[3], 255u);

    const igi::SmokeSprite smoke2 = LoadShipped("smoke2.spr");
    ASSERT_TRUE(smoke2.valid) << smoke2.error;
    EXPECT_EQ(smoke2.format, 2u);
    ASSERT_EQ(smoke2.frames.size(), 1u);
    EXPECT_EQ(smoke2.frames[0].width, 128);
    EXPECT_EQ(smoke2.frames[0].height, 128);
    const auto c2 = Texel(smoke2.frames[0], 64, 64);
    EXPECT_EQ(c2[0], 184u);
    EXPECT_EQ(c2[1], 184u);
    EXPECT_EQ(c2[2], 184u);
    EXPECT_EQ(c2[3], 255u);
    const auto corner2 = Texel(smoke2.frames[0], 0, 0);
    EXPECT_EQ(corner2[3], 0u);        // 1555 alpha bit clear at the edge
}

// blood1.spr is the only one of the four with more than one frame, and its
// frames fade out: 255, 255, 250, 224, 17, 1 at the centre.
TEST(SmokeSprite, Blood1IsTheSevenFrameSprite) {
    if (!CorpusAvailable()) GTEST_SKIP() << "no retail install at " << CorpusRoot();
    const igi::SmokeSprite s = LoadShipped("blood1.spr");
    ASSERT_TRUE(s.valid) << s.error;
    EXPECT_EQ(s.version, 9u);
    EXPECT_EQ(s.format, 3u);
    ASSERT_EQ(s.frames.size(), 7u);
    for (const igi::SmokeSpriteFrame& f : s.frames) {
        EXPECT_EQ(f.width, 32);
        EXPECT_EQ(f.height, 32);
        ASSERT_EQ(f.rgba.size(), 32u * 32u * 4u);
    }
    EXPECT_EQ(Texel(s.frames[0], 16, 16)[3], 255u);
    EXPECT_EQ(Texel(s.frames[3], 16, 16)[3], 250u);
    EXPECT_EQ(Texel(s.frames[5], 16, 16)[3], 17u);
    EXPECT_EQ(Texel(s.frames[6], 16, 16)[3], 1u);
    // Blood red: byte2 of the file is R, so RGBA = (0x73, 0x19, 0x17, 0xff).
    const auto c = Texel(s.frames[0], 16, 16);
    EXPECT_EQ(c[0], 115u);
    EXPECT_EQ(c[1], 25u);
    EXPECT_EQ(c[2], 23u);
}

// The char map is what decides whether a particle's FRAME advances
// (Particle2D_Run's `last = map->cols - 1`, particle.c:271-272) and which frame
// it draws (Particle2D_Draw, particle.c:204-205). Every shipped smoke sprite
// carries one, because the header's `charmap` word is non-zero.
TEST(SmokeSprite, CharmapColumnsAreTheEnginesFrameCounts) {
    if (!CorpusAvailable()) GTEST_SKIP() << "no retail install at " << CorpusRoot();
    struct Expect {
        const char* file;
        int cols;
        int last;
    };
    const Expect expected[] = {
        {"fireball.spr", 1, 0},
        {"blood1.spr", 7, 6},
        {"smoke2.spr", 1, 0},
        {"smoke1.spr", 1, 0},
    };
    for (const Expect& e : expected) {
        const igi::SmokeSprite s = LoadShipped(e.file);
        ASSERT_TRUE(s.valid) << e.file << ": " << s.error;
        EXPECT_TRUE(s.Animated()) << e.file;      // sprite->map != NULL
        EXPECT_EQ(s.charmap_cols, e.cols) << e.file;
        EXPECT_EQ(s.charmap_rows, 1) << e.file;
        EXPECT_EQ(static_cast<int>(s.charmap_cells.size()), e.cols) << e.file;
        EXPECT_EQ(s.LastFrame(), e.last) << e.file;
        // The cells are 1-based frame numbers, so cell i is frame i (a 1-column
        // map always yields frame 0 through FrameForParticle(0)).
        for (int i = 0; i < e.cols; ++i) {
            EXPECT_EQ(static_cast<int>(s.charmap_cells[static_cast<std::size_t>(i)]), i + 1)
                << e.file << " cell " << i;
            EXPECT_EQ(s.FrameForParticle(i), i) << e.file << " particle frame " << i;
        }
        // The engine's frame never runs past the last frame of the sprite.
        EXPECT_LT(s.FrameForParticle(e.cols), static_cast<int>(s.frames.size()));
    }
}

// ─── synthetic: every conversion arm ───────────────────────────────────────

// Pixel_1555To32 0x517C90 (src/terrdraw/pixel.c:59-83).
TEST(SmokeSprite, SyntheticArgb1555Conversion) {
    // 0x8000 | (31 << 10) | (0 << 5) | 1  =  0xF801: A set, R5 = 31, G5 = 0, B5 = 1
    //   R = (0xF801 >> 7) & 0xF8 = 0xF0 = 240
    //   G = (0xF801 >> 2) & 0xF8 = 0x00 = 0
    //   B = (0xF801 & 0x1F) << 3 = 0x08 = 8
    const std::vector<uint8_t> texels = {0x01, 0xF8};   // little endian 0xF801
    const SyntheticSprite s = MakeOneFrame(2, 2, 1, 1, texels);
    igi::SmokeSprite out;
    ASSERT_TRUE(igi::SmokeSpriteParse(s.bytes.data(), s.bytes.size(), "synth1555", &out))
        << out.error;
    ASSERT_EQ(out.frames.size(), 1u);
    const auto px = Texel(out.frames[0], 0, 0);
    EXPECT_EQ(px[0], 240u);
    EXPECT_EQ(px[1], 0u);
    EXPECT_EQ(px[2], 8u);
    EXPECT_EQ(px[3], 255u);
    // The alpha bit is bit 15: clear it and the texel is fully transparent.
    const std::vector<uint8_t> clear = {0x01, 0x78};    // 0x7801
    const SyntheticSprite t = MakeOneFrame(2, 2, 1, 1, clear);
    igi::SmokeSprite out2;
    ASSERT_TRUE(igi::SmokeSpriteParse(t.bytes.data(), t.bytes.size(), "synth1555b", &out2));
    EXPECT_EQ(Texel(out2.frames[0], 0, 0)[3], 0u);
}

// Pixel_Copy32 0x517F00 (src/terrdraw/pixel.c:112-124): the dword is B,G,R,A.
TEST(SmokeSprite, Synthetic32BitConversionIsBgra) {
    const std::vector<uint8_t> texels = {1, 2, 3, 4};   // B=1 G=2 R=3 A=4
    const SyntheticSprite s = MakeOneFrame(3, 4, 1, 1, texels);
    igi::SmokeSprite out;
    ASSERT_TRUE(igi::SmokeSpriteParse(s.bytes.data(), s.bytes.size(), "synth32", &out))
        << out.error;
    const auto px = Texel(out.frames[0], 0, 0);
    EXPECT_EQ(px[0], 3u);   // R
    EXPECT_EQ(px[1], 2u);   // G
    EXPECT_EQ(px[2], 1u);   // B
    EXPECT_EQ(px[3], 4u);   // A
}

// Pixel_8To32 0x517FD0 (src/terrdraw/pixel.c:127-158): the palette entry is
// B,G,R,X and the alpha is `index != 0`, NOT the palette's fourth byte.
TEST(SmokeSprite, SyntheticPalettisedConversion) {
    std::vector<uint8_t> palette(0x400, 0u);
    // entry 0 = (B 1, G 2, R 3, X 4), entry 1 = (B 10, G 20, R 30, X 77)
    palette[0] = 1; palette[1] = 2; palette[2] = 3; palette[3] = 4;
    palette[4] = 10; palette[5] = 20; palette[6] = 30; palette[7] = 77;
    const std::vector<uint8_t> texels = {0, 1};         // two pixels, indices 0 and 1
    const SyntheticSprite s = MakeOneFrame(1, 2, 2, 1, texels, palette);
    igi::SmokeSprite out;
    ASSERT_TRUE(igi::SmokeSpriteParse(s.bytes.data(), s.bytes.size(), "synth8", &out))
        << out.error;
    // Index 0 is the transparent colour but keeps the palette's RGB.
    const auto p0 = Texel(out.frames[0], 0, 0);
    EXPECT_EQ(p0[0], 3u);
    EXPECT_EQ(p0[1], 2u);
    EXPECT_EQ(p0[2], 1u);
    EXPECT_EQ(p0[3], 0u);
    // Index 1 is opaque; the entry's X byte (77) is not used as alpha.
    const auto p1 = Texel(out.frames[0], 1, 0);
    EXPECT_EQ(p1[0], 30u);
    EXPECT_EQ(p1[1], 20u);
    EXPECT_EQ(p1[2], 10u);
    EXPECT_EQ(p1[3], 255u);
}

// Grid_LoadCharMap 0x4B75C0: cells are 1-based, and `cols` is the frame count.
TEST(SmokeSprite, SyntheticCharmapIsOneBased) {
    // Two frames of 2x1 32-bit texels, and a 2x1 char map whose cells name
    // frame 2 then frame 1 (the cells are 1-based).
    const std::vector<uint8_t> texels = {9, 8, 7, 6, 5, 4, 3, 2,
                                         1, 2, 3, 4, 5, 6, 7, 8};
    const std::vector<uint8_t> cells = {2, 1};
    const SyntheticSprite s = MakeSprite(3, 8, 2, 1, texels, {}, cells, 2);
    igi::SmokeSprite out;
    ASSERT_TRUE(igi::SmokeSpriteParse(s.bytes.data(), s.bytes.size(), "synthcmap", &out))
        << out.error;
    ASSERT_TRUE(out.Animated());
    EXPECT_EQ(out.charmap_cols, 2);
    EXPECT_EQ(out.charmap_rows, 1);
    EXPECT_EQ(out.LastFrame(), 1);
    EXPECT_EQ(out.FrameForParticle(0), 1);   // cell 2 -> frame index 1
    EXPECT_EQ(out.FrameForParticle(1), 0);   // cell 1 -> frame index 0
}

// ─── synthetic: the engine's fatal cases ───────────────────────────────────

// D3DTex_DownloadTexture's default arm is "Unsupported texture format" and then
// for(;;) (src/sysmisc/d3dtex.c:435-438). The decoder reports it.
TEST(SmokeSprite, UnsupportedFormatIsAnError) {
    const std::vector<uint8_t> texels = {0, 0, 0, 0};
    const SyntheticSprite s = MakeOneFrame(6, 4, 1, 1, texels);
    igi::SmokeSprite out;
    EXPECT_FALSE(igi::SmokeSpriteParse(s.bytes.data(), s.bytes.size(), "synthbad", &out));
    EXPECT_FALSE(out.valid);
    EXPECT_NE(out.error.find("unsupported texture format 6"), std::string::npos) << out.error;
}

TEST(SmokeSprite, TruncatedFileIsAnError) {
    // A header that claims a 4x4 32-bit frame the file does not carry.
    const std::vector<uint8_t> texels = {0, 0, 0, 0};
    SyntheticSprite s = MakeOneFrame(3, 16, 4, 4, texels);
    s.bytes.resize(84 + 4);   // keep the header and the frame record, drop texels
    igi::SmokeSprite out;
    EXPECT_FALSE(igi::SmokeSpriteParse(s.bytes.data(), s.bytes.size(), "synthtrunc", &out));
    EXPECT_FALSE(out.valid);
    EXPECT_FALSE(out.error.empty());

    igi::SmokeSprite tiny;
    const std::vector<uint8_t> junk = {'L', 'O', 'O', 'P'};
    EXPECT_FALSE(igi::SmokeSpriteParse(junk.data(), junk.size(), "tiny", &tiny));
    EXPECT_FALSE(tiny.valid);
    EXPECT_FALSE(igi::SmokeSpriteParse(nullptr, 0, "null", &tiny));
}

// ─── the simulation uses the sprite's frame count ──────────────────────────

// Particle2D_New marks a group `animated` when the sprite has a char map, and
// Particle2D_Run takes `last = map->cols - 1` (particle.c:519-520, :271-272).
// The simulation must therefore advance a blood particle's frame once per tick
// (Smoke passes frameDelay 0, smoke.c:403) and leave a smoke1/smoke2/fireball
// particle's frame at 0, whose shipped maps have one column.
TEST(SmokeSprite, SpriteColumnsDriveTheParticleFrame) {
    igi::SmokeSimulation sim;
    const int cols[igi::kSmokeSpriteCount] = {1, 7, 1, 1};   // fireball, blood1, smoke2, smoke1
    sim.SetSpriteColumns(cols);
    EXPECT_EQ(sim.SpriteColumns(0), 1);
    EXPECT_EQ(sim.SpriteColumns(1), 7);
    EXPECT_EQ(sim.SpriteColumns(3), 1);

    sim.SetEmitters({Bloodish(1, 30.0, 8), Bloodish(3, 30.0, 8)});
    ASSERT_EQ(sim.EmitterCount(), 2u);
    EXPECT_EQ(sim.Emitter(0).group.last_frame, 6);   // cols - 1
    EXPECT_EQ(sim.Emitter(1).group.last_frame, 0);   // cols - 1
    EXPECT_EQ(sim.Emitter(0).group.sprite_index, 1);
    EXPECT_EQ(sim.Emitter(1).group.sprite_index, 3);

    // One tick spawns the first particle (rate 30 = one per tick); seven more
    // ticks walk the blood particle's frame to the last one and leave the smoke
    // particle's at 0.
    for (int i = 0; i < 8; ++i) sim.Tick();
    ASSERT_GE(sim.ParticleCount(0), 1);
    ASSERT_GE(sim.ParticleCount(1), 1);
    EXPECT_EQ(sim.Emitter(0).group.particles[0].frame, 6);
    EXPECT_EQ(sim.Emitter(1).group.particles[0].frame, 0);

    // With no sprite loaded (columns all 0) nothing advances, which is the
    // engine's outcome for a sprite without a char map.
    igi::SmokeSimulation bare;
    bare.SetEmitters({Bloodish(1, 30.0, 8)});
    EXPECT_EQ(bare.Emitter(0).group.last_frame, 0);
    for (int i = 0; i < 8; ++i) bare.Tick();
    ASSERT_GE(bare.ParticleCount(0), 1);
    EXPECT_EQ(bare.Emitter(0).group.particles[0].frame, 0);
}

// ─── the colour the texels are modulated with ──────────────────────────────

// The quad's vertex colour is the group colour on the engine's SPRITE scale,
// not the 0..255 byte scale: QVertex_DrawQuadEx multiplies by
// gColorScale = 0.0078125 (1/128, `DATA(534AB0) const float gColorScale
// INIT(0.0078125f)`, src/res/qvertex.c:189) and D3DQVertex_Emit packs that
// through *255 (src/qvertex/d3d.c:415-421). The editor divided by 255, drawing
// every smoke quad at half the game's brightness.
TEST(SmokeSprite, QuadColourUsesTheEngineColourScale) {
    igi::SmokeParams params;                     // "Colour" (2,2,2) is the default
    float group_colour[3] = {0.0f, 0.0f, 0.0f};
    igi::SmokeGroupColour(params, group_colour);
    EXPECT_FLOAT_EQ(group_colour[0], 126.0f);    // 2 * kScale(63), smoke.c:398-400

    // Fade mode 0 -> flags 0x1080000 | 0x40000: the level IS the alpha
    // (particle.c:195-196) and the colour is the group's (divided by 128).
    float rgba[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    igi::SmokeParticleColour(group_colour, 0x10C0000u, 0.5f, rgba);
    EXPECT_FLOAT_EQ(rgba[0], 126.0f / 128.0f);
    EXPECT_FLOAT_EQ(rgba[1], 126.0f / 128.0f);
    EXPECT_FLOAT_EQ(rgba[2], 126.0f / 128.0f);
    EXPECT_FLOAT_EQ(rgba[3], 0.5f);
    // The two scales are far apart, so this pins which one is used.
    EXPECT_GT(rgba[0], 0.9f);
    EXPECT_LT(126.0f / 255.0f, 0.5f);

    // Fade modes 1 and 2 -> flags 0x1000008 / 0x1000010 | 0x40000: alpha is 1
    // and the LEVEL scales the colour (particle.c:198-202).
    igi::SmokeParticleColour(group_colour, 0x1040008u, 0.5f, rgba);
    EXPECT_FLOAT_EQ(rgba[0], 126.0f / 128.0f * 0.5f);
    EXPECT_FLOAT_EQ(rgba[3], 1.0f);
    igi::SmokeParticleColour(group_colour, 0x1040010u, 0.25f, rgba);
    EXPECT_FLOAT_EQ(rgba[2], 126.0f / 128.0f * 0.25f);
    EXPECT_FLOAT_EQ(rgba[3], 1.0f);

    // No flags at all: the engine leaves `alpha` at 0 (particle.c:110) and the
    // quad is invisible -- not a fallback, the engine's own outcome.
    igi::SmokeParticleColour(group_colour, 0u, 1.0f, rgba);
    EXPECT_FLOAT_EQ(rgba[3], 0.0f);
    // ... and the values stay unclamped, as the engine's float diffuse does.
    float hot[3] = {400.0f, 0.0f, 0.0f};
    igi::SmokeParticleColour(hot, 0x10C0000u, 1.0f, rgba);
    EXPECT_GT(rgba[0], 1.0f);
}
