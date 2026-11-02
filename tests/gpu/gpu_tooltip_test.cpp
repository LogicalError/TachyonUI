// A tooltip on a real device: its arrow, a bordered square on its corner, comes out of the
// box near the box's left end (not at the middle of a wide target), comes to a point one
// pixel wide, and has nothing drawn past that point.

#include <gtest/gtest.h>

#include <gui_controls/gui_application_internal.h>

#include <vector>

namespace
{
    using namespace tachyon;

    class TooltipApp : public GUIApplicationInternals
    {
    public:
        ID windowID = ID::Invalid(), fieldID = ID::Invalid();
        IntRect fieldRect{}, tooltipRect{};
        std::vector<uint8_t> pixels;
        int frame = 0;

        void OnGUI() override
        {
            if (BeginWindow(windowID, "Tooltip", theme.windowStyleRef, LayoutOverride{}.Size(420, 160),
                            ElementFlags::WindowRenderToImage))
            {
                BeginVertical(LayoutOverride{}.FlexGrow(1));
                Button(fieldID, "Editable text...", [] {}, LayoutOverride{}.Size(360, 26));
                SetTooltip("Short");
                EndVertical();
            }
            EndWindow();
            // Once laid out, the pointer goes to the field's middle.
            if (frame == 2)
                InjectMouseMove(windowID, int2{ fieldRect.x + fieldRect.w / 2, fieldRect.y + fieldRect.h / 2 });
            if (frame == 8)
            {
                tooltipRect = windowManager->GetWindowRect(tooltipWindowID);
                pixels.resize(size_t(tooltipRect.w) * tooltipRect.h * 4);
                if (!ReadWindowPixels(tooltipWindowID, pixels))
                    pixels.clear();
            }
            if (++frame >= 10)
                applicationQuit = true;
        }

        bool Start(float scale)
        {
            SetUIScale(scale);
            if (!Initialize())
                return false;
            windowID = idManager.CreateStaticID();
            fieldID  = idManager.CreateStaticID();
            tooltipManager.delaySeconds = 0.0f;
            m_frame.OnPostLayout([this](FrameContext&)
            {
                if (const ElementRef ref = elementStorage.FindElementRefByID(fieldID); ref.IsValid())
                    fieldRect = elementStorage.GetElementRectPtr(ref)->rect;
            });
            return true;
        }
    };

    // At 100% and at a zoom that puts the arrow between pixels unless it's snapped to one.
    class GpuTooltip : public ::testing::TestWithParam<float> {};

    TEST_P(GpuTooltip, TheArrowIsNearTheLeftAndComesToAPoint)
    {
        const float scale = GetParam();
        TooltipApp app;
        if (!app.Start(scale))
        {
            app.Cleanup();
            GTEST_SKIP() << "no Vulkan device available";
        }
        ASSERT_TRUE(app.Run());
        ASSERT_FALSE(app.pixels.empty()) << "the tooltip didn't show";

        const IntRect r = app.tooltipRect;
        auto alpha = [&](int x, int y) { return int(app.pixels[(size_t(y) * r.w + size_t(x)) * 4 + 3]); };
        // The topmost row with anything in it is the arrow's point.
        int top = -1;
        for (int y = 0; y < r.h && top < 0; ++y)
            for (int x = 0; x < r.w; ++x)
                if (alpha(x, y) > 0) { top = y; break; }
        ASSERT_GT(top, 0) << "room above the point";
        int peak = 0;
        for (int x = 1; x < r.w; ++x)
            if (alpha(x, top) > alpha(peak, top)) peak = x;

        // Near the box's left end: the field's middle is far past a box this short.
        EXPECT_GE(peak, int(12 * scale));
        EXPECT_LE(peak, int(20 * scale) + 1);
        // One pixel wide: strong in the middle, faint either side.
        EXPECT_GT(alpha(peak, top), 192);
        EXPECT_LT(alpha(peak - 1, top), 64);
        EXPECT_LT(alpha(peak + 1, top), 64);
        // Even on both sides a row down.
        EXPECT_NEAR(alpha(peak - 2, top + 1), alpha(peak + 2, top + 1), 8);
        EXPECT_NEAR(alpha(peak - 1, top + 1), alpha(peak + 1, top + 1), 8);
        app.Cleanup();
    }

    INSTANTIATE_TEST_SUITE_P(Scales, GpuTooltip, ::testing::Values(1.0f, 1.25f));
}
