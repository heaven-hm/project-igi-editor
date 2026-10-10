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

} // namespace igi
