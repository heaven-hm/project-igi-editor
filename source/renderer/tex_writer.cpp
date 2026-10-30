/******************************************************************************
 * @file    tex_writer.cpp
 * @brief   LOOP texture parser (engine Texture_LoadFile) and TGA export.
 *
 * Every field offset, size and pixel-format rule below is transcribed from
 * IGI-RE, the decompiled retail IGI.exe:
 *
 *   Texture_LoadFile            0x4B24C0  src/file/texture.c:231-252
 *     LoopHeader2  (v2)                   src/file/texture.c:11-21
 *     LoopHeader11 (v11)                  src/file/texture.c:24-36
 *   D3DTex_DownloadTexture      0x50AD10  src/sysmisc/d3dtex.c:411-432
 *     format switch + FMT_MIPMAPS mip walk (FMT_MIPMAPS 0x40 at :78)
 *   Pixel_1555To32              0x517C90  src/terrdraw/pixel.c:59-81
 *   Pixel_565To32               0x517DD0  src/terrdraw/pixel.c:83-105
 *   Pixel_Copy32                0x517F00  src/terrdraw/pixel.c:112-125
 *   Pixel_8To32                 0x517FD0  src/terrdraw/pixel.c:127-155
 *
 * There is no fallback for a version or format the engine does not accept: the
 * engine reports "Unsupported texture format" / "'%s' is not a valid Texture
 * file." and halts (src/sysmisc/d3dtex.c:429-431, src/file/texture.c:244-247).
 * The editor reports the same condition as a failed parse.
 *
 * The TGA writer is the editor's own export path (no engine equivalent); it
 * writes the engine's decoded levels and uses the engine's channel layout
 * (ARGB8888 little-endian = B,G,R,A byte order, alpha from bit 15 for 1555 and
 * 0xFF for 565).
 *****************************************************************************/

#include "tex_writer.h"
#include "../logger.h"

#include <fstream>
#include <cstring>
#include <filesystem>

// ---------------------------------------------------------------------------
// Format table — D3DTex_DownloadTexture 0x50AD10, src/sysmisc/d3dtex.c:411-432
// ---------------------------------------------------------------------------

namespace {

// FMT_MIPMAPS — src/sysmisc/d3dtex.c:78
constexpr uint32_t kFmtMipmaps = 0x40;

// Bytes per pixel of the level buffer, per the engine's format switch.
// 0 means the engine has no conversion for that format.
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

// Alpha bits of the format, for the TGA image descriptor: the engine's
// converters produce A = (p & 0x8000) ? 0xFF : 0 for 1555, A = 0xFF for 565,
// A = index ? 0xFF : 0 for the 8bpp path, and a copied alpha byte for 8888.
uint32_t AlphaForMode(uint32_t mode) {
    switch (mode) {
        case  1: return 1;
        case  2: return 1;
        case  3: return 8;
        case  4: return 8;
        case  5: return 0;
        case 0x43: return 8;
        default: return 0;
    }
}

// ---------------------------------------------------------------------------
// File helpers
// ---------------------------------------------------------------------------

template<typename T>
bool Rd(std::ifstream& f, T& v) {
    return static_cast<bool>(f.read(reinterpret_cast<char*>(&v), sizeof(T)));
}

bool RdBytes(std::ifstream& f, void* dst, std::size_t n) {
    return static_cast<bool>(f.read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(n)));
}

uint32_t RdU32LE(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint16_t RdU16LE(const uint8_t* p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

// Loop06 skip helper — used by the sprite (v7/v9) path only.
// Loop06 header layout ('4s1I4H2I' = 24 bytes):
//   signature(4)  version(4)  _0(2) _1(2) _2(2) _3(2)  count_x(4)  count_y(4)
// Followed by count_x * count_y * Item06 (16 bytes each).
void SkipLoop06(std::ifstream& f) {
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
bool ParseV2(std::ifstream& f, TEXFile& tex) {
    f.seekg(0, std::ios::beg);

    uint8_t hdr[0x18];
    if (!RdBytes(f, hdr, sizeof(hdr))) { tex.error = "v2: short header"; return false; }

    const uint16_t pitch  = RdU16LE(hdr + 16);
    const uint16_t width  = RdU16LE(hdr + 18);
    const uint16_t height = RdU16LE(hdr + 20);
    const uint16_t format = RdU16LE(hdr + 22);

    const uint32_t depth = DepthForMode(format);
    if (depth == 0) { tex.error = "v2: unsupported texture format " + std::to_string(format); return false; }

    // The engine passes the file's own pitch to the device and downloads
    // pitch * height bytes for the base level (d3dtex.c:406, :424-432).
    const std::size_t sz = static_cast<std::size_t>(pitch) * height;
    TEXImage img;
    img.width  = width;
    img.height = height;
    img.mode   = format;
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
bool ParseV7or9(std::ifstream& f, uint32_t ver, TEXFile& tex) {
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
        tex.error = "v" + std::to_string(ver) + ": unsupported texture format " + std::to_string(mode);
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
bool ParseV11(std::ifstream& f, TEXFile& tex) {
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
        if (!f.read(reinterpret_cast<char*>(img.pixels.data()),
                    static_cast<std::streamsize>(sz))) {
            tex.error = "v11: short level " + std::to_string(tex.images.size());
            return false;
        }
        tex.images.push_back(std::move(img));
        w >>= 1;
        h >>= 1;
        pitch >>= 1;
    } while ((format & kFmtMipmaps) && w > 0 && h > 0);

    return !tex.images.empty();
}

// ---------------------------------------------------------------------------
// TGA writer — the editor's own export path.
// ---------------------------------------------------------------------------

bool WriteTGA(const std::string& path, const TEXImage& img) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;

    const uint32_t depth = DepthForMode(img.mode);
    const uint32_t alpha = AlphaForMode(img.mode);
    if (depth == 0) return false;
    // The 8bpp path needs the palette the engine passes to Texture_Create
    // (d3dtex.c:412-414 dereferences d->palette); this parser has no palette.
    if (img.mode == 1) return false;

    const uint32_t rowBytes = img.width * depth;
    // Rows are pitch-strided in the engine's buffer; a pitch narrower than a
    // row cannot describe an image.
    if (img.pitch < rowBytes || img.pixels.size() < static_cast<std::size_t>(img.pitch) * img.height) {
        return false;
    }

    const uint8_t pixDepth = static_cast<uint8_t>(depth * 8);
    // ImageDescriptor: alpha-bits | mirrorX(0x10) | mirrorY(0x20)
    const uint8_t imgDesc = static_cast<uint8_t>(alpha | 0x10 | 0x20);

    uint8_t hdr[18] = {};
    hdr[2]  = 2;
    hdr[12] = static_cast<uint8_t>(img.width  & 0xFF);
    hdr[13] = static_cast<uint8_t>((img.width  >> 8) & 0xFF);
    hdr[14] = static_cast<uint8_t>(img.height & 0xFF);
    hdr[15] = static_cast<uint8_t>((img.height >> 8) & 0xFF);
    hdr[16] = pixDepth;
    hdr[17] = imgDesc;
    f.write(reinterpret_cast<const char*>(hdr), 18);

    // Row by row, using the engine's pitch.
    for (uint32_t y = 0; y < img.height; ++y) {
        f.write(reinterpret_cast<const char*>(img.pixels.data() + static_cast<std::size_t>(y) * img.pitch),
                static_cast<std::streamsize>(rowBytes));
    }

    // TGA 2.0 footer: ExtOffset(4) + DevOffset(4) + "TRUEVISION-XFILE.\0"(18) = 26 bytes
    const uint32_t zero = 0;
    f.write(reinterpret_cast<const char*>(&zero), 4);
    f.write(reinterpret_cast<const char*>(&zero), 4);
    f.write("TRUEVISION-XFILE.\0", 18);

    return f.good();
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

TEXFile TEX_Parse(const std::string& filepath) {
    TEXFile result;

    std::ifstream f(filepath, std::ios::binary);
    if (!f.is_open()) {
        result.error = "Cannot open: " + filepath;
        return result;
    }

    // Peek at signature + version (8 bytes)
    uint8_t sig[4]; uint32_t version;
    if (!RdBytes(f, sig, 4) || !Rd(f, version)) {
        result.error = "Too small to be a TEX file";
        return result;
    }
    if (std::memcmp(sig, "LOOP", 4) != 0) {
        result.error = "Not a LOOP file (bad signature)";
        return result;
    }
    // Texture_LoadFile accepts v2 and v11 only (src/file/texture.c:237-247);
    // v7/v9 are the sprite layout of Sprite_Load 0x4B6720 and are accepted
    // here because the editor also parses .spr/.pic cursors with this entry
    // point.
    if (version != 2 && version != 7 && version != 9 && version != 11) {
        result.error = "Unsupported LOOP version " + std::to_string(version);
        return result;
    }

    bool ok = false;
    switch (version) {
        case  2: ok = ParseV2(f, result);            break;
        case  7: ok = ParseV7or9(f, 7,  result);     break;
        case  9: ok = ParseV7or9(f, 9,  result);     break;
        case 11: ok = ParseV11(f, result);            break;
    }

    if (ok) {
        result.valid = true;
        Logger::Get().Log(LogLevel::INFO,
            "[TEX] Parsed '" + filepath + "' v" + std::to_string(version) +
            " images=" + std::to_string(result.images.size()));
    } else {
        Logger::Get().Log(LogLevel::ERR,
            "[TEX] Parse failed '" + filepath + "': " + result.error);
    }

    return result;
}

int TEX_ExportTGA(const TEXFile& tex, const std::string& filepath,
                  const std::string& outdir) {
    if (!tex.valid || tex.images.empty()) {
        Logger::Get().Log(LogLevel::WARNING,
                          "[TEX] ExportTGA called on invalid/empty TEXFile");
        return 0;
    }

    std::filesystem::path src(filepath);
    const std::string base = src.stem().string();
    int written = 0;

    for (std::size_t i = 0; i < tex.images.size(); ++i) {
        std::string name;
        if (tex.version == 11 && i > 0) {
            // mipmap levels: .%00.tga, .%01.tga, ...
            char buf[8]; std::snprintf(buf, sizeof(buf), ".%%%02zu", i);
            name = base + buf + ".tga";
        } else if ((tex.version == 7 || tex.version == 9) &&
                   tex.images.size() > 1) {
            // sub-images: .#00.tga, .#01.tga, ...
            char buf[8]; std::snprintf(buf, sizeof(buf), ".#%02zu", i);
            name = base + buf + ".tga";
        } else {
            name = base + ".tga";
        }

        const std::string outpath =
            (std::filesystem::path(outdir) / name).string();

        if (WriteTGA(outpath, tex.images[i])) {
            ++written;
            Logger::Get().Log(LogLevel::INFO, "[TEX] Exported: " + outpath);
        } else {
            Logger::Get().Log(LogLevel::ERR,  "[TEX] Failed: "   + outpath);
        }
    }

    return written;
}

bool TEX_WriteTGA(const std::string& path, const TEXImage& img) {
    return WriteTGA(path, img);
}
