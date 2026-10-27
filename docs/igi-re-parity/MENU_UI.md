# The in-app menu, driven by the retail menu definition — `source/menu/menu_ui.*`, `source/app_menu.cpp` (task-14)

Status: **complete**. The editor's menu structure now comes from the retail menu
definition decoded by task-13 (`igi::MenuDefinition`, `source/menu/qvm_image.h`),
with an explicit, documented fallback to the rows the editor has always drawn.
Every claim about retail behaviour below is cited to IGI-RE by function, address
and line; every claim about editor behaviour is cited to this repository.

Scope of change:

| File | Role |
| --- | --- |
| `source/menu/menu_ui.h` / `.cpp` | the pure model: screens, entries, geometry, navigation, hit testing. No GL, no Windows types, no globals |
| `source/menu/menu_ui_draw.h` / `.cpp` | the OpenGL presentation of the model (editor-only) |
| `source/app_menu.cpp` | loads `menusystem/ingamemenu.qvm`, builds the model, records the editor wiring, and builds the fallback |
| `tests/test_menu_ui.cpp` | 30 host tests: structure, geometry, navigation, screens, fallback, retail corpus differentials |
| `CMakeLists.txt` | `tests/test_menu_ui.cpp` registration, `source/menu/*.*` in the editor target, `source/menu/menu_ui.cpp` + `source/app_menu.cpp` in the `igi_tests` host pool |
| this document | the decision record |

Not touched (owned elsewhere, and task-14 must not change them):
`source/renderer/renderer_draw.cpp`, `source/app_input_mouse.cpp`,
`source/runtime/pause_menu_layout.h`, `source/menu/qvm_image.h`. The last section
lists the diff those two files need in order to be driven by this model.

Companion document: `docs/igi-re-parity/MENU_SYSTEM.md` (task-13) describes how the
container is decoded. This document starts from the decoded definition.

## TL;DR

**Retail-driven:** the list of screens (900–903, 930–933), each screen's id, name,
frame rectangle, `On Press Escape` text, its widgets, each item's label, action
string, sound, font, colour index, selectability, enable/visible expressions, the
item order, the resolved geometry of every widget, and the navigation rules
(wrapping up/down, focus, activation, screen open/push/pop).

**Editor-side:** which editor behaviour each retail item is wired to. Only two are
defensible — the resume item and the quit-to-main-menu popup item — and the
reasoning is in [Editor wiring](#editor-wiring). Every other retail item is
present-but-inert and says why. Nothing in the retail path is invented: unmapped
items keep their retail action text verbatim and are reported as inert.

**Fallback:** when the retail file is absent or unreadable, the model is built from
the editor's own 16/21 rows (`BuildEditorFallbackMenu`), with the same labels, the
same order and byte-identical hit boxes, so the pause menu keeps working.

## The model

`igi::MenuUiModel` is pure data plus navigation. It is built once per menu
definition (`BuildMenuUiModel`) and queried per input event. It holds no GL state,
includes no Windows headers, and has no globals, so the host test lane links it
directly (`source/menu/menu_ui.cpp` in the `igi_tests` pool).

```cpp
MenuUiBuildOptions options;
options.viewport_width  = <current viewport>;
options.viewport_height = <current viewport>;
options.metrics = MenuUiMetricsForFonts(use_editor_font, system_font_size); // draw layer
app_menu::EditorMenu menu = app_menu::LoadEditorMenu(game_root, terrain_expanded, options);

menu.model.MoveDown();                       // KEY_DOWN
menu.model.MoveUp();                         // KEY_UP
MenuUiActivation a = menu.model.Activate();  // KEY_ENTER on the focused item
const std::string* on_escape = menu.model.Back();   // KEY_ESCAPE
int entry = menu.model.HitTest(mouse_x, mouse_y);
menu.model.FocusEntry(entry);                // hover focus
```

Coordinates: entries and screens carry rects in the retail 640×480 virtual space;
`MenuUiModel::ToViewport` maps them to the viewport with retail's own formulas;
`MenuUiToRetail` is the inverse. `HitTest` takes viewport coordinates and converts
them exactly as retail converts the pointer before the windows test themselves.

### Structure: what becomes an item

Retail collects the item list by walking the screen's widgets depth first: a
`MenuWindow` whose `selectable` flag is set **is** an item and the walk stops there;
otherwise it descends (`MenuManager_CollectItems` HOOK 0x4187F0,
src/menu/menumanager.c:594-612; `MenuManager_AddItems` HOOK 0x418840,
src/menu/menumanager.c:614-625). The item array is `items[32]`
(src/menu/menutypes.h:41), and the retail code has no bounds check; the model caps
at 32 and reports the truncation in `MenuUiModel::warnings`.

The model reproduces that walk, including the pruning:

* `MenuFrame` is selectable from its `Is Selectable` parameter
  (src/widget/menuframe.c:57; `MenuFrame_Run` HOOK 0x422A50 only acts when it is
  set).
* `MenuText` is selectable when it has a non-empty `On Click Script`
  (`MenuText_Changed` HOOK 0x420F40).
* `MenuTextConditional` is selectable from its action, and then sets
  `w.selectable = enabled` from its evaluated `isEnabled Expression`
  (`MenuTextConditional_Changed` HOOK 0x420640).

A dependent frame's descendants are still **drawn** (retail draws a frame's children)
but are not items, because the collection walk returned at the frame. The model
keeps both facts: `MenuUiEntry::item_candidate` / `collected` versus a drawable
entry in `MenuUiScreen::entries`.

### Geometry

Resolved with retail's own arithmetic, integers and all:

* `MenuWindow_Arrange` HOOK 0x4244B0 (src/widget/menuwindow.c:195-266) — the nine
  `LINEUPMODE_*` values, including the `count + 1` and `count - 1` denominators of
  the centre and fill-out modes.
* `MenuWindow_Place` HOOK 0x424420 (src/widget/menuwindow.c:172-193) — a child is
  placed by the lineup along the parent's lined-up axis, and centred
  (`((parent->w - child->w) >> 1) + parent->x`) on the other axis when the child's
  own `centreX`/`centreY` is set. Those flags are set by the per-widget create
  handlers: `MenuText`/`MenuTextConditional` (src/menuscreen/menutext.c:319-320,
  and the conditional's own create), `SlideBar` (src/menuscreen/slidebar.c:102-103), `ListBox`
  (src/menuscreen/listbox.c:101-102), `ToggleBox` (src/ui/togglebox.c:59-60),
  `DialogWindow` (src/menu/dialogwindow.c:68-69); `MenuFrame` keeps
  `MenuWindow_Create`'s zero (src/widget/menuwindow.c:99-125, `centreX`/`centreY` at
  109-110).
* `MenuWindow_GetSize` HOOK 0x424400 (src/widget/menuwindow.c:164-170) — a window's
  declared `Width`/`Height`; a text window is sized from its rendered label
  (`MAKE_LABEL`, src/menuscreen/menutext.c:317-330). The caller supplies that
  measurement (`MenuUiMetrics`), so the model and the draw layer agree.

Screen rects come from the definition: `MenuScreen` uses `Frame x0`/`y0`/`x1`/`y1`
as a rect (screen 900 is `85,80`–`555,410`), `DialogWindow` uses
`X-Pos`/`Y-Pos`/`Width`/`Height` (every retail popup is `220,180,200×120`).

Self-check: screen 900 declares its five item rows at `y = 101, 120, 139, 158, 177`
inside a `100,97,440,200` frame with `LineUpMode 1` (VERT_TOP) and `Y-Spacing 4`,
which is exactly what the lineup arithmetic produces for a 15 px line height — the
definition's hand-written y values agree with the engine's placement, and
`tests/test_menu_ui.cpp` pins both.

### Navigation

`MenuManager_ReadInput` HOOK 0x418580 (src/menu/menumanager.c:479-535), with the key
codes from src/loadscreen/keys.c:7-18:

| Key | Retail code | Model |
| --- | --- | --- |
| DOWN (-4) | `if (++i >= itemCount) i = 0;` | `MoveDown` (wraps to the first item) |
| UP (-3) | `if (--i < 0) i = itemCount - 1;` | `MoveUp` (wraps to the last item) |
| ENTER (-7) | forwarded to the focused window's key | `Activate` |
| LEFT/RIGHT (-1/-2) | forwarded to the focused window | not modelled (no item in the editor uses it) |
| ESCAPE (-8) | sets the input's `back` flag | `Back` (the screen's `On Press Escape` text) |

Activation is `RUN_ACTION` in src/menuscreen/menutext.c (`MenuText_Run` HOOK
0x421070, `MenuTextConditional_Run` HOOK 0x4207E0): the focused item runs its
`On Click Script` when the pointer clicked it or its key is ENTER, and plays its
`Click Sound Resource`. The model does **not** run anything — it reports the item
(`MenuUiActivation`) and the caller decides. A screen with no selectable item has
`item_index == -1`, moves return false, and activation reports no hit; the screen is
still drawn.

Focus is `MenuManager_SetFocus` HOOK 0x4186E0 (src/menu/menumanager.c:537-545).
`FocusEntry` refuses an entry that is not an item (retail can only focus collected
items) and reports `false` when the focus would not move.

Screens: `MenuManager_OpenScreen` HOOK 0x418710 (src/menu/menumanager.c:547-560)
resolves the id, arranges the window, recollects items and resets the index to 0;
`OpenScreen` mirrors that. Push/pop use the retail `screenStack[16]`
(src/menu/menutypes.h:76). The initial screen is the manager's
`Current menuscreen` (900 in the retail file); when that id is not in the
definition the model opens the first screen and warns instead of guessing.

### Mouse

`MenuWindow_HitTest` HOOK 0x4248A0 (src/widget/menuwindow.c:335-360) tests
`x < mx && y < my && mx < x+w && my < y+h` (the top-left edge is exclusive), and the
menu manager converts the pointer into the retail 640×480 space first
(src/menu/menumanager.c:390-392). `HitTest` does both, and prefers a collected item
over a plain drawable so a click inside a selectable frame focuses the frame, not
the label inside it.

## What is retail-driven, what is not

### Retail-driven, verified against the real file

`tests/test_menu_ui.cpp` (`MenuUiRetail.*`) loads
`/Users/haseeb-mir/Downloads/IGI/menusystem/ingamemenu.qvm` (or
`$IGI_GAME_PATH/menusystem/ingamemenu.qvm`) and asserts:

* 8 screens, ids `900, 901, 902, 903, 930, 931, 932, 933`, names `Main Menu`,
  `Graphics Configuration`, `Sound Configuration`, `Controls Configuration`,
  `Reset Controls`, `quit to main`, `restart mission`, `` (empty), types
  `MenuScreen` ×4 then `DialogWindow` ×4.
* Screen 900 `Main Menu`, frame `85,80,470,330`, `On Press Escape` =
  `MenuManager_LeaveMenus(900, MENUMANAGER_IDLE)`, 5 items in retail order
  `Resume Game`, `Restart Game`, `Sound`, `Controls`, `Quit to Main Menu` from the
  widget names `resume`, `restart`, `sound`, `controls`, `quit`.
* The resume item is `MenuTextConditional` with `isEnabled Expression`
  `!LevelFlow_LevelFailed()` and `isVisible Expression` `TRUE`; font `font3.fnt`;
  `ColourIndex 2`; item rect `(281, 101, 77, 15)`; the quit item's rect y is 177.
* Its action strings are carried verbatim, including the trailing newline some
  retail strings have (`"MenuManager_ActivatePopupScreen(931)\n"`).
* The title text `Game Paused` is drawn but is not an item.
* Both readers agree: `MenuDefinition::FromQvmFile` and
  `QVM_DecompileToString` → `MenuDefinition::FromDecompiledSource` produce the same
  81 widgets with the same ids, types, names, field names/types and values, and the
  two models built from them are item-for-item identical.

### Editor-side decisions

1. **Labels are rendered verbatim, not localised.** Retail resolves a text resource
   through `MenuManager_GetString` HOOK 0x418EA0 (src/menu/menumanager.c:832-852),
   which looks up `LOCAL:<language>/<name>`; in the retail definition the resource
   key *is* the English display string, and `LANGUAGE/ENGLISH/menusystem.res` maps
   `LOCAL:english/Resume Game` to `Resume Game`. The editor has no menu-language
   selection, so it renders the key. Documented deviation, not a guess.
2. **Script expressions are not evaluated.** There is no retail script VM in the
   editor. An item that declares `isEnabled Expression` / `isVisible Expression` is
   reported selectable with `condition_unverified = true`, and both expression
   strings are carried verbatim. The model does not pretend the condition passed or
   failed.
3. **The enable expression does not hide items.** Retail sets
   `w.selectable = enabled`, which removes a disabled item from the item array. The
   model exposes that path explicitly (`SetEntryEnabled`, which leaves the entry
   drawn but out of the item list) but does not apply an unevaluated expression.
4. **`MenuManager_PushScreen` / `PopScreen` / popup activation are model
   operations, not editor actions.** The retail script uses them to navigate; the
   model offers `OpenScreen`, `PushScreen`, `PopScreen` for the caller to wire.
   `MenuManager_DeactivatePopuScreen()` therefore reports inert with that reason
   instead of inventing an editor action.
5. **Screen ids are matched exactly.** A script addresses a screen by its task id;
   `MenuUiScreen::id` comes from `MenuWidget::id`, so a mismatched id fails loudly
   (`OpenScreen` returns false) instead of opening a neighbouring screen.

## Editor wiring

`source/app_menu.cpp` records, per collected item, which editor behaviour it drives.
The rule is: map an item only when the retail action's *effect* is something the
editor already does; otherwise inert. That leaves two:

| Retail item | Retail action (verbatim) | Why it maps | Editor behaviour |
| --- | --- | --- | --- |
| `resume` on screen 900 | `MenuManager_LeaveMenus(900, MENUMANAGER_IDLE)` | `MenuManager_LeaveMenus` HOOK 0x417790 stores the second argument as the next state (src/game/menumanager.c:249-261); `MENUMANAGER_IDLE` is state 9 (src/menu/menumanager.c:94-113) and `MenuManager_Run` handles `MENU_IDLE` by calling `idle(mm)` (src/menu/menumanager.c:429-430), i.e. the menu closes and the game continues — the editor's "Resume" row | `MenuUiAction::Resume` |
| `Yes` on popup 931 (`quit to main`) | `MenuManager_LeaveMenus(0, MENUMANAGER_QUITTOMAINMENU)` | `MENUMANAGER_QUITTOMAINMENU` is state 11; `MenuManager_Run` answers `MENU_QUIT_TO_MAIN` with `Flow_SetRequest(6)` (src/menu/menumanager.c:436-438) — leaving the game. The editor has no main menu, and its Quit row already leaves the program (`source/app_input_mouse.cpp:590`, `glutLeaveMainLoop`) | `MenuUiAction::Quit` |

Everything else is **present but inert**: it is drawn, it is in the item list, it can
take focus, and activating it reports `MenuUiAction::None` with `inert_reason` set.
The reasons are the retail action text, so the decision is auditable:

| Screen | Item | Retail action | Why inert |
| --- | --- | --- | --- |
| 900 | `Restart Game` | `MenuManager_ActivatePopupScreen(932)` | opens the restart-confirmation popup; the editor has no mission restart |
| 900 | `Sound` | `MenuManager_PushScreen(902)` | the editor has no sound-configuration screen |
| 900 | `Controls` | `MenuManager_PushScreen(903)` | the editor has no controls-configuration screen |
| 900 | `Quit to Main Menu` | `MenuManager_ActivatePopupScreen(931)` | only opens the confirmation popup; the popup's `Yes` is the wired quit |
| 901 | `Change Device`, `Change Res.`, `Gamma` (frames) | — (frames; their `ListBox`/`SlideBar` children carry the actions) | display configuration does not exist in the editor |
| 901, 902, 903 | `OK` / `Cancel` | `MenuManager_PopScreen(TRUE/FALSE)` | a menu-stack operation; use `MenuUiModel::PopScreen` |
| 903 | `Reset to Default` | `MenuManager_ActivatePopupScreen(930)` | opens a popup; the editor has no key-binding reset |
| 930 | `Yes` / `No` | `ControlsMenu_954.isReset = 1, ToggleBox_957.isOn = FALSE, MenuManager_DeactivatePopuScreen()` / `MenuManager_DeactivatePopuScreen()` | resets retail controls state the editor does not have |
| 931, 932, 933 | `Yes` / `No` | `MENUMANAGER_RESTARTLEVEL` / `MenuManager_DeactivatePopuScreen()` | no mission restart; close via `PopScreen` |

`tests/test_menu_ui.cpp` (`MenuUiRetail.OnlyTheRetailItemsTheEditorImplementsAreWired`)
enforces the invariant: every collected item of every screen is either wired
(`inert == false`) or inert with a non-empty reason — no third state.

### Editor-added entry: the one deliberate addition

The editor has functions retail does not (`Weather`, `Lightmap`,
`Calculate Lightmaps`, `Auto Save`, `Logging`, `Log Level`, `Music`, the terrain
options, `Reset Level`, `Save Level`). Injecting them into the retail screens one by
one would invent structure the definition does not declare, so they are **not**
injected — but leaving them only in the fallback would make them unreachable on
every install that has the retail file, which is the normal case, and would make
this feature a net loss for the editor's user.

The resolution, decided by the lead, is exactly **one** added entry and nothing else:

* The retail main screen (900) gains one entry labelled `Editor Options`, appended
  **after** every retail entry. It is marked `MenuUiEntry::editor_added = true`, and
  `MenuUiScreen::editor_added` marks the screen it opens.
* Its action is `MenuUiAction::OpenEditorOptions`; the screen it opens is
  `app_menu::kEditorOptionsScreenId` (`-2000`, negative so it can never collide with
  a retail task id), which is the editor's own row list built by
  `BuildEditorFallbackMenu` — 16 rows collapsed, 21 expanded, the same labels and
  the same actions the editor uses today. One implementation, no duplicated
  behaviour.
* Its geometry is editor-side by definition and is placed with retail's own rules:
  below the last retail item (`last.y + last.h +` the frame's declared `Y-Spacing`
  of 4) and centred with the retail `centreX` rule
  (`((frame.w - w) >> 1) + frame.x`). Nothing retail-declared moves.
* `MenuUiModel::warnings` records the addition, so it is visible in any diagnostic
  dump as well as in the model.
* The fallback path (no retail file) has no added entry: its screen *is* the editor
  menu, and every row there is `editor_added`.

`tests/test_menu_ui.cpp`
(`MenuUiRetail.EditorOptionsIsTheOnlyAddedEntry`) proves both halves: every retail
screen's id, name, type, frame, `On Press Escape` and **every** declared entry's
label, widget name, widget type, action string, rect and selectability are
byte-identical to a model built straight from the definition with no app wiring
(only the main screen's entry list is one longer), and exactly one screen and
exactly one entry carry `editor_added`.

## The fallback

`app_menu::BuildEditorFallbackMenu(terrain_expanded, options)` builds a one-screen
model from the rows `source/renderer/renderer_draw.cpp` has always drawn, and
`app_menu::LoadEditorMenu` uses it whenever the retail file is missing or
unreadable, recording why in `EditorMenu::reason` / `MenuUiModel::fallback_reason`.

The fallback is not decorative; it is byte-compatible with the existing pause menu:

* **Same rows, same order, same actions:** 16 collapsed, 21 with terrain options
  expanded (`igi::PauseMenuRowCount`, source/runtime/pause_menu_layout.h:26-30),
  `Resume`, `Mode`, `Collision`, `Font`, `Select Level`, `Auto Save`, `Logging`,
  `Log Level`, `Music`, `Weather`, `Lightmap`, `Calculate Lightmaps`,
  `Terrain Options`, then (expanded) `Texture`, `Height`, `Discard`, `Fog`,
  `Fog Intensity`, then `Reset Level`, `Save Level`, `Quit`.
* **Same panel:** the screen frame is `BuildPauseMenuLayout`'s
  `menu_left/menu_top/menu_width/menu_height`, so `ToViewport` recovers the exact
  panel the renderer draws.
* **Same hit boxes:** the legacy test is inclusive
  (`IsPauseMenuRowHit`: `centre - radius <= y <= centre + radius`,
  source/runtime/pause_menu_layout.h:87-92) while the model uses retail's strict
  comparison, so the fallback's rect is grown by one pixel on every side. The test
  asserts, for every row and *every* y in the legacy band, that `HitTest` returns
  that row, and that one pixel outside the band returns no row.
* **Labels are the state-independent base names.** The live suffix
  (`Mode: [Editor]`, `Collision: [Normal]`, `Font: [System]`,
  `Weather: [Rain]`, `Lightmap: [<mode>]`, the `[X]`/`[ ]` terrain checkboxes) is
  still appended by the draw layer, where it lives today. The model carries the row
  identity, not the live state text.

## Rendering

`source/menu/menu_ui_draw.cpp` draws the model with the primitives and fonts the
pause menu already uses (`source/runtime/pause_menu_font.*`):

* Panel and border: the editor's own emerald panel and green border (editor chrome,
  not retail data).
* Retail text: colour from `gMenuColors` (`ALIAS(gMenuColors, store_53B778, 0)`,
  src/menu/typewriterbox.c:616-617; `extern const Color gMenuColors[]`,
  src/menu/menuvars.h:9). Its first five entries are `(0,0,0)`, `(128,128,128)`,
  `(0,128,0)`, `(128,95,0)`, `(64,128,64)`; the registered names cover the first
  four (`TEXT_BLACK`..`TEXT_ORANGE`, src/menuscreen/menutext.c:301-304) and the
  fifth is unnamed. The table is declared with 10 entries but only these five are
  colour data, so indices 5-9 are **not** treated as colours here (they draw white
  like any other unverified index). Dim bit `0x10` drawn at half alpha
  (src/menuscreen/menutext.c:362).
* Focus: `MenuWindow_DrawEnd` HOOK 0x424980 (src/widget/menuwindow.c:368-409) — focused
  `r = b = 0.5, g = 1.0`, unfocused `r = b = 0, g = 1`.
* Fallback rows: centred in the panel, green normally and yellow when focused,
  matching what the existing pause menu draws.
* Fallback panel: `DrawMenuUiScreen` takes the panel rect from the screen frame, so
  with `style.title`/`style.subtitle` set the editor's header and separator come
  back at the same offsets the current code uses.

Not drawn, on purpose: the eight `MenuFrame` sprite resources and `MenuScreen`
background pictures (the editor does not resolve retail sprites) and colour indices
beyond the verified five (white instead of a guess).

`menu_ui_draw.cpp` is the generic presenter of the model and is **not** on the live
path: the live menu draws with `renderer_draw.cpp`'s own retail frame, palette and
font helpers (so the panel the user sees is the one the pause menu always had), and
keeps the six bespoke rows of the editor's own menu — the font/level/autosave/
log-level/terrain-intensity spinners with their `[-]`/`[+]` steppers — which are
editor furniture rather than menu items. `menu_ui_draw.cpp` remains as the
standalone renderer for a caller that has a model but not the editor's chrome; the
duplication of the two `DrawText` bodies is the known gap recorded below.

**Known duplication (agreed with the lead, to remove later):** the five text helpers
in `renderer_draw.cpp:164-330` are `static`, so `menu_ui_draw.cpp` re-implements the
two `DrawText` bodies it needs. The lead chose not to widen this change into the
renderer; the fix is to move those helpers into a shared header both files include.

## The live wiring (applied — issue #74, second half)

The pause menu the editor shows is the loaded tree. This is what the diff that used
to be listed here as "not applied" became:

| file | role |
| --- | --- |
| `source/app_pause_menu.cpp` | the App's side of the menu: load once, rebuild, hit test, focus, spinner sub-rectangles, row activation, Escape |
| `source/app.h` | `app_menu::EditorMenu pause_menu_`, the load/rebuild/activate methods |
| `source/renderer/renderer_draw.cpp` | draws the model's current screen with the retail frame the editor already had |
| `source/app_input_mouse.cpp` | wheel → the spinner row under the pointer; click → `HandlePauseMenuClick`; motion → hover focus |
| `source/app_input_keyboard.cpp` | DOWN/UP → `MoveDown`/`MoveUp`, ENTER → `ActivateFocusedPauseMenuRow`, ESCAPE → `PauseMenuBack` |
| `source/runtime/pause_menu_rows.*` | the live label and spinner width of each editor row, one source for the draw layer and the click test |

**Which screen the pause menu opens on.** `app_menu::OpenEditorPauseMenu`
(`source/app_menu.cpp`) positions the model on the editor's own screen
(`kEditorOptionsScreenId`, −2000) every time the menu opens, so the editor's own
strings are what the user sees first — the decision recorded in "Editor-added
entry". The loaded retail screens are one row away: the editor's screen carries the
`Game Menu` row, which is `MenuManager_PushScreen` HOOK 0x417690
(src/game/menumanager.c:226-235) to the definition's declared screen (900), and
Escape is `MenuManager_PopScreen` HOOK 0x417710 (:237-247) back. With nothing on the
stack Escape closes the menu, which is what screen 900's own `On Press Escape`
(`MenuManager_LeaveMenus(900, MENUMANAGER_IDLE)`) does in retail.

**The three rows the editor adds to its own screen when a retail definition is
loaded** (they do not exist in the fallback, because there is nothing to open, edit
or save):

| row | action | what it does |
| --- | --- | --- |
| `Game Menu` | `OpenGameMenu` | opens the definition's declared screen (900) and draws it from the definition: its own title, its five items, their colours and the `Editor Options` entry |
| `Menu Text` | `EditMenuText` | sets the retail main screen's title `Text Resource` to the next string of `app_menu::kEditorMenuTitleCycle` through the mutable accessors of `MenuDefinition`, then rebuilds the model so the change is drawn |
| `Save Menu` | `SaveMenu` | `app_menu::SaveEditorMenu(menu, "", &error)` — writes `menu.definition` back to the container it was read from |

The edit and the save are the issue's acceptance criteria in one path: an edit made
through `qvm_image.h`'s mutable accessors changes what the model draws and is what
`SaveEditorMenu` writes. `tests/test_menu_ui.cpp` (`MenuUiLive`) proves it host-side;
the live path is proved by a Wine run (below).

**What the draw layer keeps.** The retail frame (`MenuScreen_DrawFrame` HOOK
0x421CD0), the palette (`gMenuColors`), the row pitch and the font metrics are
unchanged; the rows are the model's. The editor's own screen keeps its two title
lines ("IGI EDITOR" / "PAUSED"); a retail screen draws the definition's own title
instead, as one of its rows. A retail row is drawn from the definition —
`display_label` when the language archive resolved the key
(`MenuManager_GetString` HOOK 0x418EA0), else the raw key — at the window's own
position, which is what the engine draws. An editor row is drawn with the live state
appended by `PauseMenuRowLabel`, exactly as before.

**What is still not executed.** Retail script expressions and retail script *actions*
stay opaque: an item whose action is not one the editor implements is drawn, is
focusable, and activating it reports `inert_reason` (in the status line and the log)
rather than being guessed at. That is why `Sound`, `Controls`, `Restart Game` and
`Quit to Main Menu` do nothing on screen 900 — the reasoning is the "Editor wiring"
table above.

## How this was verified

```
python3 tools/igi-re-parity/tu-check.py source/menu/menu_ui.cpp source/app_menu.cpp
  ==> OK (each file, both targets)      checked 4 translation unit(s), 0 failed

IGI_HOST_NO_CACHE=1 IGI_HOST_TESTS=tests/test_menu_ui.cpp bash scripts/run-host-tests.sh
  ==> PASS test_menu_ui   30 passed  0 failed (30 tests, exit 0)
      suites run: 1  failed: 0  not host-portable: 0
```

The suite covers: screen/item structure from the definition; the lineup geometry
(including the self-check against the definition's own declared y values); viewport
mapping; caller-supplied text metrics; wrapping navigation in both directions;
activation and `On Press Escape` reporting; a screen with no selectable item; an
empty and a screen-less definition; disabling and re-enabling an item; focus by
entry index; the mouse rectangle test and its exclusive edges; a selectable frame
pruning its descendants; the 32-item cap; unknown screen ids; the fallback's row
count, order, actions and hit boxes; a missing retail file; the retail corpus
structure; the wiring invariant; and the two readers agreeing widget for widget.

`source/menu/menu_ui_draw.cpp` is editor-only and is not linked by the host lane; it
was compiled with the exact cross-build flags of the editor target
(`i686-w64-mingw32-g++`, exit 0). The Windows build gate itself was not run by
task-14 (the shared `build-mingw/` tree is owned by the build-gate stream).

### The live path, verified under Wine (#74, second half)

The i686 editor was run under Wine against a **copy** of the retail install
(`Config::Init` rewrites `<IGI>/editor/qed/*.qvm` and `igi1ed.log`, so the install is
never the game root of a run), with the window driven by `PostMessage` helpers
(`scratch/pause-menu-smoke/key_helper.c` for keys, `scratch/wine-smoke/click_helper.c`
for clicks). The editor's own log is the evidence:

```
[INFO] pause menu: loaded Z:\...\scratch\pause-menu-smoke\igi/menusystem/ingamemenu.qvm
       (9 screens, screen 900 has 6 rows, editor screen -2000)
[INFO] pause menu: viewport 1680x1050 panel 605,328 470x403 rows 19 pitch 18
       first row y 376 scale 1.500000
[INFO] pause menu: opened
[INFO] pause menu: activated row 'Game Menu' (OpenGameMenu, screen -2000)
[INFO] pause menu: activated row 'Resume Game' (Resume, screen 900)
[INFO] pause menu: closed
[INFO] pause menu: activated row 'Menu Text' (EditMenuText, screen -2000)
[INFO] pause menu: Menu text edited (not saved yet)
[INFO] pause menu: activated row 'Save Menu' (SaveMenu, screen -2000)
[INFO] pause menu: Menu saved: Z:\...\scratch\pause-menu-smoke\igi/menusystem/ingamemenu.qvm
```

`Resume Game` is the *definition's* row on screen 900 — its label comes from the loaded
tree, not from the editor — and activating it resumed, which is
`MenuManager_LeaveMenus(900, MENUMANAGER_IDLE)`. `Menu Text` and `Save Menu` are the
editor's own added rows on its own screen.

and the container that click wrote:

```
$ python3 -c "...ingamemenu.qvm..."     # the copy, after the run
IGI EDITOR present: True
Game Paused present: False
size 11307                              # the shipped file is 11308 ("Game Paused" is one char longer)
```

So the live UI path — not a test — built the model from the retail container,
mutated the loaded definition through `qvm_image.h`'s accessors, and wrote it back.
Running the repository's own reader over that written file (`igi_tests
--gtest_filter=MenuQvm.*:MenuUiSave.*` with `IGI_GAME_PATH` pointing at the copy)
passes every container test; the only two failures are the two that assert the
*shipped* title text, which is exactly the edit.

## Known gaps and what is not verified

1. **Retail sprite/background resources are not resolved**, so a MenuFrame renders
   as its children only. The definition's `Sprite Resource` and `Background` strings
   are carried in the model (`MenuUiScreen::background`) for a future pass.
2. **The colour table is only verified for five indices** (`gMenuColors[0..4]`);
   index 4 has no registered constant name, and entries 5-9 of the stored table are
   not colour data. Anything unverified draws white rather than a guess.
3. **Non-text widget sizes** (`ListBox`, `SlideBar`, `ToggleBox`) are approximated by
   their declared `Width`/`Height`, because retail computes them in per-widget
   arrange handlers that are not reproduced. In the retail file this only affects
   children of the graphics/controls screens (no editor behaviour today).
4. **Retail script expressions are never evaluated** (see above): a conditional item
   is always selectable in the editor.
5. **The modal wiring is done** (see "The live wiring"): the pause menu runs on the
   model. The GL run under Wine exercised the load, the hit test, the row activation
   and the save (the log and the written container are quoted below); **no rendered
   pixel is verified** — this host gives Wine an OpenGL 2.1 context (see
   `GL21_FALLBACK.md`), and the menu is drawn with fixed-function calls, so what the
   frame looks like is unverified.
6. **Item sounds are not played** (`Click Sound Resource` is carried verbatim);
   wiring sound needs the editor's audio path, which is outside this task.
