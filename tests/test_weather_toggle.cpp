// test_weather_toggle.cpp
// Per-level weather tests (L3/L7/L9/L12) + CycleWeatherMode toggle state machine

#include <gtest/gtest.h>
#include "../source/runtime/level_weather.h"
#include "../source/runtime/weather_mode.h"
#include "../source/runtime/weather_visibility.h"
#include "../source/runtime/rain_rules.h"

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
// WeatherParticle — the engine's rain/snow extents and alpha
// ============================================================
//
// These replace the "OpenIGI constant match" pins this file used to carry
// (0.08 m rain streak, 0.012 m width, 0.045 m half-extent flake, 1200/900
// particles, boosted-and-clamped alpha). Those came from
// source/renderer/weather_math.h and source/runtime/weather_particle_style.h,
// both deleted with the port; the renderer now draws the engine's own geometry
// (source/renderer/renderer_rain.cpp, source/runtime/rain_rules.h).

TEST(WeatherParticle, RainStreakIsTheEngineHalfMetreLine) {
    // rain.c:311,671 - `len = fade * kStreak` with kStreak = 2048 units == 0.5 m.
    EXPECT_FLOAT_EQ(igi::rain::RainStreakLengthUnits() / igi::rain::kUnitsPerMetre, 0.5f);
    // rain.c:366-372 - the streak is a 2-vertex LINE, so it has no world width
    // of its own; the editor used to give it an invented 0.012 m one.
    EXPECT_EQ(igi::rain::RainVerticesPerParticle(), 2);
}

TEST(WeatherParticle, SnowFlakeIsTheEngineFortyMillimetreQuad) {
    // rain.c:309,341-342,685 - `size = fade * kFlakeSize` with kFlakeSize =
    // 81.92 units, applied as +/- size on both axes, so the quad is 0.04 m.
    EXPECT_FLOAT_EQ(igi::rain::SnowFlakeSizeUnits() / igi::rain::kUnitsPerMetre, 0.02f);
    EXPECT_FLOAT_EQ(igi::rain::SnowFlakeWidthUnits() / igi::rain::kUnitsPerMetre, 0.04f);
    EXPECT_EQ(igi::rain::SnowVerticesPerParticle(), 6);
}

TEST(WeatherParticle, SnowIsNotLargerThanTheRainStreak) {
    // The engine's flake is SMALLER than the rain streak: 0.04 m across against
    // 0.5 m long (rain.c:685,671). The old editor pin asserted the opposite,
    // because its snow quad (0.09 m) and rain streak (0.08 m) were both invented.
    EXPECT_LT(igi::rain::SnowFlakeWidthUnits(), igi::rain::RainStreakLengthUnits());
}

TEST(WeatherParticle, AlphaIsAVertexByteNotABoost) {
    // rain.c:646-649 - Rain_SetAlpha packs trunc(alpha * 255) into the top byte
    // of the rain line's bottom vertex; there is no boost and no clamp, and the
    // flake's diffuse is the opaque 0xFFFFFFFF literal (rain.c:353).
    EXPECT_EQ(igi::rain::AlphaByte(0.50f), 127);
    EXPECT_EQ((igi::rain::RainStreakBottomDiffuse(0.50f) >> 24) & 0xFFu, 127u);
    EXPECT_EQ(igi::rain::SnowFlakeDiffuse(), 0xFFFFFFFFu);
}

TEST(WeatherParticle, PoolIsEightyDropsForBothModes) {
    // rain.c:65,624 - one fixed pool of 80 for rain and snow alike.
    EXPECT_EQ(igi::rain::kDropCapacity, 80);
    EXPECT_EQ(igi::rain::DropKindCount(), 4);
}

} // namespace
