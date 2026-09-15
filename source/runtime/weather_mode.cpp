#include "weather_mode.h"

namespace igi {

WeatherRenderSettings ResolveWeatherMode(const LevelWeatherSettings& authored,
                                         WeatherMode mode,
                                         bool weather_enabled) noexcept {
    if (!weather_enabled || mode == WeatherMode::Off) return {};

    if (mode == WeatherMode::Rain || mode == WeatherMode::Snow) {
        return {true,
                true,
                mode == WeatherMode::Snow,
                authored.active ? authored.start_meters : 10.0f,
                authored.active ? authored.end_meters : 2.0f,
                (authored.active && authored.alpha > 0.0f)
                    ? authored.alpha
                    : (mode == WeatherMode::Snow ? 0.25f : 0.5f)};
    }

    return {true, authored.active, authored.is_snow, authored.start_meters,
            authored.end_meters, authored.alpha};
}

WeatherMode NextWeatherMode(WeatherMode mode) noexcept {
    switch (mode) {
    case WeatherMode::Default: return WeatherMode::Rain;
    case WeatherMode::Rain: return WeatherMode::Snow;
    case WeatherMode::Snow: return WeatherMode::Off;
    case WeatherMode::Off: return WeatherMode::Default;
    }
    return WeatherMode::Default;
}

const char* WeatherModeLabel(WeatherMode mode) noexcept {
    switch (mode) {
    case WeatherMode::Default: return "Default";
    case WeatherMode::Rain: return "Rain";
    case WeatherMode::Snow: return "Snow";
    case WeatherMode::Off: return "OFF";
    }
    return "Default";
}

} // namespace igi
