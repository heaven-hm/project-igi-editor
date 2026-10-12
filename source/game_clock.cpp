// game_clock.cpp - Fixed-step 30 Hz deterministic simulation clock implementation
#include "game_clock.h"
#include <algorithm>

namespace igi {

GameClock::GameClock() {
    Reset();
}

void GameClock::Reset() {
    last_time_ms_ = 0;
    accumulator_ms_ = 0;
    excluded_time_start_ms_ = 0;
    tick_count_ = 0;
    is_paused_ = false;
    in_excluded_scope_ = false;
    has_last_time_ = false;
}

void GameClock::Update(int64_t now_milliseconds) {
    if (in_excluded_scope_ || is_paused_) {
        last_time_ms_ = now_milliseconds;
        has_last_time_ = true;
        return;
    }

    int64_t delta_ms = 0;
    if (has_last_time_) {
        delta_ms = now_milliseconds - last_time_ms_;
    } else {
        delta_ms = 0;
    }
    last_time_ms_ = now_milliseconds;
    has_last_time_ = true;

    if (delta_ms < 0) {
        delta_ms = 0; // Handle clock jump / backwards skew
    }

    // Clamp huge frame deltas to max catch-up to prevent spiral of death
    int64_t max_allowed_delta = MAX_CATCH_UP_TICKS * TICK_INTERVAL_MS;
    delta_ms = std::min(delta_ms, max_allowed_delta);

    accumulator_ms_ += delta_ms;
}

void GameClock::SetPaused(bool paused) {
    if (is_paused_ == paused) {
        return;
    }

    is_paused_ = paused;
    if (!is_paused_) {
        // The host may not call Update while the menu is open.  Start a new
        // timing interval on resume so wall-clock time spent in the menu is
        // never converted into gameplay catch-up ticks.
        has_last_time_ = false;
    }
}

bool GameClock::IsTickDue() const {
    if (is_paused_ || in_excluded_scope_) {
        return false;
    }
    return accumulator_ms_ >= TICK_INTERVAL_MS;
}

void GameClock::CompleteTick() {
    if (accumulator_ms_ >= TICK_INTERVAL_MS) {
        accumulator_ms_ -= TICK_INTERVAL_MS;
        tick_count_++;
    }
}

void GameClock::CompleteRender() {
    // Render boundary synchronization hook
}

void GameClock::BeginExcludedTime(int64_t now_milliseconds) {
    in_excluded_scope_ = true;
    excluded_time_start_ms_ = now_milliseconds;
}

void GameClock::EndExcludedTime(int64_t now_milliseconds) {
    in_excluded_scope_ = false;
    last_time_ms_ = now_milliseconds;
    excluded_time_start_ms_ = 0;
}

} // namespace igi
