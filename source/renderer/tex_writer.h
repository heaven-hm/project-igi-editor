/******************************************************************************
 * @file    tex_parser.h
 * @brief   LOOP texture parser for IGI-1 .tex files (Texture_LoadFile).
 *
 * .tex files are read by the engine's Texture_LoadFile 0x4B24C0
 * (IGI-RE src/file/texture.c:231-252). That function accepts exactly two
 * versions and rejects everything else as a fatal error:
 *
 *   v2   LoopHeader2  (24 bytes): magic, version, unk08(H), pad0A(H),
 *        unk0C(I), pitch(H), width(H), height(H), format(H)
 *        pixels at +0x18, pitch = the file's own pitch field
 *   v11  LoopHeader11 (32 bytes): magic, version, format(I), unk0C(I),
 *        unk10(I), unk14(H), width(H), height(H), unk1A(H), unk1C(H),
 *        bytesPerPixel(H)
 *        pixels at +0x20, pitch = bytesPerPixel * unk1A
 *
 * Texture format codes and their pixel conversion come from the device's
 * download path D3DTex_DownloadTexture 0x50AD10 (src/sysmisc/d3dtex.c:411-432):
 *   1 = 8bpp palettised (needs d->palette), 2 = ARGB1555, 3/4 = ARGB8888,
 *   5 = RGB565, 0x43 = ARGB8888 | FMT_MIPMAPS (0x40).
 * A format with FMT_MIPMAPS set carries a halved mip chain after the base
 * level (w >>= 1, h >>= 1, pitch >>= 1 while w > 0 && h > 0).
 *
 * The .spr/.pic sprite versions 7 and 9 are also accepted here because the
 * editor uses this parser for cursor sprites; those belong to the engine's
 * Sprite_Load 0x4B6720 (src/res/sprite.c:221-248) and use the sprite frame
 * layout, not Texture_LoadFile's.
 *****************************************************************************/

#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct TEXImage {
    uint32_t width  = 0;
    uint32_t height = 0;
    uint32_t mode   = 2;         // the file's format word (see above)
    uint32_t pitch  = 0;         // bytes per row, as the engine computes it
    std::vector<uint8_t> pixels; // pitch * height bytes for this level
};

struct TEXFile {
    uint32_t version = 0;
    std::vector<TEXImage> images; // 1 for v2, N for v7/v9, mip chain for v11
    bool valid = false;
    std::string error;
};

// Parse a .tex binary file. Returns TEXFile with valid=false on any error.
TEXFile TEX_Parse(const std::string& filepath);

// Parse a .tex/.spr image that lives INSIDE a `.res` archive, from the bytes
// `RES_Extract` returned for it.  The engine never has a file here either: it
// hands `Sprite_Load` (HOOK 0x4B6720, src/res/sprite.c:162-207) the pointer
// `Resource_Load` returned.  `label` is only used for the log lines.
// This is the path the retail menu cursor takes
// (`LOCAL:menusystem/pointer.spr` in `menusystem/ingamemenu.res`).
TEXFile TEX_ParseBytes(const uint8_t* data, std::size_t size, const std::string& label);

// Export each image in tex as a TGA file into outdir.
// Names: <basename>_00.tga, <basename>_01.tga, etc. (just _00.tga if 1 image).
// Returns number of files written.
int TEX_ExportTGA(const TEXFile& tex, const std::string& filepath, const std::string& outdir);

// Write a single TEXImage as a TGA file to an explicit path.
// Returns true on success.
bool TEX_WriteTGA(const std::string& path, const TEXImage& img);
