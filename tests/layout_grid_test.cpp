#include <gtest/gtest.h>
#include <gmock/gmock.h>


#include <gui_core/style/gui_style_manager.h>
#include <gui_core/style/gui_gradient_manager.h>
#include <gui_core/style/gui_style.h>

#include <gui_core/layout/gui_layout_engine.h>

#include <gui_core/windows/gui_window_backend.h>

#include <gui_core/elements/gui_element_storage.h>

#include <gui_core/windows/gui_os_window_manager.h>

#include <gui_core/state/gui_scroll_management.h>

#include "gui_text_system_stub.h"
#include "mock_window_backend.h"

namespace tachyon
{
    using namespace testing;
    using ::testing::Return;
    using ::testing::_;
    using ::testing::NiceMock;
    using GUITest::MockWindowBackend;

    class LayoutGridTest : public ::testing::Test
    {
    protected:
        GradientManager gradientManager{ 256 };
        StyleManager styleManager{ gradientManager };
        StubTextSystem textSystem;
        ScrollManagement scrollManagement;
        LayoutEngine layoutEngine{ textSystem,scrollManagement };
        ElementStorage storage{ styleManager };
        StyleRef defaultStyleIndex;

        MockWindowBackend* mockBackend;
        std::unique_ptr<OSWindowManager> osWindowManager;

        void SetUp() override
        {
            mockBackend = new NiceMock<MockWindowBackend>();
            osWindowManager = std::make_unique<OSWindowManager>(nullptr, mockBackend);

            Style baseStyle;
            baseStyle.display = Display::Grid; // Set root to Grid
            defaultStyleIndex = styleManager.Register(baseStyle);
        }
    };

    // Test 1: Verify explicit grid placement (rowStart/columnStart)
    TEST_F(LayoutGridTest, ExplicitItemPlacement)
    {
        Style parentStyle;
        parentStyle.display = Display::Grid;
        StyleRef parentIdx = styleManager.Register(parentStyle);

        Style childStyle;
        childStyle.layoutData.grid.column.start = 1;
        childStyle.layoutData.grid.row.start = 1;
        // Tell the grid that the tracks this child inhabits should be 100px Fixed
        childStyle.layoutData.grid.column.track = GridTrack::Fixed(100.0f);
        childStyle.layoutData.grid.row.track = GridTrack::Fixed(100.0f);
        StyleRef childIdx = styleManager.Register(childStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        // 200x200 Grid with two 100px tracks
        [[maybe_unused]] ElementRef parentRef = storage.Add(Element{ .styleRef = parentIdx }, LayoutOverride::FixedSize(200, 200)).ref;
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);

        // Child should occupy the bottom-right quadrant
        ElementRef childRef = storage.Add(Element{ .styleRef = childIdx }, LayoutOverride::FixedSize(100, 100)).ref;

        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect& child = storage.GetElementRectUnsafe(childRef);
        EXPECT_EQ(child.rect.x, 100);
        EXPECT_EQ(child.rect.y, 100);
    }

    // Test 2: Verify that grid gaps correctly offset child positions
    TEST_F(LayoutGridTest, GridGapsAffectPosition)
    {
        Style parentStyle;
        parentStyle.display = Display::Grid;
        parentStyle.layoutData.grid.column.gap = 10;
        parentStyle.layoutData.grid.row.gap = 20;
        StyleRef parentIdx = styleManager.Register(parentStyle);

        Style child0Style;
        child0Style.layoutData.grid.column.start = 0;
        child0Style.layoutData.grid.row.start = 0;
        child0Style.layoutData.grid.column.track = GridTrack::Fixed(100.0f);
        child0Style.layoutData.grid.row.track = GridTrack::Fixed(100.0f);

        Style child1Style = child0Style;
        child1Style.layoutData.grid.column.start = 1;

        StyleRef child0Idx = styleManager.Register(child0Style);
        StyleRef child1Idx = styleManager.Register(child1Style);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        EXPECT_EQ(storage.Add(Element{ .styleRef = parentIdx }, LayoutOverride::FixedSize(500, 500)), ElementStorageResult::Success);
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);

        EXPECT_EQ(storage.Add(Element{ .styleRef = child0Idx }, LayoutOverride::FixedSize(100, 100)), ElementStorageResult::Success);
        auto result2 = storage.Add(Element{ .styleRef = child1Idx }, LayoutOverride::FixedSize(100, 100));
        EXPECT_EQ(result2.status, ElementStorageResult::Success);
        ElementRef child2Ref = result2.ref;

        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect& child2 = storage.GetElementRectUnsafe(child2Ref);
        // Second child in the next explicit column should be at (first column width + column gap)
        EXPECT_EQ(child2.rect.x, 110);
    }

    // Fractional tracks share free space by their (fractional) weights: 0.5fr
    // gets half of what 1fr gets, not the same.
    TEST_F(LayoutGridTest, FractionalTracksUseFractionalWeights)
    {
        Style parentStyle;
        parentStyle.display = Display::Grid;
        StyleRef parentIdx = styleManager.Register(parentStyle);

        Style half;
        half.layoutData.grid.column.start = 0;
        half.layoutData.grid.row.start    = 0;
        half.layoutData.grid.column.track = GridTrack::Fractional(0.5f);
        half.layoutData.grid.row.track    = GridTrack::Fixed(10.0f);
        Style one = half;
        one.layoutData.grid.column.start  = 1;
        one.layoutData.grid.column.track  = GridTrack::Fractional(1.0f);
        StyleRef halfIdx = styleManager.Register(half);
        StyleRef oneIdx  = styleManager.Register(one);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        EXPECT_EQ(storage.Add(Element{ .styleRef = parentIdx }, LayoutOverride::FixedSize(300, 100)), ElementStorageResult::Success);
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.Add(Element{ .styleRef = halfIdx }, LayoutOverride::FixedSize(10, 10)), ElementStorageResult::Success);
        ElementRef oneRef = storage.Add(Element{ .styleRef = oneIdx }, LayoutOverride::FixedSize(10, 10)).ref;
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        // Free space 280 split 1:2 -> the 0.5fr column is 10 + 93 wide.
        EXPECT_NEAR(storage.GetElementRectUnsafe(oneRef).rect.x, 103, 1);
    }

    // Test 3: Verify Column Spanning
    TEST_F(LayoutGridTest, ColumnSpanOccupiesMultipleCells)
    {
        Style parentStyle;
        parentStyle.display = Display::Grid;
        StyleRef parentIdx = styleManager.Register(parentStyle);

        Style spanStyle;
        spanStyle.layoutData.grid.column.span = 2;
        StyleRef spanIdx = styleManager.Register(spanStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        EXPECT_EQ(storage.Add(Element{ .styleRef = parentIdx }, LayoutOverride::FixedSize(200, 100)), ElementStorageResult::Success);
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);

        ElementRef childRef = storage.Add(Element{ .styleRef = spanIdx }, LayoutOverride::FixedSize(200, 50)).ref;

        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        const ElementRect& child = storage.GetElementRectUnsafe(childRef);
        EXPECT_EQ(child.rect.w, 200); // Should span the full width of two 100px columns
    }
    // A grid's content bounds reach back to its content box's top-left even when its content
    // starts further in (centred in a taller row), as a flex container's do.
    TEST_F(LayoutGridTest, ContentBoundsStartAtTheContentOrigin)
    {
        Style parentStyle;
        parentStyle.display = Display::Grid;
        parentStyle.layoutData.grid.alignItems = AlignItems::Center;
        const StyleRef parentIdx = styleManager.Register(parentStyle);

        Style childStyle;
        childStyle.layoutData.grid.column.track = GridTrack::Fixed(100.0f);
        childStyle.layoutData.grid.row.track    = GridTrack::Fixed(100.0f);
        const StyleRef childIdx = styleManager.Register(childStyle);

        EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        const ElementRef parentRef = storage.Add(Element{ .styleRef = parentIdx }, LayoutOverride::FixedSize(300, 300)).ref;
        EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        const ElementRef childRef = storage.Add(Element{ .styleRef = childIdx }, LayoutOverride::FixedSize(100, 40)).ref;
        EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        layoutEngine.Layout(*osWindowManager, storage);

        ASSERT_EQ(storage.GetElementRectUnsafe(childRef).rect.y, 30);
        const IntRect bounds = storage.GetElementRectUnsafe(parentRef).contentBounds;
        EXPECT_EQ(bounds.y, 0);
        EXPECT_EQ(bounds.h, 70);
    }

    // Grid placement cases: each child is a 100x100 cell in fixed 100px tracks, placed by
    // `place` (start < 0: auto); returns where each one lands, in cells.
    class LayoutGridPlacementTest : public LayoutGridTest
    {
    protected:
        struct Place { int16_t col = GridAxisProperties::kAuto, row = GridAxisProperties::kAuto, colSpan = 1, rowSpan = 1; };

        // An empty auto column takes the grid's free space, so a test reading a column's x gives
        // the grid none (parentSize 0).
        std::vector<int2> Cells(std::initializer_list<Place> places, int16_t columnGap = 0, int32_t parentSize = 4000)
        {
            Style parentStyle;
            parentStyle.display = Display::Grid;
            parentStyle.layoutData.grid.column.gap = columnGap;
            const StyleRef parentIdx = styleManager.Register(parentStyle);

            EXPECT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
            EXPECT_EQ(storage.Add(Element{ .styleRef = parentIdx }, LayoutOverride::FixedSize(parentSize, parentSize)), ElementStorageResult::Success);
            EXPECT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
            std::vector<ElementRef> refs;
            for (const Place& p : places)
            {
                Style childStyle;
                childStyle.layoutData.grid.column = { .track = GridTrack::Fixed(100.0f), .start = p.col, .span = p.colSpan };
                childStyle.layoutData.grid.row    = { .track = GridTrack::Fixed(100.0f), .start = p.row, .span = p.rowSpan };
                refs.push_back(storage.Add(Element{ .styleRef = styleManager.Register(childStyle) },
                                           LayoutOverride::FixedSize(100 * p.colSpan, 100 * p.rowSpan)).ref);
            }
            EXPECT_EQ(storage.EndChildren(), ElementStorageResult::Success);
            EXPECT_EQ(storage.EndConstruction(), ElementStorageResult::Success);
            layoutEngine.Layout(*osWindowManager, storage);

            std::vector<int2> cells;
            rects.clear();
            for (const ElementRef ref : refs)
            {
                const IntRect r = storage.GetElementRectUnsafe(ref).rect;
                rects.push_back(r);
                cells.push_back({ r.x / (100 + columnGap), r.y / 100 });
            }
            return cells;
        }
        std::vector<IntRect> rects;   ///< the children's rects from the last Cells
    };

    // An item spanning two columns goes where both are free, not over the item in the second.
    TEST_F(LayoutGridPlacementTest, ASpanningItemDoesNotOverlapAPlacedOne)
    {
        const auto cells = Cells({ { .col = 1, .row = 0 }, { .colSpan = 2 } });
        EXPECT_EQ(cells[0], (int2{ 1, 0 }));
        EXPECT_EQ(cells[1], (int2{ 0, 1 })) << "row 0 has only one free column";
    }

    // Items with only a column start stay in that column (not the first one), a row each.
    TEST_F(LayoutGridPlacementTest, AnItemWithOnlyAColumnKeepsIt)
    {
        (void)Cells({ { .col = 1 }, { .col = 1 } });
        EXPECT_GT(rects[0].x, 0) << "in column 1";
        EXPECT_EQ(rects[1].x, rects[0].x) << "the same column";
        EXPECT_EQ(rects[1].y, rects[0].y + 100) << "the next row";
    }

    // Items placed by both lines in different columns and rows keep their cells, and an auto
    // one takes the first free cell.
    TEST_F(LayoutGridPlacementTest, ExplicitItemsInWiderColumnsKeepTheirCells)
    {
        (void)Cells({ { .col = 0, .row = 1 }, { .col = 2, .row = 0 }, {} });
        EXPECT_EQ(rects[0].x, 0);
        EXPECT_EQ(rects[0].y, 100);
        EXPECT_EQ(rects[1].y, 0);
        EXPECT_GT(rects[1].x, 100) << "past column 1";
        EXPECT_EQ(rects[2].x, 0) << "the first free cell";
        EXPECT_EQ(rects[2].y, 0);
    }

    // A start line far past the limit lands on the last track the grid has, not thousands in:
    // the empty tracks before it are 0 wide, so it sits one gap per track in.
    TEST_F(LayoutGridPlacementTest, AStartPastTheLimitIsClamped)
    {
        (void)Cells({ { .col = 20000, .row = 0 } }, /*columnGap*/ 1, /*parentSize*/ 0);
        EXPECT_EQ(rects[0].x, GridAxisProperties::kMaxTracks - 1);
    }
}