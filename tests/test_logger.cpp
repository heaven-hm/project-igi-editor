#include <gtest/gtest.h>
#include "config.h"
#include "logger.h"
#include <filesystem>
#include <fstream>
#include <string>

TEST(LoggerTest, DisabledLoggingDoesNotCreateOrModifyDestination) {
    const auto root = std::filesystem::temp_directory_path() / "igi-logger-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto log = root / "editor.log";

    Config::Get().enableLogging = false;
    Config::Get().debugLogging = true;
    Logger::Get().Init(log.string());
    Logger::Get().Log(LogLevel::INFO, "disabled info");
    Logger::Get().Log(LogLevel::DEBUG, "disabled debug");
    EXPECT_FALSE(std::filesystem::exists(log));

    {
        std::ofstream(log) << "sentinel\n";
    }
    const auto before = std::filesystem::file_size(log);
    Config::Get().enableLogging = false;
    Logger::Get().Log(LogLevel::ERR, "disabled error");
    EXPECT_EQ(std::filesystem::file_size(log), before);

    Config::Get().enableLogging = true;
    Config::Get().debugLogging = false;
    Logger::Get().Log(LogLevel::DEBUG, "filtered debug");
    EXPECT_EQ(std::filesystem::file_size(log), before);
    Logger::Get().Log(LogLevel::INFO, "enabled info");
    EXPECT_GT(std::filesystem::file_size(log), before);

    {
        std::ifstream in(log);
        const std::string contents((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
        EXPECT_NE(contents.find("enabled info"), std::string::npos);
        EXPECT_EQ(contents.find("filtered debug"), std::string::npos);
    }

    Config::Get().enableLogging = false;
    Config::Get().debugLogging = false;
    Logger::Get().Init("editor.log");
    std::filesystem::remove_all(root);
}
