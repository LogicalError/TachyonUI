// A field whose value isn't accepted, on a real device: an error icon at its end, its text
// kept short of it, and its error tooltip's arrow in the error color. Where the tooltip's
// window reads back without its box, the tooltip's part is skipped.

#include <gtest/gtest.h>

#include <gui_controls/gui_application_internal.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace
{
    using namespace tachyon;

    // A window's pixels, read back (BGRA).
    struct WindowPixels
    {
        IntRect rect{};
        std::vector<uint8_t> pixels;

        const uint8_t* At(int x, int y) const { return pixels.data() + (size_t(y) * rect.w + size_t(x)) * 4; }
        // The error color: red well over green and blue.
        bool Reddish(int x, int y) const
        {
            const uint8_t* p = At(x, y);
            return int(p[2]) - int(p[1]) > 80 && int(p[2]) - int(p[0]) > 80;
        }
        // Text ink: light and grey.
        bool Ink(int x, int y) const
        {
            const uint8_t* p = At(x, y);
            return p[2] > 120 && std::abs(int(p[2]) - int(p[1])) < 40 && std::abs(int(p[2]) - int(p[0])) < 40;
        }
    };

    class InvalidFieldApp : public GUIApplicationInternals
    {
    public:
        ID windowID = ID::Invalid(), fieldID = ID::Invalid(), labelID = ID::Invalid();
        std::string value = "1.8 is more than the slider's range allows here";
        IntRect fieldLaid{}, labelLaid{}, windowLaid{}, tooltipLaid{}, boxLaid{};
        WindowPixels window, tooltip;
        bool drawn = false, windowRead = false;
        int frame = 0;

        void OnGUI() override
        {
            if (BeginWindow(windowID, "Invalid", theme.windowStyleRef, LayoutOverride{}.Size(420, 140),
                            ElementFlags::WindowRenderToImage))
            {
                BeginVertical(LayoutOverride{}.FlexGrow(1));
                BeginHorizontal(idManager.AllocateDynamicID("holder"), theme.emptyHorzStyleRef, LayoutOverride{}.Width(200));
                BeginInvalid();
                Field(fieldID, value);
                SetErrorTooltip("Value must be between 0 and 1");
                EndInvalid();
                EndHorizontal();
                BeginInvalid();   // not a field: no icon
                Label(labelID, "A label", LayoutOverride{}.Width(200));
                EndInvalid();
                EndVertical();
            }
            EndWindow();
            if (frame == 2)
                InjectMouseMove(windowID, int2{ fieldLaid.x + 20 - windowLaid.x, fieldLaid.y + fieldLaid.h / 2 - windowLaid.y });
            // The field once its halo has grown, and the tooltip's first drawing with its box.
            if (frame >= 12 && !windowRead)
                windowRead = Read(windowID, window);
            if (frame >= 8 && !drawn && boxLaid.w > 0)
                drawn = Read(tooltipWindowID, tooltip) && BoxStandsOut();
            if (++frame >= 40 || (drawn && windowRead && frame > 14))
                applicationQuit = true;
        }

        bool Read(ID id, WindowPixels& image)
        {
            image.rect = windowManager->GetWindowRect(id);
            image.pixels.resize(size_t(image.rect.w) * image.rect.h * 4);
            return image.rect.w > 0 && ReadWindowPixels(id, image.pixels);
        }

        bool BoxStandsOut() const
        {
            const int y = boxLaid.y - tooltipLaid.y + 3;
            if (y <= 0 || y >= tooltip.rect.h)
                return false;
            for (int x = boxLaid.x - tooltipLaid.x; x < std::min(boxLaid.Max(0) - tooltipLaid.x, tooltip.rect.w); ++x)
            {
                const uint8_t* a = tooltip.At(x, y);
                const uint8_t* b = tooltip.At(x, 0);
                for (int c = 0; c < 4; ++c)
                    if (std::abs(int(a[c]) - int(b[c])) > 16)
                        return true;
            }
            return false;
        }

        bool Start(float scale)
        {
            SetUIScale(scale);
            if (!Initialize())
                return false;
            windowID = idManager.CreateStaticID();
            fieldID  = idManager.CreateStaticID();
            labelID  = idManager.CreateStaticID();
            tooltipManager.delaySeconds = 0.0f;
            m_frame.OnPostLayout([this](FrameContext&)
            {
                if (const ElementRef ref = elementStorage.FindElementRefByID(fieldID); ref.IsValid())
                    fieldLaid = elementStorage.GetElementRectPtr(ref)->rect;
                if (const ElementRef ref = elementStorage.FindElementRefByID(labelID); ref.IsValid())
                    labelLaid = elementStorage.GetElementRectPtr(ref)->rect;
                if (const ElementRef ref = elementStorage.FindElementRefByID(windowID); ref.IsValid())
                    windowLaid = elementStorage.GetElementRectPtr(ref)->rect;
                const ElementRef win = elementStorage.FindElementRefByID(tooltipWindowID);
                if (!win.IsValid() || elementStorage.GetChildrenSpan(win).empty())
                    return;
                tooltipLaid = elementStorage.GetElementRectPtr(win)->rect;
                if (const ElementRef box = elementStorage.FindElementRefByID(elementStorage.GetChildrenSpan(win)[0].id); box.IsValid())
                    boxLaid = elementStorage.GetElementRectPtr(box)->rect;
            });
            return true;
        }
    };

    class GpuInvalidField : public ::testing::TestWithParam<float> {};

    TEST_P(GpuInvalidField, AnErrorIconEndsTheFieldAndTheTooltipsArrowIsInTheErrorColor)
    {
        const float scale = GetParam();
        InvalidFieldApp app;
        if (!app.Start(scale))
        {
            app.Cleanup();
            GTEST_SKIP() << "no Vulkan device available";
        }
        ASSERT_TRUE(app.Run());
        ASSERT_TRUE(app.windowRead) << "the field's window didn't read back";

        // The icon: at the end of the field's padding box, 13 px, centered on it.
        const LayoutStyle* layout = app.styleManager.GetLayoutStyle(
            app.elementStorage.GetElementUnsafe(app.elementStorage.FindElementRefByID(app.fieldID)).styleRef.layoutStyleIndex);
        ASSERT_NE(layout, nullptr);
        const IntRect content = layout->GetPaddingBoxContentRect(app.fieldLaid);
        const int size = app.styleManager.Px(13), gap = app.styleManager.Px(6);
        const int iconX = content.Max(0) - size - app.windowLaid.x;
        const int top = content.y - app.windowLaid.y, bottom = content.Max(1) - app.windowLaid.y;
        int red = 0, ink = 0;
        for (int y = top; y < bottom; ++y)
        {
            for (int x = iconX; x < iconX + size; ++x)
                red += app.window.Reddish(x, y);
            for (int x = iconX - gap; x < content.Max(0) - app.windowLaid.x; ++x)
                ink += app.window.Ink(x, y);
        }
        EXPECT_GT(red, size) << "an error icon at the field's end";
        EXPECT_EQ(ink, 0) << "the text stops short of it";
        int textInk = 0;   // ... but there is text before it
        for (int y = top; y < bottom; ++y)
            for (int x = content.x - app.windowLaid.x; x < iconX - gap; ++x)
                textInk += app.window.Ink(x, y);
        EXPECT_GT(textInk, 0);
        int labelRed = 0;   // inside the label (its halo is outside it)
        for (int y = app.labelLaid.y + 2 - app.windowLaid.y; y < app.labelLaid.Max(1) - 2 - app.windowLaid.y; ++y)
            for (int x = app.labelLaid.x + 2 - app.windowLaid.x; x < app.labelLaid.Max(0) - 2 - app.windowLaid.x; ++x)
                labelRed += app.window.Reddish(x, y);
        EXPECT_EQ(labelRed, 0) << "an error icon only in a field";

        if (!app.drawn)
        {
            app.Cleanup();
            GTEST_SKIP() << "the tooltip's box never shows up in what its window reads back here";
        }
        // The arrow: above the box's top edge, near its left, in the error color.
        const int boxTop = app.boxLaid.y - app.tooltipLaid.y, boxLeft = app.boxLaid.x - app.tooltipLaid.x;
        int arrow = 0;
        for (int y = boxTop - int(std::round(6.0f * scale)); y < boxTop; ++y)
            for (int x = boxLeft; x < boxLeft + int(std::round(40.0f * scale)); ++x)
                arrow += app.tooltip.Reddish(x, y);
        EXPECT_GT(arrow, 2) << "its arrow's edges in the error color";
        app.Cleanup();
    }

    INSTANTIATE_TEST_SUITE_P(Scales, GpuInvalidField, ::testing::Values(1.0f, 1.25f));
}
