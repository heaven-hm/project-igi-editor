// test_weather_toggle.cpp
// Per-level weather tests (L3/L7/L9/L12) + CycleWeatherMode toggle state machine

#include <gtest/gtest.h>
#include "../source/runtime/level_weather.h"
#include "../source/runtime/weather_mode.h"
#include "../source/runtime/weather_visibility.h"
#include "../source/runtime/weather_particle_style.h"
#include "../source/renderer/weather_math.h"

namespace {

igi::WeatherEffectObject RainEffect(const char* is_rain, const char* is_active,
                                    const char* alpha = "0.06") {
    return {"RainEffect",
            {"-1", "RainEffect", "", is_rain, "50.0", "20.0", is_active, alpha}};
}

// ============================================================
// LevelPerLevel — authored weather for specific levels
// ============================================================

TEST(LevelPerLevel, Level3HasActiveRain) {
    const igi::LevelWeatherSettings w = igi::ResolveLevelWeather(
        {RainEffect("TRUE", "1", "0.13")});
    EXPECT_TRUE(w.active)            << "Level 3 RainEffect must be active";
    EXPECT_FALSE(w.is_snow)          << "Level 3 is rain (is_rain=TRUE)";
    EXPECT_FLOAT_EQ(w.start_meters, 50.0f);
    EXPECT_FLOAT_EQ(w.end_meters,   20.0f);
    EXPECT_FLOAT_EQ(w.alpha,         0.13f);
    EXPECT_TRUE(igi::ShouldDrawWeatherForFrame(w.active, true));
    EXPECT_FALSE(igi::ShouldDrawWeatherForFrame(w.active, false));
}

TEST(LevelPerLevel, Level7HasActiveSnow) {
    const igi::LevelWeatherSettings w = igi::ResolveLevelWeather(
        {RainEffect("FALSE", "1", "0.06")});
    EXPECT_TRUE(w.active)   << "Level 7 RainEffect must be active";
    EXPECT_TRUE(w.is_snow)  << "Level 7 is snow (is_rain=FALSE)";
    EXPECT_FLOAT_EQ(w.start_meters, 50.0f);
    EXPECT_FLOAT_EQ(w.end_meters,   20.0f);
    EXPECT_FLOAT_EQ(w.alpha, 0.06f);
    EXPECT_TRUE(igi::ShouldDrawWeatherForFrame(w.active, true));
    EXPECT_FALSE(igi::ShouldDrawWeatherForFrame(w.active, false));
}

TEST(LevelPerLevel, Level9HasActiveRainViaVarStringNewline) {
    const igi::LevelWeatherSettings w = igi::ResolveLevelWeather(
        {RainEffect("TRUE", "TRUE\n", "0.15")});
    EXPECT_TRUE(w.active)   << "Level 9 VarString 'TRUE\\n' must resolve to active";
    EXPECT_FALSE(w.is_snow) << "Level 9 is rain";
    EXPECT_FLOAT_EQ(w.start_meters, 50.0f);
    EXPECT_FLOAT_EQ(w.end_meters,   20.0f);
    EXPECT_FLOAT_EQ(w.alpha, 0.15f);
    EXPECT_TRUE(igi::ShouldDrawWeatherForFrame(w.active, true));
    EXPECT_FALSE(igi::ShouldDrawWeatherForFrame(w.active, false));
}

TEST(LevelPerLevel, Level12HasActiveSnowHighAlpha) {
    const igi::LevelWeatherSettings w = igi::ResolveLevelWeather(
        {RainEffect("FALSE", "1", "0.3125")});
    EXPECT_TRUE(w.active)  << "Level 12 RainEffect must be active";
    EXPECT_TRUE(w.is_snow) << "Level 12 is snow (is_rain=FALSE)";
    EXPECT_FLOAT_EQ(w.start_meters, 50.0f);
    EXPECT_FLOAT_EQ(w.end_meters,   20.0f);
    EXPECT_FLOAT_EQ(w.alpha, 0.3125f);
    EXPECT_TRUE(igi::ShouldDrawWeatherForFrame(w.active, true));
}

TEST(LevelPerLevel, InactiveRainEffectShowsNoWeather) {
    // Levels 4/8/11 author is_active=FALSE — must NOT render
    for (const char* is_active : {"FALSE", "FALSE\n", "0"}) {
        const igi::LevelWeatherSettings w = igi::ResolveLevelWeather(
            {RainEffect("TRUE", is_active, "0.13")});
        EXPECT_FALSE(w.active) << "is_active='" << is_active << "' must disable weather";
        EXPECT_FALSE(igi::ShouldDrawWeatherForFrame(w.active, true));
    }
}

TEST(LevelPerLevel, NoRainEffectObjectMeansNoWeather) {
    const igi::LevelWeatherSettings w = igi::ResolveLevelWeather({});
    EXPECT_FALSE(w.active);
    EXPECT_FLOAT_EQ(w.start_meters, 0.0f);
    EXPECT_FLOAT_EQ(w.end_meters,   0.0f);
    EXPECT_FLOAT_EQ(w.alpha,        0.0f);
}

// ============================================================
// WeatherToggle — production weather-mode policy
// ============================================================

TEST(WeatherToggle, CycleSequence_Default_Rain_Snow_Off_Default) {
    auto mode = igi::WeatherMode::Default;
    mode = igi::NextWeatherMode(mode); EXPECT_EQ(mode, igi::WeatherMode::Rain);
    mode = igi::NextWeatherMode(mode); EXPECT_EQ(mode, igi::WeatherMode::Snow);
    mode = igi::NextWeatherMode(mode); EXPECT_EQ(mode, igi::WeatherMode::Off);
    mode = igi::NextWeatherMode(mode); EXPECT_EQ(mode, igi::WeatherMode::Default);
}

TEST(WeatherToggle, DefaultOnLevel3ShowsRain) {
    const auto authored = igi::ResolveLevelWeather({RainEffect("TRUE", "1", "0.13")});
    const auto weather = igi::ResolveWeatherMode(authored, igi::WeatherMode::Default, true);
    EXPECT_TRUE(weather.active);
    EXPECT_FALSE(weather.is_snow);
}

TEST(WeatherToggle, DefaultOnLevel7ShowsSnow) {
    const auto authored = igi::ResolveLevelWeather({RainEffect("FALSE", "1", "0.06")});
    const auto weather = igi::ResolveWeatherMode(authored, igi::WeatherMode::Default, true);
    EXPECT_TRUE(weather.active);
    EXPECT_TRUE(weather.is_snow);
}

TEST(WeatherToggle, ForcedModesOverrideAuthoredWeather) {
    const auto rain = igi::ResolveLevelWeather({RainEffect("TRUE", "1", "0.13")});
    const auto snow = igi::ResolveLevelWeather({RainEffect("FALSE", "1", "0.06")});
    EXPECT_FALSE(igi::ResolveWeatherMode(snow, igi::WeatherMode::Rain, true).is_snow);
    EXPECT_TRUE(igi::ResolveWeatherMode(rain, igi::WeatherMode::Snow, true).is_snow);
}

TEST(WeatherToggle, OffDisablesAllAuthoredLevels) {
    for (const auto& authored : {
             igi::ResolveLevelWeather({RainEffect("TRUE", "1", "0.13")}),
             igi::ResolveLevelWeather({RainEffect("FALSE", "1", "0.06")}),
             igi::ResolveLevelWeather({RainEffect("TRUE", "1", "0.15")}),
             igi::ResolveLevelWeather({RainEffect("FALSE", "1", "0.3125")})}) {
        EXPECT_FALSE(igi::ResolveWeatherMode(authored, igi::WeatherMode::Off, true).active);
        EXPECT_FALSE(igi::ResolveWeatherMode(authored, igi::WeatherMode::Default, false).active);
    }
}

TEST(WeatherToggle, ForcedModesWorkOnNoWeatherLevel) {
    const auto no_weather = igi::ResolveLevelWeather({});
    const auto enabled_no_weather = igi::ResolveWeatherMode(no_weather, igi::WeatherMode::Default, true);
    EXPECT_TRUE(enabled_no_weather.enabled) << "The global setting remains enabled without an authored effect";
    EXPECT_FALSE(enabled_no_weather.active);
    EXPECT_TRUE(igi::ResolveWeatherMode(no_weather, igi::WeatherMode::Rain, true).active);
    EXPECT_TRUE(igi::ResolveWeatherMode(no_weather, igi::WeatherMode::Snow, true).is_snow);
}

TEST(WeatherToggle, OffDisablesTheGlobalRendererGate) {
    const auto authored = igi::ResolveLevelWeather({RainEffect("TRUE", "1", "0.13")});
    EXPECT_FALSE(igi::ResolveWeatherMode(authored, igi::WeatherMode::Off, true).enabled);
    EXPECT_FALSE(igi::ResolveWeatherMode(authored, igi::WeatherMode::Default, false).enabled);
}

TEST(WeatherVisibility, AuthoredWeatherIsSuppressedByEditorBuildingBounds) {
    EXPECT_TRUE(igi::IsWithinWeatherShelterFootprint(5.0f, 2.0f, 0.0f, 10.0f, 0.0f, 4.0f));
    EXPECT_FALSE(igi::IsWithinWeatherShelterFootprint(10.1f, 2.0f, 0.0f, 10.0f, 0.0f, 4.0f));
    EXPECT_FALSE(igi::ShouldDrawWeatherForFrame(true, true, true));
    EXPECT_TRUE(igi::ShouldDrawWeatherForFrame(true, true, false));
    EXPECT_FALSE(igi::ShouldDrawWeatherForFrame(true, false, true));
}

// ============================================================
// WeatherParticle — OpenIGI constant match
// ============================================================

TEST(WeatherParticle, RainStreakIs0_08m) {
    EXPECT_FLOAT_EQ(igi::WeatherParticleLengthMeters(false), 0.08f);
}
TEST(WeatherParticle, RainStreakHasRetail0_012mWorldWidth) {
    EXPECT_FLOAT_EQ(igi::weather::kRainStreakWidthMeters, 0.012f);
}
TEST(WeatherParticle, SnowFlakeHasRetail0_09mDiameter) {
    // OpenIGI's 0.045m FlakeSizeMeters is the half-extent of its quad, so
    // the visible flake footprint is 0.09m across.
    EXPECT_FLOAT_EQ(igi::WeatherParticleLengthMeters(true), 0.09f);
}
TEST(WeatherParticle, SnowFootprintUsesItsRetailFullDiameter) {
    EXPECT_GT(igi::WeatherParticleLengthMeters(true),
              igi::WeatherParticleLengthMeters(false));
}

TEST(WeatherParticle, RestoresRetailParticleCountsAndOpacity) {
    EXPECT_EQ(igi::weather::kRainDrops, 1200);
    EXPECT_EQ(igi::weather::kSnowFlakes, 900);
    EXPECT_FLOAT_EQ(igi::weather::RainStreakAlpha(0.50f), 0.28f);
    EXPECT_FLOAT_EQ(igi::weather::SnowFlakeAlpha(0.25f), 0.42f);
}

TEST(WeatherParticle, SnowQuadUsesRetailHalfExtent) {
    EXPECT_FLOAT_EQ(igi::weather::kSnowFlakeMeters, 0.045f);
    EXPECT_FLOAT_EQ(igi::WeatherParticleLengthMeters(true),
                    igi::weather::kSnowFlakeMeters * 2.0f);
}

} // namespace
