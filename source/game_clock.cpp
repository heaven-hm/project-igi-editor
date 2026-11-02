// game_clock.cpp - Fixed-step 30 Hz deterministic simulation clock implementation
#include "game_clock.h"
#include <algorithm>

namespace igi {

GameClock::GameClock() {
    Reset();
}

void GameClock::Reset(int64_t now_milliseconds) {
    now_ms_ = now_milliseconds;
    time_base_ms_ = now_milliseconds;
    excluded_ms_ = 0;
    exclusion_start_ms_ = 0;
    time_adjust_ms_ = 0;
    tick_count_ = 0;
    frame_count_ = 0;
    catch_up_count_ = 0;
    has_time_base_ = false;
    is_paused_ = false;
    has_rendered_ = true;
    exclusion_depth_ = 0;
}

void GameClock::Update(int64_t now_milliseconds) {
    // The simulation never integrates wall-clock deltas. The supplied reading
    // is only compared to the next absolute deadline, so a long frame cannot
    // create a larger physics step and the catch-up cap deliberately drops
    // time after the budget is exhausted.
    if (!has_time_base_) {
        time_base_ms_ = now_milliseconds;
        now_ms_ = now_milliseconds;
        has_time_base_ = true;
        return;
    }
    now_ms_ = now_milliseconds;
}

int64_t GameClock::GetDueMilliseconds() const {
    const int64_t origin = time_base_ms_ + excluded_ms_;
    const double due = (1000.0 / static_cast<double>(TICK_RATE_HZ)) *
                       static_cast<double>(tick_count_) + static_cast<double>(origin);
    return static_cast<int64_t>(std::trunc(due));
}

bool GameClock::IsTickDue(int64_t now_milliseconds) const {
    if (is_paused_ || exclusion_depth_ > 0) {
        return false;
    }

    // Retail compares the signed 32-bit clock reading to the truncated
    // absolute deadline. The Windows target uses a 32-bit millisecond source;
    // keep the same wrap behavior even though the public test seam is 64-bit.
    const int32_t now32 = static_cast<int32_t>(now_milliseconds);
    const int32_t due32 = static_cast<int32_t>(GetDueMilliseconds());
    if (now32 <= due32) {
        return false;
    }
    if (catch_up_count_ > MAX_CATCH_UP_TICKS) {
        return false;
    }
    return has_rendered_ || tick_count_ >= GUARDED_STARTUP_TICKS;
}

bool GameClock::IsTickDue() const {
    return IsTickDue(now_ms_);
}

FrameAction GameClock::Decide(int64_t now_milliseconds) const {
    if (IsTickDue(now_milliseconds)) {
        return FrameAction::Tick;
    }
    return has_rendered_ && !uncapped_renders_ ? FrameAction::Idle : FrameAction::Render;
}

void GameClock::CompleteTick() {
    ++tick_count_;
    has_rendered_ = false;
    ++catch_up_count_;
}

void GameClock::CompleteRender() {
    ++frame_count_;
    has_rendered_ = true;
    catch_up_count_ = 0;
}

void GameClock::CompleteUnscheduledFrame() {
    ++tick_count_;
    ++frame_count_;
}

void GameClock::ResetDrift() {
    catch_up_count_ = 0;
    time_adjust_ms_ = 0;
    excluded_ms_ = 0;
}

void GameClock::AdvanceTimeBase() {
    time_base_ms_ += time_adjust_ms_;
}

void GameClock::BeginExcludedTime(int64_t now_milliseconds) {
    if (exclusion_depth_ == 0) {
        exclusion_start_ms_ = now_milliseconds;
    }
    ++exclusion_depth_;
}

void GameClock::EndExcludedTime(int64_t now_milliseconds) {
    if (exclusion_depth_ == 0) {
        throw std::logic_error("GameClock::EndExcludedTime without a matching begin");
    }
    if (--exclusion_depth_ == 0) {
        excluded_ms_ += now_milliseconds - exclusion_start_ms_;
        exclusion_start_ms_ = 0;
        now_ms_ = now_milliseconds;
    }
}

float GameClock::TickFraction(int64_t now_milliseconds) const {
    if (tick_count_ == 0) {
        return 1.0f;
    }
    const int64_t previous_due = static_cast<int64_t>(std::trunc(
        (1000.0 / static_cast<double>(TICK_RATE_HZ)) * static_cast<double>(tick_count_ - 1) +
        static_cast<double>(time_base_ms_ + excluded_ms_)));
    const int64_t next_due = GetDueMilliseconds();
    const int64_t period = next_due - previous_due;
    if (period <= 0) {
        return 1.0f;
    }
    const double fraction = static_cast<double>(now_milliseconds - previous_due) /
                            static_cast<double>(period);
    return static_cast<float>(std::clamp(fraction, 0.0, 1.0));
}

} // namespace igi
