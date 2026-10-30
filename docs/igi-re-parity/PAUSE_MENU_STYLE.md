# The pause menu's retail style — `source/runtime/pause_menu_style.h`

Status: **complete** for the values the retail in-game menu is authored and drawn
with. Every constant in `source/runtime/pause_menu_style.h` is either a field of
`menusystem/ingamemenu.qvm` screen 900 or a literal in the engine's own drawing
code, and each one is cited below by function + HOOK address + file:line of
IGI-RE. `tests/test_pause_menu_style.cpp` (15 host tests, 2 of them corpus-backed)
asserts the constants and reads the retail files to check the constants against
them.

The editor's own strings are untouched: the panel still says "IGI EDITOR",
"PAUSED", "Resume", "Mode: [Game Play]", … The game's text ("Game Paused",
"Resume Game", "Restart Game", "Sound", "Controls", "Quit to Main Menu") is **not**
used anywhere *as one of the editor's own rows*.

One thing did change after this document was written: the ROW LIST is no longer
hard-coded in the renderer. It is the loaded menu tree
(`igi::app_menu::EditorMenu`, issue #74's live wiring — see `MENU_UI.md` "The live
wiring"), and the editor's own screen is one screen of it. That is also why the
game's text was briefly *reachable*: the editor's screen carried a `Game Menu` row
that opened the definition's own screen 900, which the draw layer rendered from the
definition — its title, its five items and their colours — because that is what the
engine draws there.

That row has since been **removed**. Relabelling the retail rows (the rule below) made
the drawn text the editor's, but the screen that opened was still the game's menu, and
the user's requirement is that the pause menu is the editor's only. The editor's own
screen now carries no row that opens a retail screen, and
`App::ActivateFocusedPauseMenuRow` refuses `MenuUiAction::OpenGameMenu` if it is ever
reached (`source/app_pause_menu.cpp`). Screen 900 stays in the model as data — the
`Menu Text` and `Save Menu` rows edit and write that tree — and the mapping below is
what the editor would draw if it were ever shown, which is what its tests assert.

## The editor's own strings come first (the rule this file exists to keep)

Screen 900 stays a retail screen: the same five items, in retail order, with the
retail frame, palette, metrics, pitch and colour indices, and with the definition's
own text still carried verbatim on the model (`MenuUiEntry::label` — the script
key — and `display_label` — the language archive's answer). What the editor
**draws** is its own text, never the game's:

| retail widget (screen 900) | the game's text (still on the model, never drawn) | what the editor draws |
| --- | --- | --- |
| the title `MenuText` | `Game Paused` | `IGI EDITOR` (the editor's own panel title; the `Menu Text` row's own strings are shown once the definition carries one) |
| `resume` | `Resume Game` | `Resume` |
| `restart` | `Restart Game` → `Restart Mission` | `Reset Level` |
| `sound` | `Sound` | `Music` |
| `controls` | `Controls` | `Keybindings` |
| `quit` | `Quit to Main Menu` | `Quit` |
| any other retail text | as declared | `Not in Editor` |

The labels are the editor's own rows' labels (`kFallbackRows`, the same strings
`source/renderer/renderer_draw.cpp` has always drawn), the editor's own panel title,
and its own name for the one concept it has no row for (`Keybindings`, the title of
its KEYBINDINGS overlay). `app_menu::EditorLabelForRetailEntry`
(`source/app_menu.cpp`) is the single mapping, and `tests/test_menu_ui.cpp` /
`tests/test_menu_labels.cpp` hold it to the rule: every retail text row has editor
text, and that text is never the game's.

The rows keep their retail actions: `resume` still resumes, `quit` still quits, and
the three the editor has no behaviour for stay inert and say why in the status line
when activated. Only the text changed — no row was deleted, and no row's action was
changed.

## TL;DR

| what | value | source |
| --- | --- | --- |
| panel rect | 85, 80 → 555, 410 in the 640×480 menu space | `ingamemenu.qvm` screen 900 |
| panel size | 470 × 330 px, **not** scaled to the display | `SCREEN_X`/`SCREEN_Y`, `src/menuscreen/menuscreen.h:141-142` |
| panel placement | frame centre (245) is 5 px below the menu-space centre (240) | derived from the rect above |
| title band | 21 px from the panel top to the rule | `MenuScreen_DrawFrame` `:175-179` |
| title frame | 100, 77, 440 × 26, LineUpMode 0 | `ingamemenu.qvm` screen 900 |
| item frame | 100, 97, 440 × 200, LineUpMode 1 | `ingamemenu.qvm` screen 900 |
| row pitch | font line height + 4 = 25 px for `font3.fnt` | `MenuWindow_Create` `:101-102`, `MenuWindow_Arrange` `:225-229` |
| font | `font3.fnt`: line height 21, blank advance 6 | the retail archive, read by `Sprite_Load` |
| border + rule colour | r = b = 0, **g = 0.75** | `MenuScreen_DrawFrame` `:152-163`, `:190-191`, `:208-213` |
| wash | g 1.0 → 0.0, alpha 0.2 → 0.05 | `MenuScreen_DrawFrame` `:184-185`, `:215-223` |
| fill | black, alpha 0.66 | `MenuScreen_DrawFrame` `:225-227` |
| title colour | `TEXT_WHITE` (128,128,128) → **1.0** after `gColorScale` 1/128 | `gMenuColors` DATA 0x53B778 |
| item colour | `TEXT_GREEN` (0,128,0) → **(0,1,0)** | `gMenuColors` DATA 0x53B778 |
| focused item | c·0.5 + 64 → **(0.5,1,0.5)** plus a glow at c·0.2 | `DRAW_LABEL` `:203-205`, `:227-228` |

## 1. The load path — how the game loads the menu

The in-game (pause) menu is not a special case in the engine: it is a menu
manager built from a script.

```
src/levelflow/levelflow.c:139
    lf->menu = MenuManager_New(*(void **)(Game_Get() + 0x44),
                               "LOCAL:menusystem/ingamemenu.qsc", 0, 0, 0);
```

`menusystem/ingamemenu.qvm` is that script's compiled container — the file the
editor reads. The main menu is the same manager from `mainmenu.qsc`
(`src/flow/flow.c:229`), which is why both containers carry the same widget
schema. `MenuLoader_Changed` (HOOK 0x419210, `src/menu/menuloader.c:53-62`) is
the run-time variant: a script can push another menu file by setting `load`.

`MenuManager_New` (HOOK 0x418B00, `src/menu/menumanager.c:713-800`) loads the
script, runs it with `Task_New` bound to the parent task, and finds the
`MenuManager` child it created. The screen's `Resource path`
("LOCAL:menusystem/ingamemenu.res") is then loaded by
`MenuManager_LoadResources` (HOOK 0x417D50, `src/menu/menumanager.c:238-257`),
which walks the archive's directory with `Resource_ForEach`.

`MenuManager_AddResource` (HOOK 0x417E20, `src/menu/menumanager.c:260-296`)
classifies each entry by extension; `"FNT"` becomes a font:

```c
} else if (kind == 2) {
    strcpy(font->name, info->name + i);
    Sprite_Load(&font->sprite, name);
    mm->fontCount++;
}
```

and `MenuManager_FindFont` (HOOK 0x418E40, `src/menu/menumanager.c:820-829`) is
the case-insensitive lookup a `MenuText`'s `Font Resource` uses. That is why the
editor resolves `font3.fnt` out of `ingamemenu.res` rather than off disk: it is
the same path the engine takes.

The editor's reader for the container is `igi::MenuDefinition::FromQvmFile`
(`source/menu/qvm_image.h`), whose own oracle is documented in
`docs/igi-re-parity/MENU_SYSTEM.md`.

## 2. The authored geometry

Decoded from `menusystem/ingamemenu.qvm` (the tests assert every field):

```
Task_New(900, "MenuScreen", "Main Menu", "", 85, 80, 555, 410,
         "MenuManager_LeaveMenus(900, MENUMANAGER_IDLE)", "", "",
    Task_New(-1, "MenuFrame", "", "", 100, 77, 440, 26, 0, 4, 4, FALSE,
        Task_New(-1, "MenuText", "", "Game Paused", "font3.fnt", 1, "", "")),
    Task_New(-1, "MenuFrame", "", "", 100, 97, 440, 200, 1, 4, 4, FALSE,
        ... five MenuText / MenuTextConditional items, font3.fnt, ColourIndex 2))
```

The argument order is the C `ParamTable` order, registered by `MenuScreen_Init`
(HOOK 0x421750, `src/menuscreen/menuscreen.c:58-67`) and `MenuFrame_Init`
(HOOK 0x4223D0, `src/widget/menuframe.c:48-58`).

Two things about the geometry matter and are easy to get wrong:

* **The menu is a fixed pixel box, not a fraction of the display.**
  `SCREEN_X(x) = (Display_GetMode()->width >> 1) - 320 + x` and
  `SCREEN_Y(y) = (Display_GetMode()->height >> 1) - 240 + y`
  (`src/menuscreen/menuscreen.h:141-142`). The engine centres a virtual 640×480
  space and does **not** scale it, so at 1920×1080 the retail pause panel is
  still 470×330 physical pixels. The editor's panel is the same 470 px wide and
  centred by the same rule; the tests check `menu_left == (width >> 1) - 320 + 85`
  at 640, 800, 1280 and 1920.

* **The rule and the first item are the same line.** The rule is at
  `y0 + 20` where `y0 = frameY0 + 1` (`MenuScreen_DrawFrame` `:175-179`), i.e.
  panel top + 21. The item frame starts at panel top + 17 and
  `MenuWindow_Arrange`'s `LINEUPMODE_VERT_TOP` puts its first child at
  `frame.y + spacingY` (`src/widget/menuwindow.c:225-229`), i.e. panel top + 21.
  `kRetailTitleRuleOffset == kRetailFirstRowOffset` is a `static_assert`.

## 3. Spacing

`MenuWindow_Create` (HOOK 0x4241C0, `src/widget/menuwindow.c:99-123`) seeds
every window with `spacingX = spacingY = 4`, and the retail frames restate it
(`Y-Spacing` 4). `MenuWindow_Arrange` (HOOK 0x4244B0) turns `LineUpMode` into a
running cursor and `MenuWindow_Place` (HOOK 0x424420) advances it:

```c
/* MenuWindow_Arrange, :225-229 */
case LINEUPMODE_VERT_TOP:
    l.spacingY = w->spacingY;
    l.y = w->y + l.spacingY;
    l.vertical = 1;
    break;

/* MenuWindow_Place, :182-187 */
if (l->vertical) {
    w->y = l->y;
    l->placed = 1;
    l.y += w->h + l->spacingY;
    return;
}
```

A child's height is its own text height — `MAKE_LABEL` sets
`w.h = Text_GetHeight(label)` (`src/menuscreen/menutext.c:126-149`) — and
`Text_GetHeight` is `Grid_Height` = `Sprite_Height(sprite) * rows`
(HOOK 0x4B7E00 `src/res/text.c:139-143`; HOOK 0x4B7C20 `src/res/grid.c:209-213`)
with one row (`Text_New`, HOOK 0x4B6EC0). So the row pitch is exactly the font's
line height plus the frame's Y-Spacing: **21 + 4 = 25 px**.

Horizontally, `MenuWindow_Place`'s centreX branch (`:179-181`) centres each item
in its frame, and MenuText is created with `centreX = centreY = 1`
(`src/menuscreen/menutext.c:311-319`). The item column is inset 15 px from the
panel's left edge (100 − 85).

### Where the text sits inside the row

The engine's text draw takes the **line origin**:
`QVertex_DrawFrame` places a glyph at `v[0].y = f->y + y`
(HOOK 0x4B50B0, `src/res/qvertex.c:45-57`) with `y` handed down by `Grid_Draw`
(HOOK 0x4B7930, `src/res/grid.c:172-195`), and `MenuWindow_Place` sets the row's
`y` to the row's top. So in retail the row's top **is** the line origin.

The editor's helpers (`DrawRetailPauseText`, `DrawFontText` in
`source/renderer/renderer_draw.cpp`) take a baseline-ish y and lift it with
`y_gl += 11`. `kRetailTextLineLift = 11` names that literal, and the layout
therefore draws row *i* at `retail_row_top + 11`. For `font3.fnt` the result is
exact: a capital has `f->y = 2` and `f->h = 15`, so the drawn glyph spans
`row_top + 2 … row_top + 17` inside a `row_top … row_top + 21` line box, which is
what the engine draws.

## 4. The font

Every `MenuText` on screen 900 names `font3.fnt`. Read out of
`menusystem/ingamemenu.res` (`RES_Extract` → `FNT_ParseBytes`, the editor's
reader for the engine's `Sprite_Load` FONT branch, HOOK 0x4B6720,
`src/res/sprite.c:171-207`):

| `FontHeader` field | value | how the engine uses it |
| --- | --- | --- |
| `proportional` | 1 | per-glyph advances (`Grid_New` `:43` sets `proportional = !Sprite_IsFixed`) |
| `advance` | 6 | the advance of a character the TRAN map leaves empty (`Grid_UpdateRow` `:93-105`) |
| `w` | 24 | fixed cell width, unused while proportional |
| `h` | **21** | the line height (`Grid_Height` `:209-213` × 1 row) |

`tests/test_fnt_parser.cpp` pins those two numbers and the engine's own width for
`"Resume Game"` (104 px) against the real file.

## 5. The frame

`MenuScreen_DrawFrame` (HOOK 0x421CD0, `src/menuscreen/menuscreen.c:168-229`) is
the whole frame, in this order:

1. **a 5-vertex line strip** — the closed rectangle — drawn one pixel inside and
   below the declared rect (`:175-178`) with the colour from `set_vertex`
   (`:152-163`): `r = 0`, `g = 0.75`, `b = 0`, `a` = the fade (`:190-191`).
2. **a 2-vertex line strip** at `line = y0 + 20` — the title rule — with the same
   colour (`:179`, `:208-213`).
3. **a green-to-black wash** over the whole rect: `g = 1.0` at the top,
   `g = 0.0` at the bottom, alpha `a1 = a * 0.2` → `a2 = a * 0.05`
   (`:184-185`, `:215-223`).
4. **a flat black fill** at `a * 0.66`, taken only when the screen has no
   background picture (`:225-227`). Screen 900's `Background` is `""`, so the
   in-game menu takes it.

The fade is `min(MenuManager_GetFade(menu) * 0.125, 1)` (`:180-183`); the editor's
pause menu has no fade counter and uses the settled value, 1.

The editor reproduces all four, with two documented departures:

* **Draw order.** The engine calls the children's draw *before*
  `MenuScreen_DrawFrame` (`MenuScreen_DrawHandler` HOOK 0x421C20,
  `src/menuscreen/menuscreen.c:131-150`), so retail's own order puts the wash and
  the 0.66-alpha black fill **over** the labels. The labels go through
  `Text_DrawAlt` (HOOK 0x4B6F50, `src/res/text.c:102-107`) with
  `flags | 0x40000000`. **What that bit does is not in IGI-RE**: the strip driver
  that receives the flags is registered by name with
  `RenderMode_CreateMethod` (HOOK 0x4B0E70, `src/res/qvertex.c:8`) and is not
  decompiled, so whether retail's labels survive the fill cannot be established
  from the oracle. The editor therefore draws the frame's background *first* and
  its labels last, which keeps the labels legible and leaves every colour, alpha
  and coordinate retail's.
* **Line width.** The engine's strips are handed to that same undecoded driver,
  so the border's and the rule's line width has **no engine oracle**. The editor
  keeps the 2.5 px it already used for the border and 1 px for the rule.

## 6. The colours, and the 1/128 scale

`gMenuColors` is DATA 0x53B778 (`src/menu/typewriterbox.c:616-617`). Its first
five entries are `(0,0,0) (128,128,128) (0,128,0) (128,95,0) (64,128,64)`, and
`MenuText_Init` (HOOK 0x420CC0, `src/menuscreen/menutext.c:301-304`) registers the
first four as `TEXT_BLACK`, `TEXT_WHITE`, `TEXT_GREEN` and `TEXT_ORANGE`.
`MenuText_Draw` (HOOK 0x421130, `:353-364`) selects with `mt->color & 0xF`.

**The values are 0..255 in a 1/128 domain, not 0..255 in a /255 domain.**
Everything the engine hands to a vertex is multiplied by `gColorScale` =
`0.0078125f` = 1/128 (DATA 0x534AB0, `src/res/qvertex.c:189`, applied in
`QVertex_DrawFrame` `:45-57`) or by `COLOR_SCALE` = 1/128 (`src/math/draw.c:12`).
So `(0,128,0)` is **full green**, and `(128,128,128)` is **white**. The editor's
menu code previously divided by 255, drawing every menu colour at about half
brightness; `kRetailColourScale = 1.0f / 128.0f` fixes that.

`DRAW_LABEL` (`src/menuscreen/menutext.c:196-230`) finishes the colour:

* a focused label at full fade is brightened to `c * 0.5 + 64` (`:203-205`),
  which for `TEXT_GREEN` is `(64,128,64)` = `(0.5,1,0.5)` — exactly
  `gMenuColors[4]`;
* it also gets a glow quad behind it at `c * 0.2` (`:227-228`) sized
  `x0 = SCREEN_X(w.x) - w.w * 0.4`, `y0 = SCREEN_Y(w.y) - w.h * 0.2`,
  `x1 = x0 + w.w * 1.8`, `y1 = y0 + w.h * 1.4` (`:219-229`), with vertex alpha 1
  (`QVertex_DrawQuad` HOOK 0x4B51F0, `src/res/qvertex.c:63-80`);
* `dim` halves the alpha (`:211-212`); for `MenuText` that is colour-index bit
  `0x10` (`:362`).

The editor does not resolve retail sprites, so the glow quad is **filled**
instead of textured; its geometry and colour are retail's.

## 7. The editor's title band, and what that costs

Retail screen 900 has one title line ("Game Paused", ColourIndex 1) in a 26-tall
frame whose top is 3 px **above** the panel top, so its 21-tall text line spans
−1…20 and the rule is at 21 — the title overlaps the top border by one pixel.

The editor's panel has two title lines ("IGI EDITOR", "PAUSED"). The band
therefore grows by one retail line plus one retail spacing when a second line is
present (`PauseMenuTitleBandHeight`), and the editor keeps the text inside the
panel (line 0's origin is the panel top) rather than overhanging it by a pixel.
With one title line the band is exactly retail's 21.

## 8. What the editor keeps, and why

| editor value | why it is not retail |
| --- | --- |
| 16 / 21 rows | the editor's own feature list; retail has five items |
| second title line | the editor's own string |
| `Mode:` / `Collision:` / `Logging` / `Weather` / `Lightmap` / terrain rows | editor features; retail has none |
| row STATE colour (`TEXT_ORANGE`) | retail's table has no "on" colour; the editor's previous green/orange pair maps onto the retail palette instead of introducing an off-palette colour |
| panel grows past 330 px | the retail panel is 330 px for five rows; the editor's list is longer, and the growth keeps retail's pitch, band and width |
| 12 px screen margin, 24 px bottom padding, 18 px minimum row height | the editor's own clamps, so the rows stay clickable on a viewport too short for the retail pitch |
| spinner boxes | the editor's own widgets. Their outline takes the retail window outline colour (`MenuWindow_DrawEnd` HOOK 0x424980, `src/widget/menuwindow.c:379-387`: idle `r = b = 0`, `g = 1`) and their text the retail item colour |
| border / rule line width | no engine oracle (see §5) |

## 8b. The keys that drive it, and the pointer it draws

Two things about the menu are the engine's, not the editor's, and both were wrong
before this pass. They are the subject of
`source/runtime/pause_menu_input_policy.h` and
`source/runtime/retail_pointer_sprite.h`, and
`tests/test_pause_menu_input.cpp` (13 host tests, 4 of them corpus-backed) holds
them.

### Which keys drive the menu

`MenuManager_ReadInput` HOOK 0x418580 (src/menu/menumanager.c:488-551) is the
whole of it. It drains the engine's key queue — `while (Keys_GetCount())` — and
acts on four of the values it pops; everything else falls out of its `switch` and
is **discarded**, not handed on:

| engine key | code | what the menu does |
| --- | --- | --- |
| `KEY_DOWN` | −4 | `if (++i >= mm->itemCount) i = 0;` then `MenuManager_SetFocus` (:536-541) |
| `KEY_UP` | −3 | `if (--i < 0) i = mm->itemCount - 1;` then `MenuManager_SetFocus` (:542-547) |
| `KEY_ENTER` | −7 | stored on the focused window (`mm->focus->key`, :530-535); `RUN_ACTION` tests `w.key == -7` and runs the item's `On Click Script` (src/menuscreen/menutext.c:170-179) |
| `KEY_ESCAPE` | −8 | `in->back = 1`; `MenuScreen_Run` evaluates the screen's `On Press Escape` (src/widget/menuscreen.c:7-18) |
| `KEY_TAB` | −11 | `in->select = 1` — and **nothing in IGI-RE reads `MenuInput::select`**, so this is accepted and ignored |
| `KEY_LEFT` / `KEY_RIGHT` | −1 / −2 | stored on the focused window like ENTER; only −7 is acted on, so also accepted and ignored |

The negative codes are the engine's typed keys (src/loadscreen/keys.c:10-22), and
they are the only values that ever reach the queue: `Keys_WndProc` HOOK 0x48A730
(src/loadscreen/keys.c:47-71) maps eleven `WM_KEYDOWN` virtual codes to them and
pushes every other `WM_CHAR` verbatim.

**What the editor used to do wrong.** Its pause overlay was a set of ad-hoc
branches rather than the engine's switch, so keys the engine discards still
reached the editor:

* `Input_OnSpecial` ran **F8** (Editor ↔ Game Play), **F6/F7** (window focus) and
  **F5** (apply + restart gameplay) *before* it checked whether the pause menu was
  up, so those shortcuts fired **behind the overlay**. F11 was (and is) a
  deliberate editor exception.
* `Input_OnKeyboard`'s level-selector branch could fall through instead of
  returning, and the pause branch's `Escape`/digit handling did not cover every
  path.
* Nothing in the editor modelled "the engine pops this and does nothing with it",
  so there was no single place to see which keys the menu owns.

The fix routes **every** pause-mode key through
`igi::PauseMenuSpecialKeyAction` / `igi::PauseMenuCharKeyAction`, which are the
engine's `switch` written out. Anything the policy does not name is `Consume`:
the editor's own shortcuts can no longer fire while the overlay is up. The one
documented editor-only deviation is the level-selector row, which owns the
keyboard while it is focused and accepts the digits and BACKSPACE — retail has no
text input anywhere in a menu, so this is the editor's own furniture and is
marked as such at its use site.

F11's camera snap is the single deliberate exception and is unchanged: it is an
editor feature, not a menu key, and `ShouldProcessF11WhilePaused`
(source/runtime/graph_camera_target.h:52) is where that decision is recorded.

### The pointer is the GAME's sprite

Retail never shows the platform cursor in a menu. Every menu manager owns a
`Cursor` task and points it at an archive member:

```c
/* MenuManager_Update HOOK 0x417CF0, src/menu/menumanager.c:233 */
Cursor_SetSprite(mm->pointer, MenuManager_FindSprite(mm, "pointer.spr"));
```

`MenuManager_FindSprite` HOOK 0x418DE0 (:810-818) matches that basename against
the sprites `MenuManager_LoadResources` HOOK 0x417D50 (:238-257) collected with
`Resource_ForEach` HOOK 0x4B5D80, and `Cursor_Draw` HOOK 0x424C40
(src/widget/cursor.c:38-41) blits it at the pointer with `128.0f, 128.0f,
128.0f` — which is **full** intensity, because `DrawSprite` multiplies by
`COLOR_SCALE` = 1/128 (src/math/draw.c:12, :124-126).

The member is `LOCAL:menusystem/pointer.spr`, 1884 bytes, LOOP v9, one frame,
format 3 (ARGB8888), **20 × 22** pixels. It is in both menu archives
(`ingamemenu.res` and `menusystem.res`), and the editor reads it from the same
one it already reads `font3.fnt` from.

**What the editor used to do wrong.** The pause menu called
`glutSetCursor(GLUT_CURSOR_INHERIT)` and showed the *Windows* arrow. Its own
`DrawCustomCursor` was explicitly disabled while paused
(`ShouldDrawCustomCursor` returned false), so the game's sprite was never drawn
at all. The editor's 12 tool cursors (`editor/qed/*.spr`) are the QED editor's
own art and are unrelated to this.

The fix loads `pointer.spr` out of `menusystem/ingamemenu.res`
(`igi::LoadRetailPointerSprite`, source/runtime/retail_pointer_sprite.cpp) and
draws it while the menu is up, keeping the platform cursor hidden — with the
platform cursor as the fallback only when the archive or member cannot be read,
so the menu stays usable on an incomplete install.

Two things about the sprite have **no trustworthy engine oracle** and are
recorded rather than papered over:

* the hotspot. `Cursor_CreateHandler` HOOK 0x424C10 (src/widget/cursor.c:25-36)
  seeds `hotX = -2, hotY = 0`, and `Draw_Sprite` adds the frame's own `x`/`y`.
  The arrow's tip is the pixel at (2, 0) of the 20 × 22 bitmap, so −2,0 puts the
  drawn tip exactly on the pointer. That is an inference from the bitmap's shape,
  not a decompiled constant.
* the frame-record boundary inside the LOOP v9 file. The bitmap sits at offset
  0x50 and runs to the member's end, which is where the editor's reader lands,
  but the field-by-field layout in the decompiled `LoopHeader` / `LoopFrame9`
  (src/res/sprite.c:113-146) does not reproduce that offset. The editor therefore
  takes the bitmap from its own reader rather than re-deriving the struct.

### The fonts and the background

* **Font.** Every `MenuText` on screen 900 names `font3.fnt`, and the editor
  reads it from `menusystem/ingamemenu.res` — the same path the engine takes (§1,
  §4). This was already the game's resource.
* **Background.** Screen 900's `Background` is `""`, so the pause menu has no
  picture (which is why `MenuScreen_DrawFrame` takes its 0.66-alpha black fill,
  §5). The main menu's `mainmenu.pic` and `logo.pic` belong to a screen the
  editor does not show: the editor's pause menu is the loaded tree's editor-added
  screen, and the loaded retail screens are unreachable from it.
* The editor's tool cursors (`editor/qed/*.spr`) stay the editor's: they are QED
  art, not menu art, and the engine has no counterpart.

## 9. Scale

When the viewport cannot hold the retail pitch for every row, the whole retail
metric is scaled by one factor (`text_scale`), so the rows, the text and the
title band stay in proportion. `text_scale` is 1.0 whenever the retail pitch
fits, which is the normal case at any ordinary window size; the tests check that
the rows stay inside the panel and that consecutive hit bands never overlap at
480, 600, 720, 900, 1080 and 1440 tall by 640, 1280 and 1920 wide.

## 10. Verification

| check | command | result |
| --- | --- | --- |
| build gate | `bash scripts/build-windows.sh` | **exit 0** — x64 link gate OK, i686 compile gate OK, build integrity OK |
| host lane | `bash scripts/run-host-tests.sh` | **exit 0** — 1231 tests passed in 69 suites, 0 failed |
| pause-menu style suite (host) | `IGI_HOST_TESTS=tests/test_pause_menu_style.cpp bash scripts/run-host-tests.sh` | 21 passed, 0 failed |
| label tests (host) | `IGI_HOST_TESTS='tests/test_menu_ui.cpp tests/test_menu_labels.cpp' bash scripts/run-host-tests.sh` | 54 passed, 0 failed (`MenuUiEditorText` 7, `MenuLabels` 9) |
| Windows suite | `wine bin/igi_tests.exe '--gtest_filter=-*VerifyLevel*' --gtest_color=no` with `IGI_GAME_PATH` and `WINEPATH` set | **exit 0** — 1667 tests ran, 1655 passed, 0 failed, 12 skipped |
| headless end-to-end (`--fast`) | `bash scripts/e2e-headless.sh --fast` | **exit 0** — smoke + 278/278 native assertions |
| loading bar reached | i686 editor under Wine, `-level 1`, DEBUG logging on | 8 overlay frames, `2% … 100%`, retail rect — see `LOADING_BAR.md` §5 |

The 21 `PauseMenuRetailStyle` tests and the 1 `FntParserRetailMenuFontTest` all
pass in the Windows suite, which is where the FNT parser is exercised.
`LodChainCorpusTest`'s corpus test — the one the project notes as a pre-existing
failure on this install — passed in these runs.

### §8b's checks (the input contract and the game's pointer)

| check | command | result |
| --- | --- | --- |
| key contract + retail pointer (host) | `IGI_HOST_TESTS=tests/test_pause_menu_input.cpp bash scripts/run-host-tests.sh` | **exit 0** — 13 passed, 0 failed |
| key contract + retail pointer (Windows) | `wine bin/igi_tests.exe --gtest_filter='PauseMenuInput.*'` with `IGI_GAME_PATH` and `WINEPATH` set | **exit 0** — 13 passed, 0 failed |
| full host lane | `IGI_HOST_NO_CACHE=1 bash scripts/run-host-tests.sh` | 4 suites fail with **14 pre-existing corpus failures** (below); every other suite passes, including the new one |
| Windows suite | `wine bin/igi_tests.exe '--gtest_filter=-*VerifyLevel*:MenuQvm.OperandLayoutsPinnedByTheRetailBytes'` | 2035 ran, **2008 passed, 15 failed** — all 15 corpus damage, see below |
| build integrity | `bash scripts/check-build-integrity.sh` | **PASS** — 397 objects consistent |
| headless end-to-end (`--fast`) | `IGI_E2E_STAGE=$PWD/scratch/e2e-menu bash scripts/e2e-headless.sh --fast` | **exit 0** — smoke + 278/278 native assertions, levels 1–14 |
| the real loader, on the real install | `LoadRetailPointerSprite("/Users/haseeb-mir/Downloads/IGI")` in a host program | **ok** — 20×22, 1760 bytes RGBA, arrow tip at x=2 |

**The 15 Windows failures (and the host lane's 14) are NOT from this change.** They
are all in suites that read the *installed* `menusystem/ingamemenu.qvm` /
`MAINMENU.QVM`, and the install's copies are no longer the shipped ones:
`menusystem/ingamemenu.qvm` is now the editor's own compiled output
(md5 `5f72faf3…`, 15219 bytes, byte-identical to the
`*.overwritten-20261029-233007` backup), against the pristine
`ingamemenu.qvm.orig` (md5 `76f112a1…`, 11308 bytes). Screen 900 in the current
file has **6** items where the shipped file has **5**, which is exactly the
assertion that fails (`tests/test_pause_menu_style.cpp:572`), and
`MenuQvm.OperandLayoutsPinnedByTheRetailBytes` — which pins byte offsets in the
shipped container — reads past the end of the recompiled one and **crashes the
process**, which is why the suite has to exclude it to print a summary at all.

Proved three ways:

1. **The same failures on a clean tree.** The four host-lane suites were run
   against a `git worktree` of `HEAD` with no changes at all: the same 14
   failures, same suites, same counts.
2. **The failures pass on a pristine corpus.** A copy of the install's
   `menusystem/` + `LANGUAGE/` with `ingamemenu.qvm.orig` / `mainmenu.qvm.orig`
   put back in place, used as `IGI_GAME_PATH`, turns **all 15 Windows failures
   plus the crashing test into 21 passed, 0 failed**. Nothing in this change
   touches menu QVM parsing.
3. **The damage predates this pass.** The qvm's *content* is identical to the
   backup the editor's own overwrite left at `2026-10-29 22:15:55`; this session
   began at `2026-10-30 01:29`. The file's mtime moved to `01:54:16` without its
   content changing.

Restoring `menusystem/ingamemenu.qvm` and `MAINMENU.QVM` from their `.orig`
copies is what would clear them — **not done here**, because the task forbids
writing to that tree.

**Not verified:** the rendered result, for this pass as for the rest of this
document. This host has no OpenGL 4.1 context, so the drawn pointer sprite, its
hotspot alignment and the menu's compositing are asserted only through the model,
the resource bytes and the geometry — never seen. The input routing is verified
at the policy level (which key maps to which action) and by reading the call
sites; that the keys then *reach* the overlay in a live window is not something
this host can show.


The **"the editor draws its own text"** rule is held by 7 `MenuUiEditorText` tests
(host, corpus-free: the hand-built retail-shaped definition) and 3 `MenuLabels`
tests (corpus-backed: the real `ingamemenu.qvm`). Together they assert that every
retail text row has editor text, that the text is never the retail key, the
resolved language text or the engine's `RM: ` miss string, that a wired retail row
is labelled by the editor action it drives, that screen 900 keeps its five rows,
their order and their verbatim game text, and that the `Menu Text` edit's own
strings are what the title row then shows.

One **flaky, unrelated** test was seen while re-running the suite:
`McpHttpIntegrationTest.StopUnblocksAnIdleAuthenticatedOrUnauthenticatedClient`
(`tests/test_mcp_http.cpp:267-290`) binds a loopback TCP port, opens an idle
connection and asserts `HttpTransport::Stop()` returns in under 1000 ms. It failed
at 5018-5073 ms in three full runs and passed (8 ms in its own group, and green in
the final full run) in the others; it touches nothing this change touches.

**Not verified:** the rendered result. This host has no OpenGL 4.1 context and Wine
never maps a visible window for the editor (the X root has no window and the macOS
screen is unchanged across a run), so the panel's appearance and the drawn labels
are asserted only through the model, geometry, colour and metric tests above. The
label mapping's *effect* is verified at the model level:
`MenuUiEntry::editor_label` is what the draw layer passes to the text helper, and
no code path hands it `display_label`. The compositing order in §5 and the glow
approximation in §6 are the two places where the on-screen result may differ from
retail, and both are recorded rather than papered over.
