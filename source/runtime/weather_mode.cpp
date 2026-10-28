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

// See the header for why this control has no engine counterpart: the engine's
// speed is `speed = r->snow ? kSnowSpeed : kRainSpeed` (Rain_Update, HOOK
// 0x499CF0, src/dxinit/rain.c:572, constants at rain.c:122-123) and nothing
// scales it. The steps below are the editor's own preview rates.
float WeatherSpeedScale(int step) noexcept {
    switch (static_cast<WeatherSpeed>(step)) {
    case WeatherSpeed::Frozen: return 0.0f;
    case WeatherSpeed::Authored: return 1.0f;
    case WeatherSpeed::Double: return 2.0f;
    }
    return 1.0f;  // out of range -> the engine's authored rate
}

int NextWeatherSpeed(int step) noexcept {
    switch (static_cast<WeatherSpeed>(step)) {
    case WeatherSpeed::Frozen: return static_cast<int>(WeatherSpeed::Authored);
    case WeatherSpeed::Authored: return static_cast<int>(WeatherSpeed::Double);
    case WeatherSpeed::Double: return static_cast<int>(WeatherSpeed::Frozen);
    }
    return kWeatherSpeedDefault;
}

const char* WeatherSpeedLabel(int step) noexcept {
    switch (static_cast<WeatherSpeed>(step)) {
    case WeatherSpeed::Frozen: return "0x";
    case WeatherSpeed::Authored: return "1x";
    case WeatherSpeed::Double: return "2x";
    }
    return "1x";
}

} // namespace igi
