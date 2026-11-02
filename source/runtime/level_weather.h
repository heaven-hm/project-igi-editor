#pragma once

#include <string>
#include <vector>

namespace igi {

struct WeatherEffectObject {
    std::string type;
    std::vector<std::string> arg_tokens;
};

struct LevelWeatherSettings {
    bool active = false;
    bool is_snow = false;
    float start_meters = 0.0f;
    float end_meters = 0.0f;
    float alpha = 0.0f;
};

LevelWeatherSettings ResolveLevelWeather(
    const std::vector<WeatherEffectObject>& objects);

} // namespace igi
