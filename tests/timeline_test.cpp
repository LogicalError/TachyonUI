// Timeline: the transport buttons, the time and frame, the scrubber (pointer and keys), what
// it draws, and AdvanceTimeline.

#include <gtest/gtest.h>

#include "headless_gui_app.h"
#include "skribidi_test_fixture.h"

#include <gui_core/graphics/gui_custom_draw_context.h>
#include <gui_core/elements/gui_element_storage.h>

#include <cmath>
#include <limits>
#include <vector>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;
    using GUITest::StubGUIRenderer;

    namespace
    {
        constexpr float kFrame = 1.0f / 30.0f;

        class TimelineTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp     app;
            TimelineState      state;
            std::vector<float> markers{ 1.5f, 5.2f };
            TimelineOptions    options{ .duration = 8.0f, .fps = 30.0f };
            ID                 timelineID{};
            bool               changed = false;
            bool               disabled = false;

            void SetUp() override
            {
                ASSERT_TRUE(app.Initialize());
                timelineID = app.idManager.CreateStaticID();
                options.markers = markers;
            }
            void TearDown() override { app.Cleanup(); }

            void Frame()
            {
                app.Frame([&] {
                    app.BeginRootWindow({ 1920, 1080 }, ElementFlags::None, app.theme.mainWindowStyleRef);
                    if (disabled) app.BeginDisabled();
                    changed = app.Timeline(timelineID, state, options);
                    if (disabled) app.EndDisabled();
                    app.EndRootWindow();
                });
            }
            void Settle() { Frame(); Frame(); }

            // The elements in `style`, in layout order.
            std::vector<ID> WithStyle(StyleRef style) const
            {
                std::vector<ID> out;
                for (uint32_t d = 0; d < app.elementStorage.NumberOfDepthLevels(); d++)
                    for (uint32_t i = 0; i < app.elementStorage.DepthSizeOf(d); i++)
                    {
                        const ElementRef ref{ d, i };
                        if (app.elementStorage.IsValidRef(ref) && app.elementStorage.GetElementUnsafe(ref).styleRef == style)
                            out.push_back(app.elementStorage.GetElementUnsafe(ref).id);
                    }
                return out;
            }
            ID One(StyleRef style) const
            {
                const std::vector<ID> ids = WithStyle(style);
                return ids.size() == 1 ? ids[0] : ID::Invalid();
            }
            // The transport buttons without a style of their own: start, back, step, end.
            ID Button(int i) const
            {
                const std::vector<ID> ids = WithStyle(app.theme.timelineButtonStyleRef);
                return i < (int)ids.size() ? ids[size_t(i)] : ID::Invalid();
            }
            ID Scrubber() const { return One(app.theme.timelineScrubberStyleRef); }
            IntRect ScrubberRange() const
            {
                const ElementRect* rects = app.elementStorage.GetElementRectPtr(app.elementStorage.FindElementRefByID(Scrubber()));
                return rects ? rects->innerRect : IntRect{};
            }
            int2 OnScrubber(float part) const
            {
                const IntRect r = ScrubberRange();
                return { r.x + int32_t(std::lround(float(r.w) * part)), r.y + r.h / 2 };
            }

            void Click(ID id)
            {
                app.ClickAt(app.CenterOf(id));
                Frame();
            }
            void Key(KeyCode key)
            {
                app.InjectKey(key, KeyAction::KeyDown);
                Frame();
                app.InjectKey(key, KeyAction::KeyUp);
                Frame();
            }

            StubGUIRenderer Draw()
            {
                StubGUIRenderer renderer;
                const ElementRef ref = app.elementStorage.FindElementRefByID(Scrubber());
                const ElementCustomDraw* draw = ref.IsValid() ? app.elementStorage.GetCustomDraw(ref) : nullptr;
                if (draw == nullptr || !draw->callback)
                    return renderer;
                const ElementRect* rects = app.elementStorage.GetElementRectPtr(ref);
                CustomDrawContext ctx{ renderer, app.elementStorage, ref, rects->rect, rects->innerRect,
                                       IntRect{ -4000, -4000, 8000, 8000 }, float3x2{}, int2{ 0, 0 } };
                draw->callback(ctx);
                return renderer;
            }
            static uint32_t SlotOf(StyleRef style) { return GUIRenderer::StyleIndex(style.visualBoxStyleIndex, ElementState::None); }
            static std::vector<IntRect> BoxesIn(const StubGUIRenderer& renderer, StyleRef style)
            {
                std::vector<IntRect> out;
                for (size_t i = 0; i < renderer.boxes.size(); ++i)
                    if (renderer.boxStyles[i] == SlotOf(style))
                        out.push_back(renderer.boxes[i]);
                return out;
            }
        };

        // The time now and the length, minutes, seconds and hundredths, and the frame.
        TEST_F(TimelineTest, ItShowsTheTimeAndTheFrame)
        {
            state.time = 4.47f;
            Settle();
            EXPECT_NE(app.FindByText("00:04.47"), ID::Invalid());
            EXPECT_NE(app.FindByText("/ 00:08.00"), ID::Invalid());
            EXPECT_NE(app.FindByText("f 134"), ID::Invalid());
            state.time = 0.2f;
            Settle();
            EXPECT_NE(app.FindByText("f   6"), ID::Invalid()) << "as wide as the last frame's, 240";
            EXPECT_EQ(WithStyle(app.theme.timelineButtonStyleRef).size(), 4u);
            EXPECT_NE(One(app.theme.timelinePlayStyleRef), ID::Invalid());
            EXPECT_NE(One(app.theme.timelineLoopStyleRef), ID::Invalid());
            EXPECT_NE(Scrubber(), ID::Invalid());

            // Over a minute; without frames, no frame chip.
            options.duration = 125.0f;
            options.fps = 0.0f;
            state.time = 61.005f;
            Settle();
            EXPECT_NE(app.FindByText("01:01.01"), ID::Invalid());
            EXPECT_NE(app.FindByText("/ 02:05.00"), ID::Invalid());
            EXPECT_EQ(WithStyle(app.theme.timelineFrameStyleRef).size(), 0u);
            options.fps = -30.0f;
            Settle();
            EXPECT_EQ(WithStyle(app.theme.timelineFrameStyleRef).size(), 0u) << "nor with a negative rate";
        }

        // A time outside the length, or not a number, is put inside it; so is any time when
        // the length isn't one.
        TEST_F(TimelineTest, TheTimeIsKeptInTheLength)
        {
            state.time = 12.0f;
            Frame();
            EXPECT_EQ(state.time, 8.0f);
            state.time = -3.0f;
            Frame();
            EXPECT_EQ(state.time, 0.0f);
            state.time = std::numeric_limits<float>::quiet_NaN();
            Frame();
            EXPECT_EQ(state.time, 0.0f);
            options.duration = std::numeric_limits<float>::quiet_NaN();
            state.time = 2.0f;
            Frame();
            EXPECT_EQ(state.time, 0.0f);
            EXPECT_FALSE(changed) << "not the user's change";
        }

        // Play plays and pauses, reported on the frame after; played from the end, it starts over.
        TEST_F(TimelineTest, PlayPlaysAndPauses)
        {
            state.time = 2.0f;
            Settle();
            EXPECT_FALSE(changed);
            Click(One(app.theme.timelinePlayStyleRef));
            EXPECT_TRUE(state.playing);
            EXPECT_EQ(state.time, 2.0f);
            Frame();
            EXPECT_TRUE(changed);
            EXPECT_TRUE(app.guiStateManager.IsModified());
            Frame();
            EXPECT_FALSE(changed) << "only on the frame it changed";

            Click(One(app.theme.timelinePlayStyleRef));
            EXPECT_FALSE(state.playing);

            state.time = 8.0f;
            Settle();
            Click(One(app.theme.timelinePlayStyleRef));
            EXPECT_TRUE(state.playing);
            EXPECT_EQ(state.time, 0.0f) << "from the start again";
        }

        // Start, a frame back, a frame on and the end; stepping pauses, so does the end, the start doesn't.
        TEST_F(TimelineTest, TheButtonsMoveThePlayhead)
        {
            state.time = 4.0f;
            state.playing = true;
            Settle();
            Click(Button(1));
            EXPECT_NEAR(state.time, 4.0f - kFrame, 1e-5f);
            EXPECT_FALSE(state.playing);
            Settle();
            Click(Button(2));
            Settle();
            Click(Button(2));
            EXPECT_NEAR(state.time, 4.0f + kFrame, 1e-5f);

            state.playing = true;
            Settle();
            Click(Button(0));
            EXPECT_EQ(state.time, 0.0f);
            EXPECT_TRUE(state.playing) << "going on from the start";
            Settle();
            Click(Button(1));
            EXPECT_EQ(state.time, 0.0f) << "not before the start";

            state.playing = true;
            Settle();
            Click(Button(3));
            EXPECT_EQ(state.time, 8.0f);
            EXPECT_FALSE(state.playing);
            Settle();
            Click(Button(2));
            EXPECT_EQ(state.time, 8.0f) << "not past the end";

            // Without frames a step is a hundredth of the length.
            options.fps = 0.0f;
            state.time = 4.0f;
            Settle();
            Click(Button(2));
            EXPECT_NEAR(state.time, 4.08f, 1e-5f);
        }

        TEST_F(TimelineTest, TheLoopToggleSetsLooping)
        {
            Settle();
            Click(One(app.theme.timelineLoopStyleRef));
            EXPECT_TRUE(state.loop);
            Frame();
            EXPECT_TRUE(changed);
            Click(One(app.theme.timelineLoopStyleRef));
            EXPECT_FALSE(state.loop);
        }

        // A press on the scrubber puts the playhead there, on a frame; a drag moves it along,
        // past the ends only to the ends; Escape puts it back where it was.
        TEST_F(TimelineTest, TheScrubberMovesThePlayhead)
        {
            Settle();
            const IntRect range = ScrubberRange();
            ASSERT_GT(range.w, 100);
            const int2 middle = OnScrubber(0.5f);
            app.InjectMouseMove(middle);
            app.InjectMouseDown(middle);
            Frame();
            EXPECT_NEAR(state.time, 4.0f, kFrame / 2.0f);
            EXPECT_NEAR(std::fmod(state.time * 30.0f + 0.5f, 1.0f), 0.5f, 1e-3f) << "on a frame";
            EXPECT_EQ(app.guiStateManager.Interaction().GetFocusedID(), Scrubber());

            app.InjectMouseMove(OnScrubber(0.25f), MouseButtons::Left);
            Frame();
            EXPECT_NEAR(state.time, 2.0f, kFrame / 2.0f);
            app.InjectMouseMove({ range.x - 300, middle.y }, MouseButtons::Left);
            Frame();
            EXPECT_EQ(state.time, 0.0f);
            app.InjectMouseMove({ range.x + range.w + 300, middle.y }, MouseButtons::Left);
            Frame();
            EXPECT_EQ(state.time, 8.0f);
            app.InjectMouseUp({ range.x + range.w + 300, middle.y });
            Frame();
            EXPECT_EQ(state.time, 8.0f);
            Frame();
            Frame();

            state.time = 1.0f;
            Settle();
            app.InjectMouseMove(OnScrubber(0.75f));
            app.InjectMouseDown(OnScrubber(0.75f));
            Frame();
            EXPECT_NEAR(state.time, 6.0f, kFrame / 2.0f);
            app.InjectKey(KeyCode::Escape, KeyAction::KeyDown);
            Frame();
            EXPECT_EQ(state.time, 1.0f) << "back where it was";
            app.InjectMouseMove(OnScrubber(0.5f), MouseButtons::Left);
            Frame();
            EXPECT_EQ(state.time, 1.0f) << "the drag is over";
            app.InjectMouseUp(OnScrubber(0.5f));
            Frame();

            // Between two frames, onto the nearer one.
            Settle();
            app.ClickAt(OnScrubber(0.3f));
            Frame();
            EXPECT_NEAR(state.time, 2.4f, kFrame / 2.0f);
            EXPECT_NEAR(state.time * 30.0f, std::round(state.time * 30.0f), 1e-3f) << "on a frame: " << state.time;
        }

        // On the focused scrubber: Space plays and pauses, Left / Right step a frame and pause,
        // Home / End go to the ends, Page Down / Up to the marker before / after.
        TEST_F(TimelineTest, TheScrubberTakesKeys)
        {
            Settle();
            app.ClickAt(OnScrubber(0.5f));
            Frame();
            ASSERT_EQ(app.guiStateManager.Interaction().GetFocusedID(), Scrubber());
            ASSERT_NEAR(state.time, 4.0f, kFrame / 2.0f);
            const float start = state.time;

            Key(KeyCode::Space);
            EXPECT_TRUE(state.playing);
            Key(KeyCode::Right);
            EXPECT_NEAR(state.time, start + kFrame, 1e-5f);
            EXPECT_FALSE(state.playing);
            Key(KeyCode::Left);
            Key(KeyCode::Left);
            EXPECT_NEAR(state.time, start - kFrame, 1e-5f);

            Key(KeyCode::PageDown);
            EXPECT_EQ(state.time, 1.5f);
            Key(KeyCode::PageDown);
            EXPECT_EQ(state.time, 1.5f) << "none before the first";
            Key(KeyCode::PageUp);
            EXPECT_EQ(state.time, 5.2f);
            Key(KeyCode::PageUp);
            EXPECT_EQ(state.time, 5.2f) << "none after the last";
            Key(KeyCode::PageDown);
            EXPECT_EQ(state.time, 1.5f) << "from on a marker, the one before it";

            Key(KeyCode::End);
            EXPECT_EQ(state.time, 8.0f);
            Key(KeyCode::Home);
            EXPECT_EQ(state.time, 0.0f);
            Frame();
            EXPECT_FALSE(changed);
            Key(KeyCode::Left);
            EXPECT_EQ(state.time, 0.0f);
            EXPECT_FALSE(changed) << "a frame back from the start changes nothing";
            Key(KeyCode::Space);
            EXPECT_TRUE(changed) << "reported on the frame after";

            // Keys it doesn't use go on to the app.
            app.InjectKey(KeyCode::KeyA, KeyAction::KeyDown);
            Frame();
            EXPECT_FALSE(changed);
        }

        // Playing, the scrubber is drawn anew every frame (the app moves the playhead), so a
        // cached scope holding it is rebuilt; paused, it can be replayed.
        TEST_F(TimelineTest, PlayingItIsAnimated)
        {
            auto animated = [&] {
                const ElementRef ref = app.elementStorage.FindElementRefByID(Scrubber());
                return ref.IsValid() && (app.elementStorage.GetElementUnsafe(ref).flags & ElementFlags::Animated) != ElementFlags::None;
            };
            Settle();
            EXPECT_FALSE(animated());
            state.playing = true;
            Frame();
            EXPECT_TRUE(animated());
        }

        // Disabled, the scrubber and buttons do nothing.
        TEST_F(TimelineTest, DisabledItDoesNothing)
        {
            disabled = true;
            state.time = 2.0f;
            Settle();
            app.ClickAt(OnScrubber(0.75f));
            Frame();
            Click(One(app.theme.timelinePlayStyleRef));
            Click(Button(3));
            Frame();
            EXPECT_EQ(state.time, 2.0f);
            EXPECT_FALSE(state.playing);
            EXPECT_FALSE(changed);
        }

        // Disabled while the scrubber has the focus, it takes no keys.
        TEST_F(TimelineTest, DisabledWithTheFocusItTakesNoKeys)
        {
            state.time = 2.0f;
            Settle();
            app.guiStateManager.Interaction().SetFocusID(Scrubber());
            Frame();
            Key(KeyCode::Right);
            ASSERT_NEAR(state.time, 2.0f + kFrame, 1e-5f) << "test setup: the focused scrubber takes keys";
            disabled = true;
            Settle();
            app.guiStateManager.Interaction().SetFocusID(Scrubber());
            Frame();
            Key(KeyCode::Right);
            Key(KeyCode::Space);
            EXPECT_NEAR(state.time, 2.0f + kFrame, 1e-5f);
            EXPECT_FALSE(state.playing);
        }

        // The scrubber draws the progress fill from its start, a line per marker inside the
        // length, the ticks and the playhead.
        TEST_F(TimelineTest, TheScrubberDrawsWhatsReadyTheMarkersAndThePlayhead)
        {
            markers = { 5.2f, 1.5f, 20.0f, -1.0f, std::numeric_limits<float>::quiet_NaN() };
            options.markers = markers;
            options.progress = 6.0f;
            state.time = 2.0f;
            Settle();
            // Drawn in the scrubber's own space: 0 is the padding box's left, the start.
            const IntRect range = ScrubberRange();
            const float perSecond = float(range.w) / 8.0f;
            const StubGUIRenderer drawn = Draw();

            const std::vector<IntRect> ready = BoxesIn(drawn, app.theme.timelineReadyStyleRef);
            ASSERT_EQ(ready.size(), 1u);
            EXPECT_LT(ready[0].x, 0) << "from the scrubber's own start, before the padding";
            EXPECT_NEAR(float(ready[0].x + ready[0].w), 6.0f * perSecond, 1.0f);

            const std::vector<IntRect> lines = BoxesIn(drawn, app.theme.timelineMarkerStyleRef);
            ASSERT_EQ(lines.size(), 2u) << "only the two inside the length";
            EXPECT_NEAR(float(lines[0].x) + float(lines[0].w) / 2.0f, 1.5f * perSecond, 1.0f) << "in order";
            EXPECT_NEAR(float(lines[1].x) + float(lines[1].w) / 2.0f, 5.2f * perSecond, 1.0f);

            const std::vector<IntRect> head = BoxesIn(drawn, app.theme.timelinePlayheadStyleRef);
            ASSERT_EQ(head.size(), 2u) << "the line and its head";
            EXPECT_NEAR(float(head[0].x) + float(head[0].w) / 2.0f, 2.0f * perSecond, 1.0f);
            EXPECT_EQ(head[0].h, range.h);
            EXPECT_LT(head[1].h, range.h);

            // Major ticks one of the usual steps apart, at least 48px, from the start to the
            // end; three minor ones between.
            const std::vector<IntRect> major = BoxesIn(drawn, app.theme.timelineTickMajorStyleRef);
            ASSERT_GE(major.size(), 2u);
            const float step = float(major[1].x - major[0].x) / perSecond;
            EXPECT_GE(major[1].x - major[0].x, app.styleManager.Px(48));
            EXPECT_TRUE(std::fabs(step - 0.25f) < 0.02f || std::fabs(step - 0.5f) < 0.02f || std::fabs(step - 1.0f) < 0.02f) << step;
            EXPECT_EQ(major.size(), size_t(std::lround(8.0f / step)) + 1);
            EXPECT_EQ(major.front().x, 0);
            EXPECT_NEAR(float(major.back().x), float(range.w), 1.0f);
            EXPECT_EQ(BoxesIn(drawn, app.theme.timelineTickStyleRef).size(), (major.size() - 1) * 3);

            // Without progress, no fill.
            options.progress = -1.0f;
            Settle();
            EXPECT_TRUE(BoxesIn(Draw(), app.theme.timelineReadyStyleRef).empty());
        }

        // Fewer, farther apart ticks where a second is too short to tick.
        TEST_F(TimelineTest, LongerTimelinesTickLessOften)
        {
            options.duration = 3600.0f;
            Settle();
            const std::vector<IntRect> major = BoxesIn(Draw(), app.theme.timelineTickMajorStyleRef);
            ASSERT_GE(major.size(), 2u);
            EXPECT_LE(major.size(), 61u);
            EXPECT_GE(major[1].x - major[0].x, app.styleManager.Px(48));
        }

        // ---------------------------------------------------------------------
        // AdvanceTimeline
        // ---------------------------------------------------------------------

        TEST(AdvanceTimelineTest, ItMovesOnWhilePlaying)
        {
            const TimelineOptions options{ .duration = 2.0f };
            TimelineState state{ .time = 0.5f };
            EXPECT_FALSE(AdvanceTimeline(state, options, 0.25f)) << "paused";
            EXPECT_EQ(state.time, 0.5f);
            state.playing = true;
            EXPECT_TRUE(AdvanceTimeline(state, options, 0.25f));
            EXPECT_EQ(state.time, 0.75f);
            EXPECT_FALSE(AdvanceTimeline(state, options, 0.0f));
            EXPECT_FALSE(AdvanceTimeline(state, options, -1.0f));
            EXPECT_FALSE(AdvanceTimeline(state, options, std::numeric_limits<float>::quiet_NaN()));
            EXPECT_FALSE(AdvanceTimeline(state, options, std::numeric_limits<float>::infinity()));
            EXPECT_EQ(state.time, 0.75f);
        }

        TEST(AdvanceTimelineTest, AtTheEndItStopsOrLoops)
        {
            const TimelineOptions options{ .duration = 2.0f };
            TimelineState state{ .time = 1.75f, .playing = true };
            EXPECT_TRUE(AdvanceTimeline(state, options, 0.5f));
            EXPECT_EQ(state.time, 2.0f);
            EXPECT_FALSE(state.playing);

            state = { .time = 1.75f, .playing = true, .loop = true };
            EXPECT_TRUE(AdvanceTimeline(state, options, 0.5f));
            EXPECT_FLOAT_EQ(state.time, 0.25f);
            EXPECT_TRUE(state.playing);
            EXPECT_TRUE(AdvanceTimeline(state, options, 4.5f));
            EXPECT_FLOAT_EQ(state.time, 0.75f) << "round as often as it takes";

            // Looping round a loop range: from its end back to its start, once the playhead is
            // in it; played past it, the timeline's end goes back to its start.
            const TimelineOptions ranged{ .duration = 8.0f, .loopRange = { 2.0f, 4.0f } };
            state = { .time = 3.5f, .playing = true, .loop = true };
            EXPECT_TRUE(AdvanceTimeline(state, ranged, 1.0f));
            EXPECT_FLOAT_EQ(state.time, 2.5f);
            state = { .time = 1.0f, .playing = true, .loop = true };
            AdvanceTimeline(state, ranged, 2.0f);
            EXPECT_FLOAT_EQ(state.time, 3.0f) << "into it from before";
            state = { .time = 7.5f, .playing = true, .loop = true };
            AdvanceTimeline(state, ranged, 1.0f);
            EXPECT_FLOAT_EQ(state.time, 2.0f) << "past it: at the end, back to its start";
            state = { .time = 3.5f, .playing = true, .loop = false };
            AdvanceTimeline(state, ranged, 1.0f);
            EXPECT_FLOAT_EQ(state.time, 4.5f) << "not looping: the range doesn't matter";
            const TimelineOptions outside{ .duration = 8.0f, .loopRange = { 9.0f, 12.0f } };
            state = { .time = 7.5f, .playing = true, .loop = true };
            AdvanceTimeline(state, outside, 1.0f);
            EXPECT_FLOAT_EQ(state.time, 0.5f) << "a range outside the length: the whole length";

            // No length: it stops at 0, looping or not.
            state = { .time = 0.0f, .playing = true, .loop = true };
            EXPECT_FALSE(AdvanceTimeline(state, TimelineOptions{}, 0.5f));
            EXPECT_EQ(state.time, 0.0f);
            EXPECT_FALSE(state.playing);
        }
    }
}
