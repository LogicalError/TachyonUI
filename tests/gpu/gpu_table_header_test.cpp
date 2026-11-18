// A table's header row on a real device, with real text and icons: as tall as its text and
// padding (the design's 28px), sorted or not, the first row right under it, and each header's
// text where its column's cells put theirs.

#include <gtest/gtest.h>

#include <gui_controls/gui_application_internal.h>

#include "gpu_injected_input.h"

#include <span>
#include <vector>

namespace
{
    using namespace tachyon;

    class TableHeaderApp : public GUIApplicationInternals
    {
    public:
        ID windowID = ID::Invalid();
        ID tableID  = ID::Invalid();
        TableState state;
        std::vector<VirtualTableColumn> columns;

        void OnGUI() override
        {
            if (BeginWindow(windowID, "Table", theme.mainWindowStyleRef, LayoutOverride{}.Size(500, 300),
                            ElementFlags::WindowRenderToImage))
            {
                std::span<VirtualTableColumn> span(columns.data(), columns.size());
                // Few enough rows to fit: no scrollbar over the last column's cells.
                VirtualTable(tableID, state, span, 5, 0, theme.tableFillStyleRef, LayoutOverride::Auto().FlexGrow(1).MinHeight(0));
            }
            EndWindow();
        }

        bool Start()
        {
            UseInjectedInputOnly(*this);
            if (!Initialize())
                return false;
            windowID = idManager.CreateStaticID();
            tableID  = idManager.CreateStaticID();
            columns.resize(3);
            columns[0].column = { .header = "#", .initialWidth = 60 };
            columns[0].generateCell = [this](int) { Cell("1"); };
            columns[0].sortAscending = [](int a, int b) { return a < b; };   // sorted: its header has the chevron
            columns[1].column = { .header = "Name", .initialWidth = 160 };
            columns[1].generateCell = [this](int) { Cell("Row"); };
            columns[2].column = { .header = "Value", .initialWidth = 120, .cellStyle = theme.tableCellNumberStyleRef };   // right-aligned
            columns[2].generateCell = [this](int) { Cell("1234"); };
            state.sortColumn = 0;
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

        const ElementRect& HeaderRect(size_t c) { return elementStorage.GetElementRectUnsafe(columns[c].internal.headerRef); }

        std::vector<uint8_t> Pixels()
        {
            const IntRect rect = windowManager->GetWindowRect(windowID);
            std::vector<uint8_t> pixels(size_t(std::max(rect.w, 0)) * size_t(std::max(rect.h, 0)) * 4);
            if (pixels.empty() || !ReadWindowPixels(windowID, pixels)) pixels.clear();
            return pixels;
        }

        // The leftmost and rightmost columns of ink (pixels unlike the box's background, read at
        // its left edge on each row) inside `box`.
        std::pair<int, int> InkSpan(const std::vector<uint8_t>& pixels, IntRect box)
        {
            const int w = windowManager->GetWindowRect(windowID).w;
            int first = -1, last = -1;
            for (int y = box.y; y < box.y + box.h; ++y)
            {
                const uint8_t* bg = pixels.data() + (size_t(y) * size_t(w) + size_t(box.x)) * 4;
                for (int x = box.x; x < box.x + box.w; ++x)
                {
                    const uint8_t* p = pixels.data() + (size_t(y) * size_t(w) + size_t(x)) * 4;
                    if (std::abs(p[0] - bg[0]) + std::abs(p[1] - bg[1]) + std::abs(p[2] - bg[2]) > 90)
                    {
                        if (first < 0 || x < first) first = x;
                        if (x > last) last = x;
                    }
                }
            }
            return { first, last };
        }
    };

    TEST(GpuTableHeader, TheHeaderRowIsAsTallAsItsTextSortedOrNot)
    {
        TableHeaderApp app;
        if (!app.Start())
        {
            app.Cleanup();
            GTEST_SKIP() << "no GPU device available";
        }
        ASSERT_TRUE(app.Frames(4));
        auto check = [&](const char* what) {
            const ElementRect& header = app.HeaderRect(1);
            const ElementRef row0Ref  = app.elementStorage.FindElementRefByID(app.state.RowIDForIndex(0));
            ASSERT_TRUE(row0Ref.IsValid());
            const IntRect row0 = app.elementStorage.GetElementRectUnsafe(row0Ref).rect;
            const auto text = app.textSystem->GetTextMeasurement(app.elementStorage.GetElementUnsafe(app.columns[1].internal.headerRef).textID);
            EXPECT_EQ(header.innerRect.h, text.minHeight) << what << ": as tall as its text, so the text is in its middle";
            EXPECT_LE(header.rect.h, 30) << what;
            EXPECT_EQ(row0.y, header.rect.y + header.rect.h) << what << ": the first row right under the header";
            for (size_t c = 0; c < app.columns.size(); ++c)
                EXPECT_EQ(app.HeaderRect(c).rect.h, header.rect.h) << what << ": column " << c;
        };
        check("sorted");

        app.state.sortColumn = -1;
        ASSERT_TRUE(app.Frames(3));
        check("unsorted");
        app.Cleanup();
    }

    // Each header's text sits where its column's cells put theirs: a text column's at the start,
    // a number column's at the end, over its values, with its sort chevron before it.
    TEST(GpuTableHeader, AHeadersTextLinesUpWithItsCells)
    {
        TableHeaderApp app;
        if (!app.Start())
        {
            app.Cleanup();
            GTEST_SKIP() << "no GPU device available";
        }
        app.columns[2].sortAscending = [](int a, int b) { return a < b; };
        app.state.sortColumn = 2;   // the number column, sorted: its label is still drawn, at the end
        app.SetRedrawMode(RedrawMode::Always);
        ASSERT_TRUE(app.Frames(4));
        const std::vector<uint8_t> pixels = app.Pixels();
        ASSERT_FALSE(pixels.empty());
        const ElementRef row0Ref = app.elementStorage.FindElementRefByID(app.state.RowIDForIndex(0));
        ASSERT_TRUE(row0Ref.IsValid());
        const IntRect row0 = app.elementStorage.GetElementRectUnsafe(row0Ref).rect;
        for (size_t c : { size_t(1), size_t(2) })
        {
            const IntRect header = app.HeaderRect(c).rect;
            const IntRect cells{ header.x, row0.y, header.w, row0.h };   // the column, in row 0
            const auto [headerFirst, headerLast] = app.InkSpan(pixels, IntRect{ header.x + 1, header.y + 1, header.w - 2, header.h - 2 });
            const auto [cellFirst, cellLast]     = app.InkSpan(pixels, IntRect{ cells.x + 1, cells.y + 1, cells.w - 2, cells.h - 2 });
            ASSERT_GE(headerFirst, 0) << "column " << c << ": the header's text is drawn";
            ASSERT_GE(cellFirst, 0) << "column " << c << ": the cell's text is drawn";
            if (c == 1)
                EXPECT_NEAR(headerFirst, cellFirst, 2) << "text column: header and cells start together";
            else
                EXPECT_NEAR(headerLast, cellLast, 2) << "number column: header and cells end together";
        }
        app.Cleanup();
    }
}
