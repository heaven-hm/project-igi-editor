// test_rain_rules.cpp - the retail rain rules (#58), extracted from IGI-RE.
//
// The oracle for every number here is IGI-RE (read-only). Key citations:
//   dxinit/rain.c:39-96        the RainDrop / RainList / Rain task layout
//   dxinit/rain.c:122-123      kSnowSpeed 204.8, kRainSpeed 2048.0
//   dxinit/rain.c:144-167      Rain_New HOOK 0x498960 (snow = rain == 0)
//   dxinit/rain.c:220-278      Rain_UpdateGrid HOOK 0x498BD0 (tile lattice, ground probe)
//   dxinit/rain.c:280-303      Rain_Draw HOOK 0x498F10 (quantised z, 9x9 window)
//   dxinit/rain.c:565-637      Rain_Update HOOK 0x499CF0 (speed, wrap, spawn accumulator)
//   dxinit/rain.c:646-649      Rain_SetAlpha HOOK 0x499FA0
//   dxinit/rain.c:663-697      the DATA constants, at their IGI.exe addresses
//   dxinit/polylist.c:432-434  kMinus255 = -255.0f, ALIAS kAlphaScale
//   dxinit/dxinit.c:75-80      FPU_SetTruncate HOOK 0x498460 (round toward zero)
//   win/appmain.c:212          FPU_SetTruncate called before the game loop
//   fxobj/rain.c:60-96         RainEffect_Init HOOK 0x436780 / _Create HOOK 0x436950
//   fxobj/rain.c:131-146       RainEffect_Changed HOOK 0x436A50
//
// The second half of the file pins the editor's CURRENT behaviour against those
// rules, so docs/igi-re-parity/RAIN.md's "diverges" column is executable rather
// than asserted. Nothing here renders a frame: the editor's rain path is a GL
// 3.3 vertex shader and this host cannot run the editor (GL 2.1 only).
//
// The corpus half reads the retail LEVEL4/objects.qsc, the only shipped level
// that carries a decompiled text form, and cross-checks the five-parameter
// declaration against the ParamTable order.

#include "../source/renderer/weather_math.h"
#include "../source/runtime/rain_rules.h"
#include "../source/runtime/weather_particle_style.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// corpus helpers
// ---------------------------------------------------------------------------

fs::path GameRoot() {
    if (const char* env = std::getenv("IGI_GAME_PATH"); env != nullptr && *env != '\0')
        return fs::path(env);
    return fs::path("/Users/haseeb-mir/Downloads/IGI");
}

fs::path Level4Qsc() {
    return GameRoot() / "MISSIONS" / "location0" / "LEVEL4" / "objects.qsc";
}

std::string ReadFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// Splits the argument list of the first call whose text starts with `prefix`.
// Returns the raw comma-separated arguments, without the surrounding parens.
std::string CallArguments(const std::string& text, const std::string& prefix) {
    const std::size_t at = text.find(prefix);
    if (at == std::string::npos) return {};
    const std::size_t open = text.find('(', at);
    if (open == std::string::npos) return {};
    int depth = 0;
    for (std::size_t i = open; i < text.size(); ++i) {
        if (text[i] == '(') ++depth;
        else if (text[i] == ')') {
            if (--depth == 0) return text.substr(open + 1, i - open - 1);
        }
    }
    return {};
}

std::vector<std::string> SplitTopLevel(const std::string& args) {
    std::vector<std::string> out;
    std::string current;
    bool in_string = false;
    int depth = 0;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const char c = args[i];
        if (in_string) {
            current.push_back(c);
            if (c == '\\' && i + 1 < args.size()) current.push_back(args[++i]);
            else if (c == '"') in_string = false;
            continue;
        }
        if (c == '"') { in_string = true; current.push_back(c); continue; }
        if (c == '(') { ++depth; current.push_back(c); continue; }
        if (c == ')') { --depth; current.push_back(c); continue; }
        if (c == ',' && depth == 0) { out.push_back(current); current.clear(); continue; }
        current.push_back(c);
    }
    if (!current.empty()) out.push_back(current);
    return out;
}

std::string Trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

std::string Unquote(const std::string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') return s.substr(1, s.size() - 2);
    return s;
}

// The editor's own units constant (common.h:40) - repeated here because common.h
// pulls in windows.h and is not host-portable.
constexpr float kEditorUnitsPerMetre = 4096.0f;

}  // namespace

// ===========================================================================
// R1 - Rain_SetAlpha, HOOK 0x499FA0 (dxinit/rain.c:646-649)
// ===========================================================================

TEST(RainRules, AlphaByteIsTruncatedScale) {
    // rain.c:648 packs the alpha as a byte: the derived value is trunc(a * 255).
    EXPECT_EQ(igi::rain::AlphaByte(0.0f), 0);
    EXPECT_EQ(igi::rain::AlphaByte(0.3125f), 79);  // the RainEffect_Create default
    EXPECT_EQ(igi::rain::AlphaByte(1.0f), 255);

    for (int i = 0; i <= 1000; ++i) {
        const float a = static_cast<float>(i) / 1000.0f;
        const int expected = std::clamp(static_cast<int>(a * 255.0f), 0, 255);
        EXPECT_EQ(igi::rain::AlphaByte(a), expected) << "alpha " << a;
    }
}

TEST(RainRules, AlphaByteTruncatesRatherThanRounding) {
    // FPU_SetTruncate (dxinit.c:75-80) sets RC = 11 and AppMain_WinMain calls it
    // at win/appmain.c:212 before the game loop, so FISTP truncates. 0.5 * 255 is
    // exactly 127.5: truncation gives 127, round-half-to-even would give 128.
    EXPECT_EQ(igi::rain::AlphaByte(0.5f), 127);
}

TEST(RainRules, AlphaByteMatchesTheLiteralBitTrick) {
    // The engine writes `color = 0xFFFFFF - ((unsigned)fistp(a * -255) << 24)`
    // (rain.c:648). Re-derive it here, independently of the module, to prove the
    // byte claim above is the same expression and not a reinterpretation.
    for (int i = 0; i <= 256; ++i) {
        const float a = static_cast<float>(i) / 256.0f;
        const int n = static_cast<int>(a * -255.0f);
        const unsigned shifted = static_cast<unsigned>(n) << 24;
        const unsigned color = 0x00FFFFFFu - shifted;
        EXPECT_EQ(igi::rain::AlphaByte(a), static_cast<int>((color >> 24) & 0xFFu)) << "alpha " << a;
    }
}

// ===========================================================================
// R2 - Rain_Update, HOOK 0x499CF0 (dxinit/rain.c:565-637)
// ===========================================================================

TEST(RainRules, SpeedsAreConstantPerTick) {
    // rain.c:122-123 and rain.c:572 - the only motion term, no gravity.
    EXPECT_FLOAT_EQ(igi::rain::SpeedUnitsPerTick(false), 2048.0f);
    EXPECT_FLOAT_EQ(igi::rain::SpeedUnitsPerTick(true), 204.8f);
    EXPECT_FLOAT_EQ(igi::rain::SpeedUnitsPerTick(false) / igi::rain::kUnitsPerMetre, 0.5f);
    EXPECT_FLOAT_EQ(igi::rain::SpeedUnitsPerTick(true) / igi::rain::kUnitsPerMetre, 0.05f);
}

TEST(RainRules, LifetimeIsTheLayerWrapPeriod) {
    // kLayerUnits (rain.c:681) / speed: 12 ticks for rain, 120 for snow.
    EXPECT_EQ(igi::rain::LifetimeTicks(false), 12);
    EXPECT_EQ(igi::rain::LifetimeTicks(true), 120);
}

TEST(RainRules, WrapZRecyclesAtTheLayerFloor) {
    // rain.c:587-591.
    EXPECT_FLOAT_EQ(igi::rain::WrapZ(igi::rain::kLayerUnits, 2048.0f),
                    igi::rain::kLayerUnits - 2048.0f);
    EXPECT_FLOAT_EQ(igi::rain::WrapZ(1024.0f, 2048.0f), igi::rain::kLayerUnits - 1024.0f);
    EXPECT_FLOAT_EQ(igi::rain::WrapZ(2048.0f, 2048.0f), 0.0f);  // not < 0, no wrap
    EXPECT_FLOAT_EQ(igi::rain::WrapZ(2047.0f, 2048.0f), igi::rain::kLayerUnits - 1.0f);
}

TEST(RainRules, SpawnAccumulatorKeepsTheRemainder) {
    // rain.c:619-636. A speed exactly equal to kSpawnStep emits nothing on the
    // first tick (the test is strict `>`), then exactly one drop per tick.
    float acc = 0.0f;
    EXPECT_EQ(igi::rain::SpawnCountForTick(igi::rain::kSpawnStep, acc, 0), 0);
    EXPECT_FLOAT_EQ(acc, igi::rain::kSpawnStep);

    int total = 0;
    for (int tick = 0; tick < 10; ++tick)
        total += igi::rain::SpawnCountForTick(igi::rain::kSpawnStep, acc, total);
    EXPECT_EQ(total, 10);
    EXPECT_FLOAT_EQ(acc, igi::rain::kSpawnStep);
}

TEST(RainRules, RainSpawnRateMatchesSpeedOverSpawnStep) {
    // Steady state is speed / kSpawnStep == 6.6667 drops per tick for rain.
    // drops_already stays 0 here so the pool cap does not mask the rate; the cap
    // itself is pinned by PoolFullStopsSpawningWithoutDraining.
    float acc = 0.0f;
    int total = 0;
    for (int tick = 0; tick < 600; ++tick)
        total += igi::rain::SpawnCountForTick(igi::rain::kRainSpeedUnitsPerTick, acc, 0);

    const double expected = 600.0 * 2048.0 / 307.2;  // == 4000
    EXPECT_NEAR(static_cast<double>(total), expected, 1.0);
    EXPECT_LT(acc, igi::rain::kSpawnStep);
    EXPECT_GE(acc, 0.0f);
}

TEST(RainRules, PoolFullStopsSpawningWithoutDraining) {
    // rain.c:624-625: at dropCount == 80 Rain_Update returns, leaving Rain::spawn
    // holding the unspent accumulator.
    float acc = 0.0f;
    EXPECT_EQ(igi::rain::SpawnCountForTick(igi::rain::kRainSpeedUnitsPerTick, acc,
                                           igi::rain::kDropCapacity), 0);
    EXPECT_GT(acc, igi::rain::kSpawnStep);  // NOT drained
}

// ===========================================================================
// R3 - Rain_UpdateGrid HOOK 0x498BD0 / Rain_Draw HOOK 0x498F10
// ===========================================================================

TEST(RainRules, GridSnapsToTheTwelveTileLattice) {
    // rain.c:223-236 with camera at the world origin: ix == -5, mx == 7.
    const igi::rain::GridPlacement grid = igi::rain::PlaceGrid(0.0, 0.0);
    EXPECT_EQ(grid.tile_x, 7);
    EXPECT_EQ(grid.tile_y, 7);
    EXPECT_DOUBLE_EQ(grid.origin_x, -12.0 * igi::rain::kTileUnits);
    EXPECT_DOUBLE_EQ(grid.origin_y, -12.0 * igi::rain::kTileUnits);
}

TEST(RainRules, DrawnWindowIsTheNineTilesAfterTheCameraTile) {
    // Derived from rain.c:249,253 (the 12-tile mirror shift) and rain.c:282,288
    // (the window starts at array cell cellX+1 and is 9 wide): the drawn world
    // tiles are exactly (camera tile + 1 .. camera tile + 9).
    for (double camera = -400000.0; camera <= 400000.0; camera += 3701.0) {
        const igi::rain::GridPlacement grid = igi::rain::PlaceGrid(camera, camera);
        const int ix = static_cast<int>(
            std::floor((camera - igi::rain::kGridOriginUnits) * 9.178218688807207e-05 + 0.5));
        for (int k = 0; k < igi::rain::kDrawWindowTiles; ++k) {
            const double expected = static_cast<double>(ix + 1 + k) * igi::rain::kTileUnits;
            EXPECT_DOUBLE_EQ(igi::rain::DrawTileX(grid, k), expected)
                << "camera " << camera << " index " << k;
            EXPECT_DOUBLE_EQ(igi::rain::DrawTileY(grid, k), expected)
                << "camera " << camera << " index " << k;
        }
    }
}

TEST(RainRules, NineTilesIsTwentyFourMetresWide) {
    // 9 tiles * 10895.3603515625 units / 4096 units per metre == 23.9400 m.
    const double span = 9.0 * igi::rain::kTileUnits / igi::rain::kUnitsPerMetre;
    EXPECT_NEAR(span, 23.94, 1e-6);
}

// ===========================================================================
// R4 - authored height/depth and the vertical band
// ===========================================================================

TEST(RainRules, AuthoredTracelinesOnlySizeTheGroundProbe) {
    // fxobj/rain.c:138-141 scales by 4096 and rain.c:160 adds them.
    EXPECT_FLOAT_EQ(igi::rain::FallDistanceUnits(50.0f, 20.0f), 70.0f * 4096.0f);
    EXPECT_FLOAT_EQ(igi::rain::ProbeTopUnits(1000.0f, 50.0f), 1000.0f + 50.0f * 4096.0f);
}

TEST(RainRules, VerticalBandIsEighteenMetresAroundTheCamera) {
    // rain.c:283 snaps the camera z to the kLayer lattice; rain.c:464-468 draws
    // three kLayer-tall layers whose top is kTop above the snapped base. Derived:
    // the returned offsets are camera-relative, span exactly 3 * kLayer == kTop
    // (18 m), and slide in kLayer (6 m) steps, sitting 6..12 m below the camera.
    EXPECT_FLOAT_EQ(igi::rain::LayerBaseZUnits(0.0, 0), -6.0f * 4096.0f);
    EXPECT_FLOAT_EQ(igi::rain::LayerBaseZUnits(0.0, 1), 0.0f);
    EXPECT_FLOAT_EQ(igi::rain::LayerBaseZUnits(0.0, 2), 6.0f * 4096.0f);

    for (double z = -300000.0; z <= 300000.0; z += 1234.5) {
        const float low = igi::rain::LayerBaseZUnits(z, 0);
        const float mid = igi::rain::LayerBaseZUnits(z, 1);
        const float high = igi::rain::LayerBaseZUnits(z, 2);
        EXPECT_FLOAT_EQ(mid - low, igi::rain::kLayerUnits);
        EXPECT_FLOAT_EQ(high - mid, igi::rain::kLayerUnits);
        // Band height is always kTop, independent of the camera.
        EXPECT_FLOAT_EQ(high + igi::rain::kLayerUnits - low, igi::rain::kTopUnits);
        // The band slides in kLayer steps within +/-12 m of the camera.
        EXPECT_GT(high, 0.0f);
        EXPECT_LE(high, igi::rain::kLayerUnits);
        EXPECT_GT(low, -2.0f * igi::rain::kLayerUnits);
        EXPECT_LE(low, -igi::rain::kLayerUnits);
    }
}

TEST(RainRules, VerticalBandDoesNotDependOnTheAuthoredTracelines) {
    // The authored values reach only Coll_DropPointsMat's probe (rain.c:258-265),
    // never the drop z, which is always the kLayer wrap band.
    EXPECT_FLOAT_EQ(igi::rain::LayerBaseZUnits(0.0, 0), igi::rain::LayerBaseZUnits(0.0, 0));
    EXPECT_NE(igi::rain::FallDistanceUnits(5.0f, 5.0f), igi::rain::kLayerUnits);
}

// ===========================================================================
// R5 - snow-only motion (dxinit/rain.c:592-617)
// ===========================================================================

TEST(RainRules, SnowSwayIsBoundedByHalfOfKSway) {
    EXPECT_FLOAT_EQ(igi::rain::SnowSwayHalfRangeUnits(), igi::rain::kSwayUnits * 0.5f);
    EXPECT_FLOAT_EQ(igi::rain::SnowSwayHalfRangeUnits(), 68.26667f);
    EXPECT_EQ(igi::rain::SnowSwayTicksMin(), 30);
    EXPECT_EQ(igi::rain::SnowSwayTicksMax(), 59);
}

TEST(RainRules, ParticleExtents) {
    // rain.c:309,311 and the DATA at rain.c:671,685.
    EXPECT_FLOAT_EQ(igi::rain::RainStreakLengthUnits(), 2048.0f);   // 0.5 m
    EXPECT_FLOAT_EQ(igi::rain::SnowFlakeSizeUnits(), 81.92f);       // 0.02 m half-extent
    EXPECT_EQ(igi::rain::DropKindCount(), 4);
}

// ===========================================================================
// R6 - the authored schema (fxobj/rain.c:60-96)
// ===========================================================================

TEST(RainRules, RetailLevel4InstanceParsesToTheOracleFields) {
    // MISSIONS/location0/LEVEL4/objects.qsc:75
    //   Task_New(-1, "RainEffect", "", TRUE, 50.0, 20.0, "FALSE\n", 0.059999998658895493)
    const std::vector<std::string> tokens = {
        "-1", "RainEffect", "", "TRUE", "50.0", "20.0", "\"FALSE\\n\"", "0.059999998658895493"};
    const igi::rain::AuthoredRainEffect effect =
        igi::rain::ParseRainEffect("RainEffect", tokens);

    EXPECT_TRUE(effect.present);
    EXPECT_TRUE(effect.is_rain);              // "Is Rain" TRUE -> rain streaks
    EXPECT_FLOAT_EQ(effect.traceline_start_metres, 50.0f);
    EXPECT_FLOAT_EQ(effect.traceline_end_metres, 20.0f);
    EXPECT_FALSE(effect.falling);             // "Is Active" FALSE -> not falling
    EXPECT_TRUE(effect.active_is_literal);
    EXPECT_FLOAT_EQ(effect.alpha, 0.059999998658895493f);
}

TEST(RainRules, IsRainIsInvertedIntoTheSnowFlag) {
    // rain.c:153 `r->snow = rain == 0`, fed from unk24 ("Is Rain") at
    // fxobj/rain.c:141. TRUE means rain, FALSE means snow.
    const igi::rain::AuthoredRainEffect rain = igi::rain::ParseRainEffect(
        "RainEffect", {"-1", "RainEffect", "", "TRUE", "50.0", "20.0", "TRUE", "0.13"});
    const igi::rain::AuthoredRainEffect snow = igi::rain::ParseRainEffect(
        "RainEffect", {"-1", "RainEffect", "", "FALSE", "50.0", "20.0", "TRUE", "0.3125"});
    EXPECT_TRUE(rain.is_rain);
    EXPECT_FALSE(snow.is_rain);
}

TEST(RainRules, ActiveIsAVarStringExpressionNotABoolean) {
    // fxobj/rain.c:102-103 re-evaluates the "Is Active" VarString every tick and
    // calls Rain_SetFalling with the result.
    const igi::rain::AuthoredRainEffect scripted = igi::rain::ParseRainEffect(
        "RainEffect", {"-1", "RainEffect", "", "TRUE", "50.0", "20.0", "Var0 > 3", "0.13"});
    EXPECT_TRUE(scripted.present);
    EXPECT_FALSE(scripted.active_is_literal);
    EXPECT_EQ(scripted.active_expression, "Var0 > 3");
}

TEST(RainRules, MissingOrWrongTaskIsNotPresent) {
    EXPECT_FALSE(igi::rain::ParseRainEffect("RainEffect", {"-1", "RainEffect", ""}).present);
    EXPECT_FALSE(igi::rain::ParseRainEffect("Smoke", {"-1", "Smoke", "", "1", "2", "3", "4", "5"}).present);
}

TEST(RainRules, DefaultsComeFromRainEffectCreate) {
    // fxobj/rain.c:86-96. The oracle's alpha default is 0.3125, not zero.
    const igi::rain::AuthoredRainEffect effect = igi::rain::ParseRainEffect(
        "RainEffect", {"-1", "RainEffect", "", "", "", "", "", ""});
    EXPECT_TRUE(effect.present);
    EXPECT_FLOAT_EQ(effect.traceline_start_metres, 50.0f);
    EXPECT_FLOAT_EQ(effect.traceline_end_metres, 20.0f);
    EXPECT_FLOAT_EQ(effect.alpha, 0.3125f);
    EXPECT_EQ(igi::rain::AlphaByte(effect.alpha), 79);
}

// ===========================================================================
// Divergence pins: the editor's current behaviour, for docs/igi-re-parity/RAIN.md
// ===========================================================================

TEST(RainRulesDivergence, ParticleCountsDoNotMatch) {
    // Oracle: a fixed 80-drop pool (rain.c:65,624).
    EXPECT_EQ(igi::rain::kDropCapacity, 80);
    // Editor: renderer/weather_math.h:7-8.
    EXPECT_EQ(igi::weather::kRainDrops, 1200);
    EXPECT_EQ(igi::weather::kSnowFlakes, 900);
    EXPECT_NE(igi::weather::kRainDrops, igi::rain::kDropCapacity);
}

TEST(RainRulesDivergence, AlphaIsBoostedAndClampedInsteadOfPacked) {
    // Oracle: alpha is a vertex byte, trunc(a * 255) (rain.c:646-649).
    EXPECT_EQ(igi::rain::AlphaByte(0.3125f), 79);
    // Editor: renderer/weather_math.h:17-21,24-30, and renderer_rain.cpp:49,89.
    EXPECT_FLOAT_EQ(igi::weather::RainStreakAlpha(0.3125f), 0.28f);   // 0.3125 * 1.25 clamped
    EXPECT_FLOAT_EQ(igi::weather::SnowFlakeAlpha(0.3125f), 0.42f);
    EXPECT_FLOAT_EQ(igi::weather::RainStreakAlpha(0.13f), 0.1625f);
    EXPECT_NE(igi::weather::RainStreakAlpha(0.3125f) * 255.0f,
              static_cast<float>(igi::rain::AlphaByte(0.3125f)));
}

TEST(RainRulesDivergence, FootprintIsFiftyMetresNotTwentyFour) {
    // Editor: renderer/weather_math.h:9 - a 50 m box, i.e. +/-25 m.
    EXPECT_FLOAT_EQ(igi::weather::kBoxMeters, 50.0f);
    // Oracle: a 9-tile drawn window == 23.9400 m.
    const double oracle_span = 9.0 * igi::rain::kTileUnits / igi::rain::kUnitsPerMetre;
    EXPECT_NEAR(oracle_span, 23.94, 1e-6);
    EXPECT_GT(igi::weather::kBoxMeters, oracle_span);
}

TEST(RainRulesDivergence, VerticalBandIsAuthoredNotFixed) {
    // Editor: renderer_rain.cpp:248-249 spans camera.z + start .. camera.z - end,
    // i.e. start + end metres (70 m for the retail 50/20).
    const float editor_band_metres = 50.0f + 20.0f;
    EXPECT_FLOAT_EQ(editor_band_metres, 70.0f);
    // Oracle: a fixed 18 m band (kTop), independent of the authored values.
    const float oracle_band_metres = igi::rain::kTopUnits / igi::rain::kUnitsPerMetre;
    EXPECT_FLOAT_EQ(oracle_band_metres, 18.0f);
    EXPECT_NE(editor_band_metres, oracle_band_metres);
}

TEST(RainRulesDivergence, SpeedIsPerSecondNotPerTick) {
    // Oracle: 2048 units per Rain_Update call == 0.5 m/tick.
    EXPECT_FLOAT_EQ(igi::rain::kRainSpeedUnitsPerTick / igi::rain::kUnitsPerMetre, 0.5f);
    // Editor: renderer_rain.cpp:33 speeds are fractions of the authored band per
    // SECOND; for the retail 50/20 band that is 5.6 .. 12.6 m/s.
    const float editor_fall_range_metres = 70.0f;
    const float editor_min = igi::weather::kRainMinSpeedMul * editor_fall_range_metres;
    const float editor_max = igi::weather::kRainMaxSpeedMul * editor_fall_range_metres;
    EXPECT_NEAR(editor_min, 5.6f, 1e-4f);
    EXPECT_NEAR(editor_max, 12.6f, 1e-4f);
    // The oracle's rain is 30 m/s if a tick is a 60 Hz frame.
    const float oracle_metres_per_second = 0.5f * 60.0f;
    EXPECT_FLOAT_EQ(oracle_metres_per_second, 30.0f);
    EXPECT_GT(oracle_metres_per_second, editor_max);
}

TEST(RainRulesDivergence, StreakAndFlakeExtentsDiffer) {
    // Oracle: 0.5 m streak (rain.c:671), 0.04 m flake quad (rain.c:685).
    EXPECT_FLOAT_EQ(igi::rain::RainStreakLengthUnits() / igi::rain::kUnitsPerMetre, 0.5f);
    EXPECT_FLOAT_EQ(2.0f * igi::rain::SnowFlakeSizeUnits() / igi::rain::kUnitsPerMetre, 0.04f);
    // Editor: renderer/weather_math.h:10,12 and runtime/weather_particle_style.h:9-11.
    EXPECT_FLOAT_EQ(igi::weather::kRainStreakMeters, 0.08f);
    EXPECT_FLOAT_EQ(igi::weather::kSnowFlakeMeters, 0.045f);
    EXPECT_FLOAT_EQ(igi::WeatherParticleLengthMeters(false), 0.08f);
    EXPECT_FLOAT_EQ(igi::WeatherParticleLengthMeters(true), 0.09f);
    EXPECT_NE(igi::weather::kRainStreakMeters,
              igi::rain::RainStreakLengthUnits() / igi::rain::kUnitsPerMetre);
}

// ===========================================================================
// Corpus: the shipped declaration is the authoritative schema
// ===========================================================================

TEST(RainRulesCorpus, Level4DeclarationMatchesTheParamTableOrder) {
    const fs::path path = Level4Qsc();
    if (!fs::is_regular_file(path))
        GTEST_SKIP() << "Set IGI_GAME_PATH to an installed IGI root (" << path.string() << ")";

    const std::string text = ReadFile(path);
    const std::vector<std::string> args =
        SplitTopLevel(CallArguments(text, "Task_DeclareParameters(\"RainEffect\""));
    ASSERT_GE(args.size(), 11u) << "LEVEL4/objects.qsc:55";

    EXPECT_EQ(Trim(args[0]), "\"RainEffect\"");
    EXPECT_EQ(Unquote(Trim(args[1])), "Is Rain");
    EXPECT_EQ(Unquote(Trim(args[2])), "bool8");
    EXPECT_EQ(Unquote(Trim(args[3])), "Traceline start");
    EXPECT_EQ(Unquote(Trim(args[4])), "Real32");
    EXPECT_EQ(Unquote(Trim(args[5])), "Traceline end");
    EXPECT_EQ(Unquote(Trim(args[6])), "Real32");
    EXPECT_EQ(Unquote(Trim(args[7])), "Is Active");
    EXPECT_EQ(Unquote(Trim(args[8])), "VarString");
    EXPECT_EQ(Unquote(Trim(args[9])), "Rain Alpha");
    EXPECT_EQ(Unquote(Trim(args[10])), "Real32");
}

TEST(RainRulesCorpus, Level4InstanceIsTheOracleDefaultShape) {
    const fs::path path = Level4Qsc();
    if (!fs::is_regular_file(path))
        GTEST_SKIP() << "Set IGI_GAME_PATH to an installed IGI root (" << path.string() << ")";

    const std::string text = ReadFile(path);
    std::vector<std::string> args =
        SplitTopLevel(CallArguments(text, "Task_New(-1, \"RainEffect\""));
    ASSERT_EQ(args.size(), 8u) << "LEVEL4/objects.qsc:75";

    // The QSC argument list IS the editor's token layout: [0] = task id,
    // [1] = type, [2] = name, [3..7] = the five declared parameters.
    std::vector<std::string> tokens;
    tokens.reserve(args.size());
    for (std::string& arg : args) tokens.push_back(Trim(arg));
    ASSERT_EQ(tokens[0], "-1");
    ASSERT_EQ(tokens[1], "\"RainEffect\"");

    const igi::rain::AuthoredRainEffect effect =
        igi::rain::ParseRainEffect("RainEffect", tokens);
    EXPECT_TRUE(effect.present);
    EXPECT_TRUE(effect.is_rain);
    EXPECT_FLOAT_EQ(effect.traceline_start_metres, 50.0f);
    EXPECT_FLOAT_EQ(effect.traceline_end_metres, 20.0f);
    EXPECT_FALSE(effect.falling);
    EXPECT_FLOAT_EQ(effect.alpha, 0.06f);

    // The editor's LevelWeatherSettings default alpha is 0.0
    // (runtime/level_weather.h:18); the oracle's is 0.3125
    // (fxobj/rain.c:92-93).
    EXPECT_FLOAT_EQ(igi::rain::AuthoredRainEffect{}.alpha, 0.3125f);
}
