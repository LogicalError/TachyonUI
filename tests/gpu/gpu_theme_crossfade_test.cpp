// A theme switch crossfades on a real device: right after it, the window still shows the
// old theme (its last frame drawn over the new one), and 200 ms on only the new one. With
// reduced motion the new theme shows at once.

#include <gtest/gtest.h>

#include <gui_controls/gui_application_internal.h>

#include <chrono>
#include <cstdlib>
#include <optional>
#include <thread>
#include <vector>

namespace
{
    using namespace tachyon;

    class CrossfadeApp : public GUIApplicationInternals
    {
    public:
        ID windowID = ID::Invalid();
        int frame = 0;
        int switchAt = 4;
        int quitAt   = -1;                  // quit at this frame instead, mid-crossfade
        float fontScaleTo = 0.0f;           // above 0: switch only the font scale, not the palette
        std::optional<AccentColors> accentTo;   // set: switch only the accent
        ID barID = ID::Invalid();               // valid: a full ProgressBar in the window
        uint32_t barColor = 0;                  // its middle at the end, 0x00RRGGBB
        IntRect  barRect {};                    // where the last layout put it
        uint32_t before = 0, justAfter = 0, settled = 0;   // window background, 0x00RRGGBB
        bool crossfadingJustAfter = false, crossfadingAtEnd = true;

        // The pixel at `at` (window pixels) as last drawn, 0x00RRGGBB; the bottom-right corner
        // when not given.
        uint32_t Background(std::optional<int2> at = std::nullopt)
        {
            const IntRect rect = windowManager->GetWindowRect(windowID);
            std::vector<uint8_t> pixels(size_t(rect.w) * rect.h * 4);
            if (!ReadWindowPixels(windowID, pixels))
                return 0xFFFFFFFF;
            const int2 p = at.value_or(int2{ rect.w - 4, rect.h - 4 });
            const size_t o = (size_t(p.y) * rect.w + size_t(p.x)) * 4;
            return (uint32_t(pixels[o + 2]) << 16) | (uint32_t(pixels[o + 1]) << 8) | pixels[o];
        }


        void OnGUI() override
        {
            // Each read sees the frame before.
            if (frame == switchAt)
            {
                before = Background();
                if (accentTo)
                    SetAccent(accentTo);
                else if (fontScaleTo > 0.0f)
                    RequestReloadTheme(GetThemePath(), fontScaleTo);
                else
                    RequestReloadTheme("themes/light.css", 1.0f);
            }
            if (frame == quitAt)
                applicationQuit = true;
            if (frame == switchAt + 2)
            {
                justAfter = Background();
                crossfadingJustAfter = IsThemeCrossfading();
                std::this_thread::sleep_for(std::chrono::milliseconds(260));
            }
            if (frame == switchAt + 5)
            {
                settled = Background();
                if (barID != ID::Invalid())
                    barColor = Background(int2{ barRect.x + barRect.w / 2, barRect.y + barRect.h / 2 });
                crossfadingAtEnd = IsThemeCrossfading();
                applicationQuit = true;
            }
            if (BeginWindow(windowID, "Crossfade", theme.windowStyleRef, LayoutOverride{}.Size(200, 120),
                            ElementFlags::WindowRenderToImage))
            {
                if (barID != ID::Invalid())
                {
                    ProgressBar(barID, 1.0f, false, theme.progressBarStyleRef, LayoutOverride{}.Width(Pixels{ 160 }));
                    // Laid out once the frame is built; noted then.
                    VERIFY(elementStorage.AddPostLayout(elementStorage.FindElementRefByID(barID),
                                                        [this](PostLayoutContext& ctx) { barRect = ctx.ElementRect(); }));
                }
                BeginVertical(LayoutOverride{}.FlexGrow(1));
                EndVertical();
            }
            EndWindow();
            ++frame;
        }
    };

    int Distance(uint32_t a, uint32_t b)
    {
        return std::abs(int((a >> 16) & 0xFF) - int((b >> 16) & 0xFF)) +
               std::abs(int((a >> 8) & 0xFF) - int((b >> 8) & 0xFF)) +
               std::abs(int(a & 0xFF) - int(b & 0xFF));
    }

    TEST(GpuThemeCrossfade, ASwitchFadesFromTheOldTheme)
    {
        for (const bool reduced : { false, true })
        {
            CrossfadeApp app;
            if (!app.Initialize())
            {
                app.Cleanup();
                GTEST_SKIP() << "no Vulkan device available";
            }
            app.SetReducedMotion(reduced);
            app.windowID = app.idManager.CreateStaticID();
            ASSERT_TRUE(app.Run());
            ASSERT_GT(Distance(app.before, app.settled), 60) << "test setup: dark and light differ";
            if (reduced)
            {
                EXPECT_FALSE(app.crossfadingJustAfter);
                EXPECT_LT(Distance(app.justAfter, app.settled), 4) << "reduced motion: the new theme at once";
            }
            else
            {
                EXPECT_TRUE(app.crossfadingJustAfter);
                EXPECT_LT(Distance(app.justAfter, app.before), Distance(app.justAfter, app.settled))
                    << "just after the switch it still looks like the old theme";
            }
            EXPECT_FALSE(app.crossfadingAtEnd) << "done after 200 ms";
            app.Cleanup();
        }
    }

    // Only another palette crossfades: a font size change shows at once.
    TEST(GpuThemeCrossfade, AFontSizeChangeDoesNotCrossfade)
    {
        CrossfadeApp app;
        if (!app.Initialize())
        {
            app.Cleanup();
            GTEST_SKIP() << "no Vulkan device available";
        }
        app.fontScaleTo = 1.5f;
        app.windowID = app.idManager.CreateStaticID();
        ASSERT_TRUE(app.Run());
        EXPECT_FALSE(app.crossfadingJustAfter);
        app.Cleanup();
    }

    // Another accent crossfades as another palette does, and what's drawn in the accent is
    // drawn in the new one.
    TEST(GpuThemeCrossfade, AnAccentChangeCrossfadesToTheNewAccent)
    {
        CrossfadeApp app;
        if (!app.Initialize())
        {
            app.Cleanup();
            GTEST_SKIP() << "no Vulkan device available";
        }
        app.accentTo = kAccentPresets[3].colors;   // orange: #F59E4B in the dark palette
        app.windowID = app.idManager.CreateStaticID();
        app.barID    = app.idManager.CreateStaticID();
        ASSERT_TRUE(app.Run());
        EXPECT_TRUE(app.crossfadingJustAfter);
        EXPECT_FALSE(app.crossfadingAtEnd);
        EXPECT_LT(Distance(app.barColor, 0xF59E4B), 6) << std::hex << app.barColor;
        app.Cleanup();
    }

    // With an accent set, only another accent or palette crossfades: a font size change still
    // shows at once.
    TEST(GpuThemeCrossfade, WithAnAccentAFontSizeChangeDoesNotCrossfade)
    {
        CrossfadeApp app;
        app.SetAccent(kAccentPresets[1].colors);   // loaded with the theme
        if (!app.Initialize())
        {
            app.Cleanup();
            GTEST_SKIP() << "no Vulkan device available";
        }
        app.fontScaleTo = 1.5f;
        app.windowID = app.idManager.CreateStaticID();
        ASSERT_TRUE(app.Run());
        EXPECT_FALSE(app.crossfadingJustAfter);
        app.Cleanup();
    }

    // Quitting during the crossfade frees what it holds.
    TEST(GpuThemeCrossfade, QuittingMidCrossfadeFreesIt)
    {
        CrossfadeApp app;
        if (!app.Initialize())
        {
            app.Cleanup();
            GTEST_SKIP() << "no Vulkan device available";
        }
        // The switch applies at the start of the next frame, which starts the crossfade on that
        // frame's clock, so it can't run out before the frame ends however slow it is.
        app.quitAt = app.switchAt + 1;
        app.windowID = app.idManager.CreateStaticID();
        ASSERT_TRUE(app.Run());
        EXPECT_TRUE(app.IsThemeCrossfading()) << "test setup: still crossfading";
        app.Cleanup();
        EXPECT_FALSE(app.IsThemeCrossfading());
    }
}
