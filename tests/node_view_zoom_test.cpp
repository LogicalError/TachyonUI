#include <gtest/gtest.h>

#include <gui_controls/gui_node_view_helpers.h>

namespace
{
    using namespace tachyon;

    // The canvas point that view position `p` (pixels from the canvas centre) shows.
    float2 CanvasAt(const NodeView& view, float2 p) { return view.transform.Inverse().TransformPoint(p); }

    TEST(NodeViewZoom, ZoomsAroundThePivot)
    {
        NodeView view;
        view.transform = float3x2::Scale(0.5f);
        const int2 pivot{ 40, -30 };
        const float2 before = CanvasAt(view, { 40.0f, -30.0f });
        ZoomNodeView(view, 1.25f, pivot);
        EXPECT_NEAR(NodeViewZoom(view), 0.625f, 1e-4f);
        const float2 after = CanvasAt(view, { 40.0f, -30.0f });
        EXPECT_NEAR(after.x, before.x, 1e-3f);
        EXPECT_NEAR(after.y, before.y, 1e-3f);
    }

    // Past 100% up to 400%, and down to 10%, by default.
    TEST(NodeViewZoom, StaysBetweenTenAndFourHundredPercent)
    {
        NodeView view;
        ZoomNodeView(view, 1.25f);
        EXPECT_NEAR(NodeViewZoom(view), 1.25f, 1e-4f) << "past 100%";
        for (int i = 0; i < 40; ++i)
            ZoomNodeView(view, 2.0f);
        EXPECT_NEAR(NodeViewZoom(view), 4.0f, 1e-4f);
        for (int i = 0; i < 40; ++i)
            ZoomNodeView(view, 0.5f);
        EXPECT_NEAR(NodeViewZoom(view), 0.1f, 1e-4f);
    }

    // An app's own range; one set wrong (none, or upside down) is put right.
    TEST(NodeViewZoom, TheViewsOwnRange)
    {
        NodeView view;
        view.minZoom = 0.5f;
        view.maxZoom = 2.0f;
        for (int i = 0; i < 10; ++i) ZoomNodeView(view, 2.0f);
        EXPECT_NEAR(NodeViewZoom(view), 2.0f, 1e-4f);
        for (int i = 0; i < 10; ++i) ZoomNodeView(view, 0.5f);
        EXPECT_NEAR(NodeViewZoom(view), 0.5f, 1e-4f);
        SetNodeViewCenter(view, {}, 8.0f);
        EXPECT_NEAR(NodeViewZoom(view), 2.0f, 1e-4f);
        SetNodeViewCenter(view, {}, 0.01f);
        EXPECT_NEAR(NodeViewZoom(view), 0.5f, 1e-4f);

        view.maxZoom = 0.25f;   // below the minimum: the minimum
        SetNodeViewCenter(view, {}, 3.0f);
        EXPECT_NEAR(NodeViewZoom(view), 0.5f, 1e-4f);
        view.minZoom = 0.0f;    // none: 10%
        view.maxZoom = 1.0f;
        SetNodeViewCenter(view, {}, 0.01f);
        EXPECT_NEAR(NodeViewZoom(view), 0.1f, 1e-4f);
        view.minZoom = -1.0f;
        SetNodeViewCenter(view, {}, 0.01f);
        EXPECT_NEAR(NodeViewZoom(view), 0.1f, 1e-4f);
    }

    // Fit doesn't magnify: a small graph shows at 100%; a large one fits.
    TEST(NodeViewZoom, FitStopsAtAHundredPercent)
    {
        NodeView view;
        view.lastInnerSize = { 1000, 800 };
        view.nodes.resize(1);
        view.nodes[0].lastSize = { 100, 50 };
        FrameAllNodes(view);
        EXPECT_NEAR(NodeViewZoom(view), 1.0f, 1e-4f);

        view.nodes[0].lastSize = { 2000, 400 };   // wider than the view
        FrameAllNodes(view);
        EXPECT_NEAR(NodeViewZoom(view), 1000.0f / 2000.0f * 0.9f, 1e-4f);
        view.nodes[0].lastSize = { 400, 1600 };   // taller
        FrameAllNodes(view);
        EXPECT_NEAR(NodeViewZoom(view), 800.0f / 1600.0f * 0.9f, 1e-4f);
    }

    TEST(NodeViewZoom, SetCenterShowsThatPointInTheMiddle)
    {
        NodeView view;
        SetNodeViewCenter(view, { 300.0f, -120.0f }, 0.5f);
        const float2 middle = CanvasAt(view, { 0.0f, 0.0f });
        EXPECT_NEAR(middle.x, 300.0f, 1e-3f);
        EXPECT_NEAR(middle.y, -120.0f, 1e-3f);
        EXPECT_NEAR(NodeViewZoom(view), 0.5f, 1e-4f);
    }

    TEST(NodeViewZoom, FitCentresOnAllNodesAndFitsThem)
    {
        NodeView view;
        view.lastInnerSize = { 400, 300 };
        view.nodes.resize(2);
        view.nodes[0].pos = { 0, 0 };     view.nodes[0].lastSize = { 100, 50 };
        view.nodes[1].pos = { 900, 600 }; view.nodes[1].lastSize = { 100, 50 };
        FrameAllNodes(view);

        const float2 middle = CanvasAt(view, { 0.0f, 0.0f });
        EXPECT_NEAR(middle.x, 500.0f, 1.0f);
        EXPECT_NEAR(middle.y, 325.0f, 1.0f);
        // Every node inside the visible area.
        const float2 topLeft     = CanvasAt(view, { -200.0f, -150.0f });
        const float2 bottomRight = CanvasAt(view, {  200.0f,  150.0f });
        EXPECT_LE(topLeft.x, 0.0f);
        EXPECT_LE(topLeft.y, 0.0f);
        EXPECT_GE(bottomRight.x, 1000.0f);
        EXPECT_GE(bottomRight.y, 650.0f);
    }

    // With nodes selected, Fit (and Home) frames those; with none, all of them.
    TEST(NodeViewZoom, FitFramesTheSelection)
    {
        NodeView view;
        view.lastInnerSize = { 400, 300 };
        view.nodes.resize(3);
        view.nodes[0].stableID = ID{ .value{ .integer = 1 } };
        view.nodes[1].stableID = ID{ .value{ .integer = 2 } };
        view.nodes[2].stableID = ID{ .value{ .integer = 3 } };
        view.nodes[0].pos = { 0, 0 };       view.nodes[0].lastSize = { 100, 50 };
        view.nodes[1].pos = { 900, 600 };   view.nodes[1].lastSize = { 100, 50 };
        view.nodes[2].pos = { 1000, 700 };  view.nodes[2].lastSize = { 100, 50 };

        view.selectedNodeIDs = { view.nodes[1].stableID, view.nodes[2].stableID };
        FrameSelectedNodes(view);
        float2 middle = CanvasAt(view, { 0.0f, 0.0f });
        EXPECT_NEAR(middle.x, 1000.0f, 1.0f) << "between the two selected";
        EXPECT_NEAR(middle.y, 675.0f, 1.0f);
        EXPECT_NEAR(NodeViewZoom(view), 1.0f, 1e-4f) << "they fit at 100%";

        view.selectedNodeIDs.clear();
        FrameSelectedNodes(view);
        middle = CanvasAt(view, { 0.0f, 0.0f });
        EXPECT_NEAR(middle.x, 550.0f, 1.0f) << "none selected: all of them";
        EXPECT_NEAR(middle.y, 375.0f, 1.0f);
        EXPECT_LT(NodeViewZoom(view), 1.0f);
    }

    // FrameNodes frames those given, skips indices out of range, and leaves the view as it
    // is for none.
    TEST(NodeViewZoom, FrameNodesFramesTheOnesGiven)
    {
        NodeView view;
        view.lastInnerSize = { 400, 300 };
        view.nodes.resize(2);
        view.nodes[0].pos = { -100, -100 }; view.nodes[0].lastSize = { 100, 50 };
        view.nodes[1].pos = { 900, 600 };   view.nodes[1].lastSize = { 100, 50 };
        const int one[] = { 1, 7, -1 };
        FrameNodes(view, one);
        const float2 middle = CanvasAt(view, { 0.0f, 0.0f });
        EXPECT_NEAR(middle.x, 950.0f, 1.0f);
        EXPECT_NEAR(middle.y, 625.0f, 1.0f);

        // In any order: the bottom-right one first.
        const int both[] = { 1, 0 };
        FrameNodes(view, both);
        const float2 between = CanvasAt(view, { 0.0f, 0.0f });
        EXPECT_NEAR(between.x, 450.0f, 1.0f);
        EXPECT_NEAR(between.y, 275.0f, 1.0f);

        const float3x2 before = view.transform;
        FrameNodes(view, std::span<const int>{});
        EXPECT_EQ(view.transform.r0.z, before.r0.z);
        EXPECT_EQ(view.transform.r1.z, before.r1.z);
        const int outOfRange[] = { 5 };
        FrameNodes(view, outOfRange);
        EXPECT_EQ(view.transform.r0.z, before.r0.z);
    }

    TEST(NodeViewScrollIntoView, ANodeInViewDoesntMove)
    {
        NodeView view;
        view.lastInnerSize = { 400, 300 };
        view.nodes.resize(1);
        view.nodes[0].pos = { -50, -20 }; view.nodes[0].lastSize = { 100, 40 };
        const float3x2 before = view.transform;
        ScrollNodeIntoView(view, 0, 10);
        EXPECT_EQ(view.transform.r0.z, before.r0.z);
        EXPECT_EQ(view.transform.r1.z, before.r1.z);
    }

    TEST(NodeViewScrollIntoView, PansByAsLittleAsBringsItIn)
    {
        NodeView view;
        view.lastInnerSize = { 400, 300 };
        view.transform = float3x2::Scale(0.5f);
        view.nodes.resize(1);
        view.nodes[0].pos = { 1000, -2000 }; view.nodes[0].lastSize = { 100, 40 };
        ScrollNodeIntoView(view, 0, 10);
        EXPECT_NEAR(NodeViewZoom(view), 0.5f, 1e-4f) << "the zoom stays";
        const float2 tl = view.transform.TransformPoint({ 1000.0f, -2000.0f });
        const float2 br = view.transform.TransformPoint({ 1100.0f, -1960.0f });
        EXPECT_NEAR(br.x, 200.0f - 10.0f, 1e-3f) << "right edge just inside";
        EXPECT_NEAR(tl.y, -150.0f + 10.0f, 1e-3f) << "top edge just inside";
    }

    TEST(NodeViewScrollIntoView, ANodeLargerThanTheViewShowsItsTopLeft)
    {
        NodeView view;
        view.lastInnerSize = { 400, 300 };
        view.nodes.resize(1);
        view.nodes[0].pos = { 2000, 2000 }; view.nodes[0].lastSize = { 900, 700 };
        ScrollNodeIntoView(view, 0, 10);
        const float2 tl = view.transform.TransformPoint({ 2000.0f, 2000.0f });
        EXPECT_NEAR(tl.x, -200.0f + 10.0f, 1e-3f);
        EXPECT_NEAR(tl.y, -150.0f + 10.0f, 1e-3f);
    }
}
