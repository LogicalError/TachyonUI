#pragma once

#include <span>

namespace tachyon
{
    /// @addtogroup app
    /// @{

    /// Where playback is: what a Timeline shows and edits. Keep it in the app, from frame to
    /// frame; AdvanceTimeline moves it on while it plays.
    struct TimelineState
    {
        float time    = 0.0f;    ///< the playhead, in seconds from the start
        bool  playing = false;   ///< moving on by itself (AdvanceTimeline)
        bool  loop    = false;   ///< at the end, back to the start instead of stopping

        bool operator==(const TimelineState&) const = default;
    };

    /// What a Timeline covers. The markers are only read during the call.
    struct TimelineOptions
    {
        float duration = 0.0f;   ///< the length, in seconds: the scrubber's right end
        float fps      = 30.0f;  ///< frames per second: a step's length, and the frame number shown; 0 or less: no frames
        /// How far the content is ready, in seconds: a simulation's frames computed so far,
        /// drawn as a fill along the scrubber. Below 0: no fill.
        float progress = -1.0f;
        std::span<const float> markers;   ///< checkpoints, in seconds: a line each on the scrubber
    };

    /// Moves `state` on by `deltaSecs` while it plays: call it once a frame with the frame's
    /// time. At the end it goes on from the start when looping, else it stops there. Returns
    /// whether the time moved.
    ///
    ///     AdvanceTimeline(playback, options, app.frameTime);
    bool AdvanceTimeline(TimelineState& state, const TimelineOptions& options, float deltaSecs);

    /// @}
}
