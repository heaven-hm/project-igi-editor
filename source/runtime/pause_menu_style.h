#pragma once

/******************************************************************************
 * @file    source/runtime/pause_menu_style.h
 * @brief   The retail in-game menu's style: its authored geometry, its fonts,
 *          its colours and its spacing rules, as the engine draws them.
 *
 * This header carries ONLY values that come from one of two places, both named
 * per constant below:
 *
 *   (a) the engine's own drawing code, in IGI-RE (a verified C decompilation of
 *       retail IGI.exe), cited as function + HOOK address + file:line; and
 *   (b) the retail menu DATA, `menusystem/ingamemenu.qvm` screen 900, which the
 *       engine loads through `MenuManager_New` (HOOK 0x418B00,
 *       src/menu/menumanager.c:713-800) and which `igi::MenuDefinition`
 *       (source/menu/qvm_image.h) decodes.
 *
 * The editor's own strings are NOT here.  The editor keeps its own labels and
 * only takes the game's look, so nothing in this file is a string the game
 * draws.
 *
 * ── the load path (the way the game loads it) ───────────────────────────────
 *   src/levelflow/levelflow.c:139
 *       lf->menu = MenuManager_New(*(void **)(Game_Get() + 0x44),
 *                                  "LOCAL:menusystem/ingamemenu.qsc", 0, 0, 0);
 *     the in-game (pause) menu is the menu manager built from
 *     `menusystem/ingamemenu.qsc`; `menusystem/ingamemenu.qvm` is that script's
 *     compiled container, which is the file the editor reads.
 *   src/flow/flow.c:229
 *       MenuManager_New(Flow_GetScreen(), "LOCAL:menusystem\\mainmenu.qsc", 1, 1, 1);
 *     the main menu is the same manager built from `mainmenu.qsc`
 *     (`MAINMENU.QVM`), which is why both files carry the same widget schema.
 *   src/menu/menuloader.c:53-62 `MenuLoader_Changed` (HOOK 0x419210)
 *     `File_ResolvePath` + `Device_Find` + `MenuManager_New` is how a script
 *     pushes another menu file at run time.
 *   src/menu/menumanager.c:238-257 `MenuManager_LoadResources` (HOOK 0x417D50)
 *     loads `mm->resPath` and then `Resource_ForEach` over its directory; the
 *     screen's `Resource path` is "LOCAL:menusystem/ingamemenu.res", which is
 *     where `font3.fnt` comes from.
 *   src/menu/menumanager.c:260-296 `MenuManager_AddResource` (HOOK 0x417E20)
 *     classifies each entry by extension -- "FNT" (kind 2) becomes a font:
 *     `strcpy(font->name, info->name + i); Sprite_Load(&font->sprite, name);`
 *     with `i` the index just past the last '/', ':' or '\'.  So the name a
 *     MenuText's `Font Resource` matches is the bare file name, "font3.fnt".
 *   src/menu/menumanager.c:820-829 `MenuManager_FindFont` (HOOK 0x418E40)
 *     is the case-insensitive lookup MenuText uses.
 *****************************************************************************/

namespace igi {

// ─────────────────────────────────────────────────────────────────────────────
// 1. The authored geometry, from the retail menu data
//
// `menusystem/ingamemenu.qvm`, screen 900 "Main Menu".  Decoded with
// `igi::MenuDefinition::FromQvmFile`; the field names are the screen's own
// `Task_DeclareParameters` names (docs/igi-re-parity/MENU_SYSTEM.md §6), and the
// argument order is the C `ParamTable` order registered by
// `MenuScreen_Init` (HOOK 0x421750, src/menuscreen/menuscreen.c:58-67) and
// `MenuFrame_Init` (HOOK 0x4223D0, src/widget/menuframe.c:48-58):
//
//   Task_New(900, "MenuScreen", "Main Menu", "", 85, 80, 555, 410, ...)
//       Background "" -- Frame x0 85 -- Frame y0 80 -- Frame x1 555 -- Frame y1 410
//   Task_New(-1, "MenuFrame", "", "", 100, 77, 440, 26, 0, 4, 4, FALSE, ...)
//       title frame: X 100, Y 77, W 440, H 26, LineUpMode 0, X-Spacing 4, Y-Spacing 4
//   Task_New(-1, "MenuFrame", "", "", 100, 97, 440, 200, 1, 4, 4, FALSE, ...)
//       item frame:  X 100, Y 97, W 440, H 200, LineUpMode 1, X-Spacing 4, Y-Spacing 4
// ─────────────────────────────────────────────────────────────────────────────

// The screen's `Frame x0/y0/x1/y1`: the panel the engine's MenuScreen draws.
constexpr int kRetailMenuFrameX0 = 85;
constexpr int kRetailMenuFrameY0 = 80;
constexpr int kRetailMenuFrameX1 = 555;
constexpr int kRetailMenuFrameY1 = 410;
constexpr int kRetailMenuWidth = kRetailMenuFrameX1 - kRetailMenuFrameX0;    // 470
constexpr int kRetailMenuHeight = kRetailMenuFrameY1 - kRetailMenuFrameY0;   // 330

// The title frame and the item frame, both children of the screen.
constexpr int kRetailTitleFrameX = 100;
constexpr int kRetailTitleFrameY = 77;
constexpr int kRetailTitleFrameW = 440;
constexpr int kRetailTitleFrameH = 26;
constexpr int kRetailItemsFrameX = 100;
constexpr int kRetailItemsFrameY = 97;
constexpr int kRetailItemsFrameW = 440;
constexpr int kRetailItemsFrameH = 200;

// Both frames sit at x = 100 in a 470-wide panel, so the retail item column is
// inset 15 px from the panel's left edge.
constexpr int kRetailItemsInsetX = kRetailItemsFrameX - kRetailMenuFrameX0;   // 15
// The item frame's top, measured from the panel's top edge (97 - 80).
constexpr int kRetailItemsFrameOffsetY = kRetailItemsFrameY - kRetailMenuFrameY0;  // 17

// ─────────────────────────────────────────────────────────────────────────────
// 2. Spacing, from the engine's own layout code
//
// `MenuWindow_Create` (HOOK 0x4241C0, src/widget/menuwindow.c:99-123) seeds
// every window with
//       w->spacingX = 4;
//       w->spacingY = 4;
// and the retail frames restate it (X-Spacing 4 / Y-Spacing 4 above).
//
// `MenuWindow_Arrange` (HOOK 0x4244B0, src/widget/menuwindow.c:195-286) turns
// LineUpMode into a running cursor.  For LINEUPMODE_VERT_TOP -- the item
// frame's LineUpMode 1 (the enum at src/widget/menuwindow.c:44-54, registered
// as the script constant LINEUPMODE_VERT_TOP by `MenuWindow_Init` HOOK
// 0x423FC0, :85-93) -- it is
//       case LINEUPMODE_VERT_TOP:
//           l.spacingY = w->spacingY;
//           l.y = w->y + l.spacingY;            // :225-229
//           l.vertical = 1;
// so the FIRST child's top is `frame.y + spacingY`, and
// `MenuWindow_Place` (HOOK 0x424420, src/widget/menuwindow.c:172-192)
//       if (l->vertical) { w->y = l->y; l->y += w->h + l->spacingY; }   // :182-187
// gives every later child `previous top + previous height + spacingY`.
//
// A child's height is its own text height: `MAKE_LABEL`
// (src/menuscreen/menutext.c:126-149) sets
//       (mt)->w.h = Text_GetHeight((mt)->label);
// and `Text_GetHeight` (HOOK 0x4B7E00, src/res/text.c:139-143) is
// `Grid_Height`, which is `Sprite_Height(g->sprite) * g->rows`
// (HOOK 0x4B7C20, src/res/grid.c:209-213).  `Text_New` builds the grid with
// one row (`Grid_New(256, 1, s)`, HOOK 0x4B6EC0, src/res/text.c:76-93), so the
// row pitch is exactly the font's own height plus the frame's Y-Spacing.
//
// The horizontal rule is `MenuWindow_Place`'s centreX branch (:179-181):
//       else if (w->centreX == 1) w->x = ((p->w - w->w) >> 1) + p->x;
// and MenuText/MenuTextConditional are created with centreX = 1 and centreY = 1
// (src/menuscreen/menutext.c:311-319 and :101-104).
// ─────────────────────────────────────────────────────────────────────────────

constexpr int kRetailRowSpacing = 4;   // MenuWindow_Create :101-102, retail Y-Spacing

// The item frame's own LineUpMode; 1 is LINEUPMODE_VERT_TOP
// (src/widget/menuwindow.c:45-54).
constexpr int kRetailItemsLineUpMode = 1;
// The title frame's LineUpMode 0 is LINEUPMODE_NONE, so its child is placed by
// centreX/centreY inside the frame rather than by a cursor
// (MenuWindow_Arrange: `if (w->lineup)` skips the cursor block, :202; then
// MenuWindow_Place centres it, :179-190).
constexpr int kRetailTitleLineUpMode = 0;

// ─────────────────────────────────────────────────────────────────────────────
// 3. The font, from the retail archive
//
// `font3.fnt` is the font every MenuText on screen 900 names (`Font Resource`
// "font3.fnt", five items plus the title).  It is an ILFF "FONT" form; the
// engine's reader is `Sprite_Load` (HOOK 0x4B6720), FONT branch,
// src/res/sprite.c:171-207, and the header it reads is `FontHeader`
// (src/res/sprite.c:85-92):
//       int proportional, nframes, unk8, advance, w, h;
//
// Measured from the installed retail file
// (`RES_Extract(ingamemenu.res, "LOCAL:menusystem/font3.fnt")` ->
// `FNT_ParseBytes`), the values the engine uses are:
//       proportional = 1   -> per-glyph advances are used
//       advance      = 6   -> the advance of a character with no glyph (space)
//       w            = 24  -> the fixed-cell width (unused: proportional)
//       h            = 21  -> the line height
// and the archive holds 154 glyph frames.
//
// How the engine turns those into metrics:
//   * height -- `Grid_Height` (HOOK 0x4B7C20, src/res/grid.c:209-213) returns
//     `Sprite_Height(sprite) * rows`, and `Sprite_Height` (HOOK 0x4B6D00,
//     src/res/sprite.c:270-274) is `frames[nframes].h`, which `Sprite_Init`
//     (HOOK 0x4B65E0, src/res/sprite.c:41-63) took from `fh->h`.  One row, so
//     the line height IS the FontHeader height: 21.
//   * width -- `Grid_UpdateRow` (HOOK 0x4B7730, src/res/grid.c:76-107) walks
//     the grid and adds `frames[p[i] - 1].advance` for a glyph and
//     `Sprite_Advance(s)` for an empty cell; `Sprite_StringWidth`
//     (HOOK 0x4B7050, src/res/sprite.c:315-328) does the same for a string.
//     `Grid_Print` (HOOK 0x4B77F0, src/res/grid.c:109-118) maps each character
//     through the FONT's own 256-entry TRAN map (`Sprite_Load` :197-199), so an
//     unmapped character advances by the header advance, 6.
//   * the cell is proportional because `Sprite_Load` sets
//     `Sprite_Init(..., fh->proportional ? 0 : FRAME_FIXED, ...)` (:192) and
//     `Grid_New` sets `g->proportional = !Sprite_IsFixed(s)` (:43).
// ─────────────────────────────────────────────────────────────────────────────

constexpr int kRetailFontLineHeight = 21;   // font3.fnt FontHeader::h
constexpr int kRetailFontSpaceAdvance = 6;  // font3.fnt FontHeader::advance

// The retail row pitch: a text row is one font line tall and the frame adds its
// Y-Spacing below it (see section 2).
constexpr int kRetailRowPitch = kRetailFontLineHeight + kRetailRowSpacing;  // 25

// ─────────────────────────────────────────────────────────────────────────────
// 4. The frame, from the engine's drawing code
//
// `MenuScreen_DrawFrame` (HOOK 0x421CD0, src/menuscreen/menuscreen.c:168-229)
// is the green frame.  Every constant below is one of its values, with the line
// that carries it.
// ─────────────────────────────────────────────────────────────────────────────

// The frame is drawn one pixel inside and below the declared rect (:175-178):
//       x0 = frameX0 + 1.0f;
//       y0 = SCREEN_Y(0) + frameY0 + 1.0f;
//       x1 = frameX1 + 1.0f;
//       y1 = SCREEN_Y(0) + frameY1 + 1.0f;
// `SCREEN_X`/`SCREEN_Y` (src/menuscreen/menuscreen.h:141-142) are the retail
// 640x480 menu-space mapping:
//       SCREEN_X(x) = (Display_GetMode()->width  >> 1) - 320 + x
//       SCREEN_Y(y) = (Display_GetMode()->height >> 1) - 240 + y
constexpr int kRetailFrameInset = 1;

// The title rule is a second two-vertex line strip at (:179, :208-213)
//       line = y0 + 20.0f;
// i.e. 20 px below the (already inset) top edge, so 21 px below the panel top.
constexpr int kRetailTitleRuleDrop = 20;

// The rule and the first item row coincide in the retail data: the rule is at
// panel top + 1 + 20 = +21 and the item frame's first child is at
// items frame top (panel top + 17) + Y-Spacing (4) = +21.  Asserted below.
constexpr int kRetailTitleRuleOffset = kRetailFrameInset + kRetailTitleRuleDrop;  // 21
constexpr int kRetailFirstRowOffset = kRetailItemsFrameOffsetY + kRetailRowSpacing;  // 21
static_assert(kRetailTitleRuleOffset == kRetailFirstRowOffset,
              "retail: the title rule and the first item row share one line");

// The border and the title rule are ONE vertex colour: a 5-vertex line strip
// (the closed rect) and a 2-vertex line strip (the rule) built by
//       static void set_vertex(QVertex *v, float x, float y, float a, float g)
//       { ... v->a = a; v->r = 0; v->g = g; v->b = 0; }         // :152-163
//       for (i = 0; i < 4; i++) set_vertex(&v[i], 0, 0, a, 0.75f);   // :190-191
// so the colour is r = 0, g = 0.75, b = 0 and the alpha is the fade `a`.
constexpr float kRetailBorderGreen = 0.75f;

// The green-to-black wash over the panel (:215-223):
//       set_vertex(&v[0], x0, y0, a1, 1.0f);   // top,    g = 1.0, a = a1
//       set_vertex(&v[1], x1, y0, a1, 1.0f);
//       set_vertex(&v[2], x0, y1, a2, 0);      // bottom, g = 0.0, a = a2
//       set_vertex(&v[3], x1, y1, a2, 0);
// with (:184-185)
//       a1 = a * 0.2f;
//       a2 = a * 0.05f;
constexpr float kRetailWashTopGreen = 1.0f;
constexpr float kRetailWashTopAlpha = 0.2f;
constexpr float kRetailWashBottomAlpha = 0.05f;

// A screen with no background picture also gets a flat black fill (:225-227):
//       if (!ms->picText)
//           Draw_RectAZ(x0, y0, x1 - x0, y1 - y0, 0, 0, 0, a * 0.66f, 1.0f, 0x80000, 0);
// Screen 900's `Background` is "" (no picture), so the in-game menu takes it.
// `Draw_RectAZ` (HOOK 0x4B4AF0, src/math/draw.c:73-78) scales colour by
// 1/128 (`COLOR_SCALE`, src/math/draw.c:12), so 0,0,0 is black.
constexpr float kRetailFillAlpha = 0.66f;

// The fade the frame's alphas are scaled by (:180-183):
//       if (MenuManager_GetFade(ms->w.menu) * 0.125f < 1.0f)
//           a = MenuManager_GetFade(ms->w.menu) * 0.125f;
//       else a = 1.0f;
// `MenuManager_GetFade` is HOOK 0x419030.  The editor's pause menu has no fade
// counter, so it uses the settled value, a = 1.
constexpr float kRetailFadeSettled = 1.0f;

// ─────────────────────────────────────────────────────────────────────────────
// 5. The text colours, from the engine's colour table
//
// `gMenuColors` is DATA 0x53B778 (src/menu/typewriterbox.c:616-617); its first
// five entries are
//       (0,0,0) (128,128,128) (0,128,0) (128,95,0) (64,128,64)
// and `MenuText_Init` (HOOK 0x420CC0, src/menuscreen/menutext.c:301-304)
// registers the first four as the script constants TEXT_BLACK, TEXT_WHITE,
// TEXT_GREEN and TEXT_ORANGE.  `MenuText_Draw` (HOOK 0x421130,
// src/menuscreen/menutext.c:353-364) selects with `mt->color & 0xF`:
//       DRAW_LABEL(mt, gMenuColors, fade, alpha, mt->color & 0x10);
//
// `DRAW_LABEL` (src/menuscreen/menutext.c:196-230) is where the colour is
// finished:
//   * a focused label at full fade is brightened by
//         t_->color[i] = c->r * 0.5f + 64.0f;          // :203-205
//     and gets a glow quad behind it
//         QVertex_DrawQuad(glow, x0_, y0_, x1_, y0_, x0_, y1_, x1_, y1_,
//                          c_->r * 0.2f, c_->g * 0.2f, c_->b * 0.2f, 0, 8, 0);
//     at                                                          // :227-228
//         x0_ = SCREEN_X(w.x) - w.w * 0.4f
//         y0_ = SCREEN_Y(w.y) - w.h * 0.2f
//         x1_ = x0_ + w.w * 1.8f
//         y1_ = y0_ + w.h * 1.4f
//     with `glow_ = fade == 16 && w.focused` (:199).
//   * `dim` halves the alpha: `if (dim) alpha *= 0.5f;` (:211-212).  For
//     MenuText, `dim` is `mt->color & 0x10` (:362).
//
// The colour values above are in the engine's 0..255 domain.  Everything the
// engine hands to a vertex is multiplied by `gColorScale` = 0.0078125f = 1/128
// (DATA 0x534AB0, src/res/qvertex.c:189, applied in `QVertex_DrawFrame` HOOK
// 0x4B50B0 :45-57) or by `COLOR_SCALE` = 1/128 (src/math/draw.c:12).  So 128
// maps to 1.0 and a colour table entry of (0,128,0) is full green, NOT the
// 128/255 = 0.502 a /255 conversion would give.
// ─────────────────────────────────────────────────────────────────────────────

constexpr float kRetailColourScale = 1.0f / 128.0f;

// The bit that makes a text dim (MenuText_Draw :362, DRAW_LABEL :211-212).
constexpr int kRetailDimBit = 0x10;
constexpr float kRetailDimAlpha = 0.5f;

// The focused-label brightening and its glow (DRAW_LABEL :203-205, :227-228).
constexpr float kRetailFocusHighlightScale = 0.5f;
constexpr float kRetailFocusHighlightOffset = 64.0f;  // 64/128 = 0.5
constexpr float kRetailGlowScale = 0.2f;
constexpr float kRetailGlowInsetX = 0.4f;   // x0 = x - w * 0.4
constexpr float kRetailGlowInsetY = 0.2f;   // y0 = y - h * 0.2
constexpr float kRetailGlowWidthScale = 1.8f;
constexpr float kRetailGlowHeightScale = 1.4f;

// `gMenuColors`' five verified entries, already divided by 128 so they can go
// straight to glColor4f.  The fifth entry has no registered name; it is exactly
// the value the focused-label brightening produces for TEXT_GREEN
// ((0,128,0) -> (64,128,64)), which is the state the in-game menu is actually
// seen in.
struct RetailMenuColour {
    float r;
    float g;
    float b;
};

constexpr RetailMenuColour kRetailMenuColours[5] = {
    {0.0f, 0.0f, 0.0f},                             // 0 TEXT_BLACK  (0,0,0)
    {1.0f, 1.0f, 1.0f},                             // 1 TEXT_WHITE  (128,128,128)
    {0.0f, 1.0f, 0.0f},                             // 2 TEXT_GREEN  (0,128,0)
    {1.0f, 95.0f * kRetailColourScale, 0.0f},       // 3 TEXT_ORANGE (128,95,0)
    {0.5f, 1.0f, 0.5f},                             // 4 (64,128,64)
};

// The script colour constant each retail item on screen 900 names: the title is
// ColourIndex 1, every item is ColourIndex 2 (ingamemenu.qvm screen 900).
constexpr int kRetailTitleColourIndex = 1;  // TEXT_WHITE
constexpr int kRetailItemColourIndex = 2;   // TEXT_GREEN

// The colour table lookup, matching `gMenuColors[mt->color & 0xF]`
// (MenuText_Draw :362 / DRAW_LABEL :200).  The engine indexes the 10-entry
// table with no bounds check, so indices 5..15 read entries whose values are
// not recoverable from the decompilation; an unverified index returns
// TEXT_WHITE rather than a guess.
constexpr RetailMenuColour RetailMenuColourFor(int colour_index) noexcept {
    const int index = colour_index & 0x0F;
    if (index < 0 || index > 4) {
        return kRetailMenuColours[1];
    }
    return kRetailMenuColours[index];
}

// The colour a focused label is drawn in (DRAW_LABEL :203-205), in 0..1.
constexpr RetailMenuColour RetailFocusedColour(RetailMenuColour base) noexcept {
    return {
        base.r * kRetailFocusHighlightScale + kRetailFocusHighlightOffset * kRetailColourScale,
        base.g * kRetailFocusHighlightScale + kRetailFocusHighlightOffset * kRetailColourScale,
        base.b * kRetailFocusHighlightScale + kRetailFocusHighlightOffset * kRetailColourScale,
    };
}

// The colour of the glow quad behind a focused label (DRAW_LABEL :227-228).
constexpr RetailMenuColour RetailGlowColour(RetailMenuColour base) noexcept {
    return {base.r * kRetailGlowScale, base.g * kRetailGlowScale, base.b * kRetailGlowScale};
}

// ─────────────────────────────────────────────────────────────────────────────
// 6. The window outline (MenuWindow_DrawEnd)
//
// Text items do NOT use this: `MenuText_Draw` (HOOK 0x421130,
// src/menuscreen/menutext.c:353-364) draws the label and the glow only, and
// never calls `MenuWindow_DrawEnd`.  The outline belongs to the windows that do
// -- ListBox, ToggleBox, InputBox, AsciiBox, DialogWindow -- so it is the
// correct focus ring for the editor's own boxed controls (its spinner boxes).
//
// `MenuWindow_DrawEnd` (HOOK 0x424980, src/widget/menuwindow.c:368-409):
//       v[i].r = focused ? 0.5f : 0.0f;
//       v[i].g = 1.0f;
//       v[i].b = focused ? 0.5f : 0.0f;
//       v[i].a = alpha;
//       v[0].x = sx + 1;  v[0].y = sy + 1;
//       v[1].x = width + sx;  v[2].y = height + sy;
// and the fill is black at `alpha * fill` (:407).
// ─────────────────────────────────────────────────────────────────────────────

constexpr float kRetailOutlineIdleGreen = 1.0f;
constexpr float kRetailOutlineFocusedR = 0.5f;
constexpr float kRetailOutlineFocusedG = 1.0f;
constexpr float kRetailOutlineFocusedB = 0.5f;
constexpr int kRetailOutlineInset = 1;

// ─────────────────────────────────────────────────────────────────────────────
// 7. The editor's own title band
//
// The editor's pause panel carries TWO title lines ("IGI EDITOR" and "PAUSED",
// source/renderer/renderer_draw.cpp).  Retail screen 900 carries one (MenuText
// "Game Paused", ColourIndex 1, in the 26-tall title MenuFrame).  The editor
// therefore grows the retail title band by exactly one retail line plus one
// retail row spacing when the second line is drawn; with one line the band is
// the retail band, unchanged.
//
// The retail relationship the band keeps is DRAW_LABEL's: the title text's
// bottom edge sits one pixel above the rule.  Retail's title text is 21 tall,
// centred in the 26-tall title frame whose top is 3 px above the panel top
// (frame Y 77 vs panel Y 80), so it spans -1..20 and the rule is at 21.  The
// editor keeps the text inside the panel instead (0..21), which is the same
// layout shifted down by one pixel.
// ─────────────────────────────────────────────────────────────────────────────

constexpr int kPauseMenuTitleLines = 2;

// The panel top to the rule / first row, for `title_lines` title lines.
constexpr int PauseMenuTitleBandHeight(int title_lines, int line_height) noexcept {
    return kRetailTitleRuleOffset +
           (title_lines > 1 ? (title_lines - 1) * (line_height + kRetailRowSpacing) : 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// 8. Where a retail line origin sits in the editor's text coordinates
//
// The engine's text draw takes the LINE ORIGIN: `QVertex_DrawFrame`
// (HOOK 0x4B50B0, src/res/qvertex.c:45-57) places a glyph at
//       v[0].y = f->y + y;
// where `y` is the value handed down from `Grid_Draw`
// (HOOK 0x4B7930, src/res/grid.c:172-195: `cy = y` for the first row) and
// `f->y` is the FontFrame's own bearing.  A MenuText stores that origin as the
// window's `y` (`MAKE_LABEL`, src/menuscreen/menutext.c:139-143:
// `int y_ = SCREEN_Y((mt)->w.y); ... Text_New(font_, x_, y_, s_)`), and
// `MenuWindow_Place` sets `w->y` to the row's top.  So in retail the row's top
// IS the line origin, and a glyph's box is
// `[row_top + f->y, row_top + f->y + f->h]`.
//
// The editor's text helpers instead take a BASELINE-ish coordinate and lift it:
// source/renderer/renderer_draw.cpp, `DrawRetailPauseText` and `DrawFontText`,
// both do `y_gl += 11` before applying the glyph bearing.  A call at `y`
// therefore puts a glyph's top edge at screen `y - kRetailTextLineLift + f->y`.
// To land that on retail's `row_top + f->y` the editor must draw at
// `row_top + kRetailTextLineLift`.
//
// 11 is the editor's own constant (the literal in those two helpers); it is not
// a retail value.  It is named here so the layout and the renderer cannot drift
// apart.  For font3.fnt the effect is exact: a capital has f->y = 2 and
// f->h = 15, so a row drawn this way spans row_top + 2 .. row_top + 17 inside a
// row_top .. row_top + 21 line box.
// ─────────────────────────────────────────────────────────────────────────────

constexpr int kRetailTextLineLift = 11;

// The y the editor's title lines are drawn at, in the same top-down screen
// space the rows use.  Line 0's origin is the panel top, which is the retail
// title's own origin shifted down by the one pixel it overhangs the panel top
// by (see section 7).
constexpr int PauseMenuTitleLineY(int menu_top, int line_index,
                                  int line_height) noexcept {
    return menu_top + line_index * (line_height + kRetailRowSpacing) + kRetailTextLineLift;
}

}  // namespace igi
