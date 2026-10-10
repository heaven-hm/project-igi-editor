#include "pch.h"
#include "logger.h"
#include "config.h"
#include <ctime>

void Logger::Init(const std::string& logFile) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open()) {
        return;
    }
    file_.open(logFile, std::ios::out | std::ios::app);
    if (!file_.is_open()) {
        std::cerr << "Failed to open log file: " << logFile << std::endl;
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

        if (file_.is_open()) {
            // Buffered write with periodic flush: an endl-flush per line made
            // every log call a synchronous disk stall and stuttered frames.
            file_ << fullMessage << '\n';
            static constexpr int kFlushEveryLines = 64;
            if (++unflushed_lines_ >= kFlushEveryLines ||
                level >= LogLevel::ERR) {
                file_.flush();
                unflushed_lines_ = 0;
            }
        }
    }

    if (level == LogLevel::ERR || level == LogLevel::FATAL) {
        std::cerr << fullMessage << std::endl;
    }
}
