// game_clock.h - Fixed-step 30 Hz deterministic simulation clock
#pragma once

#include <cstdint>
#include <chrono>

namespace igi {

class GameClock {
public:
    static constexpr uint32_t TICK_RATE_HZ = 30;
    static constexpr double TICK_INTERVAL_SECONDS = 1.0 / static_cast<double>(TICK_RATE_HZ);
    static constexpr int64_t TICK_INTERVAL_MS = 33; // 33.33ms rounded for integer millisecond logic
    static constexpr int64_t MAX_CATCH_UP_TICKS = 4; // Bound catch-up to prevent spiral of death

    GameClock();

    // Advances internal accumulator based on current monotonic clock
    void Update(int64_t now_milliseconds);

    // Checks if at least one simulation tick is due
    bool IsTickDue() const;

    // Consumes one tick from the accumulator
    void CompleteTick();

    // Mark render frame completed
    void CompleteRender();

    // Exclusion scopes (e.g. while loading level or paused)
    void BeginExcludedTime(int64_t now_milliseconds);
    void EndExcludedTime(int64_t now_milliseconds);

    // Resets drift and accumulator state
    void Reset();

    // Accessors
    uint64_t GetTickCount() const { return tick_count_; }
    double GetTimeSeconds() const { return tick_count_ * TICK_INTERVAL_SECONDS; }
    bool IsPaused() const { return is_paused_; }
    void SetPaused(bool paused);

private:
    int64_t last_time_ms_ = 0;
    int64_t accumulator_ms_ = 0;
    int64_t excluded_time_start_ms_ = 0;
    uint64_t tick_count_ = 0;
    bool is_paused_ = false;
    bool in_excluded_scope_ = false;
    bool has_last_time_ = false;
};

} // namespace igi
