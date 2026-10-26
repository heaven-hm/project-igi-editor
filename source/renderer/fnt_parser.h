/******************************************************************************
 * @file    fnt_parser.h
 * @brief   FNT (ILFF-based bitmap font) parser.
 *
 * The engine loads a .fnt through Sprite_Load 0x4B6720, FONT branch
 * (IGI-RE src/res/sprite.c:171-207):
 *   - children of the root form are located with Chunk_FirstChild /
 *     Chunk_Find (src/res/chunk.c:17-32, :74-81);
 *   - FNTH (FontHeader, src/res/sprite.c:86-92): proportional(0), nframes(4),
 *     unk8(8), advance(12), w(16), h(20);
 *   - TRAN is a 256-entry character map, one 16-bit entry per codepoint; the
 *     engine stores the entry's LOW BYTE and uses it as a 1-based glyph index
 *     (sprite.c:197-199, Sprite_StringWidth :315-328);
 *   - ANMF is nframes FontFrame records, 0x28 bytes each
 *     (src/res/sprite.c:94-101): flags(0), uv[4] floats(4,8,12,16),
 *     texture(20), w(22), h(24), advance(26), unk1C[3](28), x(34), y(36);
 *   - the atlas comes from the resource's TEXH/BODY pairs through
 *     TexRes_Count 0x4B1FF0 and TexRes_Load 0x4B2070
 *     (src/file/texture.c:80-119). TexHeader: format(0), unk04(4), unk08(8),
 *     unk0C(12), width(14), height(16), unk12(18), unk14(20),
 *     bitsPerPixel(22); the texture pitch is bitsPerPixel * unk12 / 8.
 *
 * The glyph UVs, advance, placement and texture index are the file's values:
 * the engine passes them straight to Sprite_SetFrame (src/res/sprite.c:22-39)
 * and never derives or defaults them.
 *****************************************************************************/

#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <span>
#include <unordered_map>

struct FntGlyph {
    float u0, v0, u1, v1;   // atlas UV rect, the file's uv[0..3]
    int   width, height;    // glyph pixel size
    int   offsetX, offsetY; // placement bearing relative to the line origin
    int   advance;          // x advance, the file's value
    int   textureIndex;     // FontFrame::texture, index into the TEXH/BODY table
};

struct FntFont {
    bool valid = false;
    int  lineHeight = 0;       // FontHeader::h
    int  defaultAdvance = 0;   // FontHeader::advance
    int  defaultWidth = 0;     // FontHeader::w
    int  texWidth = 0, texHeight = 0;
    int  textureCount = 0;     // TexRes_Count of the FONT resource
    uint32_t texPitch = 0;     // bitsPerPixel * unk12 / 8 of the first texture
    std::vector<uint8_t> rgba;                       // atlas converted to RGBA, size texW*texH*4
    std::unordered_map<int, FntGlyph> glyphs;        // charcode -> glyph
};

FntFont FNT_Parse(const std::string& filepath);
FntFont FNT_ParseBytes(std::span<const uint8_t> bytes);
