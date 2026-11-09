#pragma once

// Helpers the Timeline row and the TimelineEditor share: times as text, ticks, frames.

#include <gui_controls/gui_timeline_types.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

namespace tachyon::timeline_internal
{
    inline float Finite(float value, float fallback)
    {
        return std::isfinite(value) ? value : fallback;
    }

    // `time` on the nearest frame (unchanged without frames), within the length.
    inline float SnapToFrame(float time, float frame, float length)
    {
        if (frame > 0.0f)
            time = std::round(time / frame) * frame;
        return std::clamp(time, 0.0f, length);
    }

    // The frame `time` is in, counted from 0.
    inline int FrameNumber(float time, float frame)
    {
        return frame > 0.0f ? int(std::floor(time / frame + 1e-3f)) : 0;
    }

    // "f 7", padded to as many digits as the last frame has ("f   7" of 300 frames): the chip
    // keeps its width while the playhead moves, and what comes after it its place.
    inline std::string FrameText(float time, float frame, float length)
    {
        const int digits = int(std::to_string(FrameNumber(length, frame)).size());
        return std::format("f {:>{}}", FrameNumber(time, frame), digits);
    }

    // "01:04.47": minutes, seconds and hundredths.
    inline std::string ClockText(float secs)
    {
        const long long hundredths = std::llround(double(std::max(0.0f, secs)) * 100.0);
        return std::format("{:02}:{:02}.{:02}", hundredths / 6000, (hundredths / 100) % 60, hundredths % 100);
    }

    // Plays or pauses; played from the end, it starts over.
    inline void TogglePlay(TimelineState& state, float length)
    {
        if (!state.playing && state.time >= length)
            state.time = 0.0f;
        state.playing = !state.playing;
    }

    // The seconds between major ticks: the shortest of the usual steps at least `minPixels`
    // apart at `pixelsPerSecond`; 0 when not even an hour is.
    inline float MajorTickStep(float pixelsPerSecond, float minPixels)
    {
        static constexpr float kSteps[] = { 0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 15.0f, 30.0f,
                                            60.0f, 120.0f, 300.0f, 600.0f, 1800.0f, 3600.0f };
        for (const float step : kSteps)
            if (step * pixelsPerSecond >= minPixels)
                return step;
        return 0.0f;
    }
}
