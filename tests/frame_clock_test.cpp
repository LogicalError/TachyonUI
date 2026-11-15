// Unit tests for FrameClock, the vsync-paced animation clock:
//   - the first tick starts at 0
//   - jittered frame intervals step by exactly one refresh period
//   - a missed vsync advances two periods
//   - a slightly wrong refresh rate can't drift from the wall clock
//   - two ticks within one vsync don't advance the clock
//   - without a refresh rate it follows the wall clock, never backwards
// and FramePaceWaitSeconds, the wait that keeps Run from outrunning the display:
//   - a frame whose present returned at once waits out the rest of the refresh period
//   - a frame whose present waited for vsync, or that ran long, doesn't wait
//   - a wait too short to be worth sleeping for is skipped
//   - without a refresh rate it paces to 60 Hz

#include <gtest/gtest.h>

#include <gui_core/frame/gui_frame_clock.h>

#include <cmath>

namespace tachyon
{
    namespace
    {
        constexpr int64_t kStart = 5'000'000'000;   // arbitrary origin
        int64_t Ms(double ms) { return kStart + int64_t(std::llround(ms * 1e6)); }
    }

    TEST(FrameClockTest, FirstTickStartsAtZero)
    {
        FrameClock clock;
        clock.Tick(kStart, 60.0f);
        EXPECT_EQ(clock.Seconds(), 0.0);
        EXPECT_EQ(clock.DeltaSeconds(), 0.0);
    }

    TEST(FrameClockTest, JitteredFramesStepByExactlyOnePeriod)
    {
        const double period = 1.0 / 120.0;
        const double jitterMs[] = { +1.2, -0.9, +2.5, -2.0, +0.4, -1.6, +3.0, -3.0 };
        FrameClock clock;
        clock.Tick(kStart, 120.0f);
        for (int i = 1; i <= 8; ++i)
        {
            clock.Tick(Ms(i * period * 1000.0 + jitterMs[i - 1]), 120.0f);
            EXPECT_NEAR(clock.DeltaSeconds(), period, 1e-12) << "frame " << i;
            EXPECT_NEAR(clock.Seconds(), i * period, 1e-12) << "frame " << i;
        }
    }

    TEST(FrameClockTest, MissedVsyncAdvancesTwoPeriods)
    {
        const double period = 1.0 / 60.0;
        FrameClock clock;
        clock.Tick(kStart, 60.0f);
        clock.Tick(Ms(1000.0 * period), 60.0f);
        clock.Tick(Ms(1000.0 * 3.0 * period + 0.7), 60.0f);
        EXPECT_NEAR(clock.DeltaSeconds(), 2.0 * period, 1e-12);
        EXPECT_NEAR(clock.Seconds(), 3.0 * period, 1e-12);
    }

    TEST(FrameClockTest, WrongRefreshRateDoesNotDrift)
    {
        // Display really runs at 59.94 Hz; the clock is told 60.
        const double realPeriod = 1.0 / 59.94;
        FrameClock clock;
        clock.Tick(kStart, 60.0f);
        for (int i = 1; i <= 6000; ++i)
        {
            const double wall = i * realPeriod;
            clock.Tick(Ms(wall * 1000.0), 60.0f);
            ASSERT_LE(std::abs(clock.Seconds() - wall), 0.5 / 60.0 + 1e-9) << "frame " << i;
        }
    }

    TEST(FrameClockTest, TicksWithinOneVsyncDoNotAdvance)
    {
        FrameClock clock;
        clock.Tick(kStart, 60.0f);
        clock.Tick(Ms(2.0), 60.0f);
        EXPECT_EQ(clock.Seconds(), 0.0);
        EXPECT_EQ(clock.DeltaSeconds(), 0.0);
    }

    TEST(FrameClockTest, UnknownRefreshRateFollowsWallClockMonotonically)
    {
        FrameClock clock;
        clock.Tick(kStart, 0.0f);
        clock.Tick(Ms(13.0), 0.0f);
        EXPECT_NEAR(clock.Seconds(), 0.013, 1e-9);

        // Paced ahead of the wall clock, then the rate becomes unknown.
        clock.Tick(Ms(25.0), 60.0f);   // 12 ms behind rounds up to one period
        const double paced = clock.Seconds();
        EXPECT_GT(paced, 0.027);
        clock.Tick(Ms(27.0), 0.0f);
        EXPECT_EQ(clock.Seconds(), paced);
        EXPECT_EQ(clock.DeltaSeconds(), 0.0);
    }

    // A window the compositor draws: present returned at once after 1.5 ms of work.
    TEST(FramePaceTest, AnUnblockedFrameWaitsOutTheRefreshPeriod)
    {
        const double period = 1.0 / 120.0;
        const double wait   = FramePaceWaitSeconds(kStart, Ms(1.5), 120.0f);
        EXPECT_NEAR(wait, kFramePaceShare * period - 0.0015, 1e-9);
        EXPECT_GT(wait, 0.006) << "most of a 120 Hz period";
    }

    // Present blocked until the vsync: the frame already took about a period, give or take
    // the scheduler's jitter, and must not be held back from the next vsync.
    TEST(FramePaceTest, AVsyncPacedFrameDoesNotWait)
    {
        const double periodMs = 1000.0 / 120.0;
        for (const double jitterMs : { -0.4, -0.1, 0.0, +0.3 })
            EXPECT_EQ(FramePaceWaitSeconds(kStart, Ms(periodMs + jitterMs), 120.0f), 0.0) << "jitter " << jitterMs;
    }

    TEST(FramePaceTest, ALongFrameDoesNotWait)
    {
        EXPECT_EQ(FramePaceWaitSeconds(kStart, Ms(40.0), 60.0f), 0.0);
    }

    // Left with less than a sleep is worth: no sleep.
    TEST(FramePaceTest, ATinyWaitIsSkipped)
    {
        const double periodMs = 1000.0 / 60.0;
        const double dueMs    = kFramePaceShare * periodMs;
        EXPECT_EQ(FramePaceWaitSeconds(kStart, Ms(dueMs - 0.5 * kFramePaceMinWaitSeconds * 1000.0), 60.0f), 0.0);
        EXPECT_GT(FramePaceWaitSeconds(kStart, Ms(dueMs - 2.0 * kFramePaceMinWaitSeconds * 1000.0), 60.0f), 0.0);
    }

    TEST(FramePaceTest, UnknownRefreshRatePacesTo60Hz)
    {
        EXPECT_NEAR(FramePaceWaitSeconds(kStart, kStart, 0.0f), kFramePaceShare / 60.0, 1e-9);
        EXPECT_NEAR(FramePaceWaitSeconds(kStart, kStart, -1.0f), kFramePaceShare / 60.0, 1e-9);
    }

    // A faster display, a shorter wait: pacing never holds a 240 Hz window to 60.
    TEST(FramePaceTest, TheWaitFollowsTheRefreshRate)
    {
        EXPECT_NEAR(FramePaceWaitSeconds(kStart, Ms(1.0), 240.0f), kFramePaceShare / 240.0 - 0.001, 1e-9);
    }
}
