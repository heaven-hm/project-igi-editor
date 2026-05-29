/******************************************************************************
 * @file    tex_parser.cpp
 * @brief   TEX (LOOP) texture parser — reimplemented from Python reference
 *          tools/dconv/format/tex.py  +  tools/dconv/plugins/tex/convert.py
 *
 * Key design points (matching the Python):
 *   • Raw pixel bytes are stored as-is; no channel conversion.
 *   • TGA PixelDepth = DEPTH[mode] * 8  (16 for RGB565, 32 for ARGB8888).
 *   • TGA ImageDescriptor = ALPHA[mode] | 0x10 | 0x20
 *       mirrorX(0x10) = left-to-right, mirrorY(0x20) = top-to-bottom.
 *   • TGA 2.0 footer ("TRUEVISION-XFILE.\0") is written.
 *****************************************************************************/

#include "tex_parser.h"
#include "../logger.h"

#include <fstream>
#include <cstring>
#include <filesystem>

// ---------------------------------------------------------------------------
// Constants (match Python DEPTH / ALPHA dicts)
// ---------------------------------------------------------------------------

namespace {

uint32_t DepthForMode(uint32_t mode) {
    switch (mode) {
        case  2: return 2;   // RGB565
        case  3: return 4;   // ARGB8888
        case 67: return 4;   // ARGB8888
        default: return 0;
    }
}

uint32_t AlphaForMode(uint32_t mode) {
    switch (mode) {
        case  2: return 1;
        case  3: return 8;
        case 67: return 8;
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

// ---------------------------------------------------------------------------
// Loop06 skip helper
// Loop06 header layout ('4s1I4H2I' = 24 bytes):
//   signature(4)  version(4)  _0(2) _1(2) _2(2) _3(2)  count_x(4)  count_y(4)
// Followed by count_x * count_y * Item06 (16 bytes each).
// ---------------------------------------------------------------------------
void SkipLoop06(std::ifstream& f) {
    // skip sig(4) + version(4) + four shorts(8) = 16 bytes to reach counts
    f.seekg(16, std::ios::cur);
    uint32_t count_x = 0, count_y = 0;
    if (!Rd(f, count_x) || !Rd(f, count_y)) return;
    f.seekg(static_cast<std::streamoff>(count_x) * count_y * 16, std::ios::cur);
}

// ---------------------------------------------------------------------------
// Version parsers
// ---------------------------------------------------------------------------

// Loop02 header ('4s3I4H' = 24 bytes):
//   signature(4)  version(4)  _1(4)  _2(4)
//   width_line(2)  width(2)  height(2)  mode(2)
// Bitmap: width * height * DEPTH[mode] raw bytes.
bool ParseV2(std::ifstream& f, TEXFile& tex) {
    f.seekg(0, std::ios::beg);

    uint8_t hdr[24];
    if (!RdBytes(f, hdr, 24)) { tex.error = "v2: short header"; return false; }

    // width_line is at offset 16 — ignored.  width is at 18, height at 20, mode at 22.
    uint16_t width, height, mode;
    std::memcpy(&width,  hdr + 18, 2);
    std::memcpy(&height, hdr + 20, 2);
    std::memcpy(&mode,   hdr + 22, 2);

    const uint32_t depth = DepthForMode(mode);
    if (depth == 0) { tex.error = "v2: unsupported mode " + std::to_string(mode); return false; }

    const std::size_t sz = static_cast<std::size_t>(width) * height * depth;
    TEXImage img;
    img.width  = width;
    img.height = height;
    img.mode   = mode;
    img.pixels.resize(sz);
    if (!RdBytes(f, img.pixels.data(), sz)) { tex.error = "v2: short bitmap"; return false; }

    tex.version = 2;
    tex.images.push_back(std::move(img));
    return true;
}

// Loop07 / Loop09 outer header ('4s12I' = 52 bytes):
//   signature(4)  version  _0 _1 _2 _3 _4  offset  count  _5  width  height  mode
//   (all uint32)
// Then: count * Item07(40 B) or Item09(32 B)  [skipped]
// Then: count * (width * height * DEPTH[mode])  raw bitmaps
// Then: Loop06  [skipped]
bool ParseV7or9(std::ifstream& f, uint32_t ver, TEXFile& tex) {
    f.seekg(0, std::ios::beg);

    uint8_t hdr[52];
    if (!RdBytes(f, hdr, 52)) {
        tex.error = "v" + std::to_string(ver) + ": short header"; return false;
    }

    // Offsets within the 52-byte header (all uint32 LE):
    //   [0-3]  sig     [4-7]  version  [8-11] _0  [12-15] _1  [16-19] _2
    //  [20-23] _3     [24-27] _4      [28-31] offset  [32-35] count
    //  [36-39] _5     [40-43] width   [44-47] height  [48-51] mode
    uint32_t count, width, height, mode;
    std::memcpy(&count,  hdr + 32, 4);
    std::memcpy(&width,  hdr + 40, 4);
    std::memcpy(&height, hdr + 44, 4);
    std::memcpy(&mode,   hdr + 48, 4);

    if (count == 0 || count > 4096) {
        tex.error = "v" + std::to_string(ver) + ": bad count=" + std::to_string(count);
        return false;
    }
    const uint32_t depth = DepthForMode(mode);
    if (depth == 0) {
        tex.error = "v" + std::to_string(ver) + ": unsupported mode=" + std::to_string(mode);
        return false;
    }

    // Skip item table: Item07=40 B, Item09=32 B
    const std::size_t itemBytes = count * (ver == 7 ? 40u : 32u);
    f.seekg(static_cast<std::streamoff>(itemBytes), std::ios::cur);

    // Read raw bitmaps
    const std::size_t bmpSz = static_cast<std::size_t>(width) * height * depth;
    tex.version = ver;
    for (uint32_t i = 0; i < count; ++i) {
        TEXImage img;
        img.width  = width;
        img.height = height;
        img.mode   = mode;
        img.pixels.resize(bmpSz);
        if (!RdBytes(f, img.pixels.data(), bmpSz)) {
            tex.error = "v" + std::to_string(ver) + ": short bitmap " + std::to_string(i);
            return false;
        }
        tex.images.push_back(std::move(img));
    }

    SkipLoop06(f); // trailing Loop06 — non-fatal
    return true;
}

// Loop11 header ('4s4I6H' = 32 bytes):
//   signature(4)  version(I)  mode(I)  multi(I)  _0(I)
//   _1(H)  _2(H)  _3(H)  width(H)  height(H)  depth(H)
// Followed by up to 10 mipmap bitmaps; each level i:
//   size = (width >> i) * (height >> i) * DEPTH[mode]
//   stop when read returns 0 bytes.
bool ParseV11(std::ifstream& f, TEXFile& tex) {
    f.seekg(0, std::ios::beg);

    uint8_t hdr[32];
    if (!RdBytes(f, hdr, 32)) { tex.error = "v11: short header"; return false; }

    // mode at [8-11] (uint32), width at [26-27] (uint16), height at [28-29] (uint16)
    // Wait — let me recount '4s4I6H':
    //  [0-3]  sig(4s)
    //  [4-7]  version(I)
    //  [8-11] mode(I)
    // [12-15] multi(I)
    // [16-19] _0(I)
    // [20-21] _1(H)
    // [22-23] _2(H)
    // [24-25] _3(H)
    // [26-27] width(H)   ← NOTE: Python slots say _3,width,height,depth as last 4 of 6H
    // Wait: 6H gives 6 shorts.  After 4 ints we have: _1(H) _2(H) _3(H) width(H) height(H) depth(H)
    // That's only 5 named fields for 6 values; re-check Python slots:
    //   _0, _1, _2, _3 belong to the INTS? No: slots = sig,version,mode,multi,_0,_1,_2,_3,width,height,depth
    //   '4s4I6H' → 1+4+6=11 values → sig,version,mode,multi,_0 from 4s+4I, then _1,_2,_3,width,height,depth from 6H
    // Byte offsets:
    //  [0-3]  sig      [4-7] version   [8-11] mode   [12-15] multi   [16-19] _0
    //  [20-21] _1      [22-23] _2      [24-25] _3    [26-27] width   [28-29] height  [30-31] depth(H)

    uint32_t mode;
    uint16_t width, height;
    std::memcpy(&mode,   hdr +  8, 4);
    std::memcpy(&width,  hdr + 26, 2);
    std::memcpy(&height, hdr + 28, 2);

    const uint32_t depth = DepthForMode(mode);
    if (depth == 0) { tex.error = "v11: unsupported mode=" + std::to_string(mode); return false; }

    tex.version = 11;
    for (int i = 0; i < 10; ++i) {
        const uint32_t mw = static_cast<uint32_t>(width)  >> i;
        const uint32_t mh = static_cast<uint32_t>(height) >> i;
        if (mw == 0 || mh == 0) break;

        const std::size_t sz = static_cast<std::size_t>(mw) * mh * depth;
        TEXImage img;
        img.width  = mw;
        img.height = mh;
        img.mode   = mode;
        img.pixels.resize(sz);

        const std::streamsize got =
            f.read(reinterpret_cast<char*>(img.pixels.data()),
                   static_cast<std::streamsize>(sz)).gcount();
        if (got == 0) break;                    // Python: "if len(d) == 0: break"

        img.pixels.resize(static_cast<std::size_t>(got));
        tex.images.push_back(std::move(img));
    }

    return !tex.images.empty();
}

// ---------------------------------------------------------------------------
// TGA writer (matches Python tga.py / convert.py)
// ---------------------------------------------------------------------------

bool WriteTGA(const std::string& path, const TEXImage& img) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;

    const uint32_t depth = DepthForMode(img.mode);
    const uint32_t alpha = AlphaForMode(img.mode);
    if (depth == 0) return false;

    const uint8_t pixDepth = static_cast<uint8_t>(depth * 8);
    // ImageDescriptor: alpha-bits | mirrorX(0x10) | mirrorY(0x20)
    //   → matches Python setImageDescriptor(True, True, ALPHA[mode])
    const uint8_t imgDesc = static_cast<uint8_t>(alpha | 0x10 | 0x20);

    // 18-byte TGA header (ImageType=2: uncompressed true-color)
    uint8_t hdr[18] = {};
    hdr[2]  = 2;
    hdr[12] = static_cast<uint8_t>(img.width  & 0xFF);
    hdr[13] = static_cast<uint8_t>((img.width  >> 8) & 0xFF);
    hdr[14] = static_cast<uint8_t>(img.height & 0xFF);
    hdr[15] = static_cast<uint8_t>((img.height >> 8) & 0xFF);
    hdr[16] = pixDepth;
    hdr[17] = imgDesc;

    f.write(reinterpret_cast<const char*>(hdr), 18);

    // Raw pixel data — no conversion (Python passes bitmaps[i] directly)
    f.write(reinterpret_cast<const char*>(img.pixels.data()),
            static_cast<std::streamsize>(img.pixels.size()));

    // TGA 2.0 footer: ExtOffset(4) + DevOffset(4) + "TRUEVISION-XFILE.\0"(18) = 26 bytes
    // Matches Python: fp.write(struct.pack('=2I18B', 0, 0, *b'TRUEVISION-XFILE.\x00'))
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
