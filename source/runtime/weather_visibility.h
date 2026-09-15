#pragma once

namespace igi {

// Building mesh bounds are not weather shelters. The editor has no authoritative
// roof-volume data, so authored RainEffect state controls precipitation globally.
constexpr bool IsWithinWeatherShelterFootprint(
    float localX, float localY,
    float minX, float maxX,
    float minY, float maxY) noexcept {
    return localX >= minX && localX <= maxX &&
           localY >= minY && localY <= maxY;
}

constexpr bool ShouldRenderAuthoredWeather(bool effectActive,
                                           bool /*cameraIsSheltered*/ = false) noexcept {
    return effectActive;
}

// Visibility heuristics do not control authored weather.
constexpr bool ShouldDrawWeatherForFrame(bool effectActive,
                                         bool rainRendererReady,
                                         bool cameraIsSheltered = false) noexcept {
    return ShouldRenderAuthoredWeather(effectActive, cameraIsSheltered) &&
           rainRendererReady;
}

} // namespace igi
