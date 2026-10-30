/******************************************************************************
 * @file    test_pause_menu_input.cpp
 * @brief   ITEM 1 - which keys the pause menu accepts, and what it does with
 *          the rest; ITEM 2 - that the menu's pointer is the GAME's resource.
 *
 * Everything asserted here is either
 *   * the engine's key contract, transcribed into
 *     `source/runtime/pause_menu_input_policy.h` from
 *     `MenuManager_ReadInput` HOOK 0x418580 (src/menu/menumanager.c:488-551) and
 *     the typed-key enum + window procedure in src/loadscreen/keys.c:10-71, or
 *   * the retail archive member `LOCAL:menusystem/pointer.spr` in
 *     `menusystem/ingamemenu.res`, walked here with the header-only chunk
 *     helpers of source/renderer/res_chunks.h and parsed with
 *     `igi_tex::TEX_ParseStreamCore` - the same core `TEX_ParseBytes` wraps.
 *
 * Host portability: the archive is walked IN THIS FILE rather than through
 * `RES_Extract` (source/renderer/res_writer.cpp), because that translation unit
 * logs through `Logger` -> `Config::Get()`, which is not host-portable.  The
 * chunk helpers it uses are the same ones `res_writer.cpp` is built on
 * (`ChunkFirstChildOffset` / `ChunkNextOffset` / `ChunkDataOffset`,
 * src/res/chunk.c), so the walk is the engine's, not a re-invention.
 *
 * Corpus: $IGI_GAME_PATH (scripts/run-host-tests.sh exports it, default
 * /Users/haseeb-mir/Downloads/IGI).  The corpus-backed tests skip when it is
 * absent; the constant tests always run.
 *****************************************************************************/

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "renderer/res_chunks.h"
#include "runtime/pause_menu_cursor.h"
#include "runtime/pause_menu_input_policy.h"
#include "runtime/pause_menu_state.h"

namespace {

std::filesystem::path CorpusRoot() {
    if (const char* env = std::getenv("IGI_GAME_PATH"); env != nullptr && *env != '\0') {
        return std::filesystem::path(env);
    }
    return std::filesystem::path("/Users/haseeb-mir/Downloads/IGI");
}

std::filesystem::path IngameMenuRes() {
    return CorpusRoot() / "menusystem" / "ingamemenu.res";
}

bool MenuCorpusAvailable() {
    std::error_code ec;
    return std::filesystem::is_regular_file(IngameMenuRes(), ec);
}

std::vector<uint8_t> ReadWholeFile(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f),
                                std::istreambuf_iterator<char>());
}

igi_res::ChunkHeader HeaderAt(const std::vector<uint8_t>& b, std::size_t off) {
    igi_res::ChunkHeader h;
    if (off + 16 > b.size()) return h;
    auto u32 = [&](std::size_t o) {
        return static_cast<uint32_t>(b[o]) | (static_cast<uint32_t>(b[o + 1]) << 8) |
               (static_cast<uint32_t>(b[o + 2]) << 16) |
               (static_cast<uint32_t>(b[o + 3]) << 24);
    };
    h.id = u32(off);
    h.size = u32(off + 4);
    h.align = u32(off + 8);
    h.next = u32(off + 12);
    h.form = (off + 20 <= b.size()) ? u32(off + 16) : 0;
    return h;
}

// Find a NAME/BODY pair by the engine's resource identity: the NAME text
// upper-cased with '\' folded to '/' (`Resource_Create` 0x4B59F0,
// src/res/resource.c:65-104).  Returns the BODY bytes, empty when absent.
std::vector<uint8_t> FindMember(const std::vector<uint8_t>& b,
                                const std::string& wanted) {
    const std::string want = igi_res::NormalizeResourceName(wanted);
    std::string pending;
    std::size_t off = 20;  // past "ILFF" + size/align/next + "IRES"
    std::size_t guard = 0;
    while (off + 16 <= b.size() && guard++ < 100000) {
        const igi_res::ChunkHeader h = HeaderAt(b, off);
        if (h.id == igi_res::kChunkIres) {
            off += 4;  // the form-type word that opens an ILFF body
            continue;
        }
        const std::size_t body = off + 16;
        if (h.id == igi_res::kChunkName && body + h.size <= b.size()) {
            pending.assign(reinterpret_cast<const char*>(b.data() + body), h.size);
            const std::size_t nul = pending.find('\0');
            if (nul != std::string::npos) pending.resize(nul);
        } else if ((h.id == igi_res::kChunkBody || h.id == igi_res::kChunkCstr ||
                    h.id == igi_res::kChunkPath) &&
                   body + h.size <= b.size()) {
            if (igi_res::NormalizeResourceName(pending) == want) {
                return std::vector<uint8_t>(b.begin() + static_cast<std::ptrdiff_t>(body),
                                            b.begin() + static_cast<std::ptrdiff_t>(body + h.size));
            }
        }
        if (h.next != 0 && off + h.next < b.size()) {
            off += h.next;
        } else if (h.size >= 16 && off + 16 + h.size <= b.size()) {
            off += 16 + h.size;
        } else {
            break;
        }
    }
    return {};
}

using igi::PauseKeyAction;
using igi::PauseMenuCharKeyAction;
using igi::PauseMenuSpecialKeyAction;

// `retail_pointer_sprite.h` gives the constants and the pure RGBA swizzle; this
// suite deliberately does NOT call `LoadRetailPointerSprite`, because that
// translation unit reads through `RES_Extract` -> `Logger` -> `Config::Get()`,
// which is not host-portable.  The corpus tests below do the archive walk and
// the LOOP parse themselves, with the engine's own chunk helpers.

}  // namespace

// ─── ITEM 1: the engine's key contract ──────────────────────────────────────
//
// `MenuManager_ReadInput` HOOK 0x418580 (src/menu/menumanager.c:488-551) pops
// every queued key and acts on four of them; the rest fall out of its switch and
// are discarded.  These tests pin exactly that set, so a future edit that widens
// the editor's pause menu beyond the engine's keys fails here.

TEST(PauseMenuInput, UpAndDownAreTheOnlyKeysThatMoveTheSelection) {
    // `case -3:` (KEY_UP)   -> `if (--i < 0) i = itemCount - 1;`  :542-547
    // `case -4:` (KEY_DOWN) -> `if (++i >= itemCount) i = 0;`    :536-541
    EXPECT_EQ(PauseMenuSpecialKeyAction(igi::kGlutKeyUp), PauseKeyAction::MoveUp);
    EXPECT_EQ(PauseMenuSpecialKeyAction(igi::kGlutKeyDown), PauseKeyAction::MoveDown);

    // Nothing else among the GLUT special keys moves the selection.  GLUT's
    // F-keys are 0x01..0x0C and its navigation keys 0x64..0x6C
    // (third_party/GL/include/freeglut_std.h:135-155).
    for (int key = 1; key <= 12; ++key) {                    // F1 .. F12
        EXPECT_EQ(PauseMenuSpecialKeyAction(key), PauseKeyAction::Consume) << "F" << key;
    }
    for (int key = 104; key <= 108; ++key) {                 // PAGE_UP .. INSERT
        EXPECT_EQ(PauseMenuSpecialKeyAction(key), PauseKeyAction::Consume)
            << "special key " << key;
    }
    // The four arrows, explicitly.
    EXPECT_EQ(PauseMenuSpecialKeyAction(100), PauseKeyAction::Ignore);   // LEFT
    EXPECT_EQ(PauseMenuSpecialKeyAction(101), PauseKeyAction::MoveUp);   // UP
    EXPECT_EQ(PauseMenuSpecialKeyAction(102), PauseKeyAction::Ignore);   // RIGHT
    EXPECT_EQ(PauseMenuSpecialKeyAction(103), PauseKeyAction::MoveDown); // DOWN
}

TEST(PauseMenuInput, LeftAndRightAreAcceptedAndIgnored) {
    // `case -2: case -1:` fall into `if (mm->focus) mm->focus->key = key;`
    // (:530-535).  RUN_ACTION only tests for -7 (src/menuscreen/menutext.c:175),
    // so on the pause screen they do nothing - but the engine DOES pop them, so
    // they are Ignore rather than Consume.
    EXPECT_EQ(PauseMenuSpecialKeyAction(igi::kGlutKeyLeft), PauseKeyAction::Ignore);
    EXPECT_EQ(PauseMenuSpecialKeyAction(igi::kGlutKeyRight), PauseKeyAction::Ignore);
}

TEST(PauseMenuInput, EnterActivatesAndEscapeGoesBack) {
    // `case -7:` -> `mm->focus->key = key`; RUN_ACTION's `w.key == -7` half
    // (src/menuscreen/menutext.c:170-179) runs the focused item's action.
    EXPECT_EQ(PauseMenuCharKeyAction(igi::kKeyEnter, false), PauseKeyAction::Activate);
    EXPECT_TRUE(igi::PauseKeyActionActivates(PauseKeyAction::Activate));
    EXPECT_FALSE(igi::PauseKeyActionActivates(PauseKeyAction::Back));

    // `case -8:` -> `in->back = 1`; MenuScreen_Run (src/widget/menuscreen.c:7-18)
    // then evaluates the screen's `On Press Escape`, which for screen 900 is
    // `MenuManager_LeaveMenus(900, MENUMANAGER_IDLE)`.
    EXPECT_EQ(PauseMenuCharKeyAction(igi::kKeyEscape, false), PauseKeyAction::Back);
}

TEST(PauseMenuInput, TabIsAcceptedAndIgnored) {
    // `case -11: in->select = 1; break;` (:524-526) - and no code in IGI-RE ever
    // reads `MenuInput::select`, so it is Ignore, not Consume.
    EXPECT_EQ(PauseMenuCharKeyAction(igi::kKeyTab, false), PauseKeyAction::Ignore);
}

TEST(PauseMenuInput, EveryOtherCharacterKeyIsConsumedNotPassedOn) {
    // The window procedure pushes every WM_CHAR verbatim
    // (src/loadscreen/keys.c:63-67), so a letter or digit DOES reach
    // MenuManager_ReadInput - and its switch has no case for it, so
    // `while (Keys_GetCount())` discards it.  The editor must discard it too:
    // this is the test that stops the editor's letter shortcuts (B, L, DEL, ...)
    // from firing behind the overlay.
    const int keys[] = {igi::kKeyBackspace, 9, 13, 27, 32, 44, 46, 48, 57,
                        static_cast<int>('A'), static_cast<int>('B'),
                        static_cast<int>('H'), static_cast<int>('L'),
                        static_cast<int>('P'), static_cast<int>('R'),
                        static_cast<int>('b'), static_cast<int>('h'),
                        static_cast<int>('l'), static_cast<int>('p'),
                        static_cast<int>('r'), 127};
    for (int key : keys) {
        if (key == igi::kKeyEnter || key == igi::kKeyEscape || key == igi::kKeyTab) {
            continue;
        }
        EXPECT_EQ(PauseMenuCharKeyAction(key, /*level_input_focused=*/false),
                  PauseKeyAction::Consume)
            << "char key " << key;
    }
}

TEST(PauseMenuInput, TheLevelSelectorIsTheOnlyEditorOnlyDeviation) {
    // Retail has no text input in a menu.  The editor's level-selector row owns
    // the keyboard while focused, and only then may digits and BACKSPACE be
    // typed.  With the row unfocused they are Consume like everything else.
    for (int key = '0'; key <= '9'; ++key) {
        EXPECT_EQ(PauseMenuCharKeyAction(key, /*level_input_focused=*/true),
                  PauseKeyAction::LevelInput) << "digit " << key;
        EXPECT_EQ(PauseMenuCharKeyAction(key, /*level_input_focused=*/false),
                  PauseKeyAction::Consume) << "digit " << key;
    }
    EXPECT_EQ(PauseMenuCharKeyAction(igi::kKeyBackspace, /*level_input_focused=*/true),
              PauseKeyAction::LevelInput);
    EXPECT_EQ(PauseMenuCharKeyAction(igi::kKeyBackspace, /*level_input_focused=*/false),
              PauseKeyAction::Consume);

    // ENTER still commits the level (it is KEY_ENTER on the focused row), and
    // ESCAPE still backs out, while the row owns the input.
    EXPECT_EQ(PauseMenuCharKeyAction(igi::kKeyEnter, true), PauseKeyAction::Activate);
    EXPECT_EQ(PauseMenuCharKeyAction(igi::kKeyEscape, true), PauseKeyAction::Back);

    // The row is row 4, and `PauseMenuLevelInputFocused` is the one predicate
    // both the draw layer and the input path use.
    EXPECT_EQ(igi::kPauseMenuLevelRow, 4);
    EXPECT_TRUE(igi::PauseMenuLevelInputFocused(true, igi::kPauseMenuLevelRow));
    EXPECT_FALSE(igi::PauseMenuLevelInputFocused(true, -1));
    EXPECT_FALSE(igi::PauseMenuLevelInputFocused(false, igi::kPauseMenuLevelRow));
}

TEST(PauseMenuInput, ThePolicyUsesGlutsOwnSpecialKeyCodes) {
    // GLUT's special-key numbering, read out of the header the editor compiles
    // against (third_party/GL/include/freeglut_std.h:135-155):
    //   GLUT_KEY_F1..F12 = 0x01..0x0C, GLUT_KEY_LEFT = 0x64, UP = 0x65,
    //   RIGHT = 0x66, DOWN = 0x67.
    // The policy must speak that numbering, not an invented one - the bug this
    // guards against would make every arrow key look like F-something.
    EXPECT_EQ(igi::kGlutKeyLeft, 0x0064);
    EXPECT_EQ(igi::kGlutKeyUp, 0x0065);
    EXPECT_EQ(igi::kGlutKeyRight, 0x0066);
    EXPECT_EQ(igi::kGlutKeyDown, 0x0067);

    // The character side must be the WM_CHAR values the engine's window
    // procedure receives before it maps them to KEY_* (src/loadscreen/keys.c:51-61):
    // VK_BACK 0x08, VK_TAB 0x09, VK_RETURN 0x0D, VK_ESCAPE 0x1B.
    EXPECT_EQ(igi::kKeyBackspace, 0x08);
    EXPECT_EQ(igi::kKeyTab, 0x09);
    EXPECT_EQ(igi::kKeyEnter, 0x0D);
    EXPECT_EQ(igi::kKeyEscape, 0x1B);
}

// ─── ITEM 2: the menu pointer is the GAME's resource ────────────────────────

