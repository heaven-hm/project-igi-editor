#pragma once

// Presentation and navigation model for the editor's in-app menu.
//
// The model is built from the retail menu definition (`igi::MenuDefinition`,
// source/menu/qvm_image.h), which is the decoded form of the game's
// `menusystem/ingamemenu.qvm`.  Nothing here executes retail script: the
// definition's strings are carried through verbatim and the model only
// reproduces the *structure* and *navigation* the retail menu manager
// implements.
//
// Oracle: IGI-RE, the verified C decompilation of retail IGI.exe.  Every
// behaviour below cites a function and its retail address:
//
//   * item collection   `MenuManager_CollectItems` HOOK 0x4187F0
//                       (src/menu/menumanager.c:594-612) and
//                       `MenuManager_AddItems` HOOK 0x418840
//                       (src/menu/menumanager.c:614-625): depth-first over the
//                       screen's children; a `MenuWindow` whose `selectable`
//                       flag is set is an item and descent stops there,
//                       otherwise the walk descends into the children.  The
//                       item array is `items[32]` (src/menu/menutypes.h:41).
//   * focus             `MenuManager_SetFocus` HOOK 0x4186E0
//                       (src/menu/menumanager.c:537-545).
//   * keyboard nav      `MenuManager_ReadInput` HOOK 0x418580
//                       (src/menu/menumanager.c:479-535): KEY_DOWN (-4)
//                       advances with wraparound to 0, KEY_UP (-3) steps back
//                       with wraparound to itemCount-1, KEY_ENTER (-7) and
//                       KEY_LEFT/RIGHT (-1/-2) are forwarded to the focused
//                       window, KEY_ESCAPE (-8) sets the input's `back` flag.
//                       The key codes are src/loadscreen/keys.c:7-18.
//   * activation        `RUN_ACTION` in src/menuscreen/menutext.c, used by
//                       `MenuText_Run` HOOK 0x421070 and
//                       `MenuTextConditional_Run` HOOK 0x4207E0: the focused
//                       item runs its `On Click Script` when the pointer
//                       clicked it or when its key is KEY_ENTER (-7), and
//                       plays its `Click Sound Resource`.
//   * screens           `MenuManager_OpenScreen` HOOK 0x418710
//                       (src/menu/menumanager.c:547-560): the screen id is
//                       resolved with `Task_FindById` HOOK 0x4F1030, the
//                       window is arranged, the item list is recollected and
//                       the index is reset.  Popups
//                       (`MenuManager_OpenPopup` HOOK 0x4188B0 /
//                       `MenuManager_ClosePopup` HOOK 0x418940,
//                       src/menu/menumanager.c:627-660) swap the item list and
//                       restore it on close.  The screen stack is
//                       `screenStack[16]` (src/menu/menutypes.h:76).
//   * geometry          `MenuWindow_Arrange` HOOK 0x4244B0
//                       (src/widget/menuwindow.c:200-266) and
//                       `MenuWindow_Place` HOOK 0x424420
//                       (src/widget/menuwindow.c:161-176) position children by
//                       the parent's LINEUPMODE_*, and
//                       `MenuWindow_GetSize` HOOK 0x424400
//                       (src/widget/menuwindow.c:155-159) reports a child's
//                       declared size.  Screen coordinates are the retail
//                       640x480 virtual space mapped with `SCREEN_X`/`SCREEN_Y`
//                       (src/menuscreen/menuscreen.h:141-142).
//   * mouse             `MenuWindow_HitTest` HOOK 0x4248A0
//                       (src/widget/menuwindow.c:335-360): the rectangle test
//                       is `x < mx && y < my && mx < x+w && my < y+h`, and a
//                       hover that has just entered focuses the window.
//
// What the editor deliberately does NOT do, and why it is not guessed at:
//
//   * Retail script expressions (`isEnabled Expression`, `isVisible Expression`,
//     `On Click Script`, `On Press Escape`) are opaque strings here.  The
//     editor has no retail script VM, so it cannot evaluate them; an item that
//     declares an enable expression is reported as selectable with
//     `condition_unverified == true` rather than being silently enabled or
//     disabled.  Activation returns the item; the caller decides what to do.
//   * `MenuManager_GetString` HOOK 0x418EA0 (src/menu/menumanager.c:850-870)
//     localises a text resource through `LOCAL:<language>/<name>`.  The
//     definition already carries the resource key, which in the retail file is
//     the English display string, so the editor renders it verbatim and does
//     not read the language resources.
//   * MenuFrame's eight frame sprites (src/widget/menuframe.c:130-190) and the
//     MenuScreen background picture are not resolved; the model reports the
//     names and the draw layer renders its own chrome.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "qvm_image.h"

namespace igi {

// ─────────────────────────────────────────────────────────────────────────────
// retail field names
// ─────────────────────────────────────────────────────────────────────────────
//
// The names come from the script's own `Task_DeclareParameters` calls, which is
// what `MenuField::name` carries.  They are the retail parameter names in
// declaration order:
//   MenuFrame           src/widget/menuframe.c:49-57
//   MenuScreen          src/menuscreen/menuscreen.c (Background, Frame x0..y1,
//                       On Press Escape, On Init Data, On Change Data)
//   MenuText            src/menuscreen/menutext.c:296-300
//   MenuTextConditional src/menuscreen/menutext.c:72-78
//   MenuManager         src/menu/menumanager.c (Current menuscreen, Resource
//                       path, isOwnDisplay)
//   DialogWindow        src/menu/dialogwindow.c (X-Pos, Y-Pos, Width, Height,
//                       SwapKeys)
namespace menu_field {
inline constexpr char kSpriteResource[] = "Sprite Resource";
inline constexpr char kXPos[] = "X-Pos";
inline constexpr char kYPos[] = "Y-Pos";
inline constexpr char kWidth[] = "Width";
inline constexpr char kHeight[] = "Height";
inline constexpr char kLineUpMode[] = "LineUpMode";
inline constexpr char kXSpacing[] = "X-Spacing";
inline constexpr char kYSpacing[] = "Y-Spacing";
inline constexpr char kIsSelectable[] = "Is Selectable";
inline constexpr char kBackground[] = "Background";
inline constexpr char kFrameX0[] = "Frame x0";
inline constexpr char kFrameY0[] = "Frame y0";
inline constexpr char kFrameX1[] = "Frame x1";
inline constexpr char kFrameY1[] = "Frame y1";
inline constexpr char kOnPressEscape[] = "On Press Escape";
inline constexpr char kTextResource[] = "Text Resource";
inline constexpr char kFontResource[] = "Font Resource";
inline constexpr char kColourIndex[] = "ColourIndex";
inline constexpr char kOnClickScript[] = "On Click Script";
inline constexpr char kClickSoundResource[] = "Click Sound Resource";
inline constexpr char kIsEnabledExpression[] = "isEnabled Expression";
inline constexpr char kIsVisibleExpression[] = "isVisible Expression";
inline constexpr char kCurrentMenuScreen[] = "Current menuscreen";
}  // namespace menu_field

// Retail `LINEUPMODE_*` (src/widget/menuwindow.c:41-52, registered as script
// constants in `MenuWindow_Init` HOOK 0x423FC0).
enum class MenuUiLineUp {
    None = 0,
    VertTop = 1,
    VertCentre = 2,
    VertBottom = 3,
    HorLeft = 4,
    HorCentre = 5,
    HorRight = 6,
    VertFillOut = 7,
    HorFillOut = 8,
};

// The editor behaviour an item is wired to.  These are the rows the editor
// already implements in `renderer_draw.cpp` / `app_input_mouse.cpp`; the enum
// only names them so the model can carry a decision instead of the caller
// re-deriving one from a label.  `None` means the editor has no behaviour for
// the item: it stays visible and navigable but activating it does nothing.
enum class MenuUiAction {
    None,
    Resume,
    ToggleMode,
    ToggleCollision,
    ToggleFont,
    SelectLevel,
    ToggleAutoSave,
    ToggleLogging,
    CycleLogLevel,
    ToggleMusic,
    CycleWeather,
    ToggleLightmaps,
    CalculateLightmaps,
    ToggleTerrainOptions,
    ToggleTerrainTexture,
    ToggleTerrainHeight,
    ToggleTerrainDiscard,
    ToggleTerrainFog,
    AdjustFogIntensity,
    ResetLevel,
    SaveLevel,
    Quit,
    // Editor-only: opens the editor's own options screen (app_menu's added entry).
    OpenEditorOptions,
};

const char* MenuUiActionName(MenuUiAction action);

struct MenuUiRect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

// One drawable widget of a screen, in the retail arrangement order (the order
// `MenuWindow_Arrange` walks: children first, depth first).
struct MenuUiEntry {
    std::string widget_type;   // "MenuFrame", "MenuText", "MenuTextConditional", ...
    std::string widget_name;   // the widget's own task name, verbatim
    int task_id = 0;           // the retail task id, `MenuWidget::id` (-1 when the script gave none)
    std::string label;         // own "Text Resource" value, verbatim; empty for containers
    std::string item_label;    // for collected items: own label, else the first
                               // descendant label (retail draws the child text)
    MenuUiRect rect;           // resolved rect, retail 640x480 space
    // True when this repository added the entry on top of the retail definition,
    // never when the definition declared it.  See MENU_UI.md "Editor-added entry".
    bool editor_added = false;
    bool is_container = false; // has children
    bool has_text = false;     // declares a text field with a non-empty value

    // ── label text, resolved from the game's language .RES archives ──────────
    // `label`/`item_label` above stay VERBATIM retail script data (the
    // `Text Resource` key, e.g. "Restart Game").  The engine does not draw that
    // key: `MenuManager_GetString` (HOOK 0x418EA0, src/menu/menumanager.c:832-852)
    // builds "LOCAL:<LanguageName>/<key>" and draws the archive's text, which is
    // frequently different -- the retail English archive answers "Restart Mission"
    // for the "Restart Game" key.  These fields carry that resolution, and are left
    // empty when the entry declares no text, so a consumer finding `display_label`
    // empty must fall back to `label`.
    // See docs/igi-re-parity/MENU_TEXT_RES.md.
    std::string label_key;       // the raw key == `label`, kept for diagnostics
    std::string display_label;   // resolved language text, or the engine's miss string
    bool label_resolved = false; // display_label came from a language archive

    // Retail selectability.  `selectable` is what the retail window would set:
    // MenuFrame from "Is Selectable" (src/widget/menuframe.c:57, read by
    // `MenuFrame_Run` HOOK 0x422A50), MenuText/MenuTextConditional from a
    // non-empty "On Click Script" (src/menuscreen/menutext.c
    // `MenuText_Changed` HOOK 0x420F40, `MenuTextConditional_Changed` HOOK
    // 0x420640).
    bool selectable = false;
    // Reached by the collection walk before it pruned at an outer selectable
    // window, so this widget is a candidate for the retail item list.
    bool item_candidate = false;
    // In the retail item list: `item_candidate && enabled`.
    bool collected = false;
    bool enabled = true;
    bool visible = true;
    // An enable/visible expression is declared, so retail's value depends on
    // the script VM.  The editor cannot evaluate it; the item is reported as
    // selectable and this flag says the condition is unverified.
    bool condition_unverified = false;
    std::string enable_expression;
    std::string visible_expression;

    // Retail action data, verbatim and never executed here.
    std::string action;        // "On Click Script"
    std::string sound;         // "Click Sound Resource"
    std::string font;          // "Font Resource"
    int colour_index = 1;      // "ColourIndex"

    // Editor wiring.  `editor_action == None && inert` means the item is
    // present but inert: rendered, focusable, and activating it does nothing.
    MenuUiAction editor_action = MenuUiAction::None;
    bool inert = false;
    std::string inert_reason;
};

struct MenuUiScreen {
    int id = 0;                     // retail task id (900, 901, ..., 930, ...)
    std::string type;               // "MenuScreen" or "DialogWindow"
    std::string name;               // the widget's own name
    bool popup = false;             // type == "DialogWindow"
    std::string background;         // "Background" resource name, verbatim
    MenuUiRect frame;               // screen rect, retail space
    std::string on_back;            // "On Press Escape", verbatim
    MenuUiLineUp lineup = MenuUiLineUp::None;
    std::vector<MenuUiEntry> entries;   // drawable widgets, arrangement order
    std::vector<int> selectable;        // indices into `entries`, collection order
    // True for a screen this repository added; retail screens never set it.
    bool editor_added = false;
};

// Text measurement.  Retail sizes a text window from the rendered label
// (`MAKE_LABEL` in src/menuscreen/menutext.c calls `Text_GetWidth` /
// `Text_GetHeight`), so the caller supplies the same measurement the draw layer
// uses.  The defaults (7 px per character, 15 px line height) match
// `PauseMenuFontMetrics`'s defaults and exist so the model stays usable without
// a font.
struct MenuUiMetrics {
    std::function<int(const std::string&)> text_width;
    int line_height = 15;
};

struct MenuUiBuildOptions {
    int viewport_width = 640;
    int viewport_height = 480;
    MenuUiMetrics metrics;
    // Retail's item array is 32 entries (src/menu/menutypes.h:41).  The retail
    // collection code has no bounds check; the model stops at the cap and
    // reports it in `warnings`.
    int max_items = 32;
};

// What a key/click did, so the caller can act without re-deriving state.
struct MenuUiActivation {
    bool hit = false;                 // an item was focused and activated
    int entry_index = -1;             // index into the current screen's `entries`
    MenuUiAction editor_action = MenuUiAction::None;
    bool inert = false;
    std::string action;               // retail "On Click Script", verbatim
};

struct MenuUiModel {
    bool ok = false;
    std::string error;                // why `ok == false`
    std::string source;               // "retail:<path>" or "fallback:<what>"
    bool fallback = false;            // the definition was not the retail file
    std::string fallback_reason;      // set when `fallback` is true
    std::vector<std::string> warnings;

    std::vector<MenuUiScreen> screens;
    int viewport_width = 640;
    int viewport_height = 480;
    int max_items = 32;
    // The manager's "Current menuscreen" value, verbatim (retail data).
    int declared_screen_id = 0;

    int screen_index = 0;             // current screen
    int item_index = -1;              // index into the current screen's `selectable`
    std::vector<int> screen_stack;    // retail `screenStack[16]`

    // ── queries ──────────────────────────────────────────────────────────────
    const MenuUiScreen* CurrentScreen() const;
    const MenuUiScreen* FindScreen(int id) const;
    int SelectableCount() const;
    // Index into `entries` of the focused item, or -1.
    int FocusedEntryIndex() const;
    const MenuUiEntry* FocusedEntry() const;

    // Retail 640x480 space -> viewport space (`SCREEN_X`/`SCREEN_Y`,
    // src/menuscreen/menuscreen.h:141-142).
    MenuUiRect ToViewport(const MenuUiRect& rect) const;
    // Topmost entry under a viewport-space point, using retail's rectangle test
    // (src/widget/menuwindow.c:335-360).  Prefers a collected item, so a click
    // inside a selectable frame focuses the frame and not its child label.
    // Returns an index into `entries`, or -1.
    int HitTest(int viewport_x, int viewport_y) const;

    // ── transitions ──────────────────────────────────────────────────────────
    // KEY_DOWN (-4): advance, wrapping to the first item.
    bool MoveDown();
    // KEY_UP (-3): step back, wrapping to the last item.
    bool MoveUp();
    // Mouse hover focus (`MenuManager_FocusItem` HOOK 0x418FE0).  Accepts an
    // index into `entries`; only a collected item can take focus.  Returns true
    // when the focus changed.
    bool FocusEntry(int entry_index);
    // KEY_ENTER (-7) on the focused item.  Does not execute anything: the
    // result carries the item's editor action and retail action text.
    MenuUiActivation Activate() const;
    // KEY_ESCAPE (-8): the current screen's "On Press Escape" text, verbatim.
    const std::string* Back() const;

    // `MenuManager_OpenScreen` HOOK 0x418710: switch to `id`, recollect and
    // reset the index.  Returns false when no screen has that id.
    bool OpenScreen(int id);
    // Retail `MenuManager_PushScreen` / `PopScreen` (used by the retail script;
    // the editor only follows when the caller asks).  The stack holds 16
    // entries, src/menu/menutypes.h:76.
    bool PushScreen(int id);
    bool PopScreen();
    bool ScreenOnStack(int id) const;

    // Enable/disable an entry.  Retail sets `selectable` from the evaluated
    // "isEnabled Expression" (`MenuTextConditional_Changed` HOOK 0x420640), so a
    // disabled item leaves the item list; the model mirrors that.
    void SetEntryEnabled(int entry_index, bool enabled);
    void SetEntryVisible(int entry_index, bool visible);
};

// Builds the model from a decoded definition.  `options.metrics` measures text
// the way the draw layer does; `ok` is false only for a definition that has no
// usable screen (an empty definition, or one whose parse failed).
MenuUiModel BuildMenuUiModel(const MenuDefinition& definition,
                             const MenuUiBuildOptions& options);

// True when the widget type is one of the retail menu window types the model
// understands well enough to place (it is a `MenuWindow` descendant in retail:
// src/widget/menuwindow.c `MenuWindow_Init` HOOK 0x423FC0 and the per-widget
// `*_Init` functions).
bool IsMenuUiWidgetType(const std::string& type);

// Viewport space -> retail 640x480 space; the inverse of `MenuUiModel::ToViewport`
// and the mapping `MenuManager_Run` applies to the pointer
// (src/menu/menumanager.c:390-392).
MenuUiRect MenuUiToRetail(const MenuUiRect& rect, int viewport_width, int viewport_height);

// ─────────────────────────────────────────────────────────────────────────────
// application wiring (implemented by source/app_menu.cpp)
// ─────────────────────────────────────────────────────────────────────────────
//
// These declarations live here because the menu model is what they hand back and
// because task-14's file list does not add a header of its own; `source/app_menu.cpp`
// is their only implementation.
namespace app_menu {

// The retail in-game menu inside a game data root.
inline constexpr char kRetailMenuRelativePath[] = "menusystem/ingamemenu.qvm";

// The editor's own options screen, and the single entry the retail main screen
// gains so the editor's functions stay reachable when the retail file is present.
// The id is negative so it can never collide with a retail task id.
inline constexpr int kEditorOptionsScreenId = -2000;
inline constexpr char kEditorOptionsLabel[] = "Editor Options";

// How many of an `EditorMenu`'s labels resolved against the language archives.
// Mirrors `menu_text::LookupCounters` as plain counts, so this header does not have
// to pull in the .RES reader.
struct EditorMenuLabelStats {
    bool language_data_loaded = false; // at least one language archive was read
    std::size_t labels = 0;            // entries carrying a `Text Resource` key
    std::size_t resolved = 0;          // text came from an archive
    std::size_t engine_path = 0;       // "LOCAL:<LanguageName>/<key>"
    std::size_t final_component = 0;   // last path component only (documented deviation)
    std::size_t engine_miss = 0;       // key absent -> "RM: <key>" (engine parity)
    std::size_t no_language_data = 0;  // no archive -> the raw key
};

struct EditorMenu {
    MenuUiModel model;
    // The definition `model` was built from, kept so the menu can be edited and
    // saved back (`menu/menu_qvm_writer.h`).  `ok == false` when the retail file
    // was not used, i.e. exactly when `fallback` is true; the writer refuses a
    // definition that is not ok, so a fallback menu is never written over the
    // retail file.
    MenuDefinition definition;
    bool retail = false;      // built from the retail definition
    bool fallback = false;    // built from the editor's own row list
    std::string reason;       // why the retail definition was not used
    std::string reader;       // which reader produced the definition
    // The language the labels were resolved against, and the outcome counts.
    // Empty / all-zero when no language archive was found.
    std::string language;
    EditorMenuLabelStats label_stats;
};

// Loads `game_root/menusystem/ingamemenu.qvm` and builds the editor menu from it.
//
// When the file is absent or unreadable the model is built from the editor's own
// row list instead (`BuildEditorFallbackMenu`) and `fallback` is true with
// `reason` set.  The fallback is explicit and documented; it never invents retail
// data.
EditorMenu LoadEditorMenu(const std::string& game_root, bool terrain_expanded,
                          const MenuUiBuildOptions& options);

// Saves an edited retail menu back to its container: the editor's entry point to
// the write half of issue #74 (`menu/menu_qvm_writer.h`).
//
// ORACLE: the engine's own producer chain, which `menu/menu_qvm_writer.h`
// translates -- `QscGen_Compile` HOOK 0x4BBCB0 (src/qvm/qscgen.c:86-142) ->
// `QvmAsm_OpPush` HOOK 0x4C0A10 (src/qasm/asm.c:74-124) -> `QvmAsm_Assemble`
// HOOK 0x4BB270 (src/qvm/qvmasm.c:103-276) -- and the loader that must accept the
// result, `Qvm_Load` HOOK 0x4BD220 (src/qvm/qvm.c:433-486).
//
// `menu.definition` is what is written, so an edit made through the mutable
// accessors of `MenuDefinition`/`MenuWidget` (`qvm_image.h`) is the edit that lands.
// `path` empty means the file the definition was read from
// (`MenuDefinition::source`).
//
// Refuses, with `*error` set and nothing written, when the menu was not built from
// a retail container (`menu.definition.ok` is false, which is exactly the fallback
// case) or when the path is empty.  A fallback menu therefore can never overwrite
// `menusystem/ingamemenu.qvm`.
bool SaveEditorMenu(const EditorMenu& menu, const std::string& path, std::string* error);

// The documented fallback: the rows the pause menu has always drawn, in the same
// order and with the same hit boxes, as a `MenuUiModel` so one code path can drive
// the menu.  Labels are the rows' state-independent base names; the live state
// suffix (`Mode: [Editor]`, `Weather: [Rain]`, ...) is still appended by the draw
// layer, exactly as before.
MenuUiModel BuildEditorFallbackMenu(bool terrain_expanded,
                                    const MenuUiBuildOptions& options);

// The editor behaviour an item is wired to, from its retail "On Click Script":
//   `MenuManager_LeaveMenus(<id>, MENUMANAGER_IDLE)`         -> Resume
//   `MenuManager_LeaveMenus(<id>, MENUMANAGER_QUITTOMAINMENU)` -> Quit
// Anything else is `None`; see docs/igi-re-parity/MENU_UI.md for the reasoning.
MenuUiAction EditorActionForRetailItem(const MenuUiEntry& entry);

// Row count of the fallback menu, and the row labels in order.
int EditorFallbackRowCount(bool terrain_expanded);

}  // namespace app_menu

}  // namespace igi
