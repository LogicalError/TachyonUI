// Redrawing only when needed: the window backend waiting for events and woken from another
// thread, and the application's redraw modes, requests and timers.

#include <gtest/gtest.h>

#include <gui_controls/gui_application_internal.h>
#include <gui_core/graphics/gui_primitive_renderer.h>
#include <gui_core/graphics/null/gui_null_graphics_context.h>
#include <gui_core/windows/null/gui_null_window_backend.h>

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
            std::string text     = "Hello";
            bool        spinner  = false;
            bool        still    = false;   // an element marked Animated that looks the same each frame
            int         builds   = 0;
            std::atomic<bool> quitSoon = false;   // set from another thread: quit at the next build
            int2        buttonCenter{ 0, 0 };

            void OnGUI() override
            {
                ++builds;
                if (BeginWindow(windowID, "Redraw", theme.windowStyleRef, LayoutOverride{}.Size(320, 160),
                                ElementFlags::WindowRenderToImage))
                {
                    BeginVertical(LayoutOverride{}.FlexGrow(1));
                    Text(idManager.AllocateDynamicID("text"), text);
                    Button(buttonID, "Button", [] {});
                    SetTooltip(buttonID, "A tooltip");
                    if (spinner)
                        Spinner(idManager.AllocateDynamicID("spinner"));
                    if (still)
                        RegisterElement(Element{ .id = idManager.AllocateDynamicID("still"), .styleRef = theme.emptyHorzStyleRef,
                                                 .flags = ElementFlags::NoInput | ElementFlags::Animated },
                                        LayoutOverride::FixedSize(Pixels{ 10 }, Pixels{ 10 }));
                    EndVertical();
                }
                EndWindow();
                if (quitSoon)
                    applicationQuit = true;
            }

            bool Start()
            {
                SetGraphicsBackend(std::make_unique<NullGraphicsContext>());
                SetWindowBackend(std::make_unique<NullWindowBackend>());
                if (!Initialize())
                    return false;
                windowID = idManager.CreateStaticID();
                buttonID = idManager.CreateStaticID();
                m_frame.OnPostLayout([this](FrameContext&)
                {
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
            app.Settle();
            app.InjectMouseMove(app.windowID, app.buttonCenter);
            app.Frame();
            app.Frame();
            EXPECT_TRUE(app.NeedsFrame()) << "the hover fading in";
            std::this_thread::sleep_for(std::chrono::milliseconds(200));   // past kHoverIn
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
            app.Settle();
            app.RequestRedrawIn(0.05f);
            EXPECT_FALSE(app.NeedsFrame());
            EXPECT_GT(app.SecondsUntilFrame(), 0.0f);
            EXPECT_LE(app.SecondsUntilFrame(), 0.05f);
            std::this_thread::sleep_for(std::chrono::milliseconds(60));
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
            app.Settle();
            app.InjectMouseMove(app.windowID, app.buttonCenter);
            app.Frame();
            std::this_thread::sleep_for(std::chrono::milliseconds(200));   // the hover's done
            app.Settle();
            const float until = app.SecondsUntilFrame();
            EXPECT_GT(until, 0.0f);
            EXPECT_LE(until, app.tooltipManager.delaySeconds);
            // ... and once it's up, a frame is needed.
            std::this_thread::sleep_for(std::chrono::duration<float>(until + 0.02f));
            EXPECT_TRUE(app.NeedsFrame());
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

        // Run() in WhenChanged: a frame each refresh even when nothing is presented, not as
        // fast as it can.
        TEST_F(RedrawTest, RunWhenChangedPacesItself)
        {
            app.SetRedrawMode(RedrawMode::WhenChanged);
            std::thread worker([this] {
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
                app.quitSoon = true;
            });
            EXPECT_TRUE(app.Run());
            worker.join();
            EXPECT_GT(app.builds, 3) << "it keeps building";
            EXPECT_LT(app.builds, 60) << "about one a refresh";
        }
    }
}
