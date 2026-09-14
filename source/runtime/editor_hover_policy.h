#pragma once

#include <cstdint>

namespace igi {

constexpr bool ShouldPickSceneHover(bool pointerChanged,
                                    bool leftButtonDown,
                                    bool cameraNavigating = false) noexcept {
    return pointerChanged && !leftButtonDown && !cameraNavigating;
}

// Scene picking performs a full-color pass plus a synchronous readback. Keep
// hover responsive without allowing rapid mouse motion to issue that blocking
// GPU operation every frame. Camera look/move never picks: that GPU sync is
// what makes turning the view feel like it is running in slow motion.
constexpr bool ShouldPickSceneHover(bool pointerChanged,
                                    bool leftButtonDown,
                                    int64_t nowMs,
                                    int64_t lastPickMs,
                                    int64_t minIntervalMs = 33,
                                    bool cameraNavigating = false) noexcept {
    return ShouldPickSceneHover(pointerChanged, leftButtonDown, cameraNavigating) &&
           (lastPickMs < 0 || nowMs - lastPickMs >= minIntervalMs);
}

// A level can reference models that are not present in its own model archive.
// Resolving every such model from other levels during the normal scene pass
// grows the cross-level caches until the 32-bit editor runs out of memory.
// Keep the useful magenta foreign-model preview, but load it only on demand.
constexpr bool ShouldResolveMissingSceneModel(bool modelMissingInLevel,
                                              bool selected,
                                              bool hovered) noexcept {
    return !modelMissingInLevel || selected || hovered;
}

} // namespace igi
