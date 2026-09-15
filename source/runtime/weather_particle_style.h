#pragma once

namespace igi {

// Particle visual scales matching OpenIGI (meters):
// Rain streak vertical length = 0.08m (StreakLengthMeters in RainRenderer.cs)
// Snow flake scale = 0.045m (FlakeSizeMeters in SnowRenderer.cs)
constexpr float WeatherParticleLengthMeters(bool isSnow) noexcept {
    return isSnow ? 0.045f : 0.08f;
}

} // namespace igi
