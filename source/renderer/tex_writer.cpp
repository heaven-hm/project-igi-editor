/******************************************************************************
 * @file    tex_writer.cpp
 * @brief   LOOP texture logging wrappers + TGA export.
 *
 * The PARSE itself is source/renderer/tex_parser.cpp, which does not log: this
 * file owns the log lines and the editor's TGA export path.
 *
 * Every field offset, size and pixel-format rule is transcribed from IGI-RE, the
 * decompiled retail IGI.exe:
 *
 *   Texture_LoadFile            0x4B24C0  src/file/texture.c:231-252
 *     LoopHeader2  (v2)                   src/file/texture.c:11-21
 *     LoopHeader11 (v11)                  src/file/texture.c:24-36
 *   Sprite_Load                 0x4B6720  src/res/sprite.c:162-207
 *     LOOP v7 / v9 sprite frames          src/res/sprite.c:113-146, :221-248
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

#include "tex_parser.h"
#include "../logger.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

// ---------------------------------------------------------------------------
// The two format tables the EXPORT path needs.  The parser has its own copy of
// `DepthForMode` in source/renderer/tex_parser.cpp; this one is here because the
// TGA writer below is the only remaining user in this translation unit and it
// must not drag the parser in.
//
// Oracle: D3DTex_DownloadTexture 0x50AD10, src/sysmisc/d3dtex.c:411-432.
// ---------------------------------------------------------------------------

namespace {

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
// TGA writer - the editor's own export path (no engine equivalent).
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
    if (img.pitch < rowBytes ||
        img.pixels.size() < static_cast<std::size_t>(img.pitch) * img.height) {
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
        f.write(reinterpret_cast<const char*>(
                    img.pixels.data() + static_cast<std::size_t>(y) * img.pitch),
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

// The parse itself lives in source/renderer/tex_parser.cpp, which does NOT log:
// `Logger::Log` reads `Config::Get()` and `source/config.cpp` is not
// host-portable, so a translation unit that both parses and logs cannot be used
// by the host test lane.  These two wrappers add the log lines the editor has
// always printed.
static TEXFile ParseAndLog(std::istream& f, const std::string& label) {
    TEXFile result;
    igi_tex::TEX_ParseStreamCore(f, result);
    if (result.valid) {
        Logger::Get().Log(LogLevel::INFO,
            "[TEX] Parsed '" + label + "' v" + std::to_string(result.version) +
            " images=" + std::to_string(result.images.size()));
    } else {
        Logger::Get().Log(LogLevel::ERR,
            "[TEX] Parse failed '" + label + "': " + result.error);
    }
    return result;
}

TEXFile TEX_Parse(const std::string& filepath) {
    std::ifstream f(filepath, std::ios::binary);
    if (!f.is_open()) {
        TEXFile result;
        result.error = "Cannot open: " + filepath;
        return result;
    }
    return ParseAndLog(f, filepath);
}

TEXFile TEX_ParseBytes(const uint8_t* data, std::size_t size, const std::string& label) {
    TEXFile result;
    if (data == nullptr || size == 0) {
        result.error = "Empty buffer: " + label;
        return result;
    }
    // A memory stream over the archive member: the very bytes the engine's
    // Resource_Load hands to Sprite_Load (HOOK 0x4B6720,
    // src/res/sprite.c:162-207), so this path cannot drift from the file one.
    const std::string buffer(reinterpret_cast<const char*>(data), size);
    std::istringstream f(buffer, std::ios::binary);
    return ParseAndLog(f, label);
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
