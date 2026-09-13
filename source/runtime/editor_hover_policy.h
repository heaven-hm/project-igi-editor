#pragma once

namespace igi {

constexpr bool ShouldPickSceneHover(bool pointerChanged,
                                    bool leftButtonDown) noexcept {
    return pointerChanged && !leftButtonDown;
}

} // namespace igi
