#include "pch.h"
#include "logger.h"
#include "config.h"
#include "runtime/log_policy.h"
#include <ctime>
#include <cstdlib>
#include <system_error>

namespace {

namespace fs = std::filesystem;

void AddCandidate(std::vector<fs::path>& candidates, const fs::path& path) {
    if (path.empty()) return;
    for (const auto& existing : candidates) {
        if (existing.lexically_normal() == path.lexically_normal()) return;
    }
    candidates.push_back(path);
}

std::vector<fs::path> LogCandidates(const std::string& requested) {
    const fs::path requested_path = requested.empty() ? fs::path("igi1ed.log")
                                                      : fs::path(requested);
    std::vector<fs::path> candidates;
    AddCandidate(candidates, requested_path);

    // Installing under Program Files or another read-only location is a
    // normal Windows deployment. Keep the executable-directory path as the
    // first choice, then use per-user writable locations.
    const char* local_app_data = std::getenv("LOCALAPPDATA");
    if (local_app_data != nullptr && *local_app_data != '\0') {
        fs::path fallback(local_app_data);
        fallback /= "Project IGI";
        fallback /= requested_path.filename().empty() ? fs::path("igi1ed.log")
                                                       : requested_path.filename();
        AddCandidate(candidates, fallback);
    }

    std::error_code temp_error;
    const fs::path temp_dir = fs::temp_directory_path(temp_error);
    if (!temp_error) {
        AddCandidate(candidates, temp_dir /
            (requested_path.filename().empty() ? fs::path("igi1ed.log")
                                                : requested_path.filename()));
    }
    return candidates;
}

}  // namespace

void Logger::Init(const std::string& logFile) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open() || !requested_log_path_.empty()) {
        return;
    }

    // Do not create a file until the first message which is permitted by the
    // loaded logging configuration. Config::Init runs after Logger::Init.
    requested_log_path_ = logFile.empty() ? "igi1ed.log" : logFile;
}

void Logger::OpenLogFileLocked() {
    if (file_.is_open() || requested_log_path_.empty()) {
        return;
    }

    const auto candidates = LogCandidates(requested_log_path_.string());
    for (const auto& candidate : candidates) {
        std::error_code directory_error;
        if (!candidate.parent_path().empty()) {
            std::filesystem::create_directories(candidate.parent_path(), directory_error);
            if (directory_error &&
                !std::filesystem::is_directory(candidate.parent_path())) {
                continue;
            }
        }

        file_.open(candidate, std::ios::out | std::ios::app);
        if (file_.is_open()) {
            log_path_ = candidate;
            unflushed_lines_ = 0;
            return;
        }
        // std::ofstream keeps its failbit after a failed open; clear it before
        // trying the next writable location.
        file_.clear();
    }

    std::cerr << "[Logger] Failed to open a log file; tried "
              << candidates.size() << " location(s)." << std::endl;
}

void Logger::Log(LogLevel level, const std::string& message) {
    if (!igi::IsLogLevelEnabled(
            Config::Get().enableLogging,
            Config::Get().logLevelThreshold,
            static_cast<int>(level))) {
        return;
    }

    LogAlways(level, message);
}

void Logger::LogAlways(LogLevel level, const std::string& message) {
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

        OpenLogFileLocked();
        if (file_.is_open()) {
            // Buffered write with periodic flush: an endl-flush per line made
            // every log call a synchronous disk stall and stuttered frames.
            file_ << fullMessage << '\n';
            static constexpr int kFlushEveryLines = 64;
            // Flush the first line immediately so startup failures leave
            // useful evidence even if the process exits before 64 messages.
            if (++unflushed_lines_ >= kFlushEveryLines || unflushed_lines_ == 1 ||
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

void Logger::Flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open()) {
        file_.flush();
        unflushed_lines_ = 0;
    }
}

bool Logger::IsOpen() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return file_.is_open();
}

std::string Logger::GetLogPath() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return log_path_.string();
}
