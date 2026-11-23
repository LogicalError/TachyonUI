// The small helpers the Timeline row and the TimelineEditor share: frame numbers, the frame
// chip's text and the ruler's tick step.

#include <gtest/gtest.h>

#include <gui_controls/gui_timeline_internal.h>

namespace tachyon
{
    namespace
    {
        using namespace timeline_internal;

        constexpr float kFrame30 = 1.0f / 30.0f;

        TEST(TimelineInternal, FrameNumberCountsWholeFramesFromZero)
        {
            EXPECT_EQ(FrameNumber(0.0f, kFrame30), 0);
            EXPECT_EQ(FrameNumber(1.0f, kFrame30), 30);
            EXPECT_EQ(FrameNumber(1.5f * kFrame30, kFrame30), 1) << "inside frame 1";
            // A time on a frame that a float puts a hair below it still counts as that frame.
            EXPECT_EQ(FrameNumber(7.0f * kFrame30 - 1e-6f, kFrame30), 7);
            EXPECT_EQ(FrameNumber(5.0f, 0.0f), 0) << "no frames";
            EXPECT_EQ(FrameNumber(5.0f, -1.0f), 0);
        }

        // The chip is as wide as the last frame's number, so it doesn't change width as the
        // playhead moves.
        TEST(TimelineInternal, FrameTextIsPaddedToTheLastFrame)
        {
            EXPECT_EQ(FrameText(7.0f * kFrame30, kFrame30, 10.0f), "f   7") << "300 frames: three digits";
            EXPECT_EQ(FrameText(10.0f, kFrame30, 10.0f), "f 300");
            EXPECT_EQ(FrameText(2.0f * kFrame30, kFrame30, 0.2f), "f 2") << "6 frames: one digit";
        }

        TEST(TimelineInternal, MajorTickStepIsTheShortestFarEnoughApart)
        {
            EXPECT_FLOAT_EQ(MajorTickStep(1000.0f, 50.0f), 0.1f);
            EXPECT_FLOAT_EQ(MajorTickStep(100.0f, 50.0f), 0.5f);
            EXPECT_FLOAT_EQ(MajorTickStep(100.0f, 100.0f), 1.0f) << "exactly far enough";
            EXPECT_FLOAT_EQ(MajorTickStep(1.0f, 50.0f), 60.0f);
            EXPECT_FLOAT_EQ(MajorTickStep(0.01f, 50.0f), 0.0f) << "not even an hour is";
        }

        TEST(TimelineInternal, SnapToFrameStaysInTheLength)
        {
            EXPECT_FLOAT_EQ(SnapToFrame(1.4f * kFrame30, kFrame30, 10.0f), kFrame30);
            EXPECT_FLOAT_EQ(SnapToFrame(0.123f, 0.0f, 10.0f), 0.123f) << "no frames: unchanged";
            EXPECT_FLOAT_EQ(SnapToFrame(-1.0f, kFrame30, 10.0f), 0.0f);
            EXPECT_FLOAT_EQ(SnapToFrame(12.0f, kFrame30, 10.0f), 10.0f);
        }

        TEST(TimelineInternal, ClockTextIsMinutesSecondsAndHundredths)
        {
            EXPECT_EQ(ClockText(64.47f), "01:04.47");
            EXPECT_EQ(ClockText(0.0f), "00:00.00");
            EXPECT_EQ(ClockText(-3.0f), "00:00.00") << "no negative times";
            EXPECT_EQ(ClockText(59.999f), "01:00.00") << "rounds up into the next minute";
        }
    }
}
