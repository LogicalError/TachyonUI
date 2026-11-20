// Redrawing only when needed: the window backend waiting for events and woken from another
// thread, and the application's redraw modes, requests and timers.

#include <gtest/gtest.h>

#include <gui_controls/gui_application_internal.h>
#include <gui_core/graphics/gui_primitive_renderer.h>
#include <gui_core/graphics/null/gui_null_graphics_context.h>
#include <gui_core/windows/null/gui_null_window_backend.h>

#include "fake_clock.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>
#include <thread>

namespace tachyon
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        float SecondsSince(Clock::time_point start)
        {
            return std::chrono::duration<float>(Clock::now() - start).count();
        }

        // ---------------------------------------------------------------------
        // The window backend
        // ---------------------------------------------------------------------

        // Nothing happening: the wait lasts its timeout.
        TEST(RedrawBackend, AWaitLastsItsTimeout)
        {
            NullWindowBackend backend;
            const uint64_t events = backend.GetEventCount();
            const Clock::time_point start = Clock::now();
            backend.WaitForEvents(0.03f);
            EXPECT_GE(SecondsSince(start), 0.025f);
            EXPECT_EQ(backend.GetEventCount(), events) << "nothing happened";
        }

        // Wake from another thread ends the wait early, and counts as an event.
        TEST(RedrawBackend, WakeEndsAWaitFromAnotherThread)
        {
            NullWindowBackend backend;
            const uint64_t events = backend.GetEventCount();
            std::thread waker([&backend] {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                backend.Wake();
            });
            const Clock::time_point start = Clock::now();
            backend.WaitForEvents(10.0f);
            waker.join();
            EXPECT_LT(SecondsSince(start), 5.0f) << "woken, not timed out";
            EXPECT_GT(backend.GetEventCount(), events);
        }

        // A wake before the wait isn't lost: the wait returns at once.
        TEST(RedrawBackend, AWakeBeforeTheWaitIsKept)
        {
            NullWindowBackend backend;
            backend.Wake();
            const Clock::time_point start = Clock::now();
            backend.WaitForEvents(10.0f);
            EXPECT_LT(SecondsSince(start), 5.0f);

            // Taken by that wait: the next one waits again.
            const Clock::time_point again = Clock::now();
            backend.WaitForEvents(0.02f);
            EXPECT_GE(SecondsSince(again), 0.015f);
        }

        // No timeout at all: zero returns at once.
        TEST(RedrawBackend, AZeroTimeoutReturnsAtOnce)
        {
            NullWindowBackend backend;
            const Clock::time_point start = Clock::now();
            backend.WaitForEvents(0.0f);
            EXPECT_LT(SecondsSince(start), 1.0f);
        }

        // SleepFor sleeps its time: a Wake doesn't end it, as it does a WaitForEvents.
        TEST(RedrawBackend, SleepForIsNotEndedByAWake)
        {
            NullWindowBackend backend;
            backend.Wake();
            const Clock::time_point start = Clock::now();
            backend.SleepFor(0.02f);
            EXPECT_GE(SecondsSince(start), 0.015f);
        }

        // Short sleeps and waits take their time however long the machine has been up: a
        // steady-clock time in nanoseconds, days in, is too big for a float to add 5 ms to.
        TEST(RedrawBackend, ShortSleepsAndWaitsTakeTheirTime)
        {
            NullWindowBackend backend;
            const Clock::time_point start = Clock::now();
            for (int i = 0; i < 10; ++i)
                backend.SleepFor(0.005f);
            EXPECT_GE(SecondsSince(start), 0.04f) << "ten sleeps of 5 ms";

            const Clock::time_point waits = Clock::now();
            for (int i = 0; i < 10; ++i)
                backend.WaitForEvents(0.005f);
            EXPECT_GE(SecondsSince(waits), 0.04f) << "ten waits of 5 ms";
        }

        // ---------------------------------------------------------------------
        // The renderer: whether a window's draws changed
        // ---------------------------------------------------------------------

        // A window drawn the same twice is unchanged; a box moved changes it.
        TEST(RedrawRenderer, TheSameDrawsAreUnchanged)
        {
            PrimitiveRenderer renderer;
            const ID window = ID{ 7 };
            auto frame = [&](int32_t x) {
                renderer.Reset();
                ASSERT_TRUE(renderer.BeginWindow(window));
                renderer.RenderBox(IntRect{ x, 10, 40, 20 }, IntRect{ 0, 0, 200, 200 }, 1, 1, 1.0f);
                renderer.EndWindow();
            };
            frame(10);
            EXPECT_TRUE(renderer.DrawsChanged(window)) << "drawn the first time";
            frame(10);
            EXPECT_FALSE(renderer.DrawsChanged(window));
            frame(12);
            EXPECT_TRUE(renderer.DrawsChanged(window));
            EXPECT_TRUE(renderer.DrawsChanged(ID{ 8 })) << "never drawn: unknown";
        }

        // A box's shares go to the GPU in 8 bits each and always add up to all of it.
        TEST(RedrawRenderer, ABoxsSharesAddUpToAll)
        {
            auto packed = [](const StyleBlend& blend) {
                PrimitiveRenderTarget target;
                target.RenderBox(IntRect{ 0, 0, 10, 10 }, IntRect{ 0, 0, 100, 100 }, blend);
                const auto& bytes = target.streams[size_t(PrimitiveKind::Box)].cpu;
                BoxPrimitive box;
                EXPECT_EQ(bytes.size(), sizeof(BoxPrimitive));
                if (bytes.size() == sizeof(BoxPrimitive))
                    std::memcpy(&box, bytes.data(), sizeof(box));
                return box;
            };
            auto total = [](const BoxPrimitive& box) {
                int sum = 0;
                for (const uint32_t slot : box.styles)
                    sum += int(slot >> 24);
                return sum;
            };
            StyleBlend thirds;
            thirds.count   = 3;
            thirds.styles  = { 5, 6, 7, 0 };
            thirds.weights = { 1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f, 0.0f };
            const BoxPrimitive three = packed(thirds);
            EXPECT_EQ(total(three), 255);
            EXPECT_EQ(three.styles[0] & 0xFFFFFFu, 5u);
            EXPECT_EQ(three.styles[2] & 0xFFFFFFu, 7u);
            EXPECT_EQ(three.styles[3], 0u) << "unused";
            EXPECT_EQ(total(packed(StyleBlend::Between(5, 6, 0.5f))), 255) << "two halves round up to 256 each way";
        }

        // A cached scope captured again with the same draws doesn't change its window; with
        // others, it does, though its replay is the same.
        TEST(RedrawRenderer, ACaptureTakenAgainChangesOnlyWhenItsDrawsDo)
        {
            PrimitiveRenderer renderer;
            const ID window = ID{ 7 }, scope = ID{ 9 };
            auto frame = [&](int32_t x) {
                renderer.Reset();
                ASSERT_TRUE(renderer.BeginWindow(window));
                renderer.BeginScopeCapture(scope);
                renderer.RenderBox(IntRect{ x, 10, 40, 20 }, IntRect{ 0, 0, 200, 200 }, 1, 1, 1.0f);
                renderer.EndScopeCapture(scope);
                renderer.EndWindow();
            };
            frame(10);
            frame(10);
            EXPECT_FALSE(renderer.DrawsChanged(window)) << "captured again, the same";
            frame(30);
            EXPECT_TRUE(renderer.DrawsChanged(window));
        }

        // ---------------------------------------------------------------------
        // The application
        // ---------------------------------------------------------------------

        // A window with a text that can change, and a button with a tooltip; a spinner when asked.
        class RedrawApp : public GUIApplicationInternals
        {
        public:
            ID          windowID = ID::Invalid();
            ID          buttonID = ID::Invalid();
            ID          textID   = ID::Invalid();   // has a tooltip, and no hover look of its own
            int2        textCenter{ 0, 0 };
            std::string text     = "Hello";
            bool        spinner  = false;
            bool        still    = false;   // an element marked Animated that looks the same each frame
            bool        cached   = false;   // the button in a cached scope, as a docked panel's
            int         builds   = 0;
            int         quitAfter = 0;            // quit at this many builds; 0: never
            std::atomic<bool> quitSoon = false;   // set from another thread: quit at the next build
            int2        buttonCenter{ 0, 0 };

            void OnGUI() override
            {
                ++builds;
                if (BeginWindow(windowID, "Redraw", theme.windowStyleRef, LayoutOverride{}.Size(320, 160),
                                ElementFlags::WindowRenderToImage))
                {
                    BeginVertical(LayoutOverride{}.FlexGrow(1));
                    Text(textID, text);
                    SetTooltip(textID, "The text's tooltip");
                    if (!cached || BeginCached(idManager.AllocateDynamicID("panel"), theme.emptyVertStyleRef, 0, LayoutOverride{}.Size(200, 60)))
                    {
                        Button(buttonID, "Button", [] {});
                        SetTooltip(buttonID, "A tooltip");
                    }
                    if (cached)
                        EndCached();
                    if (spinner)
                        Spinner(idManager.AllocateDynamicID("spinner"));
                    if (still)
                        RegisterElement(Element{ .id = idManager.AllocateDynamicID("still"), .styleRef = theme.emptyHorzStyleRef,
                                                 .flags = ElementFlags::NoInput | ElementFlags::Animated },
                                        LayoutOverride::FixedSize(Pixels{ 10 }, Pixels{ 10 }));
                    EndVertical();
                }
                EndWindow();
                if (quitSoon || (quitAfter > 0 && builds >= quitAfter))
                    applicationQuit = true;
            }

            // `refreshHz`: the monitor's; 0 is unknown.
            bool Start(float refreshHz = 0.0f)
            {
                SetGraphicsBackend(std::make_unique<NullGraphicsContext>());
                SetWindowBackend(std::make_unique<NullWindowBackend>(IntRect{ 0, 0, 1920, 1080 }, refreshHz));
                if (!Initialize())
                    return false;
                windowID = idManager.CreateStaticID();
                buttonID = idManager.CreateStaticID();
                textID   = idManager.CreateStaticID();
                m_frame.OnPostLayout([this](FrameContext&)
                {
                    if (const ElementRef text = elementStorage.FindElementRefByID(textID); text.IsValid())
                    {
                        const IntRect t = elementStorage.GetElementRectPtr(text)->rect;
                        textCenter = { t.x + t.w / 2, t.y + t.h / 2 };
                    }
                    const ElementRef ref = elementStorage.FindElementRefByID(buttonID);
                    if (!ref.IsValid()) return;
                    const IntRect r = elementStorage.GetElementRectPtr(ref)->rect;
                    buttonCenter = { r.x + r.w / 2, r.y + r.h / 2 };
                });
                return true;
            }

            // One frame, as a host's loop runs it.
            void Frame()
            {
                ASSERT_TRUE(UpdateFrame());
                ASSERT_TRUE(RecordFrame());
                PresentFrame();
            }
            // Frames until nothing more is needed (at most `most`).
            int Settle(int most = 20)
            {
                int n = 0;
                while (n < most && NeedsFrame())
                {
                    Frame();
                    ++n;
                }
                return n;
            }
            double FrameClockSeconds() const { return m_frameClock.Seconds(); }
            uint64_t Recorded() const
            {
                return static_cast<const NullGraphicsContext*>(graphicsContext)->RecordedFrames(windowID);
            }
        };

        class RedrawTest : public ::testing::Test
        {
        protected:
            RedrawApp app;
            void SetUp() override { ASSERT_TRUE(app.Start()); }
            void TearDown() override { app.Cleanup(); }
        };

        TEST_F(RedrawTest, WaitingIsTheDefault)
        {
            EXPECT_EQ(app.GetRedrawMode(), RedrawMode::WhenNeeded);
        }

        // A UI that doesn't change: a few frames to settle, then none needed, and one built
        // anyway draws nothing.
        TEST_F(RedrawTest, AStillUISettlesAndStops)
        {
            EXPECT_TRUE(app.NeedsFrame()) << "the first frame";
            const int frames = app.Settle();
            EXPECT_GT(frames, 0);
            EXPECT_LT(frames, 20) << "settled";
            EXPECT_FALSE(app.NeedsFrame());
            EXPECT_EQ(app.SecondsUntilFrame(), INFINITY) << "nothing scheduled";

            const uint64_t recorded = app.Recorded();
            app.Frame();
            EXPECT_EQ(app.Recorded(), recorded) << "the same draws: not recorded again";
        }

        // A change made while building is seen: drawn, then settled again.
        TEST_F(RedrawTest, ChangedDrawsAreDrawnAndAskForAnotherFrame)
        {
            app.Settle();
            const uint64_t recorded = app.Recorded();
            app.text = "Changed";
            app.RequestRedraw();
            EXPECT_TRUE(app.NeedsFrame());
            app.Frame();
            EXPECT_EQ(app.Recorded(), recorded + 1);
            EXPECT_TRUE(app.NeedsFrame()) << "the draws changed: one more to see they settled";
            app.Settle();
            EXPECT_FALSE(app.NeedsFrame());
        }

        // Always: every frame drawn, and always one needed.
        TEST_F(RedrawTest, AlwaysDrawsEveryFrame)
        {
            app.SetRedrawMode(RedrawMode::Always);
            app.Frame();
            app.Frame();
            const uint64_t recorded = app.Recorded();
            app.Frame();
            app.Frame();
            EXPECT_EQ(app.Recorded(), recorded + 2);
            EXPECT_TRUE(app.NeedsFrame());
        }

        // WhenChanged: every frame built, but only changed draws recorded.
        TEST_F(RedrawTest, WhenChangedBuildsEveryFrameAndDrawsOnlyChanges)
        {
            app.SetRedrawMode(RedrawMode::WhenChanged);
            for (int i = 0; i < 6; ++i)
                app.Frame();
            EXPECT_TRUE(app.NeedsFrame()) << "it builds every frame";
            const uint64_t recorded = app.Recorded();
            app.Frame();
            EXPECT_EQ(app.Recorded(), recorded);
            app.text = "Changed";
            app.Frame();
            EXPECT_EQ(app.Recorded(), recorded + 1);
        }

        // Input asks for a frame.
        TEST_F(RedrawTest, InputAsksForAFrame)
        {
            app.Settle();
            app.InjectMouseMove(app.windowID, { 5, 5 });
            EXPECT_TRUE(app.NeedsFrame());
            app.Settle();
            EXPECT_FALSE(app.NeedsFrame());
        }

        // A held mouse button keeps frames coming (auto-scroll, drags without moves).
        TEST_F(RedrawTest, AHeldButtonKeepsFramesComing)
        {
            app.Settle();
            app.InjectMouseButton(app.windowID, { 5, 5 }, MouseButtons::Left, true);
            for (int i = 0; i < 5; ++i)
                app.Frame();
            EXPECT_TRUE(app.NeedsFrame());
            app.InjectMouseButton(app.windowID, { 5, 5 }, MouseButtons::Left, false);
            EXPECT_LT(app.Settle(), 20);
            EXPECT_FALSE(app.NeedsFrame());
        }

        // A hover fades in: frames while it runs, then none.
        TEST_F(RedrawTest, ATransitionKeepsFramesComingUntilItEnds)
        {
            GUITest::ScopedFakeClock clock;
            app.Settle();
            app.InjectMouseMove(app.windowID, app.buttonCenter);
            app.Frame();
            app.Frame();
            EXPECT_TRUE(app.NeedsFrame()) << "the hover fading in";
            clock.Advance(std::chrono::milliseconds(200));   // past kHoverIn
            app.Frame();
            app.Frame();
            app.Frame();
            EXPECT_GT(app.SecondsUntilFrame(), 0.0f) << "the tooltip's delay, not a transition";
        }

        // Something that moves every frame keeps frames coming.
        TEST_F(RedrawTest, AnAnimatedElementKeepsFramesComing)
        {
            app.spinner = true;
            for (int i = 0; i < 25; ++i)
                app.Frame();
            EXPECT_TRUE(app.NeedsFrame());
            app.spinner = false;
            app.RequestRedraw();
            app.Settle();
            EXPECT_FALSE(app.NeedsFrame());
        }

        // A toast shown or dismissed (from a click's handler, after the build) asks for a frame
        // to draw it.
        TEST_F(RedrawTest, AToastAsksForAFrame)
        {
            app.Settle();
            const ToastID toast = app.ShowToast(ToastDesc{ .title = "Saved" });
            EXPECT_TRUE(app.NeedsFrame());
            app.Settle();
            EXPECT_FALSE(app.NeedsFrame());
            app.DismissToast(toast);
            EXPECT_TRUE(app.NeedsFrame());
        }

        // A redraw asked for later: none needed until then.
        TEST_F(RedrawTest, ARedrawLater)
        {
            GUITest::ScopedFakeClock clock;
            app.Settle();
            app.RequestRedrawIn(0.05f);
            EXPECT_FALSE(app.NeedsFrame());
            EXPECT_FLOAT_EQ(app.SecondsUntilFrame(), 0.05f);
            clock.Advance(std::chrono::milliseconds(49));
            EXPECT_FALSE(app.NeedsFrame()) << "not yet";
            clock.Advance(std::chrono::milliseconds(1));
            EXPECT_TRUE(app.NeedsFrame());
            app.Frame();
            EXPECT_EQ(app.SecondsUntilFrame(), INFINITY) << "done with";
        }

        // From another thread: a redraw asked for wakes a waiting backend.
        TEST_F(RedrawTest, ARedrawFromAnotherThreadWakesTheBackend)
        {
            app.Settle();
            std::thread worker([this] {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                app.RequestRedraw();
            });
            const Clock::time_point start = Clock::now();
            app.windowBackend->WaitForEvents(10.0f);
            worker.join();
            EXPECT_LT(SecondsSince(start), 5.0f);
            EXPECT_TRUE(app.NeedsFrame());
        }

        // The tooltip's delay is a time a frame is needed at.
        TEST_F(RedrawTest, ATooltipsDelayIsScheduled)
        {
            GUITest::ScopedFakeClock clock;
            app.Settle();
            app.InjectMouseMove(app.windowID, app.buttonCenter);
            app.Frame();
            clock.Advance(std::chrono::milliseconds(200));   // the hover's done
            app.Settle();
            const float until = app.SecondsUntilFrame();
            EXPECT_GT(until, 0.0f);
            EXPECT_LE(until, app.tooltipManager.delaySeconds);
            // ... not before it's up, and once it is, a frame is needed.
            clock.AdvanceSeconds(double(until) * 0.9);
            EXPECT_FALSE(app.NeedsFrame());
            clock.AdvanceSeconds(double(until) * 0.1 + 0.001);
            EXPECT_TRUE(app.NeedsFrame());
        }

        // Left to itself, as Run is (asleep until a frame is needed), the app shows a first
        // tooltip after its delay, not much later: in a window, and in a cached panel.
        class RedrawTooltipTest : public RedrawTest, public ::testing::WithParamInterface<bool> {};
        INSTANTIATE_TEST_SUITE_P(InACachedPanel, RedrawTooltipTest, ::testing::Bool());
        TEST_P(RedrawTooltipTest, AFirstTooltipShowsAfterItsDelay)
        {
            app.cached = GetParam();
            app.SetDrawCacheEnabled(GetParam());
            auto shown = [&] {
                for (auto it = app.elementStorage.TopDownDepthScanBegin(); it != app.elementStorage.TopDownDepthScanEnd(); ++it)
                    for (const Element& e : it->GetChildren(app.elementStorage))
                        if (e.textID != TextID::Invalid && app.textSystem->GetText(e.textID) == "A tooltip")
                            return true;
                return false;
            };
            // Run's loop on a clock moved by hand: asleep until a frame is needed, and each
            // frame a 60 Hz refresh long.
            GUITest::ScopedFakeClock clock;
            app.Settle();
            app.InjectMouseMove(app.windowID, app.buttonCenter);
            const int64_t start = clock.Now();
            const auto elapsed = [&] { return double(clock.Now() - start) * 1.0e-9; };
            const float delay = app.tooltipManager.delaySeconds;
            double took = -1.0;
            for (int frames = 0; frames < 10'000 && elapsed() < double(delay) * 4.0; ++frames)
            {
                for (int naps = 0; naps < 100 && !app.NeedsFrame(); ++naps)
                {
                    const float until = app.SecondsUntilFrame();
                    if (!std::isfinite(until)) break;
                    clock.Advance(std::chrono::nanoseconds(std::max<int64_t>(int64_t(double(until) * 1.0e9), 1)));
                }
                if (!app.NeedsFrame()) break;   // nothing will ever come
                app.Frame();
                clock.AdvanceSeconds(1.0 / 60.0);
                if (shown())
                {
                    took = elapsed();
                    break;
                }
            }
            ASSERT_GE(took, 0.0) << "the tooltip never showed";
            EXPECT_GE(took, double(delay) * 0.9);
            EXPECT_LE(took, double(delay) + 0.25) << "it waited much longer than its delay";
        }

        // The pointer coming to rest on something with a tooltip but no hover look of its own:
        // the frame that finds it there asks for the next, which starts the tooltip's wait, and
        // that schedules the frame it shows in. Without that the app sleeps until some other
        // event, and the tooltip comes seconds late.
        TEST_F(RedrawTest, ATooltipsWaitStartsWithoutAHoverLook)
        {
            app.Settle();
            app.InjectMouseMove(app.windowID, app.textCenter);
            app.Settle();
            ASSERT_EQ(app.guiStateManager.Interaction().GetHoveredID(), app.textID) << "test setup: the text is pointed at";
            EXPECT_EQ(app.tooltipManager.GetActiveTargetID(), app.textID) << "its wait has started";
            const float until = app.SecondsUntilFrame();
            EXPECT_TRUE(std::isfinite(until)) << "a frame is scheduled for when it shows";
            EXPECT_LE(until, app.tooltipManager.delaySeconds);
        }

        // A new popup (a first tooltip) is drawn a few frames before it shows: those frames are
        // needed, or the app sleeps with it hidden until the pointer moves again.
        TEST_F(RedrawTest, ANewPopupGetsTheFramesItWaitsToShow)
        {
            app.tooltipManager.delaySeconds = 0.0f;
            auto shown = [&] {
                for (auto it = app.elementStorage.TopDownDepthScanBegin(); it != app.elementStorage.TopDownDepthScanEnd(); ++it)
                    for (const Element& e : it->GetChildren(app.elementStorage))
                        if (e.textID != TextID::Invalid && app.textSystem->GetText(e.textID) == "The text's tooltip")
                            return true;
                return false;
            };
            app.Settle();
            app.InjectMouseMove(app.windowID, app.textCenter);
            for (int i = 0; i < 10 && !shown(); ++i)
                app.Frame();
            ASSERT_TRUE(shown()) << "test setup: the tooltip is built";
            EXPECT_TRUE(app.NeedsFrame()) << "its new window waits a few frames to show";
            app.Settle();
            EXPECT_FALSE(app.windowManager->IsFading()) << "shown by the time the frames stop";
        }

        // An OS event (here a wake) asks for a frame; the frame takes it, so none after.
        TEST_F(RedrawTest, AnEventAsksForAFrame)
        {
            app.Settle();
            app.windowBackend->Wake();
            EXPECT_TRUE(app.NeedsFrame());
            EXPECT_LT(app.Settle(), 20);
            EXPECT_FALSE(app.NeedsFrame()) << "seen by the frame";
        }

        // A redraw asked for now and one for later: the frame answers the first, the later one stays.
        TEST_F(RedrawTest, AFrameAnswersOnlyTheRequestsDue)
        {
            app.Settle();
            app.RequestRedraw();
            app.RequestRedrawIn(5.0f);
            app.Settle();
            EXPECT_GT(app.SecondsUntilFrame(), 4.0f);
            EXPECT_LE(app.SecondsUntilFrame(), 5.0f);
        }

        // Something marked Animated keeps frames coming though it looks the same each frame.
        TEST_F(RedrawTest, AnAnimatedElementKeepsFramesComingAsItIs)
        {
            app.still = true;
            for (int i = 0; i < 25; ++i)
                app.Frame();
            EXPECT_TRUE(app.NeedsFrame());
            EXPECT_TRUE(app.elementStorage.HasAnimated());
            app.still = false;
            app.RequestRedraw();
            app.Settle();
            EXPECT_FALSE(app.NeedsFrame());
        }

        // A transition running asks for another frame by itself; with none, the frame doesn't.
        TEST_F(RedrawTest, ARunningTransitionAsksForAnotherFrame)
        {
            app.Settle();
            ASSERT_FALSE(app.m_moreFrames);
            const ID id{ 42 };
            app.transitionManager.OnStateChange(id, ElementState::None, ElementState::Hover, 0.0f);
            app.NoteFrameNeeds(0.0f);
            EXPECT_TRUE(app.m_moreFrames);
            app.transitionManager.Clear();
            app.NoteFrameNeeds(0.0f);
            EXPECT_FALSE(app.m_moreFrames);
        }

        // Run(): a still UI doesn't spin; a request from another thread wakes it.
        TEST_F(RedrawTest, RunSleepsUntilWoken)
        {
            std::thread worker([this] {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                app.quitSoon = true;
                app.RequestRedraw();
            });
            EXPECT_TRUE(app.Run());
            worker.join();
            EXPECT_LT(app.builds, 30) << "not a frame every pass while idle";
        }

        // Runs until `frames` frames are built, and returns how long that took in seconds. A
        // watchdog quits it after 10 s, so a Run that stops building fails instead of hanging.
        // Counted in frames, not time: a slow machine makes it take longer, never fewer frames.
        double RunFrames(RedrawApp& app, int frames)
        {
            app.quitAfter = frames;
            std::atomic<bool> done = false;
            std::thread watchdog([&] {
                const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
                while (!done && std::chrono::steady_clock::now() < until)
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                if (!done)
                {
                    app.quitSoon = true;
                    app.RequestRedraw();
                }
            });
            const auto start = std::chrono::steady_clock::now();
            EXPECT_TRUE(app.Run());
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            done = true;
            watchdog.join();
            return seconds;
        }
        // At least this long for `frames` frames, one a 60 Hz refresh (the first right away),
        // with room for a timer that wakes a little early.
        constexpr double PacedSeconds(int frames) { return double(frames - 1) / 60.0 * 0.75; }

        // Run() in WhenChanged: a frame each refresh even when nothing is presented, not as
        // fast as it can.
        TEST_F(RedrawTest, RunWhenChangedPacesItself)
        {
            app.SetRedrawMode(RedrawMode::WhenChanged);
            const double seconds = RunFrames(app, 10);
            EXPECT_EQ(app.builds, 10) << "it keeps building";
            EXPECT_GE(seconds, PacedSeconds(10)) << "about one a refresh, not as fast as it can";
        }

        // Run() with presents that don't wait for vsync (none here): Always draws a frame each
        // refresh, not as fast as it can.
        TEST_F(RedrawTest, RunAlwaysPacesItselfToTheRefresh)
        {
            app.SetRedrawMode(RedrawMode::Always);
            const double seconds = RunFrames(app, 10);
            EXPECT_EQ(app.builds, 10) << "it keeps drawing";
            EXPECT_GE(seconds, PacedSeconds(10)) << "about one a refresh (60 Hz without a known display)";
            // The wait is in the profiled frame and in frameTime, beside the build.
            EXPECT_GT(app.frameTime, 0.010f) << "about a 60 Hz period";
            EXPECT_LT(app.frameBuildTime, app.frameTime);
        }

        // Presents that return at once (the null backends') aren't locked to the vsync, so the
        // animation clock follows the wall clock, not the 60 Hz monitor's whole periods.
        TEST(RedrawClock, WithoutVsyncLockTheClockFollowsTheWallClock)
        {
            RedrawApp app;
            ASSERT_TRUE(app.Start(60.0f));
            app.SetRedrawMode(RedrawMode::Always);
            const double period = 1.0 / 60.0;
            bool offTheGrid = false;
            for (int i = 0; i < 6; ++i)
            {
                app.Frame();
                const double periods = app.FrameClockSeconds() / period;
                offTheGrid = offTheGrid || std::abs(periods - std::round(periods)) * period > 0.0005;
                std::this_thread::sleep_for(std::chrono::milliseconds(3));
            }
            EXPECT_TRUE(offTheGrid) << "every frame's time a whole 60 Hz period: snapped";
            app.Cleanup();
        }

        // Something animating in WhenNeeded asks for a frame every frame: still one a refresh.
        TEST_F(RedrawTest, RunPacesAnAnimationToTheRefresh)
        {
            app.spinner = true;
            const double seconds = RunFrames(app, 10);
            EXPECT_EQ(app.builds, 10) << "it keeps drawing";
            EXPECT_GE(seconds, PacedSeconds(10)) << "about one a refresh (60 Hz without a known display)";
        }
    }
}
