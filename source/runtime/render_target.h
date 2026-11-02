#pragma once

namespace igi {

enum class RenderTarget {
    Editor,
    Gameplay,
};

enum class RuntimeAssetTarget {
    EditorSource,
    GameplaySnapshot,
};

// An active gameplay session can still receive editor-window paint messages.
// Those messages must render the authoring surface without becoming a second
// gameplay update/render target.
constexpr RenderTarget ResolveRenderTarget(
    bool gameplay_mode_active,
    bool rendering_editor_window) noexcept {
    return gameplay_mode_active && !rendering_editor_window
        ? RenderTarget::Gameplay
        : RenderTarget::Editor;
}

// Simulation continues while the editor window is repainting. In that case
// render-target selection must not decide which mutable object copy collision
// and interaction code consumes.
constexpr RuntimeAssetTarget ResolveRuntimeAssetTarget(
    bool gameplay_mode_active,
    bool gameplay_snapshot_available) noexcept {
    return gameplay_mode_active && gameplay_snapshot_available
        ? RuntimeAssetTarget::GameplaySnapshot
        : RuntimeAssetTarget::EditorSource;
}

// The pause menu is shared by editor and gameplay. It must remain interactive
// while the editor owns input, otherwise the level spinner cannot submit.
constexpr bool IsPauseMenuVisible(
    bool pause_menu_open,
    bool /*gameplay_input_focused*/) noexcept {
    return pause_menu_open;
}

constexpr bool IsPauseMenuInputActive(
    bool pause_menu_open,
    bool /*gameplay_input_focused*/) noexcept {
    return pause_menu_open;
}

constexpr bool IsEditorInteractionActive(
    bool pause_menu_open,
    bool in_game_mode,
    bool gameplay_input_focused) noexcept {
    return !pause_menu_open && (!in_game_mode || !gameplay_input_focused);
}
} // namespace igi
