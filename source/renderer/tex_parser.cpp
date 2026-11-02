/******************************************************************************
 * @file    source/renderer/tex_parser.cpp
 * @brief   LOOP texture/sprite parser core (no logging).
 *
 * Split out of tex_writer.cpp so the parse itself links WITHOUT `Logger`:
 * `Logger::Log` reads `Config::Get()` and `source/config.cpp` is not
 * host-portable, so a translation unit that both parses and logs cannot be used
 * by the host test lane.  The editor's own test suite needs the parser there to
 * prove the retail menu pointer's identity and size
 * (tests/test_pause_menu_input.cpp).
 *
 * The engine oracle and every field offset live with the code that uses them:
 *   Texture_LoadFile 0x4B24C0  src/file/texture.c:231-252   (v2, v11)
 *   Sprite_Load      0x4B6720  src/res/sprite.c:162-207     (v7, v9)
 * See source/renderer/tex_writer.cpp for the format table and the citations.
 *****************************************************************************/

#include "tex_parser.h"

#include <cstring>
#include <istream>

namespace igi_tex {
namespace {

// ---------------------------------------------------------------------------
// Format table — D3DTex_DownloadTexture 0x50AD10, src/sysmisc/d3dtex.c:411-432
// ---------------------------------------------------------------------------

// FMT_MIPMAPS — src/sysmisc/d3dtex.c:78
constexpr uint32_t kFmtMipmaps = 0x40;

template<typename T>
bool Rd(std::istream& f, T& v) {
    return static_cast<bool>(f.read(reinterpret_cast<char*>(&v), sizeof(T)));
}

bool RdBytes(std::istream& f, void* dst, std::size_t n) {
    return static_cast<bool>(
        f.read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(n)));
}

uint32_t RdU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint16_t RdU16LE(const uint8_t* p) {
    return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) |
                                 (static_cast<uint16_t>(p[1]) << 8));
}

// Loop06 skip helper — used by the sprite (v7/v9) path only.
// Loop06 header layout ('4s1I4H2I' = 24 bytes):
//   signature(4)  version(4)  _0(2) _1(2) _2(2) _3(2)  count_x(4)  count_y(4)
// Followed by count_x * count_y * Item06 (16 bytes each).
void SkipLoop06(std::istream& f) {
    f.seekg(16, std::ios::cur);
    uint32_t count_x = 0, count_y = 0;
    if (!Rd(f, count_x) || !Rd(f, count_y)) return;
    f.seekg(static_cast<std::streamoff>(count_x) * count_y * 16, std::ios::cur);
}

// ---------------------------------------------------------------------------
// v2 — Texture_LoadFile 0x4B24C0, src/file/texture.c:237-239
//   LoopHeader2: magic(0) version(4) unk08(H@8) pad0A(H@10) unk0C(I@12)
//                pitch(H@16) width(H@18) height(H@20) format(H@22)
//   Texture_Create(tex, a2, h2->pitch, h2->width, h2->height, h2->format,
//                  h2->unk08, 0, p + 0x18)
// ---------------------------------------------------------------------------
bool ParseV2(std::istream& f, TEXFile& tex) {
    f.seekg(0, std::ios::beg);

    uint8_t hdr[0x18];
    if (!RdBytes(f, hdr, sizeof(hdr))) { tex.error = "v2: short header"; return false; }

    const uint16_t pitch  = RdU16LE(hdr + 16);
    const uint16_t width  = RdU16LE(hdr + 18);
    const uint16_t height = RdU16LE(hdr + 20);
    const uint16_t mode   = RdU16LE(hdr + 22);

    const uint32_t depth = DepthForMode(mode);
    if (depth == 0) { tex.error = "v2: unsupported texture format " + std::to_string(mode); return false; }
    if (pitch == 0) { tex.error = "v2: zero pitch"; return false; }

    const std::size_t sz = static_cast<std::size_t>(pitch) * height;
    TEXImage img;
    img.width  = width;
    img.height = height;
    img.mode   = mode;
    img.pitch  = pitch;
    img.pixels.resize(sz);
    if (!RdBytes(f, img.pixels.data(), sz)) { tex.error = "v2: short bitmap"; return false; }
    tex.version = 2;
    tex.images.push_back(std::move(img));
    return true;
}

// ---------------------------------------------------------------------------
// v7/v9 sprites — Sprite_Load 0x4B6720, src/res/sprite.c:221-248.
// The editor uses this parser for .spr/.pic cursors; the pixel source is the
// per-frame record and the format is the header's `format` field when no
// palette is referenced. Item tables are skipped as the engine does not read
// them here.
// ---------------------------------------------------------------------------
bool ParseV7or9(std::istream& f, uint32_t ver, TEXFile& tex) {
    f.seekg(0, std::ios::beg);

    uint8_t hdr[52];
    if (!RdBytes(f, hdr, sizeof(hdr))) {
        tex.error = "v" + std::to_string(ver) + ": short header"; return false;
    }

    // LoopHeader (src/res/sprite.c:113-127): nframes(I@32) w(I@40) h(I@44)
    // format(I@48).
    const uint32_t count  = RdU32LE(hdr + 32);
    const uint32_t width  = RdU32LE(hdr + 40);
    const uint32_t height = RdU32LE(hdr + 44);
    const uint32_t mode   = RdU32LE(hdr + 48);

    if (count == 0) {
        tex.error = "v" + std::to_string(ver) + ": bad count=0";
        return false;
    }
    const uint32_t depth = DepthForMode(mode);
    if (depth == 0) {
        tex.error = "v" + std::to_string(ver) + ": unsupported texture format " +
                    std::to_string(mode);
        return false;
    }

    // Skip the frame table: Item07 = 40 bytes, Item09 = 32 bytes.
    const std::size_t itemBytes = static_cast<std::size_t>(count) * (ver == 7 ? 40u : 32u);
    f.seekg(static_cast<std::streamoff>(itemBytes), std::ios::cur);

    const std::size_t pitch   = static_cast<std::size_t>(width) * depth;
    const std::size_t bmpSize = pitch * height;
    tex.version = ver;
    for (uint32_t i = 0; i < count; ++i) {
        TEXImage img;
        img.width  = width;
        img.height = height;
        img.mode   = mode;
        img.pitch  = static_cast<uint32_t>(pitch);
        img.pixels.resize(bmpSize);
        if (!RdBytes(f, img.pixels.data(), bmpSize)) {
            tex.error = "v" + std::to_string(ver) + ": short bitmap " + std::to_string(i);
            return false;
        }
        tex.images.push_back(std::move(img));
    }

    SkipLoop06(f);  // trailing Loop06 — not read by this path
    return true;
}

// ---------------------------------------------------------------------------
// v11 — Texture_LoadFile 0x4B24C0, src/file/texture.c:240-243
//   LoopHeader11: magic(0) version(4) format(I@8) unk0C(I@12) unk10(I@16)
//                 unk14(H@20) width(H@22) height(H@24) unk1A(H@26)
//                 unk1C(H@28) bytesPerPixel(H@30)
//   Texture_Create(tex, a2, h11->bytesPerPixel * h11->unk1A, h11->width,
//                  h11->height, h11->format, h11->unk14, h11->unk0C, p + 0x20)
//   Mip chain: D3DTex_DownloadTexture 0x50AD10, src/sysmisc/d3dtex.c:432
//     do { ... data += h * pitch; w >>= 1; h >>= 1; pitch >>= 1; }
//     while ((format & FMT_MIPMAPS) && w > 0 && h > 0);
// ---------------------------------------------------------------------------
bool ParseV11(std::istream& f, TEXFile& tex) {
    f.seekg(0, std::ios::beg);

    uint8_t hdr[0x20];
    if (!RdBytes(f, hdr, sizeof(hdr))) { tex.error = "v11: short header"; return false; }

    const uint32_t format  = RdU32LE(hdr + 8);
    const uint16_t width   = RdU16LE(hdr + 22);
    const uint16_t height  = RdU16LE(hdr + 24);
    const uint16_t unk1A   = RdU16LE(hdr + 26);
    const uint16_t bpp     = RdU16LE(hdr + 30);

    const uint32_t depth = DepthForMode(format);
    if (depth == 0) { tex.error = "v11: unsupported texture format " + std::to_string(format); return false; }

    // pitch = bytesPerPixel * unk1A (src/file/texture.c:242)
    uint32_t pitch = static_cast<uint32_t>(bpp) * unk1A;
    if (pitch == 0) { tex.error = "v11: zero pitch"; return false; }

    tex.version = 11;

    uint32_t w = width;
    uint32_t h = height;
    do {
        if (w == 0 || h == 0) break;
        const std::size_t sz = static_cast<std::size_t>(pitch) * h;
        TEXImage img;
        img.width  = w;
        img.height = h;
        img.mode   = format;
        img.pitch  = pitch;
        img.pixels.resize(sz);
        if (!RdBytes(f, img.pixels.data(), sz)) { tex.error = "v11: short bitmap"; return false; }
        tex.images.push_back(std::move(img));
        w >>= 1;
        h >>= 1;
        pitch >>= 1;
    } while ((format & kFmtMipmaps) != 0 && w > 0 && h > 0);

    return true;
}

}  // namespace

uint32_t DepthForMode(uint32_t mode) {
    switch (mode) {
        case  1: return 1;   // Pixel_8To32   (palettised)
        case  2: return 2;   // Pixel_1555To32
        case  3: return 4;   // Pixel_Copy32
        case  4: return 4;   // Pixel_Copy32
        case  5: return 2;   // Pixel_565To32
        case 0x43: return 4; // Pixel_Copy32
        default: return 0;   // "Unsupported texture format"
    }
}

bool TEX_ParseStreamCore(std::istream& f, TEXFile& out) {
    out = TEXFile{};

    // Peek at signature + version (8 bytes)
    uint8_t sig[4];
    uint32_t version = 0;
    if (!RdBytes(f, sig, 4) || !Rd(f, version)) {
        out.error = "Too small to be a TEX file";
        return false;
    }
    if (std::memcmp(sig, "LOOP", 4) != 0) {
        out.error = "Not a LOOP file (bad signature)";
        return false;
    }
    // Texture_LoadFile accepts v2 and v11 only (src/file/texture.c:237-247);
    // v7/v9 are the sprite layout of Sprite_Load 0x4B6720 and are accepted
    // here because the editor also parses .spr/.pic cursors with this entry
    // point.
    if (version != 2 && version != 7 && version != 9 && version != 11) {
        out.error = "Unsupported LOOP version " + std::to_string(version);
        return false;
    }

    bool ok = false;
    switch (version) {
        case  2: ok = ParseV2(f, out);            break;
        case  7: ok = ParseV7or9(f, 7,  out);     break;
        case  9: ok = ParseV7or9(f, 9,  out);     break;
        case 11: ok = ParseV11(f, out);            break;
    }
    if (!ok) return false;
    out.version = version;
    out.valid = true;
    return true;
}

}  // namespace igi_tex
