#pragma once

namespace igi {

constexpr int kPauseMenuWidth = 460;
constexpr int kPauseMenuHeight = 620;
constexpr int kPauseMenuFirstRowOffset = 86;
constexpr int kPauseMenuRowHeight = 28;
constexpr int kPauseMenuRowHitRadius = 12;

constexpr int PauseMenuTop(int viewport_height) noexcept {
    return (viewport_height - kPauseMenuHeight) / 2;
}

// Kept as an explicit renderer/app state marker. The editor pause interface is
// a single page; it intentionally has no game-menu sub-pages.
enum class PauseMenuPage {
    Main,
};

enum class PauseMenuItem {
    None,
    Resume,
    Mode,
    Font,
    LevelSelector,
    AutoSave,
    ModelSearch,
    Music,
    Lightmaps,
    TerrainOptions,
    TerrainTexture,
    TerrainHeight,
    TerrainDiscard,
    TerrainFog,
    FogIntensity,
    ResetLevel,
    SaveLevel,
    Quit,
};

constexpr int PauseMenuItemCount(bool terrain_options_expanded) noexcept {
    return terrain_options_expanded ? 17 : 12;
}

constexpr PauseMenuItem PauseMenuItemAt(bool terrain_options_expanded,
                                        int row) noexcept {
    if (row < 0 || row >= PauseMenuItemCount(terrain_options_expanded)) {
        return PauseMenuItem::None;
    }
    switch (row) {
    case 0: return PauseMenuItem::Resume;
    case 1: return PauseMenuItem::Mode;
    case 2: return PauseMenuItem::Font;
    case 3: return PauseMenuItem::LevelSelector;
    case 4: return PauseMenuItem::AutoSave;
    case 5: return PauseMenuItem::ModelSearch;
    case 6: return PauseMenuItem::Music;
    case 7: return PauseMenuItem::Lightmaps;
    case 8: return PauseMenuItem::TerrainOptions;
    default: break;
    }
    if (!terrain_options_expanded) {
        switch (row) {
        case 9: return PauseMenuItem::ResetLevel;
        case 10: return PauseMenuItem::SaveLevel;
        case 11: return PauseMenuItem::Quit;
        default: return PauseMenuItem::None;
        }
    }
    switch (row) {
    case 9: return PauseMenuItem::TerrainTexture;
    case 10: return PauseMenuItem::TerrainHeight;
    case 11: return PauseMenuItem::TerrainDiscard;
    case 12: return PauseMenuItem::TerrainFog;
    case 13: return PauseMenuItem::FogIntensity;
    case 14: return PauseMenuItem::ResetLevel;
    case 15: return PauseMenuItem::SaveLevel;
    case 16: return PauseMenuItem::Quit;
    default: return PauseMenuItem::None;
    }
}

constexpr int PauseMenuRowCenter(int viewport_height, int row_index) noexcept {
    return PauseMenuTop(viewport_height) + kPauseMenuFirstRowOffset +
           row_index * kPauseMenuRowHeight;
}

constexpr bool IsPauseMenuRowHit(int viewport_height, int row_index,
                                 int mouse_y) noexcept {
    const int center = PauseMenuRowCenter(viewport_height, row_index);
    return mouse_y >= center - kPauseMenuRowHitRadius &&
           mouse_y <= center + kPauseMenuRowHitRadius;
}

struct PauseLevelSelection {
    bool valid;
    bool leave_gameplay;
    int level;
};

constexpr PauseLevelSelection ResolvePauseLevelSelection(int level,
                                                          bool in_game_mode) noexcept {
    return {level >= 1 && level <= 14, in_game_mode, level};
}

constexpr bool IsPauseMenuInputActive(bool pause_menu_open) noexcept {
    return pause_menu_open;
}

constexpr bool IsEditorInteractionActive(bool pause_menu_open,
                                         bool in_game_mode) noexcept {
    return !pause_menu_open && !in_game_mode;
}

} // namespace igi
