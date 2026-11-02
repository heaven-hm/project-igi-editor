#include <gtest/gtest.h>

#include "../source/game_clock.h"

TEST(GameClockTest, DeterministicTicksAndPause) {
    igi::GameClock clock;
    EXPECT_EQ(clock.GetTickCount(), 0u);
    EXPECT_FALSE(clock.IsTickDue());

    clock.Update(0);
    clock.Update(100);
    EXPECT_TRUE(clock.IsTickDue());

    int ticks = 0;
    while (clock.IsTickDue()) {
        clock.CompleteTick();
        ++ticks;
        if (clock.GetTickCount() < igi::GameClock::GUARDED_STARTUP_TICKS) {
            clock.CompleteRender();
        }
    }
    EXPECT_GE(ticks, 3);
    EXPECT_EQ(clock.GetTickCount(), static_cast<uint64_t>(ticks));

    clock.SetPaused(true);
    clock.Update(300);
    EXPECT_FALSE(clock.IsTickDue());
}

TEST(GameClockTest, UsesAbsoluteDeadlinesInsteadOfRoundedAccumulator) {
    igi::GameClock clock;
    clock.Reset(1000);
    clock.Update(1000);
    clock.Update(1034);

    ASSERT_TRUE(clock.IsTickDue());
    EXPECT_EQ(clock.GetDueMilliseconds(), 1000);
    clock.CompleteTick();
    clock.CompleteRender();

    clock.Update(1067);
    EXPECT_TRUE(clock.IsTickDue());
    EXPECT_EQ(clock.GetDueMilliseconds(), 1033);
}

TEST(GameClockTest, BoundsCatchUpBurstAndExcludesNestedWallTime) {
    igi::GameClock clock;
    clock.Reset(0);
    clock.Update(0);
    clock.Update(1000);
    for (int startup_tick = 0; startup_tick < igi::GameClock::GUARDED_STARTUP_TICKS;
         ++startup_tick) {
        ASSERT_TRUE(clock.IsTickDue());
        clock.CompleteTick();
        clock.CompleteRender();
    }

    int completed_ticks = 0;
    while (clock.IsTickDue()) {
        clock.CompleteTick();
        ++completed_ticks;
    }
    EXPECT_EQ(completed_ticks, 11);
    EXPECT_TRUE(clock.IsCatchUpCapped());

    clock.Reset(100);
    clock.Update(100);
    clock.BeginExcludedTime(100);
    clock.BeginExcludedTime(200);
    clock.EndExcludedTime(300);
    clock.EndExcludedTime(400);
    clock.Update(400);
    EXPECT_EQ(clock.GetExcludedMilliseconds(), 300);
    EXPECT_FALSE(clock.IsTickDue());
    EXPECT_TRUE(clock.IsTickDue(401));
}

TEST(GameClockTest, RejectsUnbalancedExcludedTimeEnd) {
    igi::GameClock clock;
    EXPECT_THROW(clock.EndExcludedTime(10), std::logic_error);
}
