// A label with property help on a real device: a dotted underline just under its text, in a
// row with its control as an inspector lays it out.

#include <gtest/gtest.h>

#include <gui_controls/gui_application_internal.h>

#include "gpu_injected_input.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    using namespace tachyon;

    class LabelHelpApp : public GUIApplicationInternals
    {
    public:
        ID windowID = ID::Invalid();
        ID labelID  = ID::Invalid();
        ID plainID  = ID::Invalid();
        ID groupedID = ID::Invalid();
        bool on = true;

        void OnGUI() override
        {
            if (BeginWindow(windowID, "Labels", theme.mainWindowStyleRef, LayoutOverride{}.Size(320, 120),
                            ElementFlags::WindowRenderToImage))
            {
                BeginHorizontal(idManager.AllocateDynamicID("helpRow"), theme.emptyHorzStyleRef, LayoutOverride::FixedWidth(300).NoShrink());
                Label(labelID, "Cast shadows");
                SetTooltip(PropertyHelp{ "Cast shadows", "Whether this mesh casts shadows.", "MeshRenderer.castShadows" });
                Checkbox(on, "On");
                EndHorizontal();
                BeginHorizontal(idManager.AllocateDynamicID("plainRow"), theme.emptyHorzStyleRef, LayoutOverride::FixedWidth(300).NoShrink());
                Label(plainID, "Cast shadows");
                Checkbox(on, "On");
                EndHorizontal();
                // In a label group, as an inspector's rows are: the label and its control in a
                // column, kept on one line by the label's break-after.
                BeginVertical(idManager.AllocateDynamicID("column"), theme.emptyVertStyleRef, LayoutOverride::FixedWidth(300).NoShrink());
                BeginLabelGroup(idManager.AllocateDynamicID("group"), 116, 40, 300);
                Label(groupedID, "Cast shadows");
                SetTooltip(PropertyHelp{ "Cast shadows", "Whether this mesh casts shadows.", "MeshRenderer.castShadows" });
                Checkbox(on, "On");
                EndLabelGroup();
                EndVertical();
            }
            EndWindow();
        }

        bool Start()
        {
            UseInjectedInputOnly(*this);
            if (!Initialize())
                return false;
            windowID = idManager.CreateStaticID();
            labelID  = idManager.CreateStaticID();
            plainID  = idManager.CreateStaticID();
            groupedID = idManager.CreateStaticID();
            SetRedrawMode(RedrawMode::Always);
            return true;
        }

        bool Frames(int count)
        {
            for (int i = 0; i < count; i++)
            {
                if (!UpdateFrame() || !RecordFrame()) return false;
                PresentFrame();
            }
            return true;
        }

        IntRect RectOf(ID id) { return elementStorage.GetElementRectUnsafe(elementStorage.FindElementRefByID(id)).rect; }

        std::vector<uint8_t> pixels;
        int width = 0;
        bool Read()
        {
            const IntRect rect = windowManager->GetWindowRect(windowID);
            width = rect.w;
            pixels.assign(size_t(std::max(rect.w, 0)) * size_t(std::max(rect.h, 0)) * 4, 0);
            return !pixels.empty() && ReadWindowPixels(windowID, pixels);
        }
        int Brightness(int x, int y) const
        {
            const uint8_t* p = pixels.data() + (size_t(y) * size_t(width) + size_t(x)) * 4;   // BGRA
            return (p[0] + p[1] + p[2]) / 3;
        }
        bool InkInRow(IntRect rect, int y, int background) const
        {
            for (int x = rect.x; x < rect.x + rect.w; ++x)
                if (Brightness(x, y) > background + 30)
                    return true;
            return false;
        }
        // From the top of the text in `rect` to its lowest ink, an underline included (looked
        // for a little under the box too, where a cut-off one would have been).
        int InkHeight(IntRect rect, int background) const
        {
            int top = -1, bottom = -1;
            for (int y = rect.y; y < rect.y + rect.h + 6; ++y)
                if (InkInRow(rect, y, background))
                {
                    if (top < 0) top = y;
                    bottom = y;
                }
            return top < 0 ? -1 : bottom - top;
        }
    };

    class GpuLabelHelp : public ::testing::Test
    {
    protected:
        LabelHelpApp app;
        void SetUp() override
        {
            if (!app.Start())
                GTEST_SKIP() << "no GPU device available";
            ASSERT_TRUE(app.Frames(4));
            ASSERT_TRUE(app.Read());
        }
        void TearDown() override { app.Cleanup(); }
    };

    // A label with help has ink a few pixels lower than the same label without: its dotted
    // underline, under the text and not cut off by the label's box. In a row of its own, and
    // in a label group's.
    TEST_F(GpuLabelHelp, ALabelWithHelpIsUnderlinedInItsRow)
    {
        const IntRect plain = app.RectOf(app.plainID);
        const int background = app.Brightness(app.width - 4, plain.y + plain.h / 2);
        const int plainInk = app.InkHeight(plain, background);
        ASSERT_GT(plainInk, 0) << "test setup: the plain label's text";
        for (const ID id : { app.labelID, app.groupedID })
        {
            const IntRect help = app.RectOf(id);
            ASSERT_GT(help.w, 0);
            const int helpInk = app.InkHeight(help, background);
            EXPECT_GE(helpInk, plainInk + app.styleManager.Px(2)) << (id == app.labelID ? "in a row" : "in a label group");

            // Dotted: along its lowest row, ink and gaps.
            int lowest = help.y + help.h + 6;
            while (lowest > help.y && !app.InkInRow(help, lowest, background)) --lowest;
            int ink = 0, gaps = 0;
            for (int x = help.x + app.styleManager.Px(10); x < help.x + help.w / 2; ++x)
                (app.Brightness(x, lowest) > background + 30 ? ink : gaps)++;
            EXPECT_GT(ink, 3);
            EXPECT_GT(gaps, 3);
        }
    }
}
