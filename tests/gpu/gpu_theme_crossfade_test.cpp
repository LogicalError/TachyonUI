// A theme switch crossfades on a real device: right after it, the window still shows the
// old theme (its last frame drawn over the new one), and 200 ms on only the new one. With
// reduced motion the new theme shows at once.

#include <gtest/gtest.h>

#include <gui_controls/gui_application_internal.h>

#include <chrono>
#include <cstdlib>
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
        uint32_t before = 0, justAfter = 0, settled = 0;   // window background, 0x00RRGGBB
        bool crossfadingJustAfter = false, crossfadingAtEnd = true;

        uint32_t Background()
        {
            const IntRect rect = windowManager->GetWindowRect(windowID);
            std::vector<uint8_t> pixels(size_t(rect.w) * rect.h * 4);
            if (!ReadWindowPixels(windowID, pixels))
                return 0xFFFFFFFF;
            const size_t o = (size_t(rect.h - 4) * rect.w + size_t(rect.w - 4)) * 4;
            return (uint32_t(pixels[o + 2]) << 16) | (uint32_t(pixels[o + 1]) << 8) | pixels[o];
        }

        void OnGUI() override
        {
            // Each read sees the frame before.
            if (frame == switchAt)
            {
                before = Background();
                if (fontScaleTo > 0.0f)
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
                crossfadingAtEnd = IsThemeCrossfading();
                applicationQuit = true;
            }
            if (BeginWindow(windowID, "Crossfade", theme.windowStyleRef, LayoutOverride{}.Size(200, 120),
                            ElementFlags::WindowRenderToImage))
            {
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

    // Quitting during the crossfade frees what it holds.
    TEST(GpuThemeCrossfade, QuittingMidCrossfadeFreesIt)
    {
        CrossfadeApp app;
        if (!app.Initialize())
        {
            app.Cleanup();
            GTEST_SKIP() << "no Vulkan device available";
        }
        app.quitAt = app.switchAt + 2;
        app.windowID = app.idManager.CreateStaticID();
        ASSERT_TRUE(app.Run());
        EXPECT_TRUE(app.IsThemeCrossfading()) << "test setup: still crossfading";
        app.Cleanup();
        EXPECT_FALSE(app.IsThemeCrossfading());
    }
}
