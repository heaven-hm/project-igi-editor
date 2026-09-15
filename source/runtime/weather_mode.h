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

} // namespace igi
