#pragma once

namespace igi {

constexpr bool ShouldRunAutoSave(bool editorMode, bool enabled, bool paused,
                                 int levelNumber) noexcept {
    return editorMode && enabled && !paused && levelNumber > 0;
}

constexpr bool ShouldSaveBeforeExternalGameLaunch(bool editorMode,
                                                   bool autoSaveEnabled) noexcept {
    return editorMode && autoSaveEnabled;
}

constexpr bool ShouldSaveBeforeEditorExit(bool editorMode, bool autoSaveEnabled,
                                          int levelNumber) noexcept {
    return editorMode && autoSaveEnabled && levelNumber > 0;
}

} // namespace igi
