#pragma once

#include <algorithm>

namespace igi::weather {

constexpr int kRainDrops = 1200;
constexpr int kSnowFlakes = 900;
constexpr float kBoxMeters = 50.0f;
constexpr float kRainStreakMeters = 0.08f;
constexpr float kRainStreakWidthMeters = 0.012f;
constexpr float kSnowFlakeMeters = 0.045f;
constexpr float kRainMinSpeedMul = 0.08f;
constexpr float kRainMaxSpeedMul = 0.18f;
constexpr float kSnowMinSpeedMul = 0.025f;
constexpr float kSnowMaxSpeedMul = 0.065f;
constexpr float kRainAlphaBoost = 1.25f;
constexpr float kRainMaxAlpha = 0.28f;
constexpr float kSnowAlphaBoost = 1.75f;
constexpr float kSnowMinAlpha = 0.10f;
constexpr float kSnowMaxAlpha = 0.42f;
constexpr float kSnowDriftMeters = 0.35f;

inline float RainStreakAlpha(float authoredAlpha) noexcept {
    return std::clamp(authoredAlpha * kRainAlphaBoost, 0.0f, kRainMaxAlpha);
}

inline float SnowFlakeAlpha(float authoredAlpha) noexcept {
    return std::clamp(authoredAlpha * kSnowAlphaBoost, kSnowMinAlpha, kSnowMaxAlpha);
}

} // namespace igi::weather
