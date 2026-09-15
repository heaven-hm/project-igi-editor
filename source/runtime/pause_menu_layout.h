#pragma once

#include <algorithm>

namespace igi {

constexpr int kPauseMenuWidth = 460;
constexpr int kPauseMenuMaxHeight = 1040;
constexpr int kPauseMenuScreenMargin = 12;
constexpr int kPauseMenuFirstRowOffset = 90;
constexpr int kPauseMenuBottomPadding = 24;
constexpr int kPauseMenuMinimumRowHeight = 18;

struct PauseMenuLayout {
    int menu_left;
    int menu_top;
    int menu_width;
    int menu_height;
    int first_row_offset;
    int row_height;
    int row_hit_radius;
    int row_count;
    float text_scale;
};

constexpr int PauseMenuRowCount(bool terrainOptionsExpanded) noexcept {
    // Resume, mode, collision, font, level, autosave, logging, log level,
    // music, weather, lightmaps, calculate-lightmaps, terrain header, reset, save, quit.
    return terrainOptionsExpanded ? 21 : 16;
}

constexpr int PauseMenuPreferredRowHeight(bool terrainOptionsExpanded) noexcept {
    return terrainOptionsExpanded ? 29 : 38;
}

constexpr PauseMenuLayout BuildPauseMenuLayout(
    int viewportWidth, int viewportHeight, bool terrainOptionsExpanded,
    int fontLineHeight = 0) noexcept {
    const int safeWidth = std::max(0, viewportWidth - 2 * kPauseMenuScreenMargin);
    const int safeHeight = std::max(0, viewportHeight - 2 * kPauseMenuScreenMargin);
    const int width = std::min(kPauseMenuWidth, safeWidth);
    const int rows = PauseMenuRowCount(terrainOptionsExpanded);
    const int preferredRowHeight = std::max(
        PauseMenuPreferredRowHeight(terrainOptionsExpanded),
        fontLineHeight > 0 ? fontLineHeight + 4 : 0);
    const int availableRows = std::max(
        kPauseMenuMinimumRowHeight,
        (safeHeight - kPauseMenuFirstRowOffset - kPauseMenuBottomPadding) / rows);
    const int rowHeight = std::min(preferredRowHeight, availableRows);
    const int contentHeight = kPauseMenuFirstRowOffset +
        rows * rowHeight + kPauseMenuBottomPadding;
    const int height = std::min(kPauseMenuMaxHeight, std::min(safeHeight, contentHeight));
    const int hitRadius = std::max(8, std::min(16, rowHeight / 2 - 1));
    const float textScale = std::min(1.0f, static_cast<float>(rowHeight) / 32.0f);
    return {
        std::max(0, (viewportWidth - width) / 2),
        std::max(0, (viewportHeight - height) / 2),
        width,
        height,
        kPauseMenuFirstRowOffset,
        rowHeight,
        hitRadius,
        rows,
        textScale,
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

constexpr int PauseMenuRowCenter(const PauseMenuLayout& layout, int rowIndex) noexcept {
    return layout.menu_top + layout.first_row_offset + rowIndex * layout.row_height;
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

constexpr PauseMenuSpinner BuildPauseMenuSpinner(
    const PauseMenuLayout& layout, int rowY, int labelWidth, int valueWidth) noexcept {
    constexpr int buttonWidth = 22;
    constexpr int gap = 6;
    constexpr int labelGap = 14;
    const int groupWidth = labelWidth + labelGap + buttonWidth + gap +
                           valueWidth + gap + buttonWidth;
    const int groupLeft = layout.menu_left + (layout.menu_width - groupWidth) / 2;
    const int minusLeft = groupLeft + labelWidth + labelGap;
    const int valueLeft = minusLeft + buttonWidth + gap;
    return {groupLeft, groupWidth, minusLeft, valueLeft,
            valueLeft + valueWidth + gap, rowY, layout.row_hit_radius};
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
