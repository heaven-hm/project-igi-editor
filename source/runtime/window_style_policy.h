#pragma once

#include <cstdint>

namespace igi {

// Win32 style bits the editor window must not show, per the borderless-
// fullscreen design: no caption, no resize frame, and no minimize/maximize/
// close chrome (Pause -> Quit is the supported exit path). Raw USER32 ABI
// constants keep this header free of windows.h so tests stay lightweight.
inline constexpr std::intptr_t kWinStyleCaption = 0x00C00000;
inline constexpr std::intptr_t kWinStyleThickFrame = 0x00040000;
inline constexpr std::intptr_t kWinStyleMinimizeBox = 0x00020000;
inline constexpr std::intptr_t kWinStyleMaximizeBox = 0x00010000;
inline constexpr std::intptr_t kWinStyleSysMenu = 0x00080000;
inline constexpr std::intptr_t kWinStylePopup = 0x80000000;
inline constexpr std::intptr_t kWinStyleVisible = 0x10000000;

// Computes the borderless-fullscreen style for a window: clears every chrome
// bit and switches to WS_POPUP so the surface can own the whole monitor.
inline std::intptr_t BorderlessFullscreenWindowStyle(std::intptr_t style) {
    const std::intptr_t chrome = kWinStyleCaption | kWinStyleThickFrame |
                                 kWinStyleMinimizeBox | kWinStyleMaximizeBox |
                                 kWinStyleSysMenu;
    return (style & ~chrome) | kWinStylePopup;
}

} // namespace igi
