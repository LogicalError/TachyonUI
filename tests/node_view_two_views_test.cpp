// One graph shown in two places: a NodeGraph and a NodeViewState for each view of it
// (RenderNodeView({ graph, state })). Each view pans, zooms and selects on its own; an edit
// made in one shows in both.

#include <gtest/gtest.h>

#include "headless_gui_app.h"
#include "misuse_reports.h"

#include <gui_controls/gui_node_view_helpers.h>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace
    {
        class NodeViewTwoViewsTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;
            NodeGraph      graph;
            NodeViewState  left, right;
            ID             nodeA{}, nodeB{};

            void SetUp() override
            {
                ASSERT_TRUE(app.Initialize());
                nodeA = app.idManager.CreateStaticID();
                nodeB = app.idManager.CreateStaticID();
                graph.nodes = {
                    NodeModel{ .stableID = nodeA, .title = "A", .pos = { 40, 40 },  .properties = { NodeProperty{ .output = true } } },
                    NodeModel{ .stableID = nodeB, .title = "B", .pos = { 320, 160 }, .properties = { NodeProperty{ .input = PropertyEndpoint{} } } },
                };
            }

            void TearDown() override { app.Cleanup(); }

            // Both views, side by side: the left one, then the right one.
            void Frame()
            {
                app.Frame([&] {
                    app.BeginRootWindow({ 1600, 800 }, ElementFlags::None, app.theme.mainWindowStyleRef);
                    app.BeginHorizontal(app.idManager.AllocateDynamicID("views"), app.theme.flexFillRowStyleRef,
                                        LayoutOverride::Auto().FlexGrow(1));
                    app.RenderNodeView({ graph, left });
                    app.RenderNodeView({ graph, right });
                    app.EndHorizontal();
                    app.EndRootWindow();
                });
            }
            void Settle() { Frame(); Frame(); }

            IntRect NodeRect(const NodeViewState& view, ID node) const { return app.GetElementRect(NodeElementID(view, node)); }
            int2 Header(const NodeViewState& view, ID node) const
            {
                const IntRect r = NodeRect(view, node);
                return { r.x + r.w / 2, r.y + 6 };
            }
        };

        // Each view has its own elements for the graph's nodes: none share an ID, and each
        // shows every node in its own half.
        TEST_F(NodeViewTwoViewsTest, EachViewBuildsItsOwnNodes)
        {
            GUITest::MisuseReports reports;
            Settle();
            EXPECT_FALSE(reports.Contains("share an ID")) << reports.All();
            for (const ID node : { nodeA, nodeB })
            {
                const IntRect l = NodeRect(left, node), r = NodeRect(right, node);
                EXPECT_GT(l.w, 0);
                EXPECT_GT(r.w, 0);
                EXPECT_LT(l.x, 800) << "in the left half";
                EXPECT_GE(r.x, 800) << "in the right half";
            }
        }

        // Zooming one view leaves the other as it was.
        TEST_F(NodeViewTwoViewsTest, EachViewZoomsOnItsOwn)
        {
            Settle();
            const float3x2 rightBefore = right.transform;
            ZoomNodeView({ graph, left }, 2.0f);
            Settle();
            EXPECT_FLOAT_EQ(NodeViewZoom({ graph, left }), 2.0f);
            EXPECT_FLOAT_EQ(NodeViewZoom({ graph, right }), 1.0f);
            EXPECT_EQ(right.transform.r0, rightBefore.r0) << "the other view's pan and zoom as they were";
            EXPECT_EQ(right.transform.r1, rightBefore.r1);
        }

        // A click selects in the view clicked; the other keeps its own selection.
        TEST_F(NodeViewTwoViewsTest, EachViewSelectsOnItsOwn)
        {
            Settle();
            app.ClickAt(Header(left, nodeA));
            Settle();
            EXPECT_EQ(left.selectedNodeIDs, (std::unordered_set<ID>{ nodeA }));
            EXPECT_TRUE(right.selectedNodeIDs.empty());

            app.ClickAt(Header(right, nodeB));
            Settle();
            EXPECT_EQ(right.selectedNodeIDs, (std::unordered_set<ID>{ nodeB }));
            EXPECT_EQ(left.selectedNodeIDs, (std::unordered_set<ID>{ nodeA })) << "unchanged by the other view";
        }

        // A node dragged in one view moves in both: they show one graph.
        TEST_F(NodeViewTwoViewsTest, ADragInOneViewMovesTheNodeInBoth)
        {
            Settle();
            const int2 before = graph.nodes[0].pos;
            const IntRect rightBefore = NodeRect(right, nodeA);
            const int2 from = Header(left, nodeA);
            app.InjectMouseMove(from);
            app.InjectMouseDown(from);
            Frame();
            app.InjectMouseMove({ from.x + 60, from.y + 40 }, MouseButtons::Left);
            Frame();
            app.InjectMouseUp({ from.x + 60, from.y + 40 });
            Settle();
            EXPECT_NE(graph.nodes[0].pos, before);
            EXPECT_NE(NodeRect(right, nodeA).position, rightBefore.position) << "the other view shows where it went";
        }

        // A node removed through one view goes from the other's selection on its next build,
        // which reports the change.
        TEST_F(NodeViewTwoViewsTest, ANodeRemovedInOneViewLeavesTheOthersSelection)
        {
            int rightChanges = 0;
            right.onSelectionChanged = [&] { ++rightChanges; };
            right.selectedNodeIDs = { nodeB };
            Settle();
            ASSERT_EQ(rightChanges, 1);

            const int b = 1;
            ApplyNodeViewEdit({ graph, left }, RemoveNodesEdit({ graph, left }, std::span<const int>(&b, 1)));
            Settle();
            ASSERT_EQ(graph.nodes.size(), 1u);
            EXPECT_TRUE(right.selectedNodeIDs.empty()) << "nothing left of it to select";
            EXPECT_EQ(rightChanges, 2) << "reported";
            EXPECT_GT(NodeRect(right, nodeA).w, 0) << "the rest still shown";
        }
    }
}
