#include <gtest/gtest.h>

#include "config.h"
#include "logger.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

TEST(LoggerTest, DoesNotCreateFileUntilLoggingIsEnabled) {
    namespace fs = std::filesystem;
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path path = fs::temp_directory_path() /
        ("igi1ed-logger-test-" + std::to_string(unique) + ".log");
    std::error_code ec;
    fs::remove(path, ec);

    Config::Get().enableLogging = false;
    Config::Get().debugLogging = false;
    Config::Get().logLevelThreshold = 1;
    Logger& logger = Logger::Get();
    logger.Init(path.string());
    EXPECT_FALSE(fs::exists(path));
    EXPECT_FALSE(logger.IsOpen());

    logger.Log(LogLevel::INFO, "logger disabled message");
    EXPECT_FALSE(fs::exists(path));

    Config::Get().enableLogging = true;
    logger.Log(LogLevel::INFO, "logger enabled message");
    ASSERT_TRUE(logger.IsOpen());
    ASSERT_EQ(fs::path(logger.GetLogPath()).lexically_normal(), path.lexically_normal());
    logger.Log(LogLevel::DEBUG, "logger debug disabled message");
    Config::Get().logLevelThreshold = 3;
    logger.Log(LogLevel::WARNING, "logger warning filtered message");
    logger.Log(LogLevel::ERR, "logger error enabled message");
    Config::Get().enableLogging = false;
    logger.Log(LogLevel::INFO, "logger disabled after opening message");
    Config::Get().enableLogging = true;
    Config::Get().debugLogging = true;
    Config::Get().logLevelThreshold = 0;
    logger.Log(LogLevel::DEBUG, "logger debug enabled message");
    logger.Flush();

    ASSERT_TRUE(fs::exists(path));
    std::ifstream in(path);
    const std::string contents((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
    EXPECT_NE(contents.find("logger enabled message"), std::string::npos);
    EXPECT_EQ(contents.find("logger debug disabled message"), std::string::npos);
    EXPECT_EQ(contents.find("logger disabled message"), std::string::npos);
    EXPECT_EQ(contents.find("logger disabled after opening message"), std::string::npos);
    EXPECT_EQ(contents.find("logger warning filtered message"), std::string::npos);
    EXPECT_NE(contents.find("logger error enabled message"), std::string::npos);
    EXPECT_NE(contents.find("logger debug enabled message"), std::string::npos);
}
