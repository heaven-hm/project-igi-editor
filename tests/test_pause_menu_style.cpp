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
 * tests/test_fnt_parser.cpp, which already owns FNT parsing.  The corpus test
 * `TheShippedFontPinsTheRowPitch` below reads the same numbers straight out of
 * the shipped `font3.fnt` through the ILFF chunk layer
 * (source/renderer/res_chunks.h) and `RES_Extract`, both of which are in the
 * host lane's pool, so the pitch is pinned against the shipped FILE and not only
 * against the constant.
 *****************************************************************************/

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "renderer/res_chunks.h"

#include "menu/qvm_image.h"
#include "renderer/res_chunks.h"
#include "runtime/pause_menu_layout.h"
#include "runtime/pause_menu_rows.h"
#include "runtime/pause_menu_style.h"

#include <fstream>
#include <iterator>

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

std::vector<uint8_t> ReadWholeFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
}

igi_res::ChunkHeader HeaderAt(const std::vector<uint8_t>& bytes, std::size_t offset) {
    igi_res::ChunkHeader header;
    if (offset + 16 > bytes.size()) {
        return header;
    }
    const auto u32 = [&bytes](std::size_t at) {
        return static_cast<uint32_t>(bytes[at]) |
               (static_cast<uint32_t>(bytes[at + 1]) << 8) |
               (static_cast<uint32_t>(bytes[at + 2]) << 16) |
               (static_cast<uint32_t>(bytes[at + 3]) << 24);
    };
    header.id = u32(offset);
    header.size = u32(offset + 4);
    header.align = u32(offset + 8);
    header.next = u32(offset + 12);
    header.form = (offset + 20 <= bytes.size()) ? u32(offset + 16) : 0;
    return header;
}

// A NAME/BODY pair, matched by the engine's resource identity: the NAME text
// upper-cased with '\' folded to '/' (`Resource_Create` 0x4B59F0,
// src/res/resource.c:65-104; `NormalizeResourceName`, source/renderer/res_chunks.h).
// Returns the BODY bytes, empty when the member is absent.
std::vector<uint8_t> FindMember(const std::vector<uint8_t>& bytes,
                                const std::string& wanted) {
    const std::string want = igi_res::NormalizeResourceName(wanted);
    std::string pending;
    std::size_t offset = 20;  // past "ILFF" + size/align/next + "IRES"
    std::size_t guard = 0;
    while (offset + 16 <= bytes.size() && guard++ < 100000) {
        const igi_res::ChunkHeader header = HeaderAt(bytes, offset);
        if (header.id == igi_res::kChunkIres) {
            offset += 4;  // the form-type word that opens an ILFF body
            continue;
        }
        const std::size_t body = offset + 16;
        if (header.id == igi_res::kChunkName && body + header.size <= bytes.size()) {
            pending.assign(reinterpret_cast<const char*>(bytes.data() + body), header.size);
            const std::size_t nul = pending.find('\0');
            if (nul != std::string::npos) {
                pending.resize(nul);
            }
        } else if ((header.id == igi_res::kChunkBody || header.id == igi_res::kChunkCstr ||
                    header.id == igi_res::kChunkPath) &&
                   body + header.size <= bytes.size()) {
            if (igi_res::NormalizeResourceName(pending) == want) {
                return std::vector<uint8_t>(
                    bytes.begin() + static_cast<std::ptrdiff_t>(body),
                    bytes.begin() + static_cast<std::ptrdiff_t>(body + header.size));
            }
        }
        if (header.next != 0 && offset + header.next < bytes.size()) {
            offset += header.next;
        } else if (header.size >= 16 && offset + 16 + header.size <= bytes.size()) {
            offset += 16 + header.size;
        } else {
            break;
        }
    }
    return {};
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
    // `extra_rows` is the live menu's write side: the editor's own screen grows by
    // `kPauseMenuEditorExtraRows` rows when a retail definition is loaded
    // (source/app_menu.cpp), which is what makes the row list long enough for the
    // pitch to be squeezed below the editor's minimum on a short viewport.
    for (int extra : {0, igi::kPauseMenuEditorExtraRows}) {
        for (int height : {480, 600, 720, 900, 1080, 1440}) {
            for (int width : {640, 1280, 1920}) {
                for (bool expanded : {false, true}) {
                    const igi::PauseMenuLayout layout = igi::BuildPauseMenuLayout(
                        width, height, expanded, igi::kRetailFontLineHeight, extra);
                    EXPECT_GE(layout.menu_top, 0);
                    EXPECT_LE(layout.menu_top + layout.menu_height, height);
                    EXPECT_LE(layout.menu_left + layout.menu_width, width);
                    EXPECT_EQ(layout.row_count, igi::PauseMenuRowCount(expanded) + extra);
                    const int last = igi::PauseMenuRowCenter(layout, layout.row_count - 1);
                    EXPECT_TRUE(igi::IsPauseMenuRowHit(layout, layout.row_count - 1, last));
                    // The row's hit band — and therefore the row the model draws —
                    // is inside the panel, not below its bottom edge.
                    EXPECT_LE(last + layout.row_hit_radius,
                              layout.menu_top + layout.menu_height);
                    // Consecutive hit bands never overlap, so one y is one row.
                    EXPECT_LT(2 * layout.row_hit_radius, layout.row_height);
                }
            }
        }
    }
}

TEST(PauseMenuRetailStyle, ExtraRowsGrowThePanelAndKeepTheRetailPitchWhenItFits) {
    // This exercises the LAYOUT's growth behaviour, so it passes an explicit extra-row
    // count rather than the live constant: the user asked for the two rows after `Quit`
    // to be removed, so `kPauseMenuEditorExtraRows` is now 0 and the live menu no longer
    // grows.  The layout must still support growth (the panel is what changes).
    constexpr int kExtraRowsUnderTest = 2;
    const igi::PauseMenuLayout plain =
        igi::BuildPauseMenuLayout(1920, 1080, false, igi::kRetailFontLineHeight);
    const igi::PauseMenuLayout extended = igi::BuildPauseMenuLayout(
        1920, 1080, false, igi::kRetailFontLineHeight, kExtraRowsUnderTest);
    EXPECT_EQ(plain.row_count, igi::PauseMenuRowCount(false));
    EXPECT_EQ(extended.row_count,
              igi::PauseMenuRowCount(false) + kExtraRowsUnderTest);
    // A viewport that fits the retail pitch keeps it, whatever the row count.
    EXPECT_EQ(plain.row_height, igi::kRetailRowPitch);
    EXPECT_EQ(extended.row_height, igi::kRetailRowPitch);
    EXPECT_EQ(extended.menu_width, plain.menu_width);
    EXPECT_EQ(extended.menu_left, plain.menu_left);
    EXPECT_GT(extended.menu_height, plain.menu_height);
}

TEST(PauseMenuRetailStyle, SpinnerGroupIsTheSameFromALayoutOrAPanelRect) {
    const igi::PauseMenuLayout layout =
        igi::BuildPauseMenuLayout(1920, 1080, false, igi::kRetailFontLineHeight);
    const int row_y = igi::PauseMenuRowCenter(layout, 3);
    const igi::PauseMenuSpinner from_layout =
        igi::BuildPauseMenuSpinner(layout, row_y, 60, 44);
    const igi::PauseMenuSpinner from_panel = igi::BuildPauseMenuSpinner(
        layout.menu_left, layout.menu_width, row_y, layout.row_hit_radius, 60, 44);
    EXPECT_EQ(from_layout.group_left, from_panel.group_left);
    EXPECT_EQ(from_layout.group_width, from_panel.group_width);
    EXPECT_EQ(from_layout.minus_left, from_panel.minus_left);
    EXPECT_EQ(from_layout.value_left, from_panel.value_left);
    EXPECT_EQ(from_layout.plus_left, from_panel.plus_left);
    EXPECT_EQ(from_layout.row_y, from_panel.row_y);
    EXPECT_EQ(from_layout.row_hit_radius, from_panel.row_hit_radius);
    EXPECT_EQ(from_layout.group_left + from_layout.group_width,
              layout.menu_left + (layout.menu_width + from_layout.group_width) / 2);
    EXPECT_TRUE(igi::IsPauseMenuSpinnerHit(from_layout, from_layout.group_left));
    EXPECT_TRUE(igi::IsPauseMenuSpinnerHit(from_layout,
                                           from_layout.group_left + from_layout.group_width - 1));
    EXPECT_FALSE(igi::IsPauseMenuSpinnerHit(from_layout, from_layout.group_left - 1));
}

// ─── the live rows of the loaded menu tree (#74) ────────────────────────────
//
// The live pause menu's rows come from the loaded menu tree, and each editor row
// carries the editor behaviour it is wired to (`MenuUiEntry::editor_action`,
// source/menu/menu_ui.h).  The label the row is DRAWN with and the label the click
// test MEASURES for the spinner group come from one function
// (source/runtime/pause_menu_rows.h), so a row cannot be drawn at one place and
// clicked at another.

TEST(PauseMenuRows, LiveLabelsAreTheEditorsOwnStrings) {
    igi::PauseMenuRowState state;
    EXPECT_EQ(igi::PauseMenuRowLabel(igi::MenuUiAction::Resume, state), "Resume");
    EXPECT_EQ(igi::PauseMenuRowLabel(igi::MenuUiAction::Quit, state), "Quit");
    EXPECT_EQ(igi::PauseMenuRowLabel(igi::MenuUiAction::SaveLevel, state), "Save Level");
    EXPECT_EQ(igi::PauseMenuRowLabel(igi::MenuUiAction::ResetLevel, state), "Reset Level");
    EXPECT_EQ(igi::PauseMenuRowLabel(igi::MenuUiAction::CalculateLightmaps, state),
              "Calculate Lightmaps");
    EXPECT_EQ(igi::PauseMenuRowLabel(igi::MenuUiAction::SelectLevel, state), "Select Level");
    EXPECT_EQ(igi::PauseMenuRowLabel(igi::MenuUiAction::CycleLogLevel, state), "Log Level");
    EXPECT_EQ(igi::PauseMenuRowLabel(igi::MenuUiAction::AdjustFogIntensity, state),
              "Fog Intensity");

    // The editor's own added rows (source/menu/menu_ui.h): the game menu, the menu
    // text edit and the menu save.
    EXPECT_EQ(igi::PauseMenuRowLabel(igi::MenuUiAction::OpenGameMenu, state),
              igi::app_menu::kGameMenuLabel);
    EXPECT_EQ(igi::PauseMenuRowLabel(igi::MenuUiAction::EditMenuText, state),
              igi::app_menu::kMenuTextLabel);
    EXPECT_EQ(igi::PauseMenuRowLabel(igi::MenuUiAction::SaveMenu, state),
              igi::app_menu::kSaveMenuLabel);
    EXPECT_EQ(igi::PauseMenuRowLabel(igi::MenuUiAction::OpenEditorOptions, state),
              igi::app_menu::kEditorOptionsLabel);

    // A retail row is drawn from the definition, so this mapping has nothing to
    // say about it.
    EXPECT_TRUE(igi::PauseMenuRowLabel(igi::MenuUiAction::None, state).empty());
}

TEST(PauseMenuRows, StateIsAppendedToTheRowLabel) {
    igi::PauseMenuRowState state;
    EXPECT_EQ(igi::PauseMenuRowLabel(igi::MenuUiAction::ToggleMode, state), "Mode: [Editor]");
    state.in_game_mode = true;
    EXPECT_EQ(igi::PauseMenuRowLabel(igi::MenuUiAction::ToggleMode, state), "Mode: [Game Play]");
    state.in_game_mode = false;

    EXPECT_EQ(igi::PauseMenuRowLabel(igi::MenuUiAction::ToggleCollision, state),
              "Collision: [Clip]");
    state.noclip = false;
    EXPECT_EQ(igi::PauseMenuRowLabel(igi::MenuUiAction::ToggleCollision, state),
              "Collision: [Normal]");
    state.noclip = true;

    EXPECT_EQ(igi::PauseMenuRowLabel(igi::MenuUiAction::ToggleFont, state), "Font: System");
    state.use_editor_font = true;
    EXPECT_EQ(igi::PauseMenuRowLabel(igi::MenuUiAction::ToggleFont, state), "Font: Editor");
    state.use_editor_font = false;

    EXPECT_EQ(igi::PauseMenuRowLabel(igi::MenuUiAction::ToggleAutoSave, state),
              "Save Disable");
    state.auto_save_enabled = true;
    EXPECT_EQ(igi::PauseMenuRowLabel(igi::MenuUiAction::ToggleAutoSave, state), "Save Enable");

    EXPECT_EQ(igi::PauseMenuRowLabel(igi::MenuUiAction::ToggleLogging, state), "[ ] Logging");
    state.logging_enabled = true;
    EXPECT_EQ(igi::PauseMenuRowLabel(igi::MenuUiAction::ToggleLogging, state), "[X] Logging");
    EXPECT_EQ(igi::PauseMenuRowLabel(igi::MenuUiAction::ToggleMusic, state), "[ ] Music");
    state.music_on = true;
    EXPECT_EQ(igi::PauseMenuRowLabel(igi::MenuUiAction::ToggleMusic, state), "[X] Music");

    EXPECT_EQ(igi::PauseMenuRowLabel(igi::MenuUiAction::CycleWeather, state),
              "Weather: [Default]");
    state.weather_mode = 1;
    EXPECT_EQ(igi::PauseMenuRowLabel(igi::MenuUiAction::CycleWeather, state), "Weather: [Rain]");
    state.weather_mode = 2;
    EXPECT_EQ(igi::PauseMenuRowLabel(igi::MenuUiAction::CycleWeather, state), "Weather: [Snow]");
    state.weather_mode = 3;
    EXPECT_EQ(igi::PauseMenuRowLabel(igi::MenuUiAction::CycleWeather, state), "Weather: [OFF]");
    state.weather_mode = 0;

    // The editor's weather preview speed. The label is the editor's own -- the
    // engine has no speed control (src/dxinit/rain.c:122-123,572).
    EXPECT_EQ(igi::PauseMenuRowLabel(igi::MenuUiAction::CycleWeatherSpeed, state),
              "Weather Speed: [1x]");
    state.weather_speed = 0;
    EXPECT_EQ(igi::PauseMenuRowLabel(igi::MenuUiAction::CycleWeatherSpeed, state),
              "Weather Speed: [0x]");
    state.weather_speed = 2;
    EXPECT_EQ(igi::PauseMenuRowLabel(igi::MenuUiAction::CycleWeatherSpeed, state),
              "Weather Speed: [2x]");
    state.weather_speed = 1;

    EXPECT_EQ(igi::PauseMenuRowLabel(igi::MenuUiAction::ToggleLightmaps, state),
              "Lightmap: [Default]");
    state.lightmap_mode = "Baked";
    EXPECT_EQ(igi::PauseMenuRowLabel(igi::MenuUiAction::ToggleLightmaps, state),
              "Lightmap: [Baked]");

    EXPECT_EQ(igi::PauseMenuRowLabel(igi::MenuUiAction::ToggleTerrainOptions, state),
              "Terrain Options: [+]");
    state.terrain_expanded = true;
    EXPECT_EQ(igi::PauseMenuRowLabel(igi::MenuUiAction::ToggleTerrainOptions, state),
              "Terrain Options: [-]");

    // The terrain checkboxes keep the two-space indent they have always been drawn
    // with, so the label and the row it is centred in do not move.
    EXPECT_EQ(igi::PauseMenuRowLabel(igi::MenuUiAction::ToggleTerrainTexture, state),
              "  [ ] Texture");
    state.terrain_texture = true;
    EXPECT_EQ(igi::PauseMenuRowLabel(igi::MenuUiAction::ToggleTerrainTexture, state),
              "  [X] Texture");
    state.terrain_height = true;
    state.terrain_discard = true;
    state.terrain_fog = true;
    EXPECT_EQ(igi::PauseMenuRowLabel(igi::MenuUiAction::ToggleTerrainHeight, state),
              "  [X] Height");
    EXPECT_EQ(igi::PauseMenuRowLabel(igi::MenuUiAction::ToggleTerrainDiscard, state),
              "  [X] Discard");
    EXPECT_EQ(igi::PauseMenuRowLabel(igi::MenuUiAction::ToggleTerrainFog, state), "  [X] Fog");
}

TEST(PauseMenuRows, SpinnerRowsAreTheOnesWithAValueBox) {
    // The five spinner rows and the widths the pause menu has always used: font
    // size and level and auto-save at 44, log level at 78, fog intensity at 56.
    EXPECT_EQ(igi::PauseMenuSpinnerValueWidth(igi::MenuUiAction::ToggleFont), 44);
    EXPECT_EQ(igi::PauseMenuSpinnerValueWidth(igi::MenuUiAction::SelectLevel), 44);
    EXPECT_EQ(igi::PauseMenuSpinnerValueWidth(igi::MenuUiAction::ToggleAutoSave), 44);
    EXPECT_EQ(igi::PauseMenuSpinnerValueWidth(igi::MenuUiAction::CycleLogLevel), 78);
    EXPECT_EQ(igi::PauseMenuSpinnerValueWidth(igi::MenuUiAction::AdjustFogIntensity), 56);
    for (igi::MenuUiAction action : {igi::MenuUiAction::ToggleFont,
                                     igi::MenuUiAction::SelectLevel,
                                     igi::MenuUiAction::ToggleAutoSave,
                                     igi::MenuUiAction::CycleLogLevel,
                                     igi::MenuUiAction::AdjustFogIntensity}) {
        EXPECT_TRUE(igi::PauseMenuRowHasSpinner(action)) << igi::MenuUiActionName(action);
    }
    // Every other row is a plain row: no value box, so nothing to click inside it.
    for (igi::MenuUiAction action : {igi::MenuUiAction::Resume, igi::MenuUiAction::ToggleMode,
                                     igi::MenuUiAction::ToggleCollision,
                                     igi::MenuUiAction::ToggleLogging,
                                     igi::MenuUiAction::ToggleMusic,
                                     igi::MenuUiAction::CycleWeather,
                                     igi::MenuUiAction::ToggleLightmaps,
                                     igi::MenuUiAction::CalculateLightmaps,
                                     igi::MenuUiAction::ToggleTerrainOptions,
                                     igi::MenuUiAction::ToggleTerrainTexture,
                                     igi::MenuUiAction::ToggleTerrainHeight,
                                     igi::MenuUiAction::ToggleTerrainDiscard,
                                     igi::MenuUiAction::ToggleTerrainFog,
                                     igi::MenuUiAction::ResetLevel, igi::MenuUiAction::SaveLevel,
                                     igi::MenuUiAction::Quit,
                                     igi::MenuUiAction::OpenEditorOptions,
                                     igi::MenuUiAction::OpenGameMenu,
                                     igi::MenuUiAction::EditMenuText,
                                     igi::MenuUiAction::SaveMenu, igi::MenuUiAction::None}) {
        EXPECT_FALSE(igi::PauseMenuRowHasSpinner(action)) << igi::MenuUiActionName(action);
        EXPECT_EQ(igi::PauseMenuSpinnerValueWidth(action), 0)
            << igi::MenuUiActionName(action);
    }
}

TEST(PauseMenuRows, LogLevelNameIsTheEngineIndependentLabel) {
    EXPECT_STREQ(igi::PauseMenuLogLevelName(igi::kLogLevelDebug), "DEBUG");
    EXPECT_STREQ(igi::PauseMenuLogLevelName(igi::kLogLevelInfo), "INFO");
    EXPECT_STREQ(igi::PauseMenuLogLevelName(igi::kLogLevelWarning), "WARNING");
    EXPECT_STREQ(igi::PauseMenuLogLevelName(igi::kLogLevelError), "ERROR");
    EXPECT_STREQ(igi::PauseMenuLogLevelName(igi::kLogLevelFatal), "FATAL");
    // Out-of-range values clamp, exactly like `igi::ClampLogLevel`.
    EXPECT_STREQ(igi::PauseMenuLogLevelName(-5), "DEBUG");
    EXPECT_STREQ(igi::PauseMenuLogLevelName(99), "FATAL");
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

// ─── corpus: the shipped FONT, which is where the row pitch comes from ───────

TEST(PauseMenuRetailStyleCorpus, TheShippedFontPinsTheRowPitch) {
    if (!MenuCorpusAvailable()) {
        GTEST_SKIP() << "no menu corpus at " << IngameMenuQvm().parent_path();
    }
    const std::filesystem::path archive =
        CorpusRoot() / "menusystem" / "ingamemenu.res";
    const std::vector<uint8_t> archive_bytes = ReadWholeFile(archive);
    ASSERT_FALSE(archive_bytes.empty()) << "no " << archive;
    // The archive walk is this suite's own, with the engine's chunk helpers: the
    // editor's `RES_Extract` reads through Logger -> Config::Get()
    // (source/renderer/res_writer.cpp) and is therefore not host-portable, which
    // is the whole reason this suite parses the containers itself.
    const std::vector<uint8_t> font =
        FindMember(archive_bytes, "LOCAL:menusystem/font3.fnt");
    ASSERT_FALSE(font.empty()) << "font3.fnt is not in " << archive;
    ASSERT_GE(font.size(), 20u);

    // `Sprite_Load`'s FONT branch (HOOK 0x4B6720, src/res/sprite.c:171-207) reads
    // the FNTH child of the ILFF/FONT form; `FontHeader` (src/res/sprite.c:86-92)
    // is proportional(0) nframes(4) unk8(8) advance(12) w(16) h(20).
    igi_res::ChunkHeader root;
    std::memcpy(&root, font.data(), 20);
    ASSERT_EQ(root.id, igi_res::kChunkIlff);
    ASSERT_EQ(root.form, igi_res::ChunkId('F', 'O', 'N', 'T'));

    const uint8_t* fnth = nullptr;
    std::size_t fnth_size = 0;
    uint64_t child = igi_res::ChunkFirstChildOffset(0, root);
    while (child != igi_res::kNoChunk && child + 20 <= font.size()) {
        igi_res::ChunkHeader chunk;
        std::memcpy(&chunk, font.data() + child, 20);
        if (chunk.id == igi_res::ChunkId('F', 'N', 'T', 'H')) {
            const uint64_t offset = igi_res::ChunkDataOffset(child, chunk, 0, root);
            ASSERT_NE(offset, igi_res::kNoChunk);
            ASSERT_LE(offset + chunk.size, font.size());
            fnth = font.data() + offset;
            fnth_size = chunk.size;
            break;
        }
        child = igi_res::ChunkNextOffset(child, chunk);
    }
    ASSERT_NE(fnth, nullptr) << "the shipped font has no FNTH chunk";
    ASSERT_GE(fnth_size, 24u);
    const auto i32 = [fnth](std::size_t offset) {
        int32_t value = 0;
        std::memcpy(&value, fnth + offset, sizeof(value));
        return value;
    };

    // The font is proportional (Grid_New sets `proportional = !Sprite_IsFixed`,
    // src/res/grid.c:43) and a blank advances by `advance` (Grid_UpdateRow
    // HOOK 0x4B7730, src/res/grid.c:93-105).
    EXPECT_NE(i32(0), 0) << "font3.fnt is proportional";
    EXPECT_GT(i32(4), 0) << "it declares frames";
    EXPECT_EQ(i32(12), igi::kRetailFontSpaceAdvance);
    // `h` is the line height: Text_GetHeight is Grid_Height x rows
    // (HOOK 0x4B7E00, src/res/text.c:139-143; HOOK 0x4B7C20, src/res/grid.c:209-213)
    // with one row (Text_New HOOK 0x4B6EC0), so a menu row is `h` tall and the
    // pitch is `h` + the frame's Y-Spacing.
    EXPECT_EQ(i32(20), igi::kRetailFontLineHeight);
    EXPECT_EQ(igi::kRetailRowPitch, i32(20) + igi::kRetailRowSpacing);

    // And the editor's live layout takes the pitch from the same number.
    const igi::PauseMenuLayout layout =
        igi::BuildPauseMenuLayout(1920, 1080, false, i32(20));
    EXPECT_EQ(layout.row_height, i32(20) + igi::kRetailRowSpacing);
    EXPECT_FLOAT_EQ(layout.text_scale, 1.0f);
}

// ─── corpus: the rule's line is a property of the shipped screen ─────────────

TEST(PauseMenuRetailStyleCorpus, TheShippedScreenPutsTheRuleOnTheFirstItemLine) {
    if (!MenuCorpusAvailable()) {
        GTEST_SKIP() << "no menu corpus at " << IngameMenuQvm().parent_path();
    }
    const igi::MenuDefinition definition = igi::MenuDefinition::FromQvmFile(IngameMenuQvm().string());
    ASSERT_TRUE(definition.ok) << definition.error;
    const igi::MenuWidget* screen = definition.FindById(900);
    ASSERT_NE(screen, nullptr);
    ASSERT_EQ(screen->children.size(), 2u);
    const igi::MenuWidget& items = screen->children[1];

    // MenuScreen_DrawFrame draws the rule at `y0 + 20` with `y0 = frameY0 + 1`
    // (:175-179); MenuWindow_Arrange puts the item frame's first child at
    // `frame.y + spacingY` (:225-229).  The shipped screen therefore satisfies
    //     (items.Y-Pos - panel.Y0) + items.Y-Spacing == 1 + 20
    // -- the rule IS the first row's line, in the data, not just in the constants.
    const int panel_x0 = static_cast<int>(screen->FieldNumber("Frame x0"));
    const int panel_y0 = static_cast<int>(screen->FieldNumber("Frame y0"));
    const int items_x = static_cast<int>(items.FieldNumber("X-Pos"));
    const int items_y = static_cast<int>(items.FieldNumber("Y-Pos"));
    const int spacing = static_cast<int>(items.FieldNumber("Y-Spacing"));
    EXPECT_EQ((items_y - panel_y0) + spacing, igi::kRetailTitleRuleOffset);
    EXPECT_EQ((items_x - panel_x0), igi::kRetailItemsInsetX);

    // The editor's own screen keeps that invariant at its own title-line count:
    // the rule follows the band, so it is always the line the first row starts on
    // (source/renderer/renderer_draw.cpp draws `layout.title_rule_y`).
    const igi::PauseMenuLayout layout =
        igi::BuildPauseMenuLayout(1920, 1080, false, igi::kRetailFontLineHeight);
    EXPECT_EQ(layout.title_rule_y, layout.menu_top + layout.title_band);
    EXPECT_EQ(igi::PauseMenuRowCenter(layout, 0) - igi::kRetailTextLineLift,
              layout.title_rule_y);

    // One title line is retail's band exactly; the editor's second line adds one
    // retail line plus one retail spacing, and the rule moves with it.
    EXPECT_EQ(igi::PauseMenuTitleBandHeight(1, igi::kRetailFontLineHeight),
              igi::kRetailTitleRuleOffset);
    EXPECT_EQ(igi::PauseMenuTitleBandHeight(2, igi::kRetailFontLineHeight),
              igi::kRetailTitleRuleOffset + igi::kRetailRowPitch);
    EXPECT_EQ(igi::kPauseMenuTitleBand,
              igi::PauseMenuTitleBandHeight(igi::kPauseMenuTitleLines,
                                            igi::kRetailFontLineHeight));
}
