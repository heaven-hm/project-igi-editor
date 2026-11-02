#pragma once

namespace igi {

// A building that shelters a player at ground level also shelters its upper
// floors. Visual meshes often omit an upper floor or roof, so their Z extent
// cannot decide whether precipitation reaches an indoor camera.
constexpr bool IsWithinWeatherShelterFootprint(
    float localX, float localY,
    float minX, float maxX,
    float minY, float maxY) noexcept {
    return localX >= minX && localX <= maxX &&
           localY >= minY && localY <= maxY;
}

// RainEffect is authored at the level scope, but precipitation is only visible
// where the camera has an open sky. A Building mesh bound containing the camera
// represents shelter for the procedural weather pass.
constexpr bool ShouldRenderAuthoredWeather(
    bool effectActive, bool visualBuildingBoundsOverlap = false) noexcept {
    return effectActive && !visualBuildingBoundsOverlap;
}

// Object visibility filters do not control weather. The shelter test is passed
// separately so rain and snow remain hidden indoors even when buildings are
// hidden in the editor.
constexpr bool ShouldDrawWeatherForFrame(bool effectActive,
                                         bool rainRendererReady,
                                         bool cameraIsSheltered = false) noexcept {
    return ShouldRenderAuthoredWeather(effectActive, cameraIsSheltered) &&
           rainRendererReady;
}

} // namespace igi
