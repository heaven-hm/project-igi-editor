#pragma once

#include <algorithm>

namespace igi {

constexpr int kLogLevelDebug = 0;
constexpr int kLogLevelInfo = 1;
constexpr int kLogLevelWarning = 2;
constexpr int kLogLevelError = 3;
constexpr int kLogLevelFatal = 4;

constexpr int ClampLogLevel(int level) noexcept {
    return std::max(kLogLevelDebug, std::min(kLogLevelFatal, level));
}

constexpr bool IsLogLevelEnabled(bool loggingEnabled, int minimumLevel,
                                 int messageLevel) noexcept {
    return loggingEnabled && messageLevel >= ClampLogLevel(minimumLevel);
}

constexpr const char* LogLevelLabel(int level) noexcept {
    switch (ClampLogLevel(level)) {
    case kLogLevelDebug: return "DEBUG";
    case kLogLevelInfo: return "INFO";
    case kLogLevelWarning: return "WARNING";
    case kLogLevelError: return "ERROR";
    default: return "FATAL";
    }
}

} // namespace igi
