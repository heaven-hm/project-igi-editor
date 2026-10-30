#pragma once

/******************************************************************************
 * @file    source/runtime/pause_menu_input_policy.h
 * @brief   Which keys the pause menu accepts, and what it does with the rest.
 *
 * Oracle: IGI-RE (/Users/haseeb-mir/Documents/Code/IGI-RE), the decompiled
 * retail IGI.exe.  The retail in-game (pause) menu is driven by exactly one
 * function, and by exactly the keys that function pops off the engine's own
 * key queue:
 *
 *   src/menu/menumanager.c:488-551   MenuManager_ReadInput HOOK 0x418580
 *
 *       in->back = 0;
 *       in->select = 0;
 *       ... wheel from gMouse.buttons & 0x100 / 0x200 ...
 *       if (!mm->inputEnabled)
 *           return;
 *       while (Keys_GetCount()) {
 *           int key = Keys_Pop();
 *           switch (key) {
 *           case -11: in->select = 1; break;                  // TAB
 *           case -8:  in->back   = 1; break;                  // ESCAPE
 *           case -7: case -2: case -1:                        // ENTER, RIGHT, LEFT
 *               if (mm->focus) mm->focus->key = key;
 *               break;
 *           case -4:                                          // DOWN
 *               if (++i >= mm->itemCount) i = 0;
 *               MenuManager_SetFocus(mm, i);
 *               Sound_PlayName("menu_high", 0, 0);
 *               break;
 *           case -3:                                          // UP
 *               if (--i < 0) i = mm->itemCount - 1;
 *               MenuManager_SetFocus(mm, i);
 *               Sound_PlayName("menu_high", 0, 0);
 *               break;
 *           }
 *       }
 *
 *   The negative codes are the engine's typed-key values
 *   (src/loadscreen/keys.c:10-22):
 *
 *       KEY_LEFT = -1   KEY_RIGHT = -2   KEY_UP   = -3   KEY_DOWN = -4
 *       KEY_BACKSPACE = -5   KEY_DELETE = -6   KEY_ENTER = -7
 *       KEY_ESCAPE    = -8   KEY_HOME   = -9   KEY_END   = -10
 *       KEY_TAB       = -11
 *
 *   and they are the ONLY values that ever reach that queue: the window
 *   procedure that fills it (Keys_WndProc HOOK 0x48A730,
 *   src/loadscreen/keys.c:47-71) maps eleven WM_KEYDOWN virtual codes  // WM_KEYDOWN
 *   (0x08 0x09 0x0D 0x1B 0x23 0x24 0x25 0x26 0x27 0x28 0x2E) to those
 *   negative codes and pushes every other WM_CHAR verbatim:  // WM_CHAR
 *
 *       if (msg == 0x100) {  // WM_KEYDOWN
 *           switch (key) {
 *           case 0x08: Keys_Push(KEY_BACKSPACE); return 1;
 *           case 0x09: Keys_Push(KEY_TAB);       return 1;
 *           case 0x0D: Keys_Push(KEY_ENTER);     return 1;
 *           case 0x1B: Keys_Push(KEY_ESCAPE);    return 1;
 *           case 0x23: Keys_Push(KEY_END);       return 1;
 *           case 0x24: Keys_Push(KEY_HOME);      return 1;
 *           case 0x25: Keys_Push(KEY_LEFT);      return 1;
 *           case 0x26: Keys_Push(KEY_UP);        return 1;
 *           case 0x27: Keys_Push(KEY_RIGHT);     return 1;
 *           case 0x28: Keys_Push(KEY_DOWN);      return 1;
 *           case 0x2E: Keys_Push(KEY_DELETE);    return 1;
 *           }
 *       } else if (msg == 0x102) {  // WM_CHAR
 *           if (key != 0x08 && key != 0x0D && key != 0x1B)
 *               Keys_Push(key);
 *       }
 *
 *   The queue is drained in full on every run (`while (Keys_GetCount())`), so
 *   a key the switch has no case for is not "passed on" to anything: it is
 *   popped and discarded.  That is the rule this header encodes.
 *
 * The actions those keys drive:
 *
 *   DOWN / UP   MenuManager_SetFocus HOOK 0x4186E0
 *               (src/menu/menumanager.c:553-562) moves the focus through
 *               `mm->items[]`, wrapping at both ends exactly as the two
 *               counters above say.
 *   ENTER       the key is stored on the focused window (`mm->focus->key`)
 *               and consumed by that window's own run handler.  For a menu
 *               item that is RUN_ACTION (src/menuscreen/menutext.c:170-179):
 *
 *                   if (((mt)->w.clicked || (mt)->w.key == -7) &&
 *                       VarString_HasExpr(&(mt)->action)) {
 *                       VarString_EvalReal(&(mt)->action, mt);
 *                       Sound_PlayName(...);
 *                   }
 *
 *               i.e. ENTER runs the focused item's `On Click Script`.
 *   ESCAPE      sets `in->back`; MenuScreen_Run
 *               (src/widget/menuscreen.c:7-18) evaluates the screen's
 *               `On Press Escape` when it is set.  Screen 900 of
 *               `menusystem/ingamemenu.qvm` declares that as
 *               `MenuManager_LeaveMenus(900, MENUMANAGER_IDLE)`
 *               (HOOK 0x417790, src/game/menumanager.c:249-261), which leaves
 *               the menus.
 *   TAB         sets `in->select`.  Nothing in IGI-RE ever reads
 *               `MenuInput::select` - the only three writes are the
 *               initialisation and the TAB case in MenuManager_ReadInput
 *               (src/menu/menumanager.c:494, :525) and the struct declaration
 *               (src/menu/menutypes.h:14).  TAB is therefore accepted and
 *               ignored, which is why the editor accepts it too.
 *   LEFT/RIGHT  stored on the focused window like ENTER; the two menu items
 *               the editor has (MenuText / MenuTextConditional) only test for
 *               -7 in RUN_ACTION, so on the pause screen they are accepted
 *               and ignored.  (MenuManager_ReadInput only swaps them for
 *               up/down while a horizontal popup is open -
 *               `DialogWindow_IsHorizontal(mm->popupTask)`, :506-522 - and the
 *               editor's pause menu has no popup.)
 *   TAB / LEFT / RIGHT
 *               reach the focused window's `key` (LEFT/RIGHT) or the input's
 *               `select` (TAB) and change nothing on this screen, but the
 *               engine does pop them, so the editor accepts them too rather
 *               than treating them as "unknown".
 *
 * Everything else - the function keys, letters, digits, HOME, END, DELETE,
 * PAGE_UP, PAGE_DOWN, INSERT, the modifiers - has no case in that switch and
 * reaches no menu code.  The editor therefore CONSUMES it while the pause menu
 * is up instead of letting it fall through to an editor shortcut behind the
 * overlay.
 *
 * DELIBERATE EDITOR-ONLY DEVIATION (one, and it is not a key):
 *
 *   The level selector row is the editor's own furniture; retail has no text
 *   input anywhere in the menu.  While that row owns the input
 *   (`PauseMenuLevelInputFocused`, source/runtime/pause_menu_state.h) the
 *   digits and BACKSPACE are accepted, because that is the only way the row can
 *   be typed into.  The deviation is marked at its use site in
 *   source/app_input_keyboard.cpp and documented in
 *   docs/igi-re-parity/PAUSE_MENU_STYLE.md.
 *****************************************************************************/

namespace igi {

// GLUT's special-key codes for the four arrows.  The engine's codes are the
// negative KEY_* values above; this header names the GLUT side so callers do
// not have to know both.
constexpr int kGlutKeyLeft = 100;
constexpr int kGlutKeyUp = 101;
constexpr int kGlutKeyRight = 102;
constexpr int kGlutKeyDown = 103;

// What the pause menu does with a key.
enum class PauseKeyAction {
	// Not a key the pause menu knows: consumed and ignored, never passed on.
	Consume,
	// KEY_UP (-3) / KEY_DOWN (-4): move the focus, wrapping.
	MoveUp,
	MoveDown,
	// KEY_ENTER (-7): run the focused item's action.
	Activate,
	// KEY_ESCAPE (-8): the screen's `On Press Escape`.
	Back,
	// KEY_TAB (-11), KEY_LEFT (-1), KEY_RIGHT (-2): popped and discarded by
	// MenuManager_ReadInput's own switch (TAB sets `select`, which nothing
	// reads; LEFT/RIGHT reach the focused window's `key`, which only -7 acts on).
	Ignore,
	// Editor-only: a digit or BACKSPACE for the level-selector row, which owns
	// the input while `PauseMenuLevelInputFocused` is true.
	LevelInput,
};

// The ASCII keys the engine's window procedure pushes as WM_CHAR values
// (src/loadscreen/keys.c:63-67).  GLUT delivers them to Input_OnKeyboard.
constexpr int kKeyTab = 9;
constexpr int kKeyBackspace = 8;
constexpr int kKeyEnter = 13;
constexpr int kKeyEscape = 27;

// True while the level-selector row owns the keyboard.  Kept as a named
// predicate so the policy below reads as the engine's switch plus the one
// documented editor deviation.
constexpr bool PauseMenuAcceptsLevelText(bool level_input_focused) noexcept {
	return level_input_focused;
}

// The engine's key contract, for a printable/character key.  `level_input_focused`
// is `PauseMenuLevelInputFocused(...)` - the editor-only deviation above.
constexpr PauseKeyAction PauseMenuCharKeyAction(int key,
                                               bool level_input_focused) noexcept {
	if (level_input_focused && (key == kKeyBackspace || (key >= '0' && key <= '9'))) {
		return PauseKeyAction::LevelInput;
	}
	switch (key) {
	case kKeyEnter:
		return PauseKeyAction::Activate;
	case kKeyEscape:
		return PauseKeyAction::Back;
	case kKeyTab:
		// KEY_TAB (-11): `in->select = 1`, and nothing reads it.
		return PauseKeyAction::Ignore;
	default:
		// No case in MenuManager_ReadInput's switch: popped and discarded.
		return PauseKeyAction::Consume;
	}
}

// The engine's key contract, for a GLUT special key.  Retail's typed keys
// include LEFT and RIGHT (both stored on the focused window and acted on by
// nobody on this screen) and UP/DOWN (the focus move).  Every other special
// key - F1..F12, HOME, END, PAGE_UP, PAGE_DOWN, INSERT - has no case and is
// discarded by the same `while (Keys_GetCount())` drain.
constexpr PauseKeyAction PauseMenuSpecialKeyAction(int key) noexcept {
	switch (key) {
	case kGlutKeyUp:
		return PauseKeyAction::MoveUp;
	case kGlutKeyDown:
		return PauseKeyAction::MoveDown;
	case kGlutKeyLeft:
	case kGlutKeyRight:
		return PauseKeyAction::Ignore;
	default:
		return PauseKeyAction::Consume;
	}
}

// True when the action runs the focused row's `On Click Script` - i.e. the
// `(mt)->w.key == -7` half of RUN_ACTION (src/menuscreen/menutext.c:175).
constexpr bool PauseKeyActionActivates(PauseKeyAction action) noexcept {
	return action == PauseKeyAction::Activate;
}

}  // namespace igi
