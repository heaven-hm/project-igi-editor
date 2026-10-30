#pragma once

namespace igi {

// The pause menu's pointer.
//
// The GAME does not use the platform cursor in a menu: every menu manager owns a
// `Cursor` task and points it at the archive member `pointer.spr`
// (`MenuManager_Update` HOOK 0x417CF0, src/menu/menumanager.c:233), which
// `Cursor_Draw` HOOK 0x424C40 then blits at the pointer
// (src/widget/cursor.c:38-41).  The editor therefore draws that sprite while the
// menu is up (`App::DrawRetailPointerCursor`, source/app_ui.cpp, loading it with
// `igi::LoadRetailPointerSprite`, source/runtime/retail_pointer_sprite.h) and
// hides the platform cursor, exactly as it hides it for its own tool cursors.
//
// It was the other way round before: the menu showed the platform cursor.  That
// is what this header now records as wrong - the menu's pointer is the game's
// resource, not the system's.
constexpr bool ShouldDrawCustomCursor(bool pauseMenuVisible) noexcept {
    return !pauseMenuVisible;
}

// True only when the retail pointer sprite could not be read AND the pause menu
// is up - the one case where the editor still falls back to the platform
// cursor, so the menu stays usable on an install whose archive is missing.
// `App::DrawCustomCursor` asks for the sprite first and calls this only after
// the load failed.
constexpr bool ShouldUseNativeCursor(bool pauseMenuVisible,
                                     bool retailPointerAvailable) noexcept {
    return pauseMenuVisible && !retailPointerAvailable;
}

} // namespace igi
