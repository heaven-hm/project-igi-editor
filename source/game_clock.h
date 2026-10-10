// game_clock.h - Fixed-step 30 Hz deterministic simulation clock
#pragma once

#include <cstdint>
#include <cmath>
#include <stdexcept>

namespace igi {

enum class FrameAction {
    Tick,
    Render,
    Idle,
};

class GameClock {
public:
    static constexpr uint32_t TICK_RATE_HZ = 30;
    static constexpr double TICK_INTERVAL_SECONDS = 1.0 / static_cast<double>(TICK_RATE_HZ);
    static constexpr int64_t MAX_CATCH_UP_TICKS = 10;
    static constexpr int64_t GUARDED_STARTUP_TICKS = 3;
    static constexpr int64_t EXCLUDED_STARTUP_COUNT = 2;

    GameClock();

    // Records the current monotonic clock reading. Deadlines are absolute
    // tick-index deadlines; no rounded 33 ms accumulator is maintained.
    void Update(int64_t now_milliseconds);

    // Decides what a frame may do at the supplied clock reading.
    FrameAction Decide(int64_t now_milliseconds) const;

    // Checks whether a scheduled simulation tick is due at the current/supplied
    // reading. The no-argument overload preserves the scheduler's old seam.
    bool IsTickDue(int64_t now_milliseconds) const;
    bool IsTickDue() const;

    // Records one completed scheduled tick.
    void CompleteTick();

    // Records one completed render frame.
    void CompleteRender();

    // Records a tick/render pair that intentionally ran outside the schedule
    // (pause/loading/cutscene). It does not refill the catch-up budget.
    void CompleteUnscheduledFrame();

    // Exclusion scopes (e.g. while loading level or paused)
    void BeginExcludedTime(int64_t now_milliseconds);
    void EndExcludedTime(int64_t now_milliseconds);

    // Resets drift and accumulator state
    void Reset(int64_t now_milliseconds = 0);
    void ResetDrift();
    void AdvanceTimeBase();

    // Accessors
    uint64_t GetTickCount() const { return tick_count_; }
    uint64_t GetFrameCount() const { return frame_count_; }
    uint64_t GetCatchUpCount() const { return catch_up_count_; }
    double GetTimeSeconds() const { return tick_count_ * TICK_INTERVAL_SECONDS; }
    int64_t GetTimeBaseMilliseconds() const { return time_base_ms_; }
    int64_t GetExcludedMilliseconds() const { return excluded_ms_; }
    int64_t GetDueMilliseconds() const;
    bool HasRendered() const { return has_rendered_; }
    bool IsExcludingTime() const { return exclusion_depth_ > 0; }
    bool IsStartupTick() const { return tick_count_ < EXCLUDED_STARTUP_COUNT; }
    bool IsStartupFrame() const { return frame_count_ < EXCLUDED_STARTUP_COUNT; }
    bool IsCatchUpCapped() const { return catch_up_count_ > MAX_CATCH_UP_TICKS; }
    float TickFraction(int64_t now_milliseconds) const;
    int64_t GetTimeAdjustMilliseconds() const { return time_adjust_ms_; }
    void SetTimeAdjustMilliseconds(int64_t adjust) { time_adjust_ms_ = adjust; }
    bool IsPaused() const { return is_paused_; }
    void SetPaused(bool paused) { is_paused_ = paused; }
    bool GetUncappedRenders() const { return uncapped_renders_; }
    void SetUncappedRenders(bool enabled) { uncapped_renders_ = enabled; }

private:
    int64_t now_ms_ = 0;
    int64_t time_base_ms_ = 0;
    int64_t excluded_ms_ = 0;
    int64_t exclusion_start_ms_ = 0;
    int64_t time_adjust_ms_ = 0;
    uint64_t tick_count_ = 0;
    uint64_t frame_count_ = 0;
    uint64_t catch_up_count_ = 0;
    bool has_time_base_ = false;
    bool is_paused_ = false;
    bool has_rendered_ = true;
    bool uncapped_renders_ = false;
    uint32_t exclusion_depth_ = 0;
};

} // namespace igi
