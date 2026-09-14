#pragma once

#include <cstdint>

namespace igi {

constexpr bool ShouldPickSceneHover(bool pointerChanged,
                                    bool leftButtonDown) noexcept {
    return pointerChanged && !leftButtonDown;
}

// Scene picking performs a full-color pass plus a synchronous readback. Keep
// hover responsive without allowing rapid mouse motion to issue that blocking
// GPU operation every frame.
constexpr bool ShouldPickSceneHover(bool pointerChanged,
                                    bool leftButtonDown,
                                    int64_t nowMs,
                                    int64_t lastPickMs,
                                    int64_t minIntervalMs = 33) noexcept {
    return ShouldPickSceneHover(pointerChanged, leftButtonDown) &&
           (lastPickMs < 0 || nowMs - lastPickMs >= minIntervalMs);
}

} // namespace igi
