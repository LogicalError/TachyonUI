#pragma once

// A steady clock a test moves on by hand instead of sleeping: while one is alive,
// SteadyClockNowNanos reads it, so redraw timers, the frame clock, tooltips, double-clicks
// and thumbnails see the time the test says. It starts at the real time, so times taken
// before it never lie ahead of it. One at a time.

#include <gui_core/gui_base_types.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>

namespace GUITest
{
    class ScopedFakeClock
    {
    public:
        ScopedFakeClock()
        {
            assert(!s_alive && "one ScopedFakeClock at a time");
            s_alive = true;
            s_now = tachyon::SteadyClockNowNanos();
            tachyon::SetSteadyClockSource(&Now);
        }
        ~ScopedFakeClock()
        {
            tachyon::SetSteadyClockSource(nullptr);
            s_alive = false;
        }
        ScopedFakeClock(const ScopedFakeClock&) = delete;
        ScopedFakeClock& operator=(const ScopedFakeClock&) = delete;

        void Advance(std::chrono::nanoseconds by) { s_now += by.count(); }
        void AdvanceSeconds(double seconds) { s_now += int64_t(seconds * 1.0e9); }

        static int64_t Now() { return s_now.load(); }

    private:
        static inline std::atomic<int64_t> s_now{ 0 };
        static inline bool                 s_alive = false;
    };
}
