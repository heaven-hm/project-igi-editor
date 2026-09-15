#include <gtest/gtest.h>
#include "config.h"
#include "level/qsc_lexer.h"
#include "level/qsc_parser.h"
#include "level/qvm_compiler.h"
#include "level/qvm_decompiler.h"
#include "level/qvm_parser.h"
#include "utils.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::string ReadTextFile(const std::filesystem::path& path) {
    std::ifstream in(path);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

class ScopedEnvironmentVariable {
public:
    ScopedEnvironmentVariable(const char* name, const std::string& value) : name_(name) {
        char* oldValue = nullptr;
        size_t oldValueLength = 0;
        if (_dupenv_s(&oldValue, &oldValueLength, name) == 0 && oldValue != nullptr) {
            oldValue_ = oldValue;
            free(oldValue);
        }
        _putenv_s(name_.c_str(), value.c_str());
    }

    ~ScopedEnvironmentVariable() {
        _putenv_s(name_.c_str(), oldValue_.c_str());
    }

private:
    std::string name_;
    std::string oldValue_;
};

bool QscBooleanSetting(const std::string& source, const std::string& name) {
    const std::string enabled = name + "(TRUE)";
    const std::string disabled = name + "(FALSE)";
    if (source.find(enabled) != std::string::npos) return true;
    if (source.find(disabled) != std::string::npos) return false;
    ADD_FAILURE() << "Missing " << name << " setting";
    return false;
}

} // namespace

// ============================================================
//  Config — full suite
//
//  Config::Init() loads from embedded QVM config data.
//  All checks are against runtime-observable guarantees.
// ============================================================

// Call Init() once before every test so state is fresh.
class ConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        Config::Init();
    }
};

// ---------- level number ----------

TEST_F(ConfigTest, LevelIsPositive) {
    EXPECT_GT(Config::Get().level, 0);
}

TEST_F(ConfigTest, LevelInValidRange) {
    int lvl = Config::Get().level;
    EXPECT_GE(lvl, 1);
    EXPECT_LE(lvl, 14);
}

// ---------- rendering ----------

TEST_F(ConfigTest, RenderZNearIsPositive) {
    EXPECT_GT(Config::Get().renderZNear, 0.0f);
}

TEST_F(ConfigTest, FontSizeIsPositive) {
    EXPECT_GT(Config::Get().fontSize, 0.0f);
}

TEST_F(ConfigTest, FontColorChannelsInRange) {
    auto& d = Config::Get();
    EXPECT_GE(d.fontColorR, 0);   EXPECT_LE(d.fontColorR, 255);
    EXPECT_GE(d.fontColorG, 0);   EXPECT_LE(d.fontColorG, 255);
    EXPECT_GE(d.fontColorB, 0);   EXPECT_LE(d.fontColorB, 255);
}

TEST_F(ConfigTest, SystemFontSizeIsWithinSupportedUiRange) {
    // The UI scales continuously from 8 through 32; the GLUT fallback picks
    // the nearest available 10, 12, or 18 point bitmap font.
    int sz = Config::Get().systemFontSize;
    EXPECT_GE(sz, 8);
    EXPECT_LE(sz, 32)
        << "unexpected systemFontSize: " << sz;
}

// ---------- singleton ----------

TEST_F(ConfigTest, GetReturnsSameInstance) {
    auto& a = Config::Get();
    auto& b = Config::Get();
    EXPECT_EQ(&a, &b);
}

TEST_F(ConfigTest, MultipleInitCallsAreSafe) {
    // Init can be called repeatedly without crashing
    Config::Init();
    Config::Init();
    EXPECT_GT(Config::Get().level, 0);
}

// ---------- logging configuration ----------

TEST_F(ConfigTest, LoggingSettingsFromActiveQscAreCompiledToQvm) {
    namespace fs = std::filesystem;
    const fs::path qedDir = fs::path(Utils::GetIGIRootPath()) / "editor" / "qed";
    const fs::path qscPath = qedDir / "qedconfig.qsc";
    const fs::path qvmPath = qedDir / "qedconfig.qvm";

    ASSERT_TRUE(fs::is_regular_file(qscPath));
    ASSERT_TRUE(fs::is_regular_file(qvmPath));

    const std::string qscSource = ReadTextFile(qscPath);
    const bool qscLoggingEnabled = QscBooleanSetting(qscSource, "QEDLogs");
    const bool qscDebugEnabled = QscBooleanSetting(qscSource, "QEDDebug");

    const QVMFile qvm = QVM_Parse(qvmPath.string());
    ASSERT_TRUE(qvm.valid) << qvm.error;
    const std::string qvmSource = QVM_DecompileToString(qvm);
    EXPECT_NE(qvmSource.find(qscLoggingEnabled ? "QEDLogs(TRUE)" : "QEDLogs(FALSE)"), std::string::npos);
    EXPECT_NE(qvmSource.find(qscDebugEnabled ? "QEDDebug(TRUE)" : "QEDDebug(FALSE)"), std::string::npos);

    EXPECT_EQ(Config::Get().enableLogging, qscLoggingEnabled);
    EXPECT_EQ(Config::Get().debugLogging, qscDebugEnabled);
}

TEST(ConfigQvmTest, PreservesDisabledLoggingAndEnabledDebug) {
    namespace fs = std::filesystem;
    constexpr const char* source = "QEDLogs(FALSE);\nQEDDebug(TRUE);\n";

    const qsc::LexResult lexed = qsc::Lex(source);
    ASSERT_TRUE(lexed.ok) << lexed.error;
    const qsc::ParseResult parsed = qsc::Parse(lexed.tokens);
    ASSERT_TRUE(parsed.ok) << parsed.error;

    const fs::path qvmPath = fs::temp_directory_path() /
        ("igi1ed-logging-config-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
         ".qvm");
    std::string compileError;
    ASSERT_TRUE(qvm::CompileToFile(*parsed.program, qvmPath.string(), &compileError))
        << compileError;

    const QVMFile qvm = QVM_Parse(qvmPath.string());
    std::error_code ec;
    fs::remove(qvmPath, ec);
    ASSERT_TRUE(qvm.valid) << qvm.error;
    const std::string decompiled = QVM_DecompileToString(qvm);
    EXPECT_NE(decompiled.find("QEDLogs(FALSE)"), std::string::npos);
    EXPECT_NE(decompiled.find("QEDDebug(TRUE)"), std::string::npos);
}

TEST(ConfigQvmTest, UsesGameQscAndRebuildsItsQvm) {
    namespace fs = std::filesystem;
    const fs::path gameRoot = fs::temp_directory_path() /
        ("igi1ed-game-config-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path qedDir = gameRoot / "editor" / "qed";
    const fs::path qscPath = qedDir / "qedconfig.qsc";
    const fs::path qvmPath = qedDir / "qedconfig.qvm";
    std::error_code ec;
    fs::remove_all(gameRoot, ec);
    fs::create_directories(qedDir);
    {
        std::ofstream qsc(qscPath);
        ASSERT_TRUE(qsc.is_open());
        qsc << "QEDLogs(FALSE);\nQEDDebug(FALSE);\nQEDLogLevel(3);\n";
    }

    ScopedEnvironmentVariable gamePath("IGI_GAME_PATH", gameRoot.string());
    Config::Init();

    EXPECT_FALSE(Config::Get().enableLogging);
    EXPECT_FALSE(Config::Get().debugLogging);
    EXPECT_EQ(Config::Get().logLevelThreshold, 3);
    ASSERT_TRUE(fs::is_regular_file(qvmPath));
    const QVMFile qvm = QVM_Parse(qvmPath.string());
    ASSERT_TRUE(qvm.valid) << qvm.error;
    const std::string decompiled = QVM_DecompileToString(qvm);
    EXPECT_NE(decompiled.find("QEDLogs(FALSE)"), std::string::npos);
    EXPECT_NE(decompiled.find("QEDDebug(FALSE)"), std::string::npos);
    EXPECT_NE(decompiled.find("QEDLogLevel(3)"), std::string::npos);

    fs::remove_all(gameRoot, ec);
}

TEST(ConfigQvmTest, SavesAndReloadsPauseMenuLogEnableAndSeverity) {
    namespace fs = std::filesystem;
    const fs::path gameRoot = fs::temp_directory_path() /
        ("igi1ed-log-severity-config-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path qedDir = gameRoot / "editor" / "qed";
    std::error_code ec;
    fs::create_directories(qedDir, ec);
    ASSERT_FALSE(ec);
    {
        std::ofstream qsc(qedDir / "qedconfig.qsc");
        ASSERT_TRUE(qsc.is_open());
        qsc << "QEDLogs(TRUE);\nQEDDebug(FALSE);\nQEDLogLevel(2);\n";
    }

    {
        ScopedEnvironmentVariable gamePath("IGI_GAME_PATH", gameRoot.string());
        Config::Init();
        EXPECT_TRUE(Config::Get().enableLogging);
        EXPECT_EQ(Config::Get().logLevelThreshold, 2);

        Config::Get().enableLogging = false;
        Config::Get().logLevelThreshold = 3;
        Config::Save();

        Config::Init();
        EXPECT_FALSE(Config::Get().enableLogging);
        EXPECT_EQ(Config::Get().logLevelThreshold, 3);
    }

    const QVMFile qvm = QVM_Parse((qedDir / "qedconfig.qvm").string());
    ASSERT_TRUE(qvm.valid) << qvm.error;
    const std::string decompiled = QVM_DecompileToString(qvm);
    EXPECT_NE(decompiled.find("QEDLogs(FALSE)"), std::string::npos);
    EXPECT_NE(decompiled.find("QEDLogLevel(3)"), std::string::npos);
    fs::remove_all(gameRoot, ec);
    Config::Init();
}

TEST(ConfigQvmTest, SavesAndReloadsWeatherEnablement) {
    namespace fs = std::filesystem;
    const fs::path gameRoot = fs::temp_directory_path() /
        ("igi1ed-weather-config-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path qedDir = gameRoot / "editor" / "qed";
    std::error_code ec;
    fs::create_directories(qedDir, ec);
    ASSERT_FALSE(ec);
    {
        std::ofstream qsc(qedDir / "qedconfig.qsc");
        ASSERT_TRUE(qsc.is_open());
        qsc << "QEDLogs(FALSE);\nQEDWeatherEnabled(FALSE);\n";
    }

    {
        ScopedEnvironmentVariable gamePath("IGI_GAME_PATH", gameRoot.string());
        Config::Init();
        EXPECT_FALSE(Config::Get().weatherEnabled);

        Config::Get().weatherEnabled = true;
        Config::Save();
        Config::Init();
        EXPECT_TRUE(Config::Get().weatherEnabled);
    }

    const std::string qscSource = ReadTextFile(qedDir / "qedconfig.qsc");
    EXPECT_TRUE(QscBooleanSetting(qscSource, "QEDWeatherEnabled"));
    fs::remove_all(gameRoot, ec);
    Config::Init();
}

TEST(ConfigQvmTest, AcceptsUtf8BomInAuthoritativeGameQsc) {
    namespace fs = std::filesystem;
    const fs::path gameRoot = fs::temp_directory_path() /
        ("igi1ed-bom-config-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path qedDir = gameRoot / "editor" / "qed";
    const fs::path qscPath = qedDir / "qedconfig.qsc";
    const fs::path qvmPath = qedDir / "qedconfig.qvm";
    std::error_code ec;
    fs::create_directories(qedDir, ec);
    ASSERT_FALSE(ec);

    {
        std::ofstream qsc(qscPath, std::ios::binary);
        ASSERT_TRUE(qsc.is_open());
        qsc << "\xEF\xBB\xBFQEDLogs(TRUE);\nQEDDebug(FALSE);\n";
    }

    {
        ScopedEnvironmentVariable gamePath("IGI_GAME_PATH", gameRoot.string());
        Config::Init();
        EXPECT_TRUE(Config::Get().enableLogging);
        EXPECT_FALSE(Config::Get().debugLogging);
    }

    ASSERT_TRUE(fs::is_regular_file(qvmPath));
    EXPECT_TRUE(QVM_Parse(qvmPath.string()).valid);
    fs::remove_all(gameRoot, ec);
    Config::Init();
}

TEST(ConfigQvmTest, InvalidQscFailsClosedInsteadOfUsingStaleQvm) {
    namespace fs = std::filesystem;
    const fs::path gameRoot = fs::temp_directory_path() /
        ("igi1ed-invalid-config-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path qedDir = gameRoot / "editor" / "qed";
    const fs::path qscPath = qedDir / "qedconfig.qsc";
    const fs::path qvmPath = qedDir / "qedconfig.qvm";
    std::error_code ec;
    fs::create_directories(qedDir, ec);
    ASSERT_FALSE(ec);

    {
        std::ofstream qsc(qscPath);
        ASSERT_TRUE(qsc.is_open());
        qsc << "QEDLogs(FALSE);\nQEDDebug(FALSE);\nQEDBroken(\n";
    }

    constexpr const char* staleSource = "QEDLogs(TRUE);\nQEDDebug(TRUE);\n";
    const qsc::LexResult lexed = qsc::Lex(staleSource);
    ASSERT_TRUE(lexed.ok) << lexed.error;
    const qsc::ParseResult parsed = qsc::Parse(lexed.tokens);
    ASSERT_TRUE(parsed.ok) << parsed.error;
    std::string compileError;
    ASSERT_TRUE(qvm::CompileToFile(*parsed.program, qvmPath.string(), &compileError))
        << compileError;

    {
        ScopedEnvironmentVariable gamePath("IGI_GAME_PATH", gameRoot.string());
        Config::Init();
        EXPECT_FALSE(Config::Get().enableLogging);
        EXPECT_FALSE(Config::Get().debugLogging);
    }

    fs::remove_all(gameRoot, ec);
    Config::Init();
}

TEST(ConfigQvmTest, NonConfigQvmCannotOverrideLoggingSettings) {
    namespace fs = std::filesystem;
    const fs::path gameRoot = fs::temp_directory_path() /
        ("igi1ed-authoritative-config-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path qedDir = gameRoot / "editor" / "qed";
    std::error_code ec;
    fs::create_directories(qedDir, ec);
    ASSERT_FALSE(ec);

    {
        std::ofstream config(qedDir / "qedconfig.qsc");
        ASSERT_TRUE(config.is_open());
        config << "QEDLogs(FALSE);\nQEDDebug(FALSE);\n";
    }
    {
        std::ofstream other(qedDir / "qedconfigtask.qsc");
        ASSERT_TRUE(other.is_open());
        other << "QEDLogs(TRUE);\nQEDDebug(TRUE);\n";
    }

    {
        ScopedEnvironmentVariable gamePath("IGI_GAME_PATH", gameRoot.string());
        Config::Init();
        EXPECT_FALSE(Config::Get().enableLogging);
        EXPECT_FALSE(Config::Get().debugLogging);
    }

    fs::remove_all(gameRoot, ec);
    Config::Init();
}

TEST(ConfigQvmTest, SaveRegeneratesSiblingQvmFromUpdatedQsc) {
    namespace fs = std::filesystem;
    const fs::path gameRoot = fs::temp_directory_path() /
        ("igi1ed-save-config-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path qedDir = gameRoot / "editor" / "qed";
    std::error_code ec;
    fs::create_directories(qedDir, ec);
    ASSERT_FALSE(ec);
    {
        std::ofstream qsc(qedDir / "qedconfig.qsc");
        ASSERT_TRUE(qsc.is_open());
        qsc << "QEDLogs(FALSE);\nQEDDebug(FALSE);\nQEDSystemFontSize(14);\n";
    }

    {
        ScopedEnvironmentVariable gamePath("IGI_GAME_PATH", gameRoot.string());
        Config::Init();
        Config::Get().enableLogging = false;
        Config::Get().debugLogging = false;
        Config::Get().systemFontSize = 18;
        Config::Save();

        const fs::path qvmPath = qedDir / "qedconfig.qvm";
        ASSERT_TRUE(fs::is_regular_file(qvmPath));
        const QVMFile qvm = QVM_Parse(qvmPath.string());
        ASSERT_TRUE(qvm.valid) << qvm.error;
        const std::string decompiled = QVM_DecompileToString(qvm);
        EXPECT_NE(decompiled.find("QEDLogs(FALSE)"), std::string::npos);
        EXPECT_NE(decompiled.find("QEDSystemFontSize(18)"), std::string::npos);
    }
    fs::remove_all(gameRoot, ec);
    Config::Init();
}

// ---------- keybindings structure ----------

TEST_F(ConfigTest, KeybindingsHaveNonZeroVkCodes) {
    // At least one keybinding should have a non-zero VK code,
    // confirming keybinding data was loaded.
    auto& d = Config::Get();
    bool any_nonzero =
        d.keySave.vkCode   != 0 ||
        d.keyQuit.vkCode   != 0 ||
        d.keyUndo.vkCode   != 0 ||
        d.keyRedo.vkCode   != 0;
    EXPECT_TRUE(any_nonzero) << "all keybinding vkCodes are 0 — config may not have loaded";
}

// ---------- interpolation ----------

TEST_F(ConfigTest, InterpolationIsNonNegative) {
    EXPECT_GE(Config::Get().interpolation, 0);
}
