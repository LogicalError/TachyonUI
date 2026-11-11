#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace tachyon
{
    /// @addtogroup app
    /// @{

    /// How often GUIApplication::Run draws a frame (GUIApplication::SetRedrawMode).
    enum class RedrawMode : uint8_t
    {
        Always,       ///< every frame, as often as the display refreshes
        WhenChanged,  ///< builds every frame at the refresh rate; records and presents only the windows whose draws changed
        WhenNeeded,   ///< sleeps until there's input, a redraw is asked for, a timer is due or something animates (the default)
    };

    /// Redraws asked for: now, or at a time on the steady clock (SteadyClockNowNanos). Safe
    /// from any thread.
    class RedrawRequests
    {
    public:
        /// A frame as soon as possible.
        void Request() { m_now = true; }
        /// A frame at `nanos`.
        void RequestAt(int64_t nanos);
        /// True when a frame was asked for now, or for `nowNanos` or before.
        bool IsDue(int64_t nowNanos) const;
        /// The earliest time asked for; INT64_MAX when there's none.
        int64_t NextAt() const;
        /// At a frame's start: the requests it answers (now, and up to `nowNanos`) are done.
        void Answer(int64_t nowNanos);

    private:
        std::atomic<bool>    m_now = false;
        mutable std::mutex   m_mutex;
        std::vector<int64_t> m_at;
    };

    /// @}
}
