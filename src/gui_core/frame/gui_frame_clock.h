#pragma once

#include <cstdint>

namespace tachyon
{
    /// @addtogroup app
    /// @{

    /// Animation time locked to the display refresh. Frames reach the screen on
    /// vsyncs, a whole number of refresh periods after the previous one, but the CPU
    /// sees those intervals with jitter. The clock advances by whole periods,
    /// rounded against the wall clock, so animations step evenly and never drift
    /// from real time by more than half a period. Without a known refresh rate it
    /// follows the wall clock.
    class FrameClock
    {
    public:
        /// Advances the clock; call once per frame, before anything reads the time. `nowNanos`
        /// is a monotonic timestamp (SteadyClockNowNanos); `refreshHz` is 0 when unknown.
        void Tick(int64_t nowNanos, float refreshHz);

        /// Seconds since the first Tick.
        double Seconds() const { return m_seconds; }
        /// Seconds the last Tick advanced; 0 on the first.
        double DeltaSeconds() const { return m_deltaSeconds; }

    private:
        int64_t m_originNanos  = 0;
        bool    m_started      = false;
        double  m_seconds      = 0.0;
        double  m_deltaSeconds = 0.0;
    };

    /// The share of a refresh period a frame takes at least when GUIApplication::Run paces
    /// it. Under 1, so a refresh rate reported a little low, or a vsync-paced frame that
    /// ends a little early, never waits and misses the next vsync.
    inline constexpr double kFramePaceShare = 0.95;
    /// A wait shorter than this is skipped: it costs about as much as it saves.
    inline constexpr double kFramePaceMinWaitSeconds = 0.00025;

    /// How long to wait after presenting a frame, in seconds, so the loop doesn't run
    /// faster than the display shows frames. A present that blocked until the vsync already
    /// took about a period, so nothing is left to wait; one that returned at once (mailbox
    /// present, or a window the compositor draws) leaves the rest of the period.
    /// `frameStartNanos` and `nowNanos` are SteadyClockNowNanos timestamps; `refreshHz` is
    /// 0 when unknown, taken as 60 Hz. 0 when the frame needs no wait.
    double FramePaceWaitSeconds(int64_t frameStartNanos, int64_t nowNanos, float refreshHz);
    /// @}
}
