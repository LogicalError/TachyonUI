// Integration tests for VirtualTreeView (the flat-data tree-view widget,
// backed by VirtualTable + TreeCell). Selection and scroll live on
// TableState; the widget exposes sub-range row IDs as TableState.selectedRowID
// - translate to the dataset (flat) index via visibleIdx.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_core/gui_id.h>
#include <gui_core/gui_id_manager.h>
#include <gui_core/state/gui_state_manager.h>
#include <gui_core/elements/gui_element_storage.h>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace
    {
        class VirtualTreeViewTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;
            ID treeID{};

            std::vector<tachyon::TreeViewRow> rows;
            std::vector<int>                         visibleIdx;
            tachyon::TableState               state;

            void SetUp() override
            {
                ASSERT_TRUE(app.Initialize());
                treeID = app.idManager.CreateStaticID();
            }
            void TearDown() override { app.Cleanup(); }

            // Build a tree with N depth-0 nodes, each with `childrenPerNode`
            // depth-1 children. Leaves use static IDs starting at base+i.
            void BuildFlatTree(int n, int childrenPerNode = 0,
                               bool branchesOpen = true)
            {
                rows.clear();
                int idCounter = 5000;
                auto makeID = [&]() {
                    ID id{};
                    id.value.dynamicParts.mode = IDMode::Static;
                    id.value.dynamicParts.hashValue = ++idCounter;
                    return id;
                };
                for (int i = 0; i < n; ++i)
                {
                    tachyon::TreeViewRow row{};
                    row.id     = makeID();
                    row.name   = "Branch";
                    row.depth  = 0;
                    row.isOpen = branchesOpen;
                    rows.push_back(std::move(row));
                    for (int c = 0; c < childrenPerNode; ++c)
                    {
                        tachyon::TreeViewRow child{};
                        child.id    = makeID();
                        child.name  = "Leaf";
                        child.depth = 1;
                        rows.push_back(std::move(child));
                    }
                }
                app.RebuildTreeViewIndex(rows);
            }

            void RenderFrame()
            {
                app.Frame([&] {
                    app.BeginRootWindow();
                    app.VirtualTreeView(
                        treeID, rows, visibleIdx, state,
                        StyleRef{}, LayoutOverride{}.Size(400, 300));
                    app.EndRootWindow();
                });
            }

            // Row backdrop ID for a visible row index (usable outside a frame).
            ID RowIDAt(int visIdx) const
            {
                return state.RowIDForIndex(visIdx);
            }
        };

        TEST_F(VirtualTreeViewTest, RendersAllVisibleRows)
        {
            BuildFlatTree(/*n*/3, /*children*/0);
            RenderFrame();

            ASSERT_EQ(int(visibleIdx.size()), 3);
            for (int i = 0; i < 3; ++i)
                EXPECT_TRUE(app.HasElement(RowIDAt(i))) << "row " << i;
        }

        TEST_F(VirtualTreeViewTest, ClosedBranchHidesChildren)
        {
            BuildFlatTree(/*n*/2, /*children*/2, /*open*/true);
            // After construction:  Branch0 / Leaf / Leaf / Branch1 / Leaf / Leaf.
            EXPECT_EQ(int(rows.size()), 6);

            // Close the first branch - its two children should drop out
            // of the visible-index walk.
            rows[0].isOpen = false;
            RenderFrame();
            EXPECT_EQ(int(visibleIdx.size()), 4)
                << "Branch0 closed → Branch0 + Branch1 + 2 children of Branch1";

            // Reopen.
            rows[0].isOpen = true;
            RenderFrame();
            EXPECT_EQ(int(visibleIdx.size()), 6);
        }

        TEST_F(VirtualTreeViewTest, ClickingRowSetsSubRangeSelectedRowID)
        {
            BuildFlatTree(/*n*/3, /*children*/0);
            RenderFrame();

            const auto rect = app.GetElementRect(RowIDAt(1));
            app.ClickAt({ rect.x + rect.w / 2, rect.y + rect.h / 2 });
            RenderFrame();

            EXPECT_EQ(state.selectedRowID, RowIDAt(1));
        }

        // Row IDs are visible positions, so expand/collapse above the
        // selection must carry the selection along to its node.
        TEST_F(VirtualTreeViewTest, SelectionFollowsNodeAcrossCollapseAndExpand)
        {
            BuildFlatTree(/*n*/2, /*children*/2, /*open*/true);
            // Branch0 / Leaf / Leaf / Branch1 / Leaf / Leaf
            RenderFrame();
            state.selectedRowID = RowIDAt(4);   // Branch1's first leaf (flat 4)

            rows[0].isOpen = false;
            RenderFrame();
            ASSERT_EQ(int(visibleIdx.size()), 4);
            EXPECT_EQ(state.selectedRowID, RowIDAt(2)) << "flat 4 is now visible row 2";

            rows[0].isOpen = true;
            RenderFrame();
            EXPECT_EQ(state.selectedRowID, RowIDAt(4));
        }

        // Rows removed or moved above the selection: it stays on its row (by the row's own ID).
        TEST_F(VirtualTreeViewTest, SelectionFollowsItsRowWhenRowsAboveChange)
        {
            BuildFlatTree(/*n*/3, /*children*/0);
            RenderFrame();
            const ID third = rows[2].id;
            state.selectedRowID = RowIDAt(2);

            rows.erase(rows.begin());
            app.RebuildTreeViewIndex(rows);
            RenderFrame();
            EXPECT_EQ(state.selectedRowID, RowIDAt(1)) << "the row moved up one";
            ASSERT_EQ(rows[1].id, third);

            std::swap(rows[0], rows[1]);   // same positions shown, other rows in them
            app.RebuildTreeViewIndex(rows);
            RenderFrame();
            EXPECT_EQ(state.selectedRowID, RowIDAt(0));

            rows.erase(rows.begin());      // the selected row itself
            app.RebuildTreeViewIndex(rows);
            RenderFrame();
            EXPECT_EQ(state.selectedRowID, ID::Invalid());
        }

        TEST_F(VirtualTreeViewTest, CollapsingHidesSelectionOntoTheBranch)
        {
            BuildFlatTree(/*n*/2, /*children*/2, /*open*/true);
            RenderFrame();
            state.selectedRowID = RowIDAt(5);   // Branch1's second leaf

            rows[3].isOpen = false;
            RenderFrame();
            EXPECT_EQ(state.selectedRowID, RowIDAt(3)) << "selection moves to the collapsed branch";
        }

        // A tree built on VirtualTable directly keeps its selection on its nodes the same way.
        TEST_F(VirtualTreeViewTest, ATreeOnAPlainTableKeepsItsSelectionOnItsNodes)
        {
            BuildFlatTree(/*n*/2, /*children*/2, /*open*/true);
            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow();
                    GUIApplication::WalkTreeViewVisibility(rows, visibleIdx, state);
                    VirtualTableColumn cols[1];
                    cols[0].column = TableColumn{ .header = {} };
                    cols[0].generateCell = [&](int row) {
                        TreeViewRow& r = rows[visibleIdx[row]];
                        app.TreeCell(r.name, r.depth, r.HasChildren() ? &r.isOpen : nullptr);
                    };
                    app.VirtualTable(treeID, state, std::span<VirtualTableColumn>(cols, 1), int(visibleIdx.size()), 0,
                                     StyleRef{}, LayoutOverride{}.Size(400, 300));
                    app.EndRootWindow();
                });
            };
            frame();
            state.selectedRowID = RowIDAt(4);   // Branch1's first leaf (flat 4)

            rows[0].isOpen = false;
            frame();
            EXPECT_EQ(state.selectedRowID, RowIDAt(2)) << "flat 4 is now visible row 2";
        }

        TEST_F(VirtualTreeViewTest, MultiSelectionFollowsNodes)
        {
            BuildFlatTree(/*n*/2, /*children*/2, /*open*/true);
            MultiSelectState ms;
            state.multiSelect = &ms;
            RenderFrame();
            ms.selectedIDs = { RowIDAt(1), RowIDAt(4) };   // leaf under Branch0, leaf under Branch1
            ms.lastClickedID = RowIDAt(4);

            rows[0].isOpen = false;
            RenderFrame();
            EXPECT_EQ(ms.selectedIDs.size(), 1u) << "the hidden leaf drops out";
            EXPECT_EQ(ms.selectedIDs.count(RowIDAt(2)), 1u);
            EXPECT_EQ(ms.lastClickedID, RowIDAt(2));
        }

        TEST_F(VirtualTreeViewTest, RebuildIndexComputesSkipCount)
        {
            BuildFlatTree(/*n*/2, /*children*/3);
            // Branch (3 descendants) / 3 leaves / Branch (3 descendants) / 3 leaves
            EXPECT_EQ(rows[0].skipCount, 3);
            EXPECT_EQ(rows[1].skipCount, 0);  // leaf
            EXPECT_EQ(rows[4].skipCount, 3);
            EXPECT_TRUE(rows[0].HasChildren());
            EXPECT_FALSE(rows[1].HasChildren());
        }

        // In row selection mode a double-click on a branch row goes to the caller's
        // onRowDoubleClick (which overrides the default branch toggle).
        TEST_F(VirtualTreeViewTest, RowDoubleClickOnBranchReachesCallback)
        {
            BuildFlatTree(/*n*/1, /*children*/1, /*open*/true);
            std::vector<tachyon::ID> doubleClicked;
            state.onRowDoubleClick = [&](tachyon::ID rowID) { doubleClicked.push_back(rowID); };
            RenderFrame();

            const tachyon::int2 pt = app.CenterOf(RowIDAt(0));
            app.ClickAt(pt);
            RenderFrame();
            app.ClickAt(pt);
            RenderFrame();

            ASSERT_EQ(doubleClicked.size(), 1u);
            EXPECT_EQ(doubleClicked[0], RowIDAt(0));
            EXPECT_TRUE(rows[0].isOpen) << "the callback replaces the default toggle";
        }

        // Double-clicking a branch's icon toggles the branch, like double-clicking
        // the rest of the row.
        TEST_F(VirtualTreeViewTest, DoubleClickOnBranchIconToggles)
        {
            BuildFlatTree(/*n*/1, /*children*/1, /*open*/true);
            rows[0].icon = app.theme.addIcon;
            RenderFrame();

            // The icon element is the branch row's only IconID-bearing tree icon.
            tachyon::IntRect iconRect{};
            for (uint32_t d = 0; d < app.elementStorage.NumberOfDepthLevels(); d++)
                for (uint32_t i = 0; i < app.elementStorage.DepthSizeOf(d); i++)
                {
                    tachyon::ElementRef ref{ d, i };
                    if (!app.elementStorage.IsValidRef(ref)) continue;
                    if (app.elementStorage.GetElementUnsafe(ref).styleRef != app.theme.treeTableIconStyleRef) continue;
                    if (const auto* rp = app.elementStorage.GetElementRectPtr(ref)) { iconRect = rp->rect; }
                }
            ASSERT_GT(iconRect.w, 0);
            const tachyon::int2 pt{ iconRect.x + iconRect.w / 2, iconRect.y + iconRect.h / 2 };

            app.ClickAt(pt);
            RenderFrame();
            app.ClickAt(pt);
            RenderFrame();
            EXPECT_FALSE(rows[0].isOpen);
        }

        // The row double-click tracker is per-table: a click on table A between
        // the two clicks of a double-click on table B must not cancel B's toggle.
        // A slow re-click on a selected renameable row starts a rename, except when
        // rows are draggable: the re-click would fire before the drag threshold.
        TEST_F(VirtualTreeViewTest, SlowReClickRenamesOnlyWithoutDragAndDrop)
        {
            auto slowReClickStartsRename = [&](bool dragDrop) {
                BuildFlatTree(/*n*/2);
                std::string name = "Row";
                rows[0].renameBuffer = &name;
                state = {};
                ItemDragDropConfig dd{ .payloadType = 1 };
                state.rowDragDrop = dragDrop ? &dd : nullptr;
                app.m_renameSub->Cancel();

                auto render = [&] {
                    app.BeginRootWindow();
                    app.VirtualTreeView(treeID, rows, visibleIdx, state, StyleRef{}, LayoutOverride{}.Size(400, 150));
                    app.EndRootWindow();
                };
                app.Frame(render);
                const tachyon::ID row0 = state.RowIDForIndex(0);
                const tachyon::int2 pt = app.CenterOf(row0);
                app.ClickAt(pt);
                app.Frame(render);
                app.Frame(render);
                EXPECT_EQ(state.selectedRowID, row0) << "first click selects";

                state.internal.rowClickTracker.lastTimeNanos -= 1'000'000'000;   // the re-click comes a second later
                app.ClickAt(pt);
                app.Frame(render);
                app.Frame(render);
                const bool renaming = app.m_renameSub->isActive;
                app.m_renameSub->Cancel();
                return renaming;
            };
            EXPECT_TRUE(slowReClickStartsRename(false));
            EXPECT_FALSE(slowReClickStartsRename(true));
        }

        // A gap between two rows has one drop line: below a row and above the next is the same
        // place when the next row is where the drop goes, so it's the next row's Before.
        TEST_F(VirtualTreeViewTest, AGapBetweenRowsHasOneDropLine)
        {
            BuildFlatTree(/*n*/2, /*children*/2, /*open*/true);
            // Branch0 / Leaf / Leaf / Branch1 / Leaf / Leaf
            struct Reorder { uintptr_t source; ID target; TreeDropPosition position; };
            std::vector<Reorder> reorders;
            ItemDragDropConfig dd{ .payloadType = 1 };
            dd.getPayload = [](ID rowID) { return uintptr_t(rowID.value.subRangeParts.index); };
            dd.onReorder  = [&](uintptr_t source, ID target, TreeDropPosition position) {
                reorders.push_back({ source, target, position });
            };
            state.rowDragDrop = &dd;
            RenderFrame();
            RenderFrame();

            // Drags row 5 to `y` in `row`; the drop target while over it.
            auto dragTo = [&](int row, int y) {
                const IntRect from = app.GetElementRect(RowIDAt(5));
                const IntRect to   = app.GetElementRect(RowIDAt(row));
                const int2 start{ from.x + 60, from.y + from.h / 2 };
                const int2 end{ to.x + 60, to.y + y };
                app.InjectMouseMove(start);
                app.InjectMouseDown(start);
                RenderFrame();
                app.InjectMouseMove(int2{ start.x, (start.y + end.y) / 2 }, MouseButtons::Left);
                RenderFrame();
                app.InjectMouseMove(end, MouseButtons::Left);
                RenderFrame();
                const TreeDropState over = app.m_treeDropState;
                app.InjectMouseUp(end);
                RenderFrame();
                RenderFrame();
                return over;
            };
            const int h = app.GetElementRect(RowIDAt(0)).h;

            // An open branch's bottom: its first child's top.
            TreeDropState over = dragTo(0, h - 2);
            EXPECT_EQ(over.targetRowID, RowIDAt(1));
            EXPECT_EQ(over.position, TreeDropPosition::Before);
            EXPECT_EQ(over.targetDepth, 1);
            ASSERT_EQ(reorders.size(), 1u);
            EXPECT_EQ(reorders[0].target, RowIDAt(1)) << "the drop goes where the line is";
            EXPECT_EQ(reorders[0].position, TreeDropPosition::Before);

            // A leaf's bottom with a sibling after it: that sibling's top.
            over = dragTo(1, h - 2);
            EXPECT_EQ(over.targetRowID, RowIDAt(2));
            EXPECT_EQ(over.position, TreeDropPosition::Before);

            // The last child's bottom stays its own: the next row is its parent's sibling.
            over = dragTo(2, h - 2);
            EXPECT_EQ(over.targetRowID, RowIDAt(2));
            EXPECT_EQ(over.position, TreeDropPosition::After);
        }

        TEST_F(VirtualTreeViewTest, DoubleClickToggleTrackerIsPerTableNotShared)
        {
            // Tree A = the fixture's tree: one open branch + one child.
            BuildFlatTree(/*n*/1, /*children*/1, /*open*/true);

            // Tree B = an independent tree view (own id / rows / state).
            const tachyon::ID treeIDB = app.idManager.CreateStaticID();
            std::vector<tachyon::TreeViewRow> rowsB;
            std::vector<int>                         visibleIdxB;
            tachyon::TableState               stateB{};
            {
                int idc = 6000;
                auto makeID = [&] {
                    tachyon::ID id{};
                    id.value.dynamicParts.mode      = IDMode::Static;
                    id.value.dynamicParts.hashValue = ++idc;
                    return id;
                };
                tachyon::TreeViewRow branch{};
                branch.id = makeID(); branch.name = "Branch"; branch.depth = 0; branch.isOpen = true;
                rowsB.push_back(std::move(branch));
                tachyon::TreeViewRow leaf{};
                leaf.id = makeID(); leaf.name = "Leaf"; leaf.depth = 1;
                rowsB.push_back(std::move(leaf));
                app.RebuildTreeViewIndex(rowsB);
            }

            auto renderBoth = [&] {
                app.BeginRootWindow();
                app.VirtualTreeView(treeID,  rows,  visibleIdx,  state,
                                    StyleRef{}, LayoutOverride{}.Size(400, 150));
                app.VirtualTreeView(treeIDB, rowsB, visibleIdxB, stateB,
                                    StyleRef{}, LayoutOverride{}.Size(400, 150));
                app.EndRootWindow();
            };
            app.Frame(renderBoth);

            const tachyon::ID branchA = state.RowIDForIndex(0);
            const tachyon::ID branchB = stateB.RowIDForIndex(0);
            ASSERT_TRUE(app.HasElement(branchA));
            ASSERT_TRUE(app.HasElement(branchB));
            const tachyon::int2 ptA = app.CenterOf(branchA);
            const tachyon::int2 ptB = app.CenterOf(branchB);
            ASSERT_TRUE(ptA.x != ptB.x || ptA.y != ptB.y)
                << "tree views overlap: A=(" << ptA.x << "," << ptA.y << ") "
                << "B=(" << ptB.x << "," << ptB.y << ")";
            ASSERT_TRUE(rowsB[0].isOpen);

            // B click 1 -> A click -> B click 2. A consumed MouseDown captures the mouse
            // until a frame after button-up, so each click settles before the next.
            auto clickAndSettle = [&](tachyon::int2 pt) {
                app.ClickAt(pt);
                app.Frame(renderBoth);   // dispatch the click
                app.Frame(renderBoth);   // let the deferred capture-release fire
            };
            clickAndSettle(ptB);
            clickAndSettle(ptA);
            app.ClickAt(ptB);
            app.Frame(renderBoth);

            // Sanity: the interleaved click must actually have reached table A,
            // otherwise the test wouldn't exercise cross-table contamination.
            ASSERT_EQ(state.selectedRowID, branchA)
                << "interleaved click did not land on table A — test is vacuous";

            EXPECT_FALSE(rowsB[0].isOpen)
                << "B's double-click branch toggle was suppressed by an interleaved "
                   "click on table A — the double-click tracker is shared across tables.";
        }

    } // namespace
} // namespace tachyon
