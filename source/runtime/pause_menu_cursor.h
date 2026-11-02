#pragma once

namespace igi {

// The pause menu restores the platform cursor for menu interaction.
constexpr bool ShouldDrawCustomCursor(bool pauseMenuVisible) noexcept {
    return !pauseMenuVisible;
}

constexpr bool ShouldUseNativeCursor(bool pauseMenuVisible) noexcept {
    return pauseMenuVisible;
}

} // namespace igi
