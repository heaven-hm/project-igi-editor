#pragma once

/******************************************************************************
 * @file    source/runtime/retail_pointer_sprite_pure.h
 * @brief   The GAME's menu pointer: its identity and its pixel swizzle.
 *
 * Split out of `retail_pointer_sprite.h` so the host test lane can assert the
 * resource identity and the pixel mapping without linking the file-reading half,
 * which goes through `RES_Extract` -> `Logger` -> `Config::Get()` - and
 * `source/config.cpp` is not host-portable.
 *
 * Oracle: IGI-RE, the decompiled retail IGI.exe.
 *
 *   src/menu/menumanager.c:226-236   MenuManager_Update HOOK 0x417CF0
 *       Cursor_SetSprite(mm->pointer, MenuManager_FindSprite(mm, "pointer.spr"));
 *   src/menu/menumanager.c:810-818   MenuManager_FindSprite HOOK 0x418DE0
 *       case-insensitive basename match against the sprites
 *       `MenuManager_LoadResources` HOOK 0x417D50 (:238-257) collected from the
 *       archive directory with `Resource_ForEach` HOOK 0x4B5D80, classified by
 *       extension in `MenuManager_AddResource` HOOK 0x417E20 (:259-296).
 *   src/res/sprite.c:162-207         Sprite_Load HOOK 0x4B6720, LOOP v9 branch
 *       (:235-248) - reads the member through `Resource_Load`.
 *   src/widget/cursor.c:38-41        Cursor_Draw HOOK 0x424C40
 *       Draw_Sprite(c->sprite, c->hotX + c->x, c->hotY + c->y,
 *                   128.0f, 128.0f, 128.0f, 0, 0, 0);
 *   src/widget/cursor.c:25-36        Cursor_CreateHandler HOOK 0x424C10
 *       c->hotX = -2; c->hotY = 0;
 *   src/math/draw.c:12, :124-126     DrawSprite multiplies the colour by
 *       COLOR_SCALE = 1/128, so the 128.0f above is FULL intensity, not half.
 *****************************************************************************/

#include <cstddef>
#include <cstdint>
#include <vector>

namespace igi {

// The member name, exactly as `MenuManager_Update` looks it up
// (src/menu/menumanager.c:233).
inline constexpr const char* kRetailPointerSpriteName = "pointer.spr";

// The archive the editor reads it from - the in-game menu's, which is also where
// the editor's retail font comes from (source/runtime/pause_menu_font.cpp).
inline constexpr const char* kRetailPointerArchive = "MENUSYSTEM/ingamemenu.res";

// The full resource name inside that archive (the NAME chunk text, which is what
// the engine's resource list keys on: `Resource_Create` 0x4B59F0,
// src/res/resource.c:65-104).
inline constexpr const char* kRetailPointerResourceName = "LOCAL:menusystem/pointer.spr";

// Cursor_CreateHandler HOOK 0x424C10 (src/widget/cursor.c:29-30).
inline constexpr int kRetailPointerHotspotX = -2;
inline constexpr int kRetailPointerHotspotY = 0;

// The ARGB8888 swizzle.  The LOOP v9 frame stores B,G,R,A (little-endian
// ARGB8888, the layout `Pixel_Copy32` 0x517F00 uploads, src/terrdraw/pixel.c:112-125);
// `pic_s` (source/common.h:176-180) and GL want R,G,B,A.  Header-inline so the
// host lane can test the mapping without a GL context and without the archive.
inline std::vector<uint8_t> RetailPointerToRgba(const uint8_t* bgra, std::size_t size) {
    std::vector<uint8_t> rgba(size);
    for (std::size_t i = 0; i + 3 < size; i += 4) {
        rgba[i + 0] = bgra[i + 2];
        rgba[i + 1] = bgra[i + 1];
        rgba[i + 2] = bgra[i + 0];
        rgba[i + 3] = bgra[i + 3];
    }
    return rgba;
}

}  // namespace igi
