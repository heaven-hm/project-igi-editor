#include <gtest/gtest.h>

#include "../source/runtime/level_weather.h"
#include "../source/runtime/weather_visibility.h"
#include "../source/runtime/weather_particle_style.h"
#include "../source/level/level_common.h"
#include "../source/level/level_objects.h"
#include "../source/level/qsc_lexer.h"
#include "../source/level/qsc_parser.h"
#include "../source/level/qvm_decompiler.h"
#include "../source/level/qvm_parser.h"

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <sstream>
#include <chrono>

namespace {

igi::WeatherEffectObject RainEffect(const char* is_rain, const char* is_active,
                                    const char* alpha = "0.06") {
    return {"RainEffect",
            {"-1", "RainEffect", "", is_rain, "50.0", "20.0", is_active, alpha}};
}

std::optional<std::string> LiteralToken(const qsc::Node& node) {
    switch (node.kind) {
    case qsc::NodeKind::Unary: {
        if (node.children.size() != 1 ||
            (node.s_val != "-" && node.s_val != "+")) {
            return std::nullopt;
        }
        const std::optional<std::string> operand = LiteralToken(*node.children[0]);
        if (!operand.has_value()) {
            return std::nullopt;
        }
        return node.s_val + *operand;
    }
    case qsc::NodeKind::IntLit:
        return std::to_string(node.i_val);
    case qsc::NodeKind::FloatLit:
        return std::to_string(node.f_val);
    case qsc::NodeKind::BoolLit:
        return node.b_val ? "TRUE" : "FALSE";
    case qsc::NodeKind::StringLit:
        return '"' + node.s_val + '"';
    default:
        return std::nullopt;
    }
}

void CollectWeatherObjects(const qsc::Node& node,
                           std::vector<igi::WeatherEffectObject>& weatherObjects) {
    if (node.kind == qsc::NodeKind::Call && node.s_val == "Task_New" &&
        node.children.size() >= 8 &&
        node.children[1]->kind == qsc::NodeKind::StringLit &&
        node.children[1]->s_val == "RainEffect") {
        std::vector<std::string> arguments;
        arguments.reserve(node.children.size());
        for (const auto& child : node.children) {
            const std::optional<std::string> token = LiteralToken(*child);
            if (!token.has_value()) {
                return;
            }
            arguments.push_back(*token);
        }
        weatherObjects.push_back({"RainEffect", std::move(arguments)});
    }

    for (const auto& child : node.children) {
        CollectWeatherObjects(*child, weatherObjects);
    }
}

std::string DescribeWeatherObjects(
    const std::vector<igi::WeatherEffectObject>& weatherObjects) {
    std::ostringstream description;
    for (const auto& object : weatherObjects) {
        description << object.type << '(';
        for (size_t index = 0; index < object.arg_tokens.size(); ++index) {
            if (index != 0) description << ", ";
            description << '[' << object.arg_tokens[index] << ']';
        }
        description << ')';
    }
    return description.str();
}

TEST(LevelWeatherTest, VanillaLevelsUseOnlyTheirAuthoredWeatherObject) {
    // Values are from the decompiled location0 level objects.qvm files.
    const std::vector<std::vector<igi::WeatherEffectObject>> levels = {
        {}, {}, {RainEffect("TRUE", "1", "0.13")},
        {RainEffect("TRUE", "FALSE\n")}, {}, {},
        {RainEffect("FALSE", "1")}, {RainEffect("TRUE", "FALSE\n")},
        {RainEffect("TRUE", "TRUE\n", "0.15")},
        {RainEffect("TRUE", "TRUE", "0.135")},
        {RainEffect("TRUE", "FALSE\n")},
        {RainEffect("FALSE", "1", "0.3125")}, {}, {},
    };
    const std::vector<bool> expected_active = {
        false, false, true, false, false, false, true,
        false, true, true, false, true, false, false,
    };
    const std::vector<bool> expected_snow = {
        false, false, false, false, false, false, true,
        false, false, false, false, true, false, false,
    };

    ASSERT_EQ(levels.size(), 14u);
    ASSERT_EQ(expected_active.size(), levels.size());
    ASSERT_EQ(expected_snow.size(), levels.size());
    for (size_t i = 0; i < levels.size(); ++i) {
        const igi::LevelWeatherSettings weather = igi::ResolveLevelWeather(levels[i]);
        EXPECT_EQ(weather.active, expected_active[i]) << "level " << (i + 1);
        EXPECT_EQ(weather.is_snow, expected_snow[i]) << "level " << (i + 1);
    }
}

TEST(LevelWeatherTest, InstalledQvmsPreserveAuthoredWeatherForEveryLevel) {
    const char* corpusRoot = std::getenv("IGI_WEATHER_CORPUS");
    if (corpusRoot == nullptr || *corpusRoot == '\0') {
        GTEST_SKIP() << "Set IGI_WEATHER_CORPUS to an installed IGI root.";
    }

    namespace fs = std::filesystem;
    const fs::path root(corpusRoot);
    const std::vector<bool> expectedActive = {
        false, false, true, false, false, false, true,
        false, true, true, false, true, false, false,
    };
    const std::vector<bool> expectedSnow = {
        false, false, false, false, false, false, true,
        false, false, false, false, true, false, false,
    };
    for (int level = 1; level <= 14; ++level) {
        const fs::path qvmPath = root / "missions" / "location0" /
            ("level" + std::to_string(level)) / "objects.qvm";
        ASSERT_TRUE(fs::is_regular_file(qvmPath)) << qvmPath.string();
        const QVMFile qvm = QVM_Parse(qvmPath.string());
        ASSERT_TRUE(qvm.valid) << qvmPath.string() << ": " << qvm.error;

        std::vector<igi::WeatherEffectObject> weatherObjects;
        const qsc::LexResult lexed = qsc::Lex(QVM_DecompileToString(qvm));
        ASSERT_TRUE(lexed.ok) << qvmPath.string() << ": " << lexed.error;
        const qsc::ParseResult parsed = qsc::Parse(lexed.tokens);
        ASSERT_TRUE(parsed.ok) << qvmPath.string() << ": " << parsed.error;
        ASSERT_NE(parsed.program, nullptr) << qvmPath.string();
        CollectWeatherObjects(*parsed.program, weatherObjects);
        const igi::LevelWeatherSettings weather =
            igi::ResolveLevelWeather(weatherObjects);
        const std::string parsedObjects = DescribeWeatherObjects(weatherObjects);
        EXPECT_EQ(weather.active, expectedActive[level - 1]) << "level " << level
            << " parsed weather: " << parsedObjects;
        EXPECT_EQ(weather.is_snow, expectedSnow[level - 1]) << "level " << level
            << " parsed weather: " << parsedObjects;
    }
}

TEST(LevelWeatherTest, InstalledQvmsDriveLegacyEditorWeatherForEveryLevel) {
    const char* corpusRoot = std::getenv("IGI_WEATHER_CORPUS");
    if (corpusRoot == nullptr || *corpusRoot == '\0') {
        GTEST_SKIP() << "Set IGI_WEATHER_CORPUS to an installed IGI root.";
    }

    namespace fs = std::filesystem;
    const fs::path root(corpusRoot);
    const std::vector<bool> expectedActive = {
        false, false, true, false, false, false, true,
        false, true, true, false, true, false, false,
    };
    const std::vector<bool> expectedSnow = {
        false, false, false, false, false, false, true,
        false, false, false, false, true, false, false,
    };
    const fs::path scratch = fs::temp_directory_path() /
        ("igi1ed-legacy-weather-qvm-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code error;
    fs::create_directories(scratch, error);
    ASSERT_FALSE(error) << error.message();

    for (int level = 1; level <= 14; ++level) {
        const fs::path qvmPath = root / "missions" / "location0" /
            ("level" + std::to_string(level)) / "objects.qvm";
        ASSERT_TRUE(fs::is_regular_file(qvmPath)) << qvmPath.string();
        const QVMFile qvm = QVM_Parse(qvmPath.string());
        ASSERT_TRUE(qvm.valid) << qvmPath.string() << ": " << qvm.error;

        const fs::path qscPath = scratch / ("level" + std::to_string(level) + ".qsc");
        ASSERT_TRUE(QVM_Decompile(qvm, qscPath.string())) << qvmPath.string();
        // QSC contains fixed parser pools, so match the editor's heap allocation
        // rather than exhausting the test thread's stack.
        const auto qsc = std::make_unique<QSC>();
        qsc->Load(qscPath.string().c_str());
        ASSERT_FALSE(qsc->HadOverflow()) << qscPath.string();

        LevelObjects objects;
        objects.Load(nullptr, qsc.get());
        std::vector<igi::WeatherEffectObject> weatherObjects;
        for (const auto& object : objects.GetObjects()) {
            if (object.type == "RainEffect") {
                weatherObjects.push_back({object.type, object.argTokens});
            }
        }
        const igi::LevelWeatherSettings weather =
            igi::ResolveLevelWeather(weatherObjects);
        const std::string parsedObjects = DescribeWeatherObjects(weatherObjects);
        EXPECT_EQ(weather.active, expectedActive[level - 1]) << "level " << level
            << " parsed weather: " << parsedObjects;
        EXPECT_EQ(weather.is_snow, expectedSnow[level - 1]) << "level " << level
            << " parsed weather: " << parsedObjects;
    }
    fs::remove_all(scratch, error);
}

TEST(LevelWeatherTest, FalseVarStringDoesNotEnableWeather) {
    const igi::LevelWeatherSettings weather =
        igi::ResolveLevelWeather({RainEffect("TRUE", "FALSE\n")});
    EXPECT_FALSE(weather.active);
    EXPECT_FALSE(weather.is_snow);
    EXPECT_FLOAT_EQ(weather.alpha, 0.0f);
}

TEST(LevelWeatherTest, EscapedVarStringNewlineEnablesWeather) {
    const igi::LevelWeatherSettings weather = igi::ResolveLevelWeather(
        {RainEffect("TRUE", "\"TRUE\\n\"", "0.15")});

    EXPECT_TRUE(weather.active);
    EXPECT_FALSE(weather.is_snow);
    EXPECT_FLOAT_EQ(weather.alpha, 0.15f);
}

TEST(LevelWeatherTest, ActiveQuotedLevelNineRainIsNotOverriddenByVisualBuildingBounds) {
    const igi::LevelWeatherSettings weather = igi::ResolveLevelWeather(
        {RainEffect("TRUE", "\"TRUE\\n\"", "0.15")});

    ASSERT_TRUE(weather.active);
    EXPECT_TRUE(igi::ShouldRenderAuthoredWeather(weather.active));
    EXPECT_TRUE(igi::ShouldDrawWeatherForFrame(
        weather.active, true /* rain renderer initialized */));
}

TEST(LevelWeatherTest, ActiveAuthoredWeatherRendersWhenRendererIsReady) {
    const igi::LevelWeatherSettings weather = igi::ResolveLevelWeather(
        {RainEffect("FALSE", "TRUE", "0.06")});

    ASSERT_TRUE(weather.active);
    EXPECT_TRUE(igi::ShouldRenderAuthoredWeather(weather.active));
    EXPECT_TRUE(igi::ShouldDrawWeatherForFrame(
        weather.active, true /* rain renderer initialized */));
}

TEST(LevelWeatherTest, SnowUsesSmallFlakeScaleRatherThanRainStreakLength) {
    EXPECT_LT(igi::WeatherParticleLengthMeters(true),
              igi::WeatherParticleLengthMeters(false));
}

TEST(LevelWeatherTest, ActiveAuthoredWeatherDoesNotDependOnObjectRenderFlags) {
    for (const auto& level : std::vector<std::vector<igi::WeatherEffectObject>>{
             {RainEffect("TRUE", "TRUE", "0.13")},
             {RainEffect("FALSE", "TRUE", "0.06")},
             {RainEffect("TRUE", "\"TRUE\n\"", "0.15")},
             {RainEffect("TRUE", "TRUE", "0.135")},
             {RainEffect("FALSE", "TRUE", "0.3125")},
         }) {
        const igi::LevelWeatherSettings weather = igi::ResolveLevelWeather(level);
        ASSERT_TRUE(weather.active);
        EXPECT_TRUE(igi::ShouldDrawWeatherForFrame(
            weather.active, true /* rain renderer initialized */));
    }
    EXPECT_FALSE(igi::ShouldDrawWeatherForFrame(false, true));
    EXPECT_FALSE(igi::ShouldDrawWeatherForFrame(true, false));
}

TEST(LevelWeatherTest, InvalidRainEffectCannotEnableWeather) {
    const igi::WeatherEffectObject malformed = {
        "RainEffect",
        {"-1", "RainEffect", "", "MAYBE", "50.0", "20.0", "TRUE", "0.06"},
    };
    EXPECT_FALSE(igi::ResolveLevelWeather({malformed}).active);
}

TEST(LevelWeatherTest, MissingRainEffectResetsToDisabledDefaults) {
    const igi::LevelWeatherSettings weather = igi::ResolveLevelWeather({});
    EXPECT_FALSE(weather.active);
    EXPECT_FALSE(weather.is_snow);
    EXPECT_FLOAT_EQ(weather.start_meters, 0.0f);
    EXPECT_FLOAT_EQ(weather.end_meters, 0.0f);
    EXPECT_FLOAT_EQ(weather.alpha, 0.0f);
}

} // namespace
