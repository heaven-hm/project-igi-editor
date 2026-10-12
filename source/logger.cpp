#include "pch.h"
#include "logger.h"
#include "config.h"
#include <ctime>

void Logger::Init(const std::string& logFile) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (log_file_path_ != logFile) {
        if (file_.is_open()) {
            file_.close();
        }
        log_file_path_ = logFile;
    }
}

void Logger::Log(LogLevel level, const std::string& message) {
    // Check if logging is enabled at all (if Config is ready)
    if (!Config::Get().enableLogging) {
        return;
    }

    // Skip DEBUG logs if debugLogging is disabled
    if (level == LogLevel::DEBUG && !Config::Get().debugLogging) {
        return;
    }

    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S");
    std::string timestamp = ss.str();

    std::string levelStr;
    switch (level) {
        case LogLevel::DEBUG:   levelStr = "[DEBUG]"; break;
        case LogLevel::INFO:    levelStr = "[INFO]"; break;
        case LogLevel::WARNING: levelStr = "[WARN]"; break;
        case LogLevel::ERR:     levelStr = "[ERROR]"; break;
        case LogLevel::FATAL:   levelStr = "[FATAL]"; break;
    }

    std::string fullMessage = timestamp + " " + levelStr + " " + message;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.push_back({level, message, timestamp});
        if (entries_.size() > 1000) { // Keep last 1000 entries
            entries_.erase(entries_.begin());
        }

        if (!file_.is_open()) {
            const std::string path = log_file_path_.empty() ? "editor.log" : log_file_path_;
            file_.open(path, std::ios::out | std::ios::app);
            if (!file_.is_open()) {
                std::cerr << "Failed to open log file: " << path << std::endl;
            }
        }
        if (file_.is_open()) {
            file_ << fullMessage << std::endl;
        }
    }

    if (level == LogLevel::ERR || level == LogLevel::FATAL) {
        std::cerr << fullMessage << std::endl;
    } else {
        std::cout << fullMessage << std::endl;
    }
}
