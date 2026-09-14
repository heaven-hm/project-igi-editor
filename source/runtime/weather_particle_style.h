#pragma once

namespace igi {

// Snow must retain enough screen coverage against nearby ground. A very short
// world-space line only appears when viewed against distant sky.
constexpr float WeatherParticleLengthMeters(bool isSnow) noexcept {
    return isSnow ? 0.45f : 0.35f;
}

} // namespace igi
