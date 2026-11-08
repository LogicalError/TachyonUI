// Text cut short, on a real device: the tooltip showing it whole lies right over it, its
// letters where the cut text's are on the screen, in the same font. Compared by where the
// ink is (the two are in other colors): in the cut text's window and in the tooltip's.
// Where the tooltip's window reads back without its box, it skips.

#include <gtest/gtest.h>

#include <gui_controls/gui_application_internal.h>

#include "gpu_injected_input.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace
{
    using namespace tachyon;

    constexpr const char* kText = "UpdateLayoutWhenDirty";

    // A window's pixels, read back.
    struct WindowImage
    {
        IntRect rect{};                // where it is on the screen
        std::vector<uint8_t> pixels;   // RGBA

        const uint8_t* At(int x, int y) const { return pixels.data() + (size_t(y) * rect.w + size_t(x)) * 4; }
        // How far apart two pixels are: the most any channel differs.
        int Apart(int x0, int y0, int x1, int y1) const
        {
            const uint8_t* a = At(x0, y0);
            const uint8_t* b = At(x1, y1);
            int most = 0;
            for (int c = 0; c < 4; ++c)
                most = std::max(most, std::abs(int(a[c]) - int(b[c])));
            return most;
        }
    };

    // Where text is inked, in screen pixels: the columns from `x0` on, `count` of them, with
    // ink in rows [y0, y1), and the first and last inked rows. Ink: unlike the background,
    // which is taken from column `bgX` on each row.
    struct Ink
    {
        std::vector<bool> columns;
        int firstColumn = -1, firstRow = -1, lastRow = -1;
    };

    Ink FindInk(const WindowImage& image, int x0, int count, int y0, int y1, int bgX)
    {
        Ink ink;
        ink.columns.assign(size_t(count), false);
        for (int y = std::max(y0, 0); y < std::min(y1, image.rect.h); ++y)
            for (int i = 0; i < count; ++i)
            {
                const int x = x0 + i;
                if (x < 0 || x >= image.rect.w || image.Apart(x, y, bgX, y) <= 64)
                    continue;
                ink.columns[size_t(i)] = true;
                if (ink.firstRow < 0) ink.firstRow = y + image.rect.y;
                ink.lastRow = y + image.rect.y;
            }
        for (int i = 0; i < count; ++i)
            if (ink.columns[size_t(i)]) { ink.firstColumn = x0 + i + image.rect.x; break; }
        return ink;
    }

    class CutTextApp : public GUIApplicationInternals
    {
    public:
        ID windowID = ID::Invalid(), labelID = ID::Invalid();
        IntRect labelRect{}, windowLaid{}, tooltipLaid{}, boxLaid{}, textLaid{};
        bool gridItem = false;   // a grid's item, its icon above its name, as the showcase's files
        WindowImage window, tooltip;
        IconID icon = IconID::Invalid;
        bool drawn = false;
        int frame = 0;

        void OnGUI() override
        {
            if (BeginWindow(windowID, "Cut", theme.windowStyleRef, LayoutOverride{}.Size(420, 120),
                            ElementFlags::WindowRenderToImage))
            {
                BeginVertical(LayoutOverride{}.FlexGrow(1));
                if (gridItem)
                {
                    BeginGridView(idManager.AllocateDynamicID("grid"));
                    Style item = styleManager.ToPixels(theme.gridViewItemBaseStyle);
                    item.iconSize = static_cast<uint16_t>(styleManager.Px(34));
                    GridViewItem(labelID, GUIContent{ icon, kText }, ElementState::None, nullptr,
                                 styleManager.Register(item, StyleUnits::Pixels),
                                 LayoutOverride{}.Size(Pixels{ styleManager.Px(96) }, Pixels{ styleManager.Px(86) }));
                    EndGridView();
                }
                else
                    Label(labelID, kText, LayoutOverride{}.Width(90));
                EndVertical();
            }
            EndWindow();
            if (frame == 2)
                InjectMouseMove(windowID, int2{ labelRect.x + labelRect.w / 2, labelRect.y + labelRect.h / 2 });
            // The first drawing with the tooltip's box in it, and the window under it then.
            if (frame >= 8 && !drawn && textLaid.w > 0)
            {
                drawn = Read(tooltipWindowID, tooltip) && BoxStandsOut() && Read(windowID, window);
            }
            if (++frame >= 30 || (drawn && frame > 10))
                applicationQuit = true;
        }

        bool Read(ID id, WindowImage& image)
        {
            image.rect = windowManager->GetWindowRect(id);
            image.pixels.resize(size_t(image.rect.w) * image.rect.h * 4);
            return image.rect.w > 0 && ReadWindowPixels(id, image.pixels);
        }

        // The tooltip's box reads back unlike the window's empty top row, a pixel into it.
        bool BoxStandsOut() const
        {
            const int y = textLaid.y - tooltipLaid.y;
            if (y <= 0 || y >= tooltip.rect.h)
                return false;
            for (int x = textLaid.x - tooltipLaid.x; x < std::min(textLaid.Max(0) - tooltipLaid.x, tooltip.rect.w); ++x)
                if (tooltip.Apart(x, y, x, 0) > 16)
                    return true;
            return false;
        }

        bool Start(float scale)
        {
            SetUIScale(scale);
            UseInjectedInputOnly(*this);
            if (!Initialize())
                return false;
            windowID = idManager.CreateStaticID();
            labelID  = idManager.CreateStaticID();
            // A picture, as the OS's file icons are.
            static constexpr uint8_t kPicture[] = { 255, 0, 0, 255, 0, 0, 255, 255 };
            icon     = textSystem->RegisterIconFromPixels("file", uint2{ 2, 1 }, kPicture);
            tooltipManager.delaySeconds = 0.0f;
            m_frame.OnPostLayout([this](FrameContext&)
            {
                if (const ElementRef ref = elementStorage.FindElementRefByID(labelID); ref.IsValid())
                    labelRect = elementStorage.GetElementRectPtr(ref)->rect;
                if (const ElementRef ref = elementStorage.FindElementRefByID(windowID); ref.IsValid())
                    windowLaid = elementStorage.GetElementRectPtr(ref)->rect;
                // The tooltip's window, its box, the box's text.
                const ElementRef win = elementStorage.FindElementRefByID(tooltipWindowID);
                if (!win.IsValid() || elementStorage.GetChildrenSpan(win).empty())
                    return;
                tooltipLaid = elementStorage.GetElementRectPtr(win)->rect;
                const ElementRef box = elementStorage.FindElementRefByID(elementStorage.GetChildrenSpan(win)[0].id);
                if (box.IsValid())
                    boxLaid = elementStorage.GetElementRectPtr(box)->rect;
                if (box.IsValid() && !elementStorage.GetChildrenSpan(box).empty())
                    if (const ElementRef text = elementStorage.FindElementRefByID(elementStorage.GetChildrenSpan(box)[0].id); text.IsValid())
                        textLaid = elementStorage.GetElementRectPtr(text)->rect;
            });
            return true;
        }
    };

    class GpuCutText : public ::testing::TestWithParam<float> {};

    TEST_P(GpuCutText, TheWholeTextLiesRightOverTheCutText)
    {
        const float scale = GetParam();
        CutTextApp app;
        if (!app.Start(scale))
        {
            app.Cleanup();
            GTEST_SKIP() << "no Vulkan device available";
        }
        ASSERT_TRUE(app.Run());
        ASSERT_GT(app.textLaid.w, 0) << "the text didn't show whole";
        if (!app.drawn)
        {
            app.Cleanup();
            GTEST_SKIP() << "the tooltip's box never shows up in what its window reads back here";
        }

        // The label's first letters (before its ellipsis) against the tooltip's, both from
        // where its text starts, in each window's pixels.
        const LayoutStyle* label = app.styleManager.GetLayoutStyle(app.theme.labelStyleRef.layoutStyleIndex);
        ASSERT_NE(label, nullptr);
        const int span    = int(std::round(40.0f * scale));
        const int labelX  = app.labelRect.x + label->borderWidths.left + label->padding.left - app.windowLaid.x;
        const int labelY  = app.labelRect.y - app.windowLaid.y;
        const int textX   = app.textLaid.x - app.tooltipLaid.x;
        const int textY   = app.textLaid.y - app.tooltipLaid.y;
        const Ink cut   = FindInk(app.window, labelX, span, labelY, labelY + app.labelRect.h, labelX - 2);
        const Ink whole = FindInk(app.tooltip, textX, span, textY, textY + app.textLaid.h, textX - 3);
        ASSERT_GE(cut.firstColumn, 0) << "no ink in the cut text";
        ASSERT_GE(whole.firstColumn, 0) << "no ink in the tooltip's text";

        EXPECT_EQ(whole.firstColumn, cut.firstColumn) << "its first letter where the cut text's is";
        EXPECT_EQ(whole.firstRow, cut.firstRow) << "as high";
        EXPECT_EQ(whole.lastRow, cut.lastRow);
        int differ = 0;
        for (int i = 0; i < span; ++i)
            differ += whole.columns[size_t(i)] != cut.columns[size_t(i)];
        EXPECT_LE(differ, 2) << "the same letters, the same width: in the same font";
        app.Cleanup();
    }

    INSTANTIATE_TEST_SUITE_P(Scales, GpuCutText, ::testing::Values(1.0f, 1.25f));

    // A grid's item, its icon above its name: the box is just the name's line, as high as its
    // text, and lies over the name where it's drawn.
    TEST_P(GpuCutText, UnderAnIconTheBoxIsTheNamesLine)
    {
        const float scale = GetParam();
        CutTextApp app;
        app.gridItem = true;
        if (!app.Start(scale))
        {
            app.Cleanup();
            GTEST_SKIP() << "no Vulkan device available";
        }
        ASSERT_TRUE(app.Run());
        ASSERT_GT(app.textLaid.w, 0) << "the text didn't show whole";
        const ElementRef item = app.elementStorage.FindElementRefByID(app.labelID);
        ASSERT_TRUE(item.IsValid());
        const Element& cut = app.elementStorage.GetElementUnsafe(item);
        ASSERT_NE(cut.iconID, IconID::Invalid) << "test setup: an icon";
        const int32_t line = app.textSystem->GetTextHeightWithoutIcon(cut.textID);
        ASSERT_GT(line, 0);
        ASSERT_GT(app.textSystem->GetTextMeasurement(cut.textID).minHeight, line) << "test setup: measured with its icon";
        const LayoutStyle* box = app.styleManager.GetLayoutStyle(app.theme.tooltipExpandStyleRef.layoutStyleIndex);
        ASSERT_NE(box, nullptr);
        EXPECT_EQ(app.textLaid.h, line) << "the text's line";
        EXPECT_EQ(app.boxLaid.h, line + box->borderWidths.Vertical() + box->padding.top + box->padding.bottom)
            << "the box around just that";
        if (!app.drawn)
        {
            app.Cleanup();
            GTEST_SKIP() << "the tooltip's box never shows up in what its window reads back here";
        }

        // Its first letters as high as the name's under the icon, in the same font: the name's
        // from where it starts in the item, cut short (with an ellipsis), the tooltip's from
        // where its whole text starts.
        const IntRect content = app.elementStorage.GetElementRectUnsafe(item).layoutStyle->GetPaddingBoxContentRect(app.labelRect);
        const int span  = int(std::round(30.0f * scale));
        const int itemX = content.x - app.windowLaid.x;
        const int rowY  = app.textLaid.y - app.windowLaid.y;
        const int margin = int(std::round(4.0f * scale));   // a little above and below the line, not up to the icon
        const Ink name  = FindInk(app.window, itemX, span, rowY - margin, rowY + line + margin, itemX - 1);
        const int textX = app.textLaid.x - app.tooltipLaid.x;
        const int textY = app.textLaid.y - app.tooltipLaid.y;
        const Ink whole = FindInk(app.tooltip, textX, span, textY, textY + app.textLaid.h, textX - 3);
        ASSERT_GE(name.firstColumn, 0) << "no ink in the name";
        ASSERT_GE(whole.firstColumn, 0) << "no ink in the tooltip's text";
        // The name is in a muted color: its faintest top row may not stand out as ink.
        EXPECT_NEAR(whole.firstRow, name.firstRow, 1) << "as high";
        EXPECT_EQ(whole.lastRow, name.lastRow) << "on the same baseline";
        int differ = 0;
        for (int i = 0; i < span; ++i)
            differ += whole.columns[size_t(i)] != name.columns[size_t(i)];
        EXPECT_LE(differ, 2) << "the same letters, the same width: in the same font";
        app.Cleanup();
    }
}
