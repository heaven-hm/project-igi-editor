#pragma once

#include <algorithm>

#include "pause_menu_style.h"

namespace igi {

// The pause panel's geometry, derived from the retail in-game menu.
//
// Every number that is a *retail* number lives in source/runtime/pause_menu_style.h
// with its IGI-RE citation; this header only turns those into a layout for the
// editor's viewport, which is a different size from the game's 640x480 menu
// space and which carries more rows (16, or 21 with the terrain options open)
// than the game's five.
//
// What is retail and what is the editor's:
//   * width                -- retail: the screen's own frame width, 470 px
//                             (kRetailMenuWidth).  Retail's 640x480 menu space
//                             centres the frame (SCREEN_X, menuscreen.h:141-142)
//                             and so does this, which puts the panel at exactly
//                             the retail x = 85 in a 640-wide viewport.
//   * vertical placement   -- retail: the frame's centre is 5 px below the menu
//                             space's centre (frame Y 80..410 -> 245; space
//                             centre 240).  Centring the panel with that offset
//                             reproduces the retail position exactly at the
//                             retail panel size and keeps the panel centred
//                             when the editor's longer row list grows it.
//   * title band           -- retail: 21 px from the panel top to the title rule
//                             and to the first row (kRetailTitleRuleOffset ==
//                             kRetailFirstRowOffset), grown by one retail line +
//                             spacing for the editor's second title line.
//   * row pitch            -- retail: one font line plus the frame's Y-Spacing
//                             (kRetailFontLineHeight + kRetailRowSpacing).
//   * minimum row height   -- the editor's own floor, so the rows stay clickable
//                             on a viewport too short for the retail pitch.
//   * screen margin and
//     bottom padding       -- the editor's own, unchanged from before.
//
// The scale factor: when the viewport cannot hold the retail pitch for every
// row, the whole retail metric is scaled down by the same factor, so the rows,
// the text and the title band stay in proportion.  `text_scale` is that factor
// and it is 1.0 whenever the retail pitch fits, which is the normal case.

// Retail: the screen frame's width (kRetailMenuWidth, menusystem/ingamemenu.qvm
// screen 900 -- "Frame x1" 555 - "Frame x0" 85).
constexpr int kPauseMenuWidth = kRetailMenuWidth;
constexpr int kPauseMenuScreenMargin = 12;
// The panel top to the title rule, which is also the retail first row's top
// (kRetailTitleRuleOffset == kRetailFirstRowOffset).
constexpr int kPauseMenuTitleBand =
    PauseMenuTitleBandHeight(kPauseMenuTitleLines, kRetailFontLineHeight);
// The panel top to the y the first row is DRAWN at: the retail row top plus the
// editor's baseline lift (kRetailTextLineLift, section 8 of pause_menu_style.h).
constexpr int kPauseMenuFirstRowOffset = kPauseMenuTitleBand + kRetailTextLineLift;
constexpr int kPauseMenuBottomPadding = 24;
constexpr int kPauseMenuMinimumRowHeight = 18;

// The retail frame's centre, relative to the centre of the 640x480 menu space
// the engine maps through SCREEN_X/SCREEN_Y (src/menuscreen/menuscreen.h:141-142).
constexpr int kPauseMenuRetailCentreOffsetX =
    (kRetailMenuFrameX0 + kRetailMenuFrameX1) / 2 - 320;  // 320 - 320 = 0
constexpr int kPauseMenuRetailCentreOffsetY =
    (kRetailMenuFrameY0 + kRetailMenuFrameY1) / 2 - 240;  // 245 - 240 = 5

struct PauseMenuLayout {
    int menu_left;
    int menu_top;
    int menu_width;
    int menu_height;
    int title_band;        // panel top -> title rule, == the retail first row top
    int first_row_offset;  // panel top -> the y the first row is drawn at
    int row_height;        // the retail row pitch, scaled to fit
    int row_hit_radius;
    int row_count;
    float text_scale;      // 1.0 when the retail pitch fits
    int title_line_height; // the title lines' scaled font height
    int title_rule_y;      // absolute y of the rule
};

constexpr int PauseMenuRowCount(bool terrainOptionsExpanded) noexcept {
    // Resume, mode, collision, font, level, autosave, logging, log level,
    // music, weather, lightmaps, calculate-lightmaps, terrain header, reset, save, quit.
    return terrainOptionsExpanded ? 21 : 16;
}

// The editor-added rows the loaded game menu appends to the editor's own screen
// (`app_menu.cpp`: the menu-text edit and the menu save -- the row that opened the
// retail screens was removed, because that screen is the game's menu and the editor's
// pause menu is editor-only).  They are editor-side -- the retail definition declares
// none of them -- so they are counted ON TOP of `PauseMenuRowCount`.  A menu built
// without a retail definition (`BuildEditorFallbackMenu`) has none of them, which is
// why this is a separate constant and not part of `PauseMenuRowCount`.  It must stay
// equal to `app_menu::kEditorAddedRowCount`, which is what the live menu passes
// (`App::PauseMenuExtraRows`).
constexpr int kPauseMenuEditorExtraRows = 2;

constexpr PauseMenuLayout BuildPauseMenuLayout(
    int viewportWidth, int viewportHeight, bool terrainOptionsExpanded,
    int fontLineHeight = 0, int extraRows = 0) noexcept {
    const int safeWidth = std::max(0, viewportWidth - 2 * kPauseMenuScreenMargin);
    const int safeHeight = std::max(0, viewportHeight - 2 * kPauseMenuScreenMargin);
    const int width = std::min(kPauseMenuWidth, safeWidth);
    const int rows = PauseMenuRowCount(terrainOptionsExpanded) + std::max(0, extraRows);

    // The retail row pitch: the font's own line height plus the frame's
    // Y-Spacing (kRetailFontLineHeight / kRetailRowSpacing).  With no font
    // loaded the retail font's own line height is the fallback, so an
    // unknown-font layout is still the retail one.
    const int lineHeight = fontLineHeight > 0 ? fontLineHeight : kRetailFontLineHeight;
    const int retailPitch = std::max(1, lineHeight + kRetailRowSpacing);

    // First estimate of the band, at the retail (unscaled) line height.
    const int retailBand = PauseMenuTitleBandHeight(kPauseMenuTitleLines, lineHeight);

    // Shrink the retail pitch uniformly when the viewport cannot hold it.  The
    // editor's own minimum row height is a floor for legibility, but only while it
    // still fits: the rows must never run past the panel's bottom edge (the live
    // menu's write-side rows are what make the list long enough for that to
    // happen), so when the floor itself does not fit the pitch is the space that
    // is left.
    const int available = safeHeight - retailBand - kPauseMenuBottomPadding;
    const int maxPitch = available / std::max(1, rows);
    int rowHeight = std::min(retailPitch, maxPitch);
    if (rowHeight < kPauseMenuMinimumRowHeight) {
        rowHeight = std::min(kPauseMenuMinimumRowHeight, maxPitch);
    }
    rowHeight = std::max(1, rowHeight);
    const float textScale = static_cast<float>(rowHeight) / static_cast<float>(retailPitch);

    // The title band and the title lines at the same scale.
    const int titleLineHeight =
        std::max(1, static_cast<int>(static_cast<float>(lineHeight) * textScale + 0.5f));
    const int titleBand = PauseMenuTitleBandHeight(kPauseMenuTitleLines, titleLineHeight);

    const int contentHeight = titleBand + rows * rowHeight + kPauseMenuBottomPadding;
    const int height = std::min(safeHeight, std::max(kRetailMenuHeight, contentHeight));

    // Retail vertical placement (see the header comment), clamped into the
    // viewport when the panel is taller than the space left by the margins.
    const int centredTop =
        (viewportHeight - height) / 2 + kPauseMenuRetailCentreOffsetY;
    const int maxTop = viewportHeight - kPauseMenuScreenMargin - height;
    const int menuTop = std::max(
        kPauseMenuScreenMargin,
        std::min(centredTop, std::max(kPauseMenuScreenMargin, maxTop)));

    // The hit band is the row's own half-pitch, capped so a tall row does not
    // swallow its neighbours.  It follows the pitch rather than a fixed floor: a
    // floor larger than half the pitch would make two adjacent rows answer for the
    // same y (`IsPauseMenuRowHit` is inclusive at both ends), which is exactly the
    // case the live menu's longer row list reaches on a short viewport.
    const int hitRadius = std::max(1, std::min(16, rowHeight / 2 - 1));

    return {
        std::max(0, (viewportWidth - width) / 2 + kPauseMenuRetailCentreOffsetX),
        menuTop,
        width,
        height,
        titleBand,
        titleBand + kRetailTextLineLift,
        rowHeight,
        hitRadius,
        rows,
        textScale,
        titleLineHeight,
        menuTop + titleBand,
    };
}

constexpr int PauseMenuTop(int viewportHeight, bool terrainOptionsExpanded = false) noexcept {
    return BuildPauseMenuLayout(kPauseMenuWidth + 2 * kPauseMenuScreenMargin,
                                viewportHeight, terrainOptionsExpanded).menu_top;
}

constexpr int PauseMenuRowHeight(bool terrainOptionsExpanded, int viewportHeight = 1080) noexcept {
    return BuildPauseMenuLayout(kPauseMenuWidth + 2 * kPauseMenuScreenMargin,
                                viewportHeight, terrainOptionsExpanded).row_height;
}

constexpr int PauseMenuRowHitRadius(bool terrainOptionsExpanded, int viewportHeight = 1080) noexcept {
    return BuildPauseMenuLayout(kPauseMenuWidth + 2 * kPauseMenuScreenMargin,
                                viewportHeight, terrainOptionsExpanded).row_hit_radius;
}

// The y a row is drawn at AND hit-tested around.  It is the retail row top plus
// the editor's text lift, so the drawn glyphs land exactly where the engine
// puts them (pause_menu_style.h section 8).
constexpr int PauseMenuRowCenter(const PauseMenuLayout& layout, int rowIndex) noexcept {
    return layout.menu_top + layout.first_row_offset + rowIndex * layout.row_height;
}

// The y a title line is drawn at, same convention as PauseMenuRowCenter.
constexpr int PauseMenuTitleDrawY(const PauseMenuLayout& layout, int lineIndex) noexcept {
    return PauseMenuTitleLineY(layout.menu_top, lineIndex, layout.title_line_height);
}

constexpr bool IsPauseMenuRowHit(const PauseMenuLayout& layout,
                                 int rowIndex, int mouseY) noexcept {
    const int center = PauseMenuRowCenter(layout, rowIndex);
    return mouseY >= center - layout.row_hit_radius &&
           mouseY <= center + layout.row_hit_radius;
}

struct PauseMenuSpinner {
    int group_left;
    int group_width;
    int minus_left;
    int value_left;
    int plus_left;
    int row_y;
    int row_hit_radius;
};

// The same spinner group for a caller that has the panel rect rather than a
// `PauseMenuLayout`: the live menu draws the model's own screen rects
// (`igi::MenuUiModel::ToViewport`), which are the retail definition's rects on a
// retail screen and the layout's panel on the editor's own screen.  Only the
// panel's left edge and width are used.
constexpr PauseMenuSpinner BuildPauseMenuSpinner(int menuLeft, int menuWidth, int rowY,
                                                 int rowHitRadius, int labelWidth,
                                                 int valueWidth) noexcept {
    constexpr int buttonWidth = 22;
    constexpr int gap = 6;
    constexpr int labelGap = 14;
    const int groupWidth = labelWidth + labelGap + buttonWidth + gap +
                           valueWidth + gap + buttonWidth;
    const int groupLeft = menuLeft + (menuWidth - groupWidth) / 2;
    const int minusLeft = groupLeft + labelWidth + labelGap;
    const int valueLeft = minusLeft + buttonWidth + gap;
    return {groupLeft, groupWidth, minusLeft, valueLeft,
            valueLeft + valueWidth + gap, rowY, rowHitRadius};
}

constexpr PauseMenuSpinner BuildPauseMenuSpinner(
    const PauseMenuLayout& layout, int rowY, int labelWidth, int valueWidth) noexcept {
    return BuildPauseMenuSpinner(layout.menu_left, layout.menu_width, rowY,
                                 layout.row_hit_radius, labelWidth, valueWidth);
}

constexpr bool IsPauseMenuSpinnerHit(const PauseMenuSpinner& spinner, int x) noexcept {
    return x >= spinner.group_left && x < spinner.group_left + spinner.group_width;
}

constexpr int AdjustPauseMenuSpinner(int current, int direction,
                                     int minimum, int maximum, int step) noexcept {
    const int next = current + direction * step;
    return std::max(minimum, std::min(maximum, next));
}

} // namespace igi
