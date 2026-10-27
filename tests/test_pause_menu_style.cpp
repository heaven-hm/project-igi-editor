/******************************************************************************
 * @file    test_pause_menu_style.cpp
 * @brief   The pause menu's retail style: geometry, font metrics, colours.
 *
 * Everything asserted here is either
 *   * a value from the retail menu DATA (`menusystem/ingamemenu.qvm` screen 900
 *     and `font3.fnt` inside `menusystem/ingamemenu.res`), read here with the
 *     same readers the product uses (`igi::MenuDefinition::FromQvmFile`,
 *     `RES_Extract` + `FNT_ParseBytes`), or
 *   * a value from the engine's drawing code in IGI-RE, cited per assertion.
 *
 * Oracle: IGI-RE, a verified C decompilation of retail IGI.exe.
 *   src/menuscreen/menuscreen.c:131-150  MenuScreen_DrawHandler HOOK 0x421C20
 *   src/menuscreen/menuscreen.c:168-229  MenuScreen_DrawFrame   HOOK 0x421CD0
 *       :175-178 the +1 inset;  :179 line = y0 + 20;
 *       :190-191 the border's (0, 0.75, 0);  :215-223 the wash (g 1 -> 0,
 *       alpha 0.2a -> 0.05a);  :225-227 the 0.66a black fill
 *   src/menuscreen/menuscreen.h:141-142  SCREEN_X / SCREEN_Y, the 640x480
 *       menu-space mapping
 *   src/widget/menuwindow.c:99-123       MenuWindow_Create HOOK 0x4241C0,
 *       spacingX = spacingY = 4
 *   src/widget/menuwindow.c:195-286      MenuWindow_Arrange HOOK 0x4244B0,
 *       LINEUPMODE_VERT_TOP: first child at frame.y + spacingY (:225-229)
 *   src/widget/menuwindow.c:172-192      MenuWindow_Place HOOK 0x424420,
 *       `l->y += w->h + l->spacingY` (:182-187) and the centreX rule (:179-181)
 *   src/widget/menuwindow.c:368-409      MenuWindow_DrawEnd HOOK 0x424980,
 *       focused r = b = 0.5 / g = 1, idle r = b = 0 / g = 1
 *   src/menuscreen/menutext.c:196-230    DRAW_LABEL: the focus brightening
 *       (:203-205), the dim alpha (:211-212) and the glow quad (:219-229)
 *   src/menuscreen/menutext.c:301-304    TEXT_BLACK..TEXT_ORANGE
 *   src/menu/typewriterbox.c:616-617     gMenuColors DATA 0x53B778
 *   src/res/qvertex.c:189                gColorScale DATA 0x534AB0 = 0.0078125f
 *   src/math/draw.c:12                   COLOR_SCALE = 1/128
 *   src/res/grid.c:209-213               Grid_Height HOOK 0x4B7C20 (sprite h * rows)
 *   src/res/grid.c:76-107                Grid_UpdateRow HOOK 0x4B7730 (advances)
 *   src/res/sprite.c:171-207             Sprite_Load HOOK 0x4B6720, FONT branch
 *
 * Corpus: $IGI_GAME_PATH (scripts/run-host-tests.sh exports it, default
 * /Users/haseeb-mir/Downloads/IGI).  The corpus-backed tests skip when it is
 * absent; the constant tests always run.
 *
 * This suite is deliberately host-portable: it does not touch `FNT_Parse*`
 * (source/renderer/fnt_parser.cpp logs through Logger -> Config, neither of
 * which builds on the host).  The retail font's own metrics -- the source of
 * `kRetailFontLineHeight` and `kRetailFontSpaceAdvance` -- are pinned in
 * tests/test_fnt_parser.cpp, which already owns FNT parsing.
 *****************************************************************************/

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>

#include "menu/qvm_image.h"
#include "runtime/pause_menu_layout.h"
#include "runtime/pause_menu_style.h"

namespace {

std::filesystem::path CorpusRoot() {
    if (const char* env = std::getenv("IGI_GAME_PATH"); env != nullptr && *env != '\0') {
        return std::filesystem::path(env);
    }
    return std::filesystem::path("/Users/haseeb-mir/Downloads/IGI");
}

std::filesystem::path IngameMenuQvm() {
    return CorpusRoot() / "menusystem" / "ingamemenu.qvm";
}

bool MenuCorpusAvailable() {
    std::error_code ec;
    return std::filesystem::is_regular_file(IngameMenuQvm(), ec);
}

}  // namespace

// ─── the retail geometry, as constants ──────────────────────────────────────

TEST(PauseMenuRetailStyle, FrameIsTheAuthoredScreenRect) {
    // ingamemenu.qvm screen 900: Frame x0 85, y0 80, x1 555, y1 410.
    EXPECT_EQ(igi::kRetailMenuFrameX0, 85);
    EXPECT_EQ(igi::kRetailMenuFrameY0, 80);
    EXPECT_EQ(igi::kRetailMenuFrameX1, 555);
    EXPECT_EQ(igi::kRetailMenuFrameY1, 410);
    EXPECT_EQ(igi::kRetailMenuWidth, 470);
    EXPECT_EQ(igi::kRetailMenuHeight, 330);
}

TEST(PauseMenuRetailStyle, PanelWidthIsTheRetailFrameWidth) {
    EXPECT_EQ(igi::kPauseMenuWidth, igi::kRetailMenuWidth);
    // Retail centres the frame in its 640x480 menu space (SCREEN_X,
    // src/menuscreen/menuscreen.h:141-142), and so does the editor's layout, so
    // in a 640-wide viewport the panel lands exactly on the retail x0 = 85.
    EXPECT_EQ((640 - igi::kPauseMenuWidth) / 2, igi::kRetailMenuFrameX0);
}

TEST(PauseMenuRetailStyle, VerticalPlacementReproducesTheAuthoredFrameY) {
    // The retail frame's centre (245) is 5 px below the 480-tall menu space's
    // centre (240); centring with that offset lands on y0 = 80 for a 330-tall
    // panel.  This is the only derived placement rule in the layout.
    EXPECT_EQ(igi::kPauseMenuRetailCentreOffsetY, 5);
    EXPECT_EQ(igi::kPauseMenuRetailCentreOffsetX, 0);
    EXPECT_EQ((480 - igi::kRetailMenuHeight) / 2 + igi::kPauseMenuRetailCentreOffsetY,
              igi::kRetailMenuFrameY0);
}

TEST(PauseMenuRetailStyle, ViewportPlacementMatchesTheEngineMappingAtEverySize) {
    // SCREEN_X(x) = (width >> 1) - 320 + x (src/menuscreen/menuscreen.h:141), so
    // the retail frame's left edge is width/2 - 235 on any even width.
    for (int width : {640, 800, 1280, 1920}) {
        const igi::PauseMenuLayout layout =
            igi::BuildPauseMenuLayout(width, 1080, false, igi::kRetailFontLineHeight);
        EXPECT_EQ(layout.menu_left, (width >> 1) - 320 + igi::kRetailMenuFrameX0)
            << "viewport width " << width;
        EXPECT_EQ(layout.menu_width, igi::kRetailMenuWidth);
    }
}

// ─── spacing ────────────────────────────────────────────────────────────────

TEST(PauseMenuRetailStyle, RowPitchIsTheFontLinePlusTheFrameSpacing) {
    // kRetailRowSpacing is MenuWindow_Create's spacingY (src/widget/menuwindow.c:101-102)
    // and the retail frames' own Y-Spacing; a row is one font line tall because
    // MAKE_LABEL sets w.h = Text_GetHeight (src/menuscreen/menutext.c:126-149).
    EXPECT_EQ(igi::kRetailRowSpacing, 4);
    EXPECT_EQ(igi::kRetailFontLineHeight, 21);
    EXPECT_EQ(igi::kRetailRowPitch, 25);
    EXPECT_EQ(igi::kRetailRowPitch, igi::kRetailFontLineHeight + igi::kRetailRowSpacing);
}

TEST(PauseMenuRetailStyle, TitleRuleAndFirstRowShareTheRetailLine) {
    // The rule is at panel top + 1 + 20 (MenuScreen_DrawFrame :175-179) and the
    // first row is at items frame top (panel + 17) + spacingY 4
    // (MenuWindow_Arrange :225-229).  Both are 21.
    EXPECT_EQ(igi::kRetailFrameInset, 1);
    EXPECT_EQ(igi::kRetailTitleRuleDrop, 20);
    EXPECT_EQ(igi::kRetailTitleRuleOffset, 21);
    EXPECT_EQ(igi::kRetailItemsFrameOffsetY, 17);
    EXPECT_EQ(igi::kRetailFirstRowOffset, 21);
    EXPECT_EQ(igi::kRetailTitleRuleOffset, igi::kRetailFirstRowOffset);

    // The layout's own constants are the same two numbers, for the editor's two
    // title lines, plus the editor's baseline lift.
    EXPECT_EQ(igi::kPauseMenuTitleBand,
              igi::PauseMenuTitleBandHeight(igi::kPauseMenuTitleLines,
                                            igi::kRetailFontLineHeight));
    EXPECT_EQ(igi::kPauseMenuTitleBand, 21 + (igi::kRetailFontLineHeight + igi::kRetailRowSpacing));
    EXPECT_EQ(igi::kPauseMenuFirstRowOffset, igi::kPauseMenuTitleBand + igi::kRetailTextLineLift);
    EXPECT_EQ(igi::kRetailTextLineLift, 11);
}

TEST(PauseMenuRetailStyle, RetailPitchIsUsedVerbatimWhenTheViewportFits) {
    // 1080 tall: 16 rows at the retail pitch fit, so the scale is 1 and the row
    // height is exactly the retail pitch.
    const igi::PauseMenuLayout layout =
        igi::BuildPauseMenuLayout(1920, 1080, false, igi::kRetailFontLineHeight);
    EXPECT_EQ(layout.row_height, igi::kRetailRowPitch);
    EXPECT_FLOAT_EQ(layout.text_scale, 1.0f);
    EXPECT_EQ(layout.title_line_height, igi::kRetailFontLineHeight);
    EXPECT_EQ(layout.title_band,
              igi::PauseMenuTitleBandHeight(igi::kPauseMenuTitleLines,
                                            igi::kRetailFontLineHeight));
    EXPECT_EQ(layout.first_row_offset, layout.title_band + igi::kRetailTextLineLift);
    EXPECT_EQ(layout.title_rule_y, layout.menu_top + layout.title_band);
}

TEST(PauseMenuRetailStyle, DrawnTextLandsOnTheRetailRowLines) {
    // The engine draws a glyph at `row_top + f->y` because its text draw takes
    // the LINE ORIGIN (QVertex_DrawFrame HOOK 0x4B50B0, src/res/qvertex.c:45-57;
    // Grid_Draw HOOK 0x4B7930, src/res/grid.c:172-195) and MenuWindow_Place sets
    // the row's y to the row's top (src/widget/menuwindow.c:182-187).  The
    // editor's helpers take a baseline and lift it by kRetailTextLineLift, so
    // the y they are given must be the retail row top PLUS that lift.
    const igi::PauseMenuLayout layout =
        igi::BuildPauseMenuLayout(1920, 1080, false, igi::kRetailFontLineHeight);
    for (int row = 0; row < layout.row_count; ++row) {
        const int retail_row_top = igi::PauseMenuRowCenter(layout, row) - igi::kRetailTextLineLift;
        EXPECT_EQ(retail_row_top, layout.menu_top + layout.title_band + row * layout.row_height)
            << "row " << row;
    }
    // Row 0's top is the retail first row's top, which is the rule's line.
    EXPECT_EQ(igi::PauseMenuRowCenter(layout, 0) - igi::kRetailTextLineLift, layout.title_rule_y);
    // The title lines are on the same convention and sit above the rule.
    for (int line = 0; line < igi::kPauseMenuTitleLines; ++line) {
        const int origin = igi::PauseMenuTitleDrawY(layout, line) - igi::kRetailTextLineLift;
        EXPECT_GE(origin, layout.menu_top) << "line " << line;
        EXPECT_LE(origin + layout.title_line_height, layout.title_rule_y) << "line " << line;
    }
}

TEST(PauseMenuRetailStyle, RowsStayInsideThePanelAtEveryViewport) {
    for (int height : {480, 600, 720, 900, 1080, 1440}) {
        for (int width : {640, 1280, 1920}) {
            for (bool expanded : {false, true}) {
                const igi::PauseMenuLayout layout =
                    igi::BuildPauseMenuLayout(width, height, expanded, igi::kRetailFontLineHeight);
                EXPECT_GE(layout.menu_top, 0);
                EXPECT_LE(layout.menu_top + layout.menu_height, height);
                EXPECT_LE(layout.menu_left + layout.menu_width, width);
                EXPECT_EQ(layout.row_count, igi::PauseMenuRowCount(expanded));
                const int last = igi::PauseMenuRowCenter(layout, layout.row_count - 1);
                EXPECT_TRUE(igi::IsPauseMenuRowHit(layout, layout.row_count - 1, last));
                EXPECT_LE(last, layout.menu_top + layout.menu_height);
                // Consecutive hit bands never overlap, so one y is one row.
                EXPECT_LT(2 * layout.row_hit_radius, layout.row_height);
            }
        }
    }
}

// ─── colours ────────────────────────────────────────────────────────────────

TEST(PauseMenuRetailStyle, ColourTableIsScaledByOneover128) {
    // gColorScale DATA 0x534AB0 = 0.0078125f = 1/128 (src/res/qvertex.c:189),
    // so 128 in the table is 1.0 at the vertex, not 128/255.
    EXPECT_FLOAT_EQ(igi::kRetailColourScale, 1.0f / 128.0f);

    const igi::RetailMenuColour black = igi::RetailMenuColourFor(0);
    EXPECT_FLOAT_EQ(black.r, 0.0f);
    EXPECT_FLOAT_EQ(black.g, 0.0f);
    EXPECT_FLOAT_EQ(black.b, 0.0f);

    const igi::RetailMenuColour white = igi::RetailMenuColourFor(1);
    EXPECT_FLOAT_EQ(white.r, 1.0f);
    EXPECT_FLOAT_EQ(white.g, 1.0f);
    EXPECT_FLOAT_EQ(white.b, 1.0f);

    const igi::RetailMenuColour green = igi::RetailMenuColourFor(2);
    EXPECT_FLOAT_EQ(green.r, 0.0f);
    EXPECT_FLOAT_EQ(green.g, 1.0f);
    EXPECT_FLOAT_EQ(green.b, 0.0f);

    const igi::RetailMenuColour orange = igi::RetailMenuColourFor(3);
    EXPECT_FLOAT_EQ(orange.r, 1.0f);
    EXPECT_FLOAT_EQ(orange.g, 95.0f / 128.0f);
    EXPECT_FLOAT_EQ(orange.b, 0.0f);

    // The fifth entry (64,128,64) is exactly the focused TEXT_GREEN value.
    const igi::RetailMenuColour fifth = igi::RetailMenuColourFor(4);
    EXPECT_FLOAT_EQ(fifth.r, 0.5f);
    EXPECT_FLOAT_EQ(fifth.g, 1.0f);
    EXPECT_FLOAT_EQ(fifth.b, 0.5f);
}

TEST(PauseMenuRetailStyle, ColourIndexMasksTheLowNibble) {
    // DRAW_LABEL indexes with `mt->color & 0xF` (src/menuscreen/menutext.c:200).
    EXPECT_FLOAT_EQ(igi::RetailMenuColourFor(2 | 0x10).g, 1.0f);
    EXPECT_FLOAT_EQ(igi::RetailMenuColourFor(2 | 0x10).r, 0.0f);
    EXPECT_FLOAT_EQ(igi::RetailMenuColourFor(0x12).b, 0.0f);
    // An index the decompilation cannot resolve draws TEXT_WHITE, not a guess.
    const igi::RetailMenuColour unverified = igi::RetailMenuColourFor(7);
    EXPECT_FLOAT_EQ(unverified.r, 1.0f);
    EXPECT_FLOAT_EQ(unverified.g, 1.0f);
    EXPECT_FLOAT_EQ(unverified.b, 1.0f);
}

TEST(PauseMenuRetailStyle, FocusedTextIsTheRetailHighlight) {
    // DRAW_LABEL: `t_->color[i] = c->r * 0.5f + 64.0f` (src/menuscreen/menutext.c:203-205).
    const igi::RetailMenuColour green = igi::RetailMenuColourFor(igi::kRetailItemColourIndex);
    const igi::RetailMenuColour focused = igi::RetailFocusedColour(green);
    EXPECT_FLOAT_EQ(focused.r, 0.5f);
    EXPECT_FLOAT_EQ(focused.g, 1.0f);
    EXPECT_FLOAT_EQ(focused.b, 0.5f);
    // ... which is gMenuColors[4] for TEXT_GREEN.
    EXPECT_FLOAT_EQ(focused.r, igi::RetailMenuColourFor(4).r);
    EXPECT_FLOAT_EQ(focused.g, igi::RetailMenuColourFor(4).g);
    EXPECT_FLOAT_EQ(focused.b, igi::RetailMenuColourFor(4).b);

    // The glow is the item colour * 0.2 (DRAW_LABEL :227-228).
    const igi::RetailMenuColour glow = igi::RetailGlowColour(green);
    EXPECT_FLOAT_EQ(glow.r, 0.0f);
    EXPECT_FLOAT_EQ(glow.g, 0.2f);
    EXPECT_FLOAT_EQ(glow.b, 0.0f);
}

TEST(PauseMenuRetailStyle, FrameColoursAreTheDrawFrameValues) {
    // set_vertex(..., a, 0.75f) with r = b = 0 (src/menuscreen/menuscreen.c:152-163,
    // :190-191) is the border AND the title rule.
    EXPECT_FLOAT_EQ(igi::kRetailBorderGreen, 0.75f);
    // The wash: g = 1.0 at the top, alpha a1 = a * 0.2 / a2 = a * 0.05 (:215-223,
    // :184-185).
    EXPECT_FLOAT_EQ(igi::kRetailWashTopGreen, 1.0f);
    EXPECT_FLOAT_EQ(igi::kRetailWashTopAlpha, 0.2f);
    EXPECT_FLOAT_EQ(igi::kRetailWashBottomAlpha, 0.05f);
    // The flat black fill at a * 0.66 (:225-227); screen 900 has no Background
    // picture, so it takes this branch.
    EXPECT_FLOAT_EQ(igi::kRetailFillAlpha, 0.66f);
    // The settled fade (MenuManager_GetFade * 0.125 saturates at 1, :180-183).
    EXPECT_FLOAT_EQ(igi::kRetailFadeSettled, 1.0f);
    // The dim rule: `if (dim) alpha *= 0.5f` (:211-212).
    EXPECT_FLOAT_EQ(igi::kRetailDimAlpha, 0.5f);
    EXPECT_EQ(igi::kRetailDimBit, 0x10);
    // The retail window outline: idle r = b = 0 / g = 1, focused r = b = 0.5
    // (MenuWindow_DrawEnd, src/widget/menuwindow.c:379-387).
    EXPECT_FLOAT_EQ(igi::kRetailOutlineIdleGreen, 1.0f);
    EXPECT_FLOAT_EQ(igi::kRetailOutlineFocusedR, 0.5f);
    EXPECT_FLOAT_EQ(igi::kRetailOutlineFocusedG, 1.0f);
    EXPECT_FLOAT_EQ(igi::kRetailOutlineFocusedB, 0.5f);
}

// ─── corpus: the authored values, read from the retail files ─────────────────

TEST(PauseMenuRetailStyleCorpus, Screen900FieldsMatchTheStyleConstants) {
    if (!MenuCorpusAvailable()) {
        GTEST_SKIP() << "no menu corpus at " << IngameMenuQvm().parent_path();
    }
    const igi::MenuDefinition definition = igi::MenuDefinition::FromQvmFile(IngameMenuQvm().string());
    ASSERT_TRUE(definition.ok) << definition.error;

    const igi::MenuWidget* screen = definition.FindById(900);
    ASSERT_NE(screen, nullptr);
    ASSERT_EQ(screen->type, "MenuScreen");

    EXPECT_EQ(static_cast<int>(screen->FieldNumber("Frame x0")), igi::kRetailMenuFrameX0);
    EXPECT_EQ(static_cast<int>(screen->FieldNumber("Frame y0")), igi::kRetailMenuFrameY0);
    EXPECT_EQ(static_cast<int>(screen->FieldNumber("Frame x1")), igi::kRetailMenuFrameX1);
    EXPECT_EQ(static_cast<int>(screen->FieldNumber("Frame y1")), igi::kRetailMenuFrameY1);
    // No background picture -> MenuScreen_DrawFrame takes its black-fill branch.
    EXPECT_EQ(screen->FieldText("Background"), "");

    ASSERT_EQ(screen->children.size(), 2u);
    const igi::MenuWidget& title = screen->children[0];
    const igi::MenuWidget& items = screen->children[1];
    ASSERT_EQ(title.type, "MenuFrame");
    ASSERT_EQ(items.type, "MenuFrame");

    EXPECT_EQ(static_cast<int>(title.FieldNumber("X-Pos")), igi::kRetailTitleFrameX);
    EXPECT_EQ(static_cast<int>(title.FieldNumber("Y-Pos")), igi::kRetailTitleFrameY);
    EXPECT_EQ(static_cast<int>(title.FieldNumber("Width")), igi::kRetailTitleFrameW);
    EXPECT_EQ(static_cast<int>(title.FieldNumber("Height")), igi::kRetailTitleFrameH);
    EXPECT_EQ(static_cast<int>(title.FieldNumber("LineUpMode")), igi::kRetailTitleLineUpMode);

    EXPECT_EQ(static_cast<int>(items.FieldNumber("X-Pos")), igi::kRetailItemsFrameX);
    EXPECT_EQ(static_cast<int>(items.FieldNumber("Y-Pos")), igi::kRetailItemsFrameY);
    EXPECT_EQ(static_cast<int>(items.FieldNumber("Width")), igi::kRetailItemsFrameW);
    EXPECT_EQ(static_cast<int>(items.FieldNumber("Height")), igi::kRetailItemsFrameH);
    EXPECT_EQ(static_cast<int>(items.FieldNumber("LineUpMode")), igi::kRetailItemsLineUpMode);
    EXPECT_EQ(static_cast<int>(items.FieldNumber("X-Spacing")), igi::kRetailRowSpacing);
    EXPECT_EQ(static_cast<int>(items.FieldNumber("Y-Spacing")), igi::kRetailRowSpacing);

    // The title is ColourIndex 1 and every item is ColourIndex 2.
    ASSERT_EQ(title.children.size(), 1u);
    EXPECT_EQ(static_cast<int>(title.children[0].FieldNumber("ColourIndex")),
              igi::kRetailTitleColourIndex);
    ASSERT_EQ(items.children.size(), 5u);
    for (const igi::MenuWidget& item : items.children) {
        EXPECT_EQ(static_cast<int>(item.FieldNumber("ColourIndex")), igi::kRetailItemColourIndex);
        EXPECT_EQ(item.FieldText("Font Resource"), "font3.fnt");
    }
}

TEST(PauseMenuRetailStyleCorpus, RetailItemRowsLandOnTheRetailLines) {
    if (!MenuCorpusAvailable()) {
        GTEST_SKIP() << "no menu corpus at " << IngameMenuQvm().parent_path();
    }
    const igi::MenuDefinition definition = igi::MenuDefinition::FromQvmFile(IngameMenuQvm().string());
    ASSERT_TRUE(definition.ok) << definition.error;
    const igi::MenuWidget* screen = definition.FindById(900);
    ASSERT_NE(screen, nullptr);
    ASSERT_EQ(screen->children.size(), 2u);
    const igi::MenuWidget& items = screen->children[1];

    const int panel_y = igi::kRetailMenuFrameY0;
    const int items_y = static_cast<int>(items.FieldNumber("Y-Pos"));
    const int spacing = static_cast<int>(items.FieldNumber("Y-Spacing"));
    ASSERT_EQ(items_y, igi::kRetailMenuFrameY0 + igi::kRetailItemsFrameOffsetY);

    // MenuWindow_Arrange LINEUPMODE_VERT_TOP: the first child's top is
    // frame.y + spacingY, and MenuWindow_Place advances by h + spacingY.  With
    // h = the font line height, every row's top is on the editor's grid.
    const int font_line = igi::kRetailFontLineHeight;
    int row_top = items_y + spacing;
    for (size_t i = 0; i < items.children.size(); ++i) {
        const int relative = row_top - panel_y;
        EXPECT_EQ(relative, igi::kRetailFirstRowOffset +
                            static_cast<int>(i) * (font_line + spacing))
            << "retail item " << i;
        row_top += font_line + spacing;
    }
}
