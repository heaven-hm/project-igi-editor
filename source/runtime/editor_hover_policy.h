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

} // namespace igi
