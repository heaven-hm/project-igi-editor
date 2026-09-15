#pragma once

namespace igi {

// Particle visual scales matching OpenIGI (meters):
// RainRenderer uses 0.08m vertical streaks. SnowRenderer's 0.045m
// FlakeSizeMeters is its half-extent, so its visible quad is 0.09m across.
constexpr float WeatherParticleLengthMeters(bool isSnow) noexcept {
    return isSnow ? 0.09f : 0.08f;
}

} // namespace igi
