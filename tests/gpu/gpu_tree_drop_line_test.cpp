// A tree drawn by a table with a column before the tree, on a real device: while a row is
// dragged over another, the drop line starts at the items' icons, after that column and the
// tree's arrow column, not at the row's left edge.

#include <gtest/gtest.h>

#include <gui_controls/gui_application_internal.h>

#include "gpu_injected_input.h"

#include <algorithm>
#include <cstdlib>
#include <span>
#include <vector>

namespace
{
    using namespace tachyon;

    constexpr int kFirstColumnWidth = 80;

    class TreeDropApp : public GUIApplicationInternals
    {
    public:
        ID windowID = ID::Invalid();
        ID tableID  = ID::Invalid();
        TableState state;
        ItemDragDropConfig dragDrop;
        std::vector<std::string> names{ "Alpha", "Beta", "Gamma", "Delta" };

        void OnGUI() override
        {
            if (BeginWindow(windowID, "Tree", theme.mainWindowStyleRef, LayoutOverride{}.Size(500, 300),
                            ElementFlags::WindowRenderToImage))
            {
                VirtualTableColumn cols[2];
                cols[0].column = TableColumn{ .header = {}, .initialWidth = kFirstColumnWidth, .minWidth = kFirstColumnWidth,
                                              .maxWidth = kFirstColumnWidth, .sizing = ColumnSizing::Fixed };
                cols[0].generateCell = [this](int) { Cell(""); };
                cols[1].column = TableColumn{ .header = {} };
                cols[1].generateCell = [this](int row) { TreeCell(names[size_t(row)], 0, nullptr); };
                state.showHeaders = false;
                state.rowDragDrop = &dragDrop;
                VirtualTable(tableID, state, std::span<VirtualTableColumn>(cols, 2), int(names.size()), 0, {},
                             LayoutOverride{}.FlexGrow(1));
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
            dragDrop.payloadType = 0x54455354;   // 'TEST'
            dragDrop.getPayload  = [](ID rowID) { return uintptr_t(rowID.value.subRangeParts.index); };
            dragDrop.onReorder   = [](uintptr_t, ID, TreeDropPosition) {};
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

        std::vector<uint8_t> Pixels()
        {
            const IntRect rect = windowManager->GetWindowRect(windowID);
            std::vector<uint8_t> pixels(size_t(std::max(rect.w, 0)) * size_t(std::max(rect.h, 0)) * 4);
            if (pixels.empty() || !ReadWindowPixels(windowID, pixels)) pixels.clear();
            return pixels;
        }

        IntRect RowRect(int row)
        {
            const ElementRef ref = elementStorage.FindElementRefByID(state.RowIDForIndex(row));
            return ref.IsValid() ? elementStorage.GetElementRectUnsafe(ref).rect : IntRect{};
        }
    };

    TEST(GpuTreeDropLine, ItStartsWhereTheTreesItemsDo)
    {
        TreeDropApp app;
        if (!app.Start())
        {
            app.Cleanup();
            GTEST_SKIP() << "no GPU device available";
        }
        app.SetRedrawMode(RedrawMode::Always);
        ASSERT_TRUE(app.Frames(3));
        const std::vector<uint8_t> before = app.Pixels();
        const IntRect from = app.RowRect(0);
        const IntRect to   = app.RowRect(2);
        ASSERT_GT(to.h, 0);

        // Drag row 0 down onto the top half of row 2: the line goes along row 2's top.
        const int x = from.x + kFirstColumnWidth + 40;
        app.InjectMouseMove(app.windowID, { x, from.y + from.h / 2 });
        ASSERT_TRUE(app.Frames(1));
        app.InjectMouseButton(app.windowID, { x, from.y + from.h / 2 }, MouseButtons::Left, true);
        ASSERT_TRUE(app.Frames(1));
        for (int step = 1; step <= 4; ++step)
        {
            const int y = from.y + from.h / 2 + (to.y + 3 - (from.y + from.h / 2)) * step / 4;
            app.InjectMouseMove(app.windowID, { x, y });
            ASSERT_TRUE(app.Frames(1));
        }
        ASSERT_TRUE(app.Frames(2));
        ASSERT_EQ(app.m_treeDropState.targetRowID, app.state.RowIDForIndex(2));
        ASSERT_EQ(app.m_treeDropState.position, TreeDropPosition::Before);
        const std::vector<uint8_t> during = app.Pixels();
        app.InjectMouseButton(app.windowID, { x, to.y + 3 }, MouseButtons::Left, false);
        ASSERT_TRUE(app.Frames(1));

        // The line's pixels: far from what was there before the drag. A drag only shades the
        // rest a little.
        const IntRect window = app.windowManager->GetWindowRect(app.windowID);
        ASSERT_FALSE(during.empty());
        ASSERT_EQ(during.size(), before.size());
        int firstX = -1;
        for (int px = 0; px < window.w && firstX < 0; ++px)
            for (int py = std::max(0, to.y - 2); py < std::min(window.h, to.y + 6); ++py)
            {
                const size_t i = (size_t(py) * size_t(window.w) + size_t(px)) * 4;
                if (std::abs(int(during[i]) - int(before[i])) + std::abs(int(during[i + 1]) - int(before[i + 1]))
                    + std::abs(int(during[i + 2]) - int(before[i + 2])) > 200)
                { firstX = px; break; }
            }
        // The items' icons come after the first column and the tree's arrow column.
        const int itemsX = to.x + kFirstColumnWidth + app.theme.treeTableExpanderWidth;
        ASSERT_GE(firstX, 0) << "the drop line is drawn";
        EXPECT_GE(firstX, itemsX) << "it starts at the items' icons; the row starts at " << to.x;
        EXPECT_LE(firstX, itemsX + 16) << "at the items' icons, not further in";
        app.Cleanup();
    }
}
