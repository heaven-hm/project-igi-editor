#pragma once

#include <cstdint>

namespace igi {

constexpr int kPauseMenuLevelRow = 4;

constexpr bool PauseMenuLevelInputFocused(bool pauseVisible,
                                           int activeInput) noexcept {
    return pauseVisible && activeInput == kPauseMenuLevelRow;
}

struct PauseMenuFontLoadState {
    bool attempted = false;
    bool loaded = false;
    uint64_t source_stamp = 0;

    constexpr bool ShouldAttempt(uint64_t current_source_stamp) const noexcept {
        return !attempted || current_source_stamp != source_stamp;
    }

    constexpr void RecordAttempt(uint64_t current_source_stamp, bool success) noexcept {
        attempted = true;
        loaded = success;
        source_stamp = current_source_stamp;
    }
};

struct PauseMusicToggleResult {
    bool playing;
    bool enabled;

    friend constexpr bool operator==(const PauseMusicToggleResult&,
                                     const PauseMusicToggleResult&) = default;
};

constexpr PauseMusicToggleResult ResolvePauseMusicToggle(
    bool currently_playing, bool start_succeeded) noexcept {
    if (currently_playing) {
        return {false, false};
    }
    return {start_succeeded, start_succeeded};
}

} // namespace igi
