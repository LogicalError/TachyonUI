#include <gui_core/windows/gui_window_backend.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

namespace tachyon
{
    namespace
    {
        // Whole nanoseconds: a float duration is added to the steady clock's time as a float,
        // which days after boot can't tell a few milliseconds from nothing.
        std::chrono::nanoseconds Nanoseconds(float secs) { return std::chrono::nanoseconds(std::llround(double(secs) * 1e9)); }
    }

    void IWindowBackend::WaitForEvents(float timeoutSecs)
    {
        // Without a way to wait for the OS: a short sleep, after which the app looks again.
        const float secs = std::min(timeoutSecs, 1.0f / 60.0f);
        if (secs > 0.0f)
            std::this_thread::sleep_for(Nanoseconds(secs));
    }

    void IWindowBackend::SleepFor(float secs)
    {
        if (secs > 0.0f)
            std::this_thread::sleep_for(Nanoseconds(secs));
    }
}
