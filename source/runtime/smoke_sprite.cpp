// smoke_sprite.cpp - Sprite_Load 0x4B6720 + D3DTex_DownloadTexture 0x50AD90 +
// Grid_LoadCharMap 0x4B75C0, transcribed. See smoke_sprite.h for the whole
// chain and for the two named divergences.

#include "runtime/smoke_sprite.h"

#include <cstring>

namespace igi {
namespace {

// ── little-endian readers, bounds checked ───────────────────────────────────
bool In(const std::uint8_t* db, std::size_t n, std::size_t off, std::size_t len) {
    return db != nullptr && off <= n && len <= n - off;
}

std::uint16_t U16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[0]) |
                                      (static_cast<std::uint16_t>(p[1]) << 8));
}

std::int16_t I16(const std::uint8_t* p) { return static_cast<std::int16_t>(U16(p)); }

std::uint32_t U32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::int32_t I32(const std::uint8_t* p) { return static_cast<std::int32_t>(U32(p)); }

// ── on-disk records (src/res/sprite.c:113-127 and :129-155) ────────────────
// LoopHeader, 52 bytes. Field order is the one the shipped files use; the
// `charmap` word at 0x1C is non-zero in every v9 sprite of every shipped
// sprites.res (measured), so the char-map block below is the normal case, not
// an exotic one.
struct LoopHeaderView {
    std::int32_t version = 0;
    std::int32_t unk8 = 0;
    std::int32_t unkC = 0;
    std::int16_t unk10 = 0;
    std::int16_t unk12 = 0;
    std::int32_t unk14 = 0;
    std::int32_t palette = 0;   // offset of the 256-entry BGRA palette, 0 = none
    std::int32_t charmap = 0;   // offset of the char map, 0 = none
    std::int32_t nframes = 0;
    std::int32_t unk24 = 0;
    std::int32_t w = 0;
    std::int32_t h = 0;
    std::int32_t format = 0;
};

constexpr std::size_t kLoopHeaderSize = 52;
constexpr std::size_t kFrame7Size = 40;
constexpr std::size_t kFrame9Size = 32;
constexpr std::size_t kCharMapHeaderSize = 36;   // 4 + 4 + 4 ints + 3 ints
constexpr std::size_t kCharMapCellSize = 16;     // unsigned char frame; char unk1[15]
constexpr std::uint32_t kPaletteBytes = 0x400;   // D3DTex_AllocPalette 0x50A670

// One frame's texel source. `pixels` is an ABSOLUTE offset in the sprite file:
// Sprite_Load points at `d + f->pixels` (src/res/sprite.c:230, :244).
struct FrameView {
    std::int32_t pixels = 0;
    std::uint32_t format = 0;
    std::uint32_t pitch = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

LoopHeaderView ReadHeader(const std::uint8_t* d) {
    LoopHeaderView h;
    h.version = I32(d + 4);
    h.unk8 = I32(d + 8);
    h.unkC = I32(d + 12);
    h.unk10 = I16(d + 16);
    h.unk12 = I16(d + 18);
    h.unk14 = I32(d + 20);
    h.palette = I32(d + 24);
    h.charmap = I32(d + 28);
    h.nframes = I32(d + 32);
    h.unk24 = I32(d + 36);
    h.w = I32(d + 40);
    h.h = I32(d + 44);
    h.format = I32(d + 48);
    return h;
}

// The bytes-per-texel table of DepthForMode (source/renderer/tex_parser.cpp),
// which mirrors D3DTex_DownloadTexture's switch (src/sysmisc/d3dtex.c:411-432).
std::uint32_t DepthForFormat(std::uint32_t format) {
    switch (format) {
        case 1: return 1;      // Pixel_8To32, palettised
        case 2: return 2;      // Pixel_1555To32
        case 3: return 4;      // Pixel_Copy32
        case 4: return 4;      // Pixel_Copy32
        case 5: return 2;      // Pixel_565To32
        case 0x43: return 4;   // 3 | FMT_MIPMAPS (0x40)
        default: return 0;     // "Unsupported texture format" (d3dtex.c:436)
    }
}

// Decode one frame's texels into RGBA8 using the SAME conversion the engine's
// download path uses, level 0 only (a particle sprite is never mipmapped; the
// FMT_MIPMAPS chain, when present, is ignored and the base level is read).
bool DecodeFrame(const std::uint8_t* d, std::size_t n, const FrameView& f, const std::uint8_t* palette,
                 SmokeSpriteFrame* out, std::string* error) {
    if (f.width == 0 || f.height == 0) {
        *error = "frame has a zero dimension";
        return false;
    }
    if (f.pitch == 0) {
        *error = "frame has a zero pitch";
        return false;
    }
    const std::uint32_t depth = DepthForFormat(f.format);
    if (depth == 0) {
        *error = "unsupported texture format " + std::to_string(f.format);
        return false;
    }
    if (f.format == 1 && palette == nullptr) {
        // Pixel_8To32 would read through a NULL palette; the engine only ever
        // reaches this with a palette present (Sprite_Load passes s->palette).
        *error = "8 bit frame without a palette";
        return false;
    }

    // Pixel_8To32 / Pixel_1555To32 / Pixel_Copy32 / Pixel_565To32 all walk the
    // source as `rows` lines of `pitch` ELEMENTS (the callers pre-divide the
    // byte pitch: `pitch >> 1` for 16 bit, `pitch >> 2` for 32 bit,
    // d3dtex.c:411-432). Reading a whole pitch per row is what bounds-checks
    // the last row.
    const std::size_t elems_per_row = f.pitch / depth;
    const std::size_t row_bytes = elems_per_row * depth;
    const std::size_t need = row_bytes * f.height;
    if (f.pixels < 0 || !In(d, n, static_cast<std::size_t>(f.pixels), need)) {
        *error = "frame texels run past the end of the file";
        return false;
    }

    const std::uint8_t* src = d + f.pixels;
    out->width = static_cast<int>(f.width);
    out->height = static_cast<int>(f.height);
    out->rgba.assign(static_cast<std::size_t>(f.width) * f.height * 4u, 0u);

    std::uint8_t* dst = out->rgba.data();
    for (std::uint32_t y = 0; y < f.height; ++y) {
        const std::uint8_t* line = src + static_cast<std::size_t>(y) * row_bytes;
        for (std::uint32_t x = 0; x < f.width; ++x, dst += 4) {
            if (x >= elems_per_row) {
                // Pixel_* zeroes the columns outside the source row
                // (src/terrdraw/pixel.c:71-77, :96-102, :117-120, :148-153).
                dst[0] = dst[1] = dst[2] = dst[3] = 0;
                continue;
            }
            switch (f.format) {
                case 1: {
                    // Pixel_8To32 0x517FD0 (pixel.c:127-158): `dst[3] = alpha ?
                    // aline[x] : (i ? 0xFF : 0);` -- with no alpha map, index 0
                    // is the transparent colour and every other index is
                    // opaque. The palette's own fourth byte is NOT the alpha.
                    const std::uint8_t index = line[x];
                    const std::uint8_t* entry = palette + static_cast<std::size_t>(index) * 4u;
                    dst[0] = entry[2];  // R  (palette bytes are B,G,R,X)
                    dst[1] = entry[1];  // G
                    dst[2] = entry[0];  // B
                    dst[3] = index ? 0xFF : 0x00;
                    break;
                }
                case 2: {
                    // Pixel_1555To32 0x517C90 (pixel.c:59-83):
                    //   B = (p & 0x1F) << 3, G = (p >> 2) & 0xF8,
                    //   R = (p >> 7) & 0xF8, A = (p & 0x8000) ? 0xFF : 0
                    const std::uint16_t p =
                        U16(line + static_cast<std::size_t>(x) * 2u);
                    dst[0] = static_cast<std::uint8_t>((p >> 7) & 0xF8);          // R
                    dst[1] = static_cast<std::uint8_t>((p >> 2) & 0xF8);          // G
                    dst[2] = static_cast<std::uint8_t>((p & 0x1F) << 3);          // B
                    dst[3] = static_cast<std::uint8_t>((p & 0x8000) ? 0xFF : 0);  // A
                    break;
                }
                case 5: {
                    // Pixel_565To32 0x517DD0 (pixel.c:85-109): alpha is 0xFF.
                    const std::uint16_t p =
                        U16(line + static_cast<std::size_t>(x) * 2u);
                    dst[0] = static_cast<std::uint8_t>((p >> 8) & 0xF8);  // R
                    dst[1] = static_cast<std::uint8_t>((p >> 3) & 0xFC);  // G
                    dst[2] = static_cast<std::uint8_t>((p & 0x1F) << 3);  // B
                    dst[3] = 0xFF;
                    break;
                }
                default: {
                    // Pixel_Copy32 0x517F00 (pixel.c:112-124) copies the dword
                    // unchanged into the canonical B,G,R,A buffer.
                    const std::uint8_t* p =
                        line + static_cast<std::size_t>(x) * 4u;
                    dst[0] = p[2];  // R
                    dst[1] = p[1];  // G
                    dst[2] = p[0];  // B
                    dst[3] = p[3];  // A
                    break;
                }
            }
        }
    }
    return true;
}

}  // namespace

std::string SmokeSpriteMemberName(const std::string& file) {
    // Game_LoadSprites (src/game/game.c:539-545): `char path[0x100] =
    // "LOCAL:common/sprites/"; strcat(path, gGameSprites[i].name);`
    return "LOCAL:common/sprites/" + file;
}

bool SmokeSpriteParse(const std::uint8_t* data, std::size_t size, const std::string& name,
                      SmokeSprite* out) {
    if (out == nullptr) return false;
    *out = SmokeSprite{};
    out->name = name;

    if (data == nullptr || size < kLoopHeaderSize) {
        out->error = "too small to be a LOOP sprite";
        return false;
    }
    if (std::memcmp(data, "LOOP", 4) != 0) {
        out->error = "not a LOOP sprite (bad signature)";
        return false;
    }
    const LoopHeaderView h = ReadHeader(data);
    // Sprite_Load accepts v1, v7, v9 (and reports v5/v10 as "must be packed");
    // v2/v11 are Texture_LoadFile's, handled by igi_tex::TEX_Parse.
    if (h.version != 7 && h.version != 9) {
        out->error = "unsupported LOOP version " + std::to_string(h.version);
        return false;
    }
    if (h.nframes <= 0) {
        out->error = "sprite declares " + std::to_string(h.nframes) + " frames";
        return false;
    }
    if (h.nframes > 4096) {
        out->error = "sprite declares an absurd frame count";
        return false;
    }

    // The format the textures are created with: Sprite_Load sets `format` from
    // Loop_Palette's word when a palette is present, else the header's own
    // (src/res/sprite.c:223-227, :232-237).
    const std::uint8_t* palette = nullptr;
    std::uint32_t header_format = static_cast<std::uint32_t>(h.format);
    if (h.palette != 0) {
        if (h.palette < 0 || !In(data, size, static_cast<std::size_t>(h.palette) + 0xCu, 4u) ||
            !In(data, size, static_cast<std::size_t>(h.palette) + 0x10u, kPaletteBytes)) {
            out->error = "palette offset is outside the file";
            return false;
        }
        palette = data + h.palette + 0x10;
        header_format = U32(data + h.palette + 0xCu);   // Loop_Palette writes *count here
    }
    out->format = header_format;
    out->version = static_cast<std::uint32_t>(h.version);

    // ── the frames (LoopFrame9 32 bytes / LoopFrame7 40 bytes) ──────────────
    const std::size_t frame_size = (h.version == 9) ? kFrame9Size : kFrame7Size;
    if (!In(data, size, kLoopHeaderSize, frame_size * static_cast<std::size_t>(h.nframes))) {
        out->error = "frame table runs past the end of the file";
        return false;
    }
    for (int i = 0; i < h.nframes; ++i) {
        const std::uint8_t* r = data + kLoopHeaderSize + static_cast<std::size_t>(i) * frame_size;
        FrameView f;
        f.pixels = I32(r + 0);
        if (h.version == 9) {
            f.format = U32(r + 4);
            f.pitch = U16(r + 8);
            f.width = U16(r + 10);
            f.height = U16(r + 12);
        } else {
            f.format = header_format;
            f.pitch = static_cast<std::uint32_t>(I32(r + 4));
            f.width = static_cast<std::uint32_t>(I32(r + 8));
            f.height = static_cast<std::uint32_t>(I32(r + 12));
        }
        if (i == 0) out->pitch = static_cast<int>(f.pitch);
        SmokeSpriteFrame decoded;
        std::string error;
        if (!DecodeFrame(data, size, f, palette, &decoded, &error)) {
            out->error = "frame " + std::to_string(i) + ": " + error;
            return false;
        }
        out->frames.push_back(std::move(decoded));
    }

    // ── the char map (Grid_LoadCharMap 0x4B75C0, src/res/grid.c:14-30) ──────
    if (h.charmap != 0) {
        const std::size_t off = static_cast<std::size_t>(h.charmap);
        if (h.charmap < 0 || !In(data, size, off, kCharMapHeaderSize)) {
            out->error = "charmap offset is outside the file";
            return false;
        }
        const std::int32_t cols = I32(data + off + 16);
        const std::int32_t rows = I32(data + off + 20);
        if (cols <= 0 || rows <= 0 || cols > 1024 || rows > 1024) {
            // The engine would allocate cols*rows cells; a negative product is
            // not something the shipped data contains and is not guessed at.
            out->error = "charmap declares " + std::to_string(cols) + "x" + std::to_string(rows);
            return false;
        }
        const std::size_t cells = static_cast<std::size_t>(cols) * static_cast<std::size_t>(rows);
        out->charmap_cols = cols;
        out->charmap_rows = rows;
        out->charmap_cells.assign(cells, 0u);
        for (std::size_t i = 0; i < cells; ++i) {
            // A cell whose 1-byte `frame` fell outside the file (blood1.spr's
            // last cell is truncated in the shipped data -- its bytes stop
            // after the frame word) keeps 0 here; FrameForParticle maps that to
            // frame 0 rather than to the engine's frames[-1].
            const std::size_t at = off + kCharMapHeaderSize + i * kCharMapCellSize;
            if (In(data, size, at, 1u)) out->charmap_cells[i] = data[at];
        }
    }

    out->valid = !out->frames.empty();
    return out->valid;
}

}  // namespace igi
