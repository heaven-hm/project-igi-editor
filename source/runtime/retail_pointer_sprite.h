#pragma once

/******************************************************************************
 * @file    source/runtime/retail_pointer_sprite.h
 * @brief   The GAME's mouse pointer: `LOCAL:menusystem/pointer.spr`.
 *
 * The identity and the pixel swizzle live in
 * `retail_pointer_sprite_pure.h` (host-portable, and where the oracle citations
 * are); this header adds the archive read.  The engine path in full:
 *
 *   MenuManager_Update HOOK 0x417CF0 (src/menu/menumanager.c:226-236) points the
 *   manager's own `Cursor` task at `pointer.spr`; `MenuManager_FindSprite`
 *   HOOK 0x418DE0 (:810-818) finds it among the sprites
 *   `MenuManager_LoadResources` HOOK 0x417D50 (:238-257) collected from the
 *   archive directory, and `Sprite_Load` HOOK 0x4B6720 (src/res/sprite.c:162-207)
 *   reads the member through `Resource_Load`.
 *
 * WHERE IT LIVES.  Both menu archives carry the member and both are loaded -
 * `mm->resPath` (the screen's `Resource path`: `LOCAL:menusystem/ingamemenu.res`
 * for the in-game menu, `LOCAL:menusystem/menusystem.res` for the main menu)
 * plus every file in that directory, then `menusystem/missionsprites.res`
 * (:250-253).  The two copies are byte-identical (md5 `413f1ef5...`, 1884 bytes
 * each), so which one wins `MenuManager_FindSprite`'s first-match scan makes no
 * difference to what is drawn.  The in-game menu's archive is the one the editor
 * already reads for its font, and it is the one this module reads:
 *
 *   menusystem/ingamemenu.res -> LOCAL:menusystem/pointer.spr  (1884 bytes)
 *
 *   LOOP v9, one frame, format 3 (ARGB8888), 20 x 22 pixels, 1760 bytes of
 *   pixels (20 * 22 * 4).  The bitmap sits at file offset 0x50 and runs to the
 *   member's end (0x50 + 1760 = 1884), which is what the editor's own reader
 *   (source/renderer/tex_writer.cpp, `ParseV7or9`) lands on.  The field-by-field
 *   boundary in the decompiled `LoopHeader` / `LoopFrame9`
 *   (src/res/sprite.c:113-146) is NOT pinned by this member, so this module does
 *   not re-derive it; what the oracle does pin is the member name, the archive,
 *   the size the engine draws at (`Sprite_Width`/`Sprite_Height` HOOK 0x4B6CE0 /
 *   0x4B6D00) and the 1/128 colour scale.
 *
 * THE HOTSPOT: `Cursor_CreateHandler` HOOK 0x424C10 seeds `hotX = -2, hotY = 0`
 * (src/widget/cursor.c:29-30) and `Draw_Sprite` adds the frame's own `x`/`y` on
 * top (src/math/draw.c:127-135).  The arrow's tip is the pixel at (2, 0) of the
 * 20x22 bitmap, so -2,0 puts the drawn tip exactly on the pointer.  That is the
 * one inference here and it is recorded as such; the constants themselves are
 * the engine's.
 *****************************************************************************/

#include "retail_pointer_sprite_pure.h"

#include <string>

namespace igi {

// A decoded pointer sprite, ready to be uploaded as a GL texture.
struct RetailPointerImage {
	// True when the archive member was found, parsed and has a frame with pixels.
	bool ok = false;
	int width = 0;
	int height = 0;
	// RGBA8, `width * height * 4` bytes - the layout `pic_s` wants
	// (source/common.h:176-180) and the one `GL_RegisterTexture` uploads.
	std::vector<uint8_t> rgba;
	// Why it failed, for the log line.  Empty when `ok`.
	std::string reason;
};

// Read `LOCAL:menusystem/pointer.spr` out of
// `<igi_root>/MENUSYSTEM/ingamemenu.res` and decode its first frame to RGBA8.
//
// `igi_root` is `Utils::GetIGIRootPath()`'s answer (the retail install), the
// same root `GetRetailPauseFont` reads its font from.  A missing install, a
// missing archive, a missing member or an unsupported LOOP version all come back
// as `ok == false` with a reason - never as an exception and never as a partial
// image.
RetailPointerImage LoadRetailPointerSprite(const std::string& igi_root);

}  // namespace igi
