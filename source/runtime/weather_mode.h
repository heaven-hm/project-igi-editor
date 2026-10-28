#pragma once

#include "level_weather.h"

namespace igi {

enum class WeatherMode : int {
    Default = 0,
    Rain = 1,
    Snow = 2,
    Off = 3,
};

struct WeatherRenderSettings {
    bool enabled = false;
    bool active = false;
    bool is_snow = false;
    float start_meters = 0.0f;
    float end_meters = 0.0f;
    float alpha = 0.0f;
};

WeatherRenderSettings ResolveWeatherMode(const LevelWeatherSettings& authored,
                                         WeatherMode mode,
                                         bool weather_enabled) noexcept;
WeatherMode NextWeatherMode(WeatherMode mode) noexcept;
const char* WeatherModeLabel(WeatherMode mode) noexcept;

// ─── The editor's weather preview speed ─────────────────────────────────────
//
// NO ENGINE ORACLE. The engine's fall speed is a pair of compile-time constants
// selected by the snow flag and nothing scales them:
//
//     speed = r->snow ? kSnowSpeed : kRainSpeed;
//       -- Rain_Update, HOOK 0x499CF0, src/dxinit/rain.c:572
//     static const float kSnowSpeed = 204.8f;   // 0.05 m/tick
//     static const float kRainSpeed = 2048.0f;  // 0.50 m/tick
//       -- src/dxinit/rain.c:122-123
//
// There is no cvar, no task parameter and no global for it (the only rain
// globals in the oracle are gRainEnabled, src/dxinit/rain.c:692, and the
// cutscene's gRaining, src/game/game.c:1112). The multiplier below is therefore
// the EDITOR's preview control, and the pause-menu row says so.
//
// It is applied to exactly the one value the engine derives every other
// quantity from, so the preview stays internally consistent: the fall
// (`d->z - speed`, rain.c:587-589), the spawn accumulator (`acc = speed +
// r->spawn`, rain.c:619) and the spawn z (`d->z = kLayer - speed`, rain.c:631).
// Step 0 is a true freeze -- the accumulator is always left below kSpawnStep at
// the end of a tick (`acc = r->spawn - kSpawnStep`, rain.c:634-635), so a
// frozen system emits nothing further.
enum class WeatherSpeed : int {
    Frozen = 0,   // 0.0x -- the preview holds still
    Authored = 1, // 1.0x -- the engine's own kSnowSpeed / kRainSpeed
    Double = 2,   // 2.0x -- the preview runs at twice the authored rate
};

inline constexpr int kWeatherSpeedCount = 3;
inline constexpr int kWeatherSpeedDefault = static_cast<int>(WeatherSpeed::Authored);

// The multiplier for a persisted step. An out-of-range step resolves to the
// authored rate, so a hand-edited config can never freeze the preview by
// accident.
float WeatherSpeedScale(int step) noexcept;

// Frozen -> Authored -> Double -> Frozen; anything out of range restarts at
// Authored.
int NextWeatherSpeed(int step) noexcept;

// The value box's text: "0x", "1x", "2x".
const char* WeatherSpeedLabel(int step) noexcept;

} // namespace igi
