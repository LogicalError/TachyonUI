// A tooltip on a real device: its arrow, a bordered square on its corner, comes out of the
// box near the box's left end (not at the middle of a wide target), comes to a point one
// pixel wide, and has nothing drawn past that point.
//
// Measured against what's beside the arrow on the same row, not against "nothing": where
// the window can't be see-through its empty part reads back opaque, and a shadow may lie
// under the arrow.

#include <gtest/gtest.h>

#include <gui_controls/gui_application_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace
{
    using namespace tachyon;

    class TooltipApp : public GUIApplicationInternals
    {
    public:
        ID windowID = ID::Invalid(), fieldID = ID::Invalid();
        IntRect fieldRect{}, tooltipLaid{}, boxLaid{}, tooltipRect{};
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
                // The tooltip's window, and its box: the window's first child.
                if (const ElementRef win = elementStorage.FindElementRefByID(tooltipWindowID); win.IsValid())
                {
                    tooltipLaid = elementStorage.GetElementRectPtr(win)->rect;
                    const auto children = elementStorage.GetChildrenSpan(win);
                    if (!children.empty())
                        if (const ElementRef box = elementStorage.FindElementRefByID(children[0].id); box.IsValid())
                            boxLaid = elementStorage.GetElementRectPtr(box)->rect;
                }
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
        ASSERT_GT(app.boxLaid.w, 0);

        const IntRect r = app.tooltipRect;
        const uint8_t* px = app.pixels.data();
        // How far apart two pixels are: the most any channel (alpha too) differs.
        auto apart = [&](int x0, int y0, int x1, int y1) {
            const uint8_t* a = px + (size_t(y0) * r.w + size_t(x0)) * 4;
            const uint8_t* b = px + (size_t(y1) * r.w + size_t(x1)) * 4;
            int most = 0;
            for (int c = 0; c < 4; ++c)
                most = std::max(most, std::abs(int(a[c]) - int(b[c])));
            return most;
        };

        // The box in the window's pixels; the point is the arrow's height above its top.
        const int boxLeft = app.boxLaid.x - app.tooltipLaid.x;
        const int boxTop  = app.boxLaid.y - app.tooltipLaid.y;
        const int tip     = boxTop - int(std::round(6.0f * scale));
        ASSERT_GT(tip, 0) << "room above the point";
        const int side = int(std::round(12.0f * scale));   // beside the arrow on the same row
        // How much a pixel of the arrow stands out from what's beside it on its row, against
        // how much the box's fill does.
        int peak = boxLeft + side;
        for (int x = boxLeft + side; x < boxLeft + app.boxLaid.w - side; ++x)
            if (apart(x, tip, x - side, tip) > apart(peak, tip, peak - side, tip)) peak = x;
        const float full = float(apart(peak, boxTop + 3, peak, 0));
        ASSERT_GT(full, 16.0f);
        auto cover = [&](int x, int y) { return float(apart(x, y, peak - side, y)) / full; };

        // Near the box's left end: the field's middle is far past a box this short.
        EXPECT_GE(peak - boxLeft, int(12 * scale));
        EXPECT_LE(peak - boxLeft, int(20 * scale) + 1);
        // One pixel wide: strong in the middle, faint either side, nothing above it.
        EXPECT_GT(cover(peak, tip), 0.6f);
        EXPECT_LT(cover(peak - 1, tip), 0.35f);
        EXPECT_LT(cover(peak + 1, tip), 0.35f);
        EXPECT_LT(cover(peak, tip - 1), 0.15f);
        // Even on both sides a row down.
        EXPECT_NEAR(cover(peak - 2, tip + 1), cover(peak + 2, tip + 1), 0.08f);
        EXPECT_NEAR(cover(peak - 1, tip + 1), cover(peak + 1, tip + 1), 0.08f);
        app.Cleanup();
    }

    INSTANTIATE_TEST_SUITE_P(Scales, GpuTooltip, ::testing::Values(1.0f, 1.25f));
}
