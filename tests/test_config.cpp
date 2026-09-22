#include <gtest/gtest.h>
#include "config.h"
#include "logger.h"
#include <filesystem>
#include <fstream>
#include <string>

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

TEST_F(ConfigTest, SystemFontSizeIsValidGlutSize) {
    // GLUT system font supports 10, 12, or 18 pt
    int sz = Config::Get().systemFontSize;
    EXPECT_TRUE(sz == 10 || sz == 12 || sz == 18)
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

TEST(ConfigLoggingTest, UnsupportedSystemFontSizeUsesNearestGlutSize) {
    const auto root = std::filesystem::temp_directory_path() / "igi-config-font-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    std::ofstream(root / "qedconfig.qsc") << "QEDSystemFontSize(13);\n";
    std::ofstream(root / "qedkeybindings.qsc") << "// no bindings\n";

    ASSERT_TRUE(Config::InitFromDirectory(root.string()));
    EXPECT_EQ(Config::Get().systemFontSize, 12);
    std::filesystem::remove_all(root);
}

TEST(ConfigLoggingTest, QscControlsLoggingAndCompilesMatchingQvm) {
    const auto root = std::filesystem::temp_directory_path() / "igi-config-logging-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto config = root / "qedconfig.qsc";
    const auto keybindings = root / "qedkeybindings.qsc";
    const auto log = root / "editor.log";

    std::ofstream(keybindings) << "// no bindings\n";
    std::ofstream(config)
        << "QEDLogs(FALSE);\n"
        << "QEDDebug(FALSE);\n"
        << "QEDSaveConfigOnExit(TRUE);\n";

    Logger::Get().Init(log.string());
    ASSERT_TRUE(Config::InitFromDirectory(root.string()));
    EXPECT_FALSE(Config::Get().enableLogging);
    EXPECT_FALSE(Config::Get().debugLogging);
    EXPECT_TRUE(Config::Get().saveConfigOnExit);
    EXPECT_TRUE(std::filesystem::exists(root / "qedconfig.qvm"));
    EXPECT_FALSE(std::filesystem::exists(log));

    std::ofstream(config, std::ios::trunc)
        << "QEDLogs(TRUE);\n"
        << "QEDDebug(TRUE);\n"
        << "QEDSaveConfigOnExit(FALSE);\n";
    ASSERT_TRUE(Config::InitFromDirectory(root.string()));
    EXPECT_TRUE(Config::Get().enableLogging);
    EXPECT_TRUE(Config::Get().debugLogging);
    EXPECT_FALSE(Config::Get().saveConfigOnExit);
    EXPECT_TRUE(std::filesystem::exists(root / "qedconfig.qvm"));

    Config::Get().enableLogging = false;
    Config::Get().debugLogging = false;
    Logger::Get().Init("editor.log");
    std::filesystem::remove_all(root);
}

TEST(ConfigLoggingTest, IgnoresUnrelatedQvmAndFailsClosedForInvalidConfig) {
    const auto root = std::filesystem::temp_directory_path() / "igi-config-authority-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto config = root / "qedconfig.qsc";
    const auto unrelated = root / "unrelated.qsc";
    std::ofstream(root / "qedkeybindings.qsc") << "// no bindings\n";
    std::ofstream(config) << "QEDLogs(FALSE);\n";
    std::ofstream(unrelated) << "QEDLogs(TRUE);\n";

    ASSERT_TRUE(Config::InitFromDirectory(root.string()));
    EXPECT_FALSE(Config::Get().enableLogging);
    EXPECT_TRUE(std::filesystem::exists(root / "unrelated.qvm"));

    std::ofstream(config, std::ios::trunc) << "QEDLogs(TRUE;\n";
    EXPECT_FALSE(Config::InitFromDirectory(root.string()));
    EXPECT_FALSE(Config::Get().enableLogging);

    Config::Get().debugLogging = false;
    Logger::Get().Init("editor.log");
    std::filesystem::remove_all(root);
}

TEST(ConfigAssetTest, ShipsQscAndQvmForRuntimeConfiguration) {
    namespace fs = std::filesystem;
    const fs::path assets(IGI_EDITOR_QED_ASSETS_DIR);
    ASSERT_TRUE(fs::exists(assets / "qedconfig.qsc"));
    ASSERT_TRUE(fs::exists(assets / "qedconfig.qvm"));
    ASSERT_TRUE(fs::exists(assets / "qedkeybindings.qsc"));
    ASSERT_TRUE(fs::exists(assets / "qedkeybindings.qvm"));

    const fs::path root = fs::temp_directory_path() / "igi-config-asset-test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    ASSERT_FALSE(ec);
    fs::copy_file(assets / "qedconfig.qsc", root / "qedconfig.qsc");
    fs::copy_file(assets / "qedkeybindings.qsc", root / "qedkeybindings.qsc");

    ASSERT_TRUE(Config::InitFromDirectory(root.string()));
    EXPECT_TRUE(fs::exists(root / "qedconfig.qvm"));
    EXPECT_TRUE(fs::exists(root / "qedkeybindings.qvm"));

    Config::Get().enableLogging = false;
    Config::Get().debugLogging = false;
    Logger::Get().Init("editor.log");
    fs::remove_all(root, ec);
}
