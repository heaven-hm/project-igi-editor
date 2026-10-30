// smoke_sprite.h - the retail .spr a Smoke particle is DRAWN with, decoded to
// texels the editor's renderer can upload (issue #43, item 1).
//
// The smoke simulation (smoke_sim.h) reproduces WHERE a particle is, how big it
// is, how its colour/alpha fade and how long it lives. It does not reproduce
// what the particle LOOKS like, because that is a sprite bitmap and this module
// is the reader for it. It closes the one divergence renderer_smoke.h has named
// since the emitter was ported: "the .spr texels are NOT loaded (a procedural
// radial falloff is used instead)".
//
// THE ENGINE'S CHAIN, in the order it happens:
//
//   Smoke_Init         HOOK 0x437510  src/fxobj/smoke.c:189-192
//       Game_AddSprite("fireball.spr"), ("blood1.spr"), ("smoke2.spr"),
//       ("smoke1.spr") -- four registered names, resolved to members of the
//       pack Game_LoadSprites mounts.
//   Game_LoadSprites   HOOK 0x4161D0  src/game/game.c:536-547
//       path = "LOCAL:common/sprites/" + name, then Sprite_Load(sprite, path).
//       The pack the game mounts for that prefix is
//       "LOCAL:common/sprites/sprites.res" (game.c:430 mounts the textures pack
//       the same way), i.e. <IGI>/common/sprites/sprites.res.
//   Sprite_Load        HOOK 0x4B6720  src/res/sprite.c:162-262
//       "LOOP" v9 (and v7): LoopHeader, one LoopFrame9/7 per frame, each
//       frame's texels at that frame's own `pixels` offset, then
//       Sprite_LoadTexture -> Texture_Create (0x4B23B0) with the frame's pitch,
//       width, height and FORMAT. A palette, when the header names one, is
//       256 BGRA entries at `palette + 0x10` (D3DTex_AllocPalette 0x50A670
//       memcpy's exactly 0x400 bytes).
//   Dirty rect -> texels  D3DTex_DownloadTexture HOOK 0x50AD90
//       src/sysmisc/d3dtex.c:411-432 dispatches the format:
//         1        Pixel_8To32      0x517FD0  (palettised; index 0 is the
//                                             transparent colour)
//         2        Pixel_1555To32   0x517C90  (ARGB1555)
//         3, 4, 43 Pixel_Copy32     0x517F00  (32 bit BGRA, copied verbatim)
//         5        Pixel_565To32    0x517DD0  (RGB565, alpha 0xFF)
//       src/terrdraw/pixel.c:59-158 is that conversion; the canonical 32-bit
//       layout it produces is bytes B, G, R, A.
//   Particle2D_New     HOOK 0x4FDF00  src/graphtask/particle.c:497-538
//       `t->animated = 0; t->sprite = sprite; if (sprite->map) t->animated = 1;`
//       -- `map` is the char map Sprite_Load builds when the header's `charmap`
//       offset is non-zero (Grid_LoadCharMap 0x4B75C0, src/res/grid.c:14-30).
//   Particle2D_Run     HOOK 0x4FD660  src/graphtask/particle.c:264-305
//       `if (t->animated) last = t->sprite->map->cols - 1;` and then
//       `if (last && ++p->frameTimer >= t->frameDelay) { ... p->frame++ }`.
//       Smoke passes frameDelay 0 (smoke.c:403), so an animated sprite advances
//       one frame per engine tick and a 1-column map never advances at all.
//   Particle2D_Draw    HOOK 0x4FD020  src/graphtask/particle.c:200-205
//       `frame = t->animated ? t->sprite->map->cells[p->frame] - 1 : 0;`
//       -- the map's cells hold 1-BASED frame numbers.
//
// WHAT THE SHIPPED DATA ACTUALLY IS (measured, not assumed). All four sprites
// in <IGI>/common/sprites/sprites.res are "LOOP" version 9 with a non-zero
// `charmap` offset that points at a trailing block of `cols`/`rows` cells:
//
//   smoke1.spr  128x128  format 3 (32 bit)  1 frame   charmap 1x1  cell 1
//   smoke2.spr  128x128  format 2 (1555)    1 frame   charmap 1x1  cell 1
//   fireball.spr 32x32   format 2 (1555)    1 frame   charmap 1x1  cell 1
//   blood1.spr   32x32   format 3 (32 bit)  7 frames  charmap 7x1  cell 1..7
//
// so smoke1/smoke2/fireball always draw frame 0 (cols == 1 -> `last` == 0) and
// only blood1 animates, one frame per tick. Every frame's own `format` field
// equals the header's, and every frame's width/height equals the header's.
//
// NAMED DIVERGENCES (never silent):
//   * A D3D surface is rounded up to a power of two, and QVertex_DrawQuadEx's uv
//     0..1 is multiplied by the texture's uScale/vScale before the vertex is
//     emitted (`d->u = n->texture->uScale * v->u + kTexelOffset`,
//     D3DQVertex_Emit 0x50D4C0, src/qvertex/d3d.c:434-437, with
//     uScale = width / outWidth from D3DTex_Upload 0x50B4E0). A padded surface
//     therefore samples exactly the image, so handing the renderer the frame's
//     OWN width/height and sampling 0..1 is equivalent -- there is no
//     behavioural difference here, and the four particle sprites are all
//     power-of-two anyway. What is NOT reproduced is the half-texel
//     `kTexelOffset` added to every u/v (d3d.c:434-437): it shifts sampling by
//     half a texel, which matters for point-sampled texel-exact sprites and not
//     for a LINEAR-filtered smoke puff.
//   * The engine's texture is a D3D surface with mip filtering disabled and
//     LINEAR magnification (flags 0x1000000 -> D3DTFN_LINEAR/D3DTFG_LINEAR,
//     D3DQVertex_SetTextureStates 0x50DE00, src/qvertex/d3d.c:592-598). The
//     editor uploads RGBA8 with LINEAR and CLAMP_TO_EDGE, no mipmaps.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace igi {

// The pack Game_LoadSprites resolves "LOCAL:common/sprites/<name>" against
// (src/game/game.c:430 mounts the shared packs by name).
constexpr const char* kSmokeSpritePackName = "LOCAL:common/sprites/sprites.res";

// "LOCAL:common/sprites/<file>", the resource identity Game_LoadSprites builds
// (src/game/game.c:539-545).
std::string SmokeSpriteMemberName(const std::string& file);

// What SmokeSpriteFile() (smoke.h) returns for a slot: the four names
// Game_AddSprite interns (smoke.c:189-192).
constexpr int kSmokeSpriteCount = 4;

// One frame of a "LOOP" sprite, decoded to RGBA8: `width * height * 4` bytes,
// row-major, R,G,B,A. This is the canonical BGRA buffer D3DTex_DownloadTexture
// builds, byte-swapped to the order OpenGL wants.
struct SmokeSpriteFrame {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

// One "LOOP" v7/v9 sprite (Sprite_Load 0x4B6720).
struct SmokeSprite {
    bool valid = false;
    std::string error;
    std::string name;                  // the file name, e.g. "smoke1.spr"
    std::uint32_t version = 0;         // 7 or 9
    std::uint32_t format = 0;          // the format word Texture_Create received
    int pitch = 0;                     // the first frame's recorded pitch
    std::vector<SmokeSpriteFrame> frames;

    // Grid_LoadCharMap 0x4B75C0 (src/res/grid.c:14-30): cells hold 1-BASED
    // frame numbers. Empty when the header's `charmap` offset is zero, which is
    // exactly the case where Particle2D_New leaves `animated` at 0.
    std::vector<std::uint8_t> charmap_cells;
    int charmap_cols = 0;
    int charmap_rows = 0;

    // `sprite->map != NULL` (Particle2D_New, particle.c:519-520).
    bool Animated() const { return !charmap_cells.empty(); }

    // Particle2D_Run's `last` (particle.c:271-272): `map->cols - 1`, else 0.
    int LastFrame() const {
        if (!Animated()) return 0;
        const int last = charmap_cols - 1;
        return last > 0 ? last : 0;
    }

    // Particle2D_Draw's frame index (particle.c:204-205). A cell of 0 (the
    // engine's "unmapped character") would index frames[-1]; the engine only
    // reaches that with a char map whose cells were never filled, which never
    // happens for the four particle sprites, so this clamps to a valid frame
    // and says so instead of reading out of bounds.
    int FrameForParticle(int particle_frame) const {
        if (!Animated()) return 0;
        if (particle_frame < 0 || particle_frame >= static_cast<int>(charmap_cells.size()))
            particle_frame = 0;
        const int cell = charmap_cells[static_cast<std::size_t>(particle_frame)];
        const int last = static_cast<int>(frames.size()) - 1;
        if (cell <= 0 || last < 0) return 0;
        const int index = cell - 1;    // the cells are 1-based
        return index > last ? last : index;
    }
};

// Parse a "LOOP" v7/v9 sprite exactly as Sprite_Load 0x4B6720 does, including
// the char map (0x4B75C0) and the format dispatch of D3DTex_DownloadTexture
// 0x50AD90 (the Pixel_* conversions of src/terrdraw/pixel.c:59-158).
// Returns out->valid. `data`/`size` are one whole sprite file (a .res member).
// An unsupported version or format is an error, never a silent fallback: the
// engine reports "Unsupported texture format" and stops (d3dtex.c:436).
bool SmokeSpriteParse(const std::uint8_t* data, std::size_t size, const std::string& name,
                      SmokeSprite* out);

}  // namespace igi
