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
used anywhere.

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
| build gate | `bash scripts/build-windows.sh` | **exit 0** — x64 link gate OK, i686 compile gate OK |
| host lane | `bash scripts/run-host-tests.sh` | **exit 0** — 1111 tests passed in 65 suites, 0 failed |
| new suite (host) | `IGI_HOST_TESTS=tests/test_pause_menu_style.cpp bash scripts/run-host-tests.sh` | 15 passed, 0 failed |
| Windows suite | `wine bin/igi_tests.exe '--gtest_filter=-*VerifyLevel*' --gtest_color=no` with `IGI_GAME_PATH` and `WINEPATH` set | **exit 0** — 1548 tests ran, 1536 passed, 0 failed, 12 skipped |

The 15 new tests (`PauseMenuRetailStyle` 13, `PauseMenuRetailStyleCorpus` 2) and
the 1 new `FntParserRetailMenuFontTest` all pass in the Windows suite, which is
where the FNT parser is exercised. `LodChainCorpusTest`'s corpus test — the one
the project notes as a pre-existing failure on this install — passed in these
runs.

One **flaky, unrelated** test was seen while re-running the suite:
`McpHttpIntegrationTest.StopUnblocksAnIdleAuthenticatedOrUnauthenticatedClient`
(`tests/test_mcp_http.cpp:267-290`) binds a loopback TCP port and asserts
`HttpTransport::Stop()` returns in under 1000 ms. It passed in 4 ms in one full
run, failed at 5010 ms in the next, then passed again in isolation (2 of 3
isolated runs) and passed in the following full run. It touches nothing this
change touches.

**Not verified:** the rendered result. This host has no OpenGL 4.1 context, so
the editor's GUI cannot run here; the panel's appearance is asserted only through
the geometry, colour and metric tests above. The compositing order in §5 and the
glow approximation in §6 are the two places where the on-screen result may differ
from retail, and both are recorded rather than papered over.
