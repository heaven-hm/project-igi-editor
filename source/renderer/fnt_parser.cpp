/******************************************************************************
 * @file    fnt_parser.cpp
 * @brief   FNT (ILFF-based bitmap font) parser.
 *
 * Transcribed from IGI-RE (decompiled retail IGI.exe):
 *   Sprite_Load        0x4B6720  src/res/sprite.c:162-262  (FONT branch :171-207)
 *   Sprite_SetFrame    0x4B6540  src/res/sprite.c:22-39
 *   Sprite_Init        0x4B65E0  src/res/sprite.c:41-64
 *   Sprite_StringWidth 0x4B7050  src/res/sprite.c:315-328
 *   TexRes_Count       0x4B1FF0  src/file/texture.c:80-96
 *   TexRes_Load        0x4B2070  src/file/texture.c:98-119
 *   Chunk_*            0x4B7470-0x4B7590  src/res/chunk.c (see res_chunks.h)
 *
 * Structure of a FONT resource:
 *   root ILFF form, form type "FONT"
 *   FNTH  FontHeader   (24 bytes, src/res/sprite.c:86-92)
 *   ANMF  nframes * FontFrame (0x28 bytes, src/res/sprite.c:94-101)
 *   TRAN  256 * uint16 character map (src/res/sprite.c:197-199)
 *   TEXH  TexHeader    (24 bytes, src/file/texture.c:38-49)
 *   BODY  atlas pixels (pitch * height bytes)
 *
 * Reading rules that are the engine's, not the editor's:
 *   - chunk data starts at round_up(chunk + 0x10, chunk.align) (chunk.c:47);
 *   - the character map entry is used as its LOW BYTE and is a 1-based glyph
 *     index (sprite.c:199 stores an unsigned char, sprite.c:324 indexes
 *     frames[c - 1]);
 *   - glyph UVs, advance, placement and texture index are the file's values
 *     (sprite.c:201-202) — nothing is derived and nothing is defaulted;
 *   - the atlas pitch is bitsPerPixel * unk12 / 8 (texture.c:110);
 *   - a missing FNTH/TRAN/ANMF or an unusable atlas is the engine's fatal
 *     "ILF file corrupt" path (sprite.c:204-207), reported here as a failed
 *     parse rather than a silent partial font.
 *
 * The RGBA conversion is the editor's rendering adaptation: the engine hands
 * the atlas to the device, which converts per format (D3DTex_DownloadTexture
 * 0x50AD10, src/sysmisc/d3dtex.c:411-432). Format 2 is ARGB1555 there
 * (Pixel_1555To32 0x517C90, src/terrdraw/pixel.c:59-81), so the intensity this
 * parser turns into alpha is computed from the 1555 fields.
 *****************************************************************************/

#include "utils.h"   // Utils::ToNativePath
#include "fnt_parser.h"
#include "res_chunks.h"
#include "../logger.h"
#include <fstream>
#include <cstring>
#include <limits>

using igi_res::ChunkHeader;
using igi_res::kNoChunk;

namespace {

constexpr uint32_t FOURCC_FNTH = igi_res::ChunkId('F', 'N', 'T', 'H');
constexpr uint32_t FOURCC_ANMF = igi_res::ChunkId('A', 'N', 'M', 'F');
constexpr uint32_t FOURCC_TRAN = igi_res::ChunkId('T', 'R', 'A', 'N');
constexpr uint32_t FOURCC_TEXH = igi_res::ChunkId('T', 'E', 'X', 'H');
constexpr uint32_t FOURCC_BODY = igi_res::ChunkId('B', 'O', 'D', 'Y');

constexpr size_t kFontHeaderSize = 0x18;   // FontHeader, src/res/sprite.c:86-92
constexpr size_t kFontFrameSize  = 0x28;   // FontFrame,  src/res/sprite.c:94-101
constexpr size_t kTexHeaderSize  = 0x18;   // TexHeader,  src/file/texture.c:38-49
constexpr size_t kCharMapEntries = 256;    // src/res/sprite.c:198

uint16_t ReadU16LE(const uint8_t* p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

// FontFrame and TexHeader fields are `short` in the engine
// (src/res/sprite.c:94-101, src/file/texture.c:38-49).
int16_t ReadI16LE(const uint8_t* p) {
    return (int16_t)ReadU16LE(p);
}

uint32_t ReadU32LE(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

float ReadF32LE(const uint8_t* p) {
    uint32_t u = ReadU32LE(p);
    float f;
    std::memcpy(&f, &u, sizeof(f));
    return f;
}

// Chunk_Data (chunk.c:34-49) plus a bounds test against the buffer the editor
// actually holds; the engine reads mapped memory and has no such test.
bool ChunkData(const uint8_t* bytes, size_t fileSize,
               uint64_t chunkOffset, const ChunkHeader& chunk,
               uint64_t parentOffset, const ChunkHeader& parent,
               const uint8_t*& out, size_t& outSize) {
    const uint64_t off = igi_res::ChunkDataOffset(chunkOffset, chunk, parentOffset, parent);
    if (off == kNoChunk || off > fileSize || chunk.size > fileSize - off) return false;
    out = bytes + off;
    outSize = chunk.size;
    return true;
}

// Sprite_Load's FONT branch — src/res/sprite.c:171-207.
FntFont ParseFntBytes(std::span<const uint8_t> bytes, const std::string& source) {
    FntFont font;
    const uint8_t* data = bytes.data();
    const size_t fileSize = bytes.size();

    if (fileSize < 20) {
        Logger::Get().Log(LogLevel::ERR, "[FNT] File too small for ILFF header: " + source);
        return font;
    }
    ChunkHeader root;
    std::memcpy(&root, data, 20);
    if (root.id != igi_res::kChunkIlff || root.form != igi_res::ChunkId('F', 'O', 'N', 'T')) {
        Logger::Get().Log(LogLevel::ERR, "[FNT] Not an ILFF FONT resource: " + source);
        return font;
    }

    // Chunk_FirstChild(root) — chunk.c:17-32.
    const uint64_t firstChild = igi_res::ChunkFirstChildOffset(0, root);
    if (firstChild == kNoChunk) {
        Logger::Get().Log(LogLevel::ERR, "[FNT] ILF file corrupt (no chunks): " + source);
        return font;
    }

    // Chunk_Find(firstChild, id) — chunk.c:74-81: walk siblings for the first
    // match, and TexRes_Count/TexRes_Load walk TEXH followed by BODY.
    const uint8_t* fnth = nullptr; size_t fnthSize = 0;
    const uint8_t* anmf = nullptr; size_t anmfSize = 0;
    const uint8_t* tran = nullptr; size_t tranSize = 0;
    const uint8_t* texh = nullptr; size_t texhSize = 0;
    const uint8_t* body = nullptr; size_t bodySize = 0;
    int textureCount = 0;

    bool expectBody = false;   // TexRes_Load: a TEXH must be followed by a BODY
    uint64_t c = firstChild;
    while (c != kNoChunk) {
        ChunkHeader ch;
        if (c > fileSize || fileSize - c < 16) break;
        std::memcpy(&ch, data + c, 16);
        if (fileSize - c >= 20) ch.form = ReadU32LE(data + c + 16);

        if (expectBody) {
            // TexRes_Count/TexRes_Load: the chunk after TEXH must be BODY with
            // data (src/file/texture.c:86-90, :104-109).
            if (ch.id != FOURCC_BODY) {
                Logger::Get().Log(LogLevel::ERR, "[FNT] TEXH not followed by BODY: " + source);
                return font;
            }
            const uint8_t* d = nullptr; size_t n = 0;
            if (!ChunkData(data, fileSize, c, ch, 0, root, d, n)) {
                Logger::Get().Log(LogLevel::ERR, "[FNT] BODY chunk has no data: " + source);
                return font;
            }
            if (!body) { body = d; bodySize = n; }
            textureCount++;
            expectBody = false;
        } else if (ch.id == FOURCC_FNTH) {
            if (!ChunkData(data, fileSize, c, ch, 0, root, fnth, fnthSize)) {
                Logger::Get().Log(LogLevel::ERR, "[FNT] FNTH chunk has no data: " + source);
                return font;
            }
        } else if (ch.id == FOURCC_ANMF) {
            if (!ChunkData(data, fileSize, c, ch, 0, root, anmf, anmfSize)) {
                Logger::Get().Log(LogLevel::ERR, "[FNT] ANMF chunk has no data: " + source);
                return font;
            }
        } else if (ch.id == FOURCC_TRAN) {
            if (!ChunkData(data, fileSize, c, ch, 0, root, tran, tranSize)) {
                Logger::Get().Log(LogLevel::ERR, "[FNT] TRAN chunk has no data: " + source);
                return font;
            }
        } else if (ch.id == FOURCC_TEXH) {
            if (!ChunkData(data, fileSize, c, ch, 0, root, texh, texhSize)) {
                Logger::Get().Log(LogLevel::ERR, "[FNT] TEXH chunk has no data: " + source);
                return font;
            }
            expectBody = true;
        }

        c = igi_res::ChunkNextOffset(c, ch);
    }

    // TexRes_Count: a TEXH with no following BODY makes the count unusable
    // (src/file/texture.c:86-90).
    if (expectBody) {
        Logger::Get().Log(LogLevel::ERR, "[FNT] TEXH not followed by BODY: " + source);
        return font;
    }

    // Sprite_Load: `if (!fnth || !tran || !anmf) goto corrupt;` (sprite.c:183-184)
    if (!fnth || !tran || !anmf || !texh || !body) {
        Logger::Get().Log(LogLevel::ERR, "[FNT] ILF file corrupt (missing chunk): " + source);
        return font;
    }
    if (fnthSize < kFontHeaderSize || texhSize < kTexHeaderSize) {
        Logger::Get().Log(LogLevel::ERR, "[FNT] ILF file corrupt (short header chunk): " + source);
        return font;
    }

    // FontHeader — src/res/sprite.c:86-92: proportional(0), nframes(4),
    // unk8(8), advance(12), w(16), h(20).
    const uint32_t numGlyphs = ReadU32LE(fnth + 4);
    font.defaultAdvance = (int)ReadU32LE(fnth + 12);   // FontHeader::advance
    font.defaultWidth   = (int)ReadU32LE(fnth + 16);   // FontHeader::w
    font.lineHeight     = (int)ReadU32LE(fnth + 20);   // FontHeader::h

    if (numGlyphs == 0) {
        Logger::Get().Log(LogLevel::ERR, "[FNT] ILF file corrupt (no frames): " + source);
        return font;
    }
    if (numGlyphs > std::numeric_limits<size_t>::max() / kFontFrameSize ||
        anmfSize < static_cast<size_t>(numGlyphs) * kFontFrameSize) {
        Logger::Get().Log(LogLevel::ERR, "[FNT] Glyph table truncated in: " + source);
        return font;
    }

    // TexRes_Count/TexRes_Load reject a font with no usable texture
    // (sprite.c:194-195: `if (s->ntextures <= 0) goto corrupt;`).
    if (textureCount <= 0) {
        Logger::Get().Log(LogLevel::ERR, "[FNT] ILF file corrupt (no texture): " + source);
        return font;
    }
    font.textureCount = textureCount;

    // TexHeader — src/file/texture.c:38-49.
    const uint32_t texFormat = ReadU32LE(texh + 0);
    const uint16_t texWidth  = ReadU16LE(texh + 14);
    const uint16_t texHeight = ReadU16LE(texh + 16);
    const uint16_t unk12     = ReadU16LE(texh + 18);
    const uint16_t bitsPerPixel = ReadU16LE(texh + 22);

    // TexRes_Load: pitch = h->bitsPerPixel * h->unk12 / 8 (texture.c:110)
    const uint32_t pitch = (uint32_t)bitsPerPixel * unk12 / 8;
    if (texWidth == 0 || texHeight == 0 || pitch == 0) {
        Logger::Get().Log(LogLevel::ERR, "[FNT] ILF file corrupt (zero texture size): " + source);
        return font;
    }
    font.texWidth  = texWidth;
    font.texHeight = texHeight;
    font.texPitch  = pitch;

    const size_t numPixels = (size_t)texWidth * texHeight;
    if (bodySize < (size_t)pitch * texHeight) {
        Logger::Get().Log(LogLevel::ERR, "[FNT] Atlas BODY is truncated: " + source);
        return font;
    }
    try {
        font.rgba.resize(numPixels * 4);
    } catch (const std::bad_alloc&) {
        Logger::Get().Log(LogLevel::ERR, "[FNT] Memory allocation failed for atlas: " + source);
        return font;
    }

    // Atlas decode, per the engine's format switch (src/sysmisc/d3dtex.c:411-432).
    if (texFormat == 3 || texFormat == 4 || texFormat == 0x43) {
        // Pixel_Copy32: 32-bit pixels, byte order B,G,R,A.
        for (size_t y = 0; y < texHeight; ++y) {
            const uint8_t* row = body + y * pitch;
            for (size_t x = 0; x < texWidth; ++x) {
                const uint8_t* p = row + x * 4;
                uint8_t* o = font.rgba.data() + (y * texWidth + x) * 4;
                o[0] = p[2];  // R
                o[1] = p[1];  // G
                o[2] = p[0];  // B
                o[3] = p[3];  // A
            }
        }
    } else if (texFormat == 2) {
        // Pixel_1555To32: B = (p & 0x1F) << 3, G = (p >> 2) & 0xF8,
        // R = (p >> 7) & 0xF8, A = (p & 0x8000) ? 0xFF : 0.
        // The editor draws the atlas as a tintable mask, so the engine's RGB
        // intensity becomes the alpha channel (documented adaptation).
        for (size_t y = 0; y < texHeight; ++y) {
            const uint8_t* row = body + y * pitch;
            for (size_t x = 0; x < texWidth; ++x) {
                const uint16_t v = ReadU16LE(row + x * 2);
                const int b = (v & 0x1F) << 3;
                const int g = (v >> 2) & 0xF8;
                const int r = (v >> 7) & 0xF8;
                int lum = (r + g + b) / 3;
                if (lum > 255) lum = 255;
                uint8_t* o = font.rgba.data() + (y * texWidth + x) * 4;
                o[0] = 255;
                o[1] = 255;
                o[2] = 255;
                o[3] = (uint8_t)lum;
            }
        }
    } else if (texFormat == 5) {
        // Pixel_565To32: B = (p & 0x1F) << 3, G = (p >> 3) & 0xFC,
        // R = (p >> 8) & 0xF8, A = 0xFF.
        for (size_t y = 0; y < texHeight; ++y) {
            const uint8_t* row = body + y * pitch;
            for (size_t x = 0; x < texWidth; ++x) {
                const uint16_t v = ReadU16LE(row + x * 2);
                const int b = (v & 0x1F) << 3;
                const int g = (v >> 3) & 0xFC;
                const int r = (v >> 8) & 0xF8;
                int lum = (r + g + b) / 3;
                if (lum > 255) lum = 255;
                uint8_t* o = font.rgba.data() + (y * texWidth + x) * 4;
                o[0] = 255;
                o[1] = 255;
                o[2] = 255;
                o[3] = (uint8_t)lum;
            }
        }
    } else {
        // Format 1 (palettised) needs the palette TexRes_Load is given; the
        // FONT path passes 0 (src/res/sprite.c:196), and any other format is
        // the engine's "Unsupported texture format" fatal error
        // (src/sysmisc/d3dtex.c:428-431).
        Logger::Get().Log(LogLevel::ERR,
            "[FNT] Unsupported texture format " + std::to_string(texFormat) + " in: " + source);
        return font;
    }

    // Character map — src/res/sprite.c:197-199:
    //   for (i = 0; i < 256; i++, map += 2) Grid_Set(s->map, i, 0, *map);
    // `map` is an unsigned char*, so the LOW BYTE of each 16-bit entry is the
    // value the engine stores and later uses as a 1-based glyph index.
    if (tranSize < kCharMapEntries * 2) {
        Logger::Get().Log(LogLevel::ERR, "[FNT] Character map truncated in: " + source);
        return font;
    }
    for (size_t code = 0; code < kCharMapEntries; ++code) {
        const uint8_t glyphIndex = tran[code * 2];
        if (glyphIndex == 0) continue;            // no glyph: the default advance
        if (glyphIndex > numGlyphs) {
            // The engine indexes frames[c - 1] without a bound check
            // (src/res/sprite.c:324); an index past the table is a corrupt file.
            Logger::Get().Log(LogLevel::ERR, "[FNT] Character map index out of range in: " + source);
            return font;
        }

        // FontFrame — src/res/sprite.c:94-101. Everything is the file's value:
        // Sprite_SetFrame(s, i, ff->texture, ff->w, ff->h, ff->x, ff->y,
        //                 ff->advance, ff->flags, ff->uv[0..3]) (sprite.c:201-202).
        const uint8_t* g = anmf + (size_t)(glyphIndex - 1) * kFontFrameSize;
        FntGlyph glyph;
        glyph.u0 = ReadF32LE(g + 4);
        glyph.v0 = ReadF32LE(g + 8);
        glyph.u1 = ReadF32LE(g + 12);
        glyph.v1 = ReadF32LE(g + 16);
        glyph.textureIndex = (int)ReadI16LE(g + 20);
        glyph.width   = (int)ReadI16LE(g + 22);
        glyph.height  = (int)ReadI16LE(g + 24);
        glyph.advance = (int)ReadI16LE(g + 26);
        glyph.offsetX = (int)ReadI16LE(g + 34);
        glyph.offsetY = (int)ReadI16LE(g + 36);
        if (glyph.textureIndex >= textureCount) {
            Logger::Get().Log(LogLevel::ERR, "[FNT] Glyph texture index out of range in: " + source);
            return font;
        }
        font.glyphs[(int)code] = glyph;
    }

    font.valid = true;
    Logger::Get().Log(LogLevel::INFO, "[FNT] Parsed " + source + " | Glyphs: " +
        std::to_string(font.glyphs.size()) + " | Atlas: " +
        std::to_string(texWidth) + "x" + std::to_string(texHeight) +
        " | LineHeight: " + std::to_string(font.lineHeight));
    return font;
}

}  // namespace

FntFont FNT_ParseBytes(std::span<const uint8_t> bytes) {
    return ParseFntBytes(bytes, "<memory>");
}

FntFont FNT_Parse(const std::string& filepath) {
    FntFont font;
    std::ifstream file(Utils::ToNativePath(filepath), std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        Logger::Get().Log(LogLevel::ERR, "[FNT] Could not open file: " + filepath);
        return font;
    }
    const std::streampos pos = file.tellg();
    if (pos == std::streampos(-1)) {
        Logger::Get().Log(LogLevel::ERR, "[FNT] Could not determine file size: " + filepath);
        return font;
    }
    std::vector<uint8_t> bytes;
    try {
        bytes.resize(static_cast<size_t>(pos));
    } catch (const std::bad_alloc&) {
        Logger::Get().Log(LogLevel::ERR, "[FNT] Memory allocation failed for file: " + filepath);
        return font;
    }
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (static_cast<size_t>(file.gcount()) != bytes.size()) {
        Logger::Get().Log(LogLevel::ERR, "[FNT] Could not read whole file: " + filepath);
        return font;
    }
    return ParseFntBytes(bytes, filepath);
}
