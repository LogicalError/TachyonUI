// A wire dropped on a node's body connects to the node's first input that takes it
// (FirstInputAccepting in gui_node_view_helpers).

#include <gtest/gtest.h>

#include <gui_controls/gui_node_view_helpers.h>

namespace
{
    using namespace tachyon;

    // Node 0 has one output; node 1 has a row without an input, then two inputs.
    NodeView MakeView()
    {
        NodeView view;
        view.nodes.resize(2);
        view.nodes[0].properties.resize(1);
        view.nodes[0].properties[0].output = true;
        view.nodes[1].properties.resize(3);
        view.nodes[1].properties[1].input = PropertyEndpoint{};
        view.nodes[1].properties[2].input = PropertyEndpoint{};
        return view;
    }

    constexpr PropertyEndpoint kOutput{ 0, 0 };

    TEST(NodeViewDrop, PicksTheFirstInput)
    {
        const NodeView view = MakeView();
        EXPECT_EQ(FirstInputAccepting(view, kOutput, 1), (PropertyEndpoint{ 1, 1 }));
    }

    TEST(NodeViewDrop, SkipsInputsTheViewRefuses)
    {
        NodeView view = MakeView();
        view.canConnect = [](const NodeView&, PropertyEndpoint, PropertyEndpoint in) { return in.propIdx != 1; };
        EXPECT_EQ(FirstInputAccepting(view, kOutput, 1), (PropertyEndpoint{ 1, 2 }));
    }

    TEST(NodeViewDrop, NothingWhenNoInputTakesIt)
    {
        NodeView view = MakeView();
        view.canConnect = [](const NodeView&, PropertyEndpoint, PropertyEndpoint) { return false; };
        EXPECT_FALSE(FirstInputAccepting(view, kOutput, 1).valid());
    }

    TEST(NodeViewDrop, NeverTheSourceNodeItself)
    {
        NodeView view = MakeView();
        view.nodes[0].properties[0].input = PropertyEndpoint{};
        EXPECT_FALSE(FirstInputAccepting(view, kOutput, 0).valid());
    }

    TEST(NodeViewDrop, AnInputAlreadyInUseStillTakesIt)
    {
        NodeView view = MakeView();
        view.nodes[1].properties[1].input = PropertyEndpoint{ 0, 0 };
        EXPECT_EQ(FirstInputAccepting(view, kOutput, 1), (PropertyEndpoint{ 1, 1 }));
    }

    // A wire dragged from an input, dropped on a node: its first output that may feed it.
    TEST(NodeViewDrop, AWireFromAnInputPicksTheFirstOutput)
    {
        NodeView view = MakeView();
        view.nodes[0].properties.resize(3);
        view.nodes[0].properties[0].output = false;
        view.nodes[0].properties[1].output = true;
        view.nodes[0].properties[2].output = true;
        constexpr PropertyEndpoint kInput{ 1, 1 };
        EXPECT_EQ(FirstOutputAccepting(view, kInput, 0), (PropertyEndpoint{ 0, 1 }));

        view.canConnect = [](const NodeView&, PropertyEndpoint out, PropertyEndpoint) { return out.propIdx != 1; };
        EXPECT_EQ(FirstOutputAccepting(view, kInput, 0), (PropertyEndpoint{ 0, 2 })) << "skips what the view refuses";

        view.canConnect = [](const NodeView&, PropertyEndpoint, PropertyEndpoint) { return false; };
        EXPECT_FALSE(FirstOutputAccepting(view, kInput, 0).valid());
        view.canConnect = {};
        view.nodes[1].properties[0].output = true;
        EXPECT_FALSE(FirstOutputAccepting(view, kInput, 1).valid()) << "never the input's own node";
        EXPECT_FALSE(FirstOutputAccepting(view, kInput, 2).valid()) << "no such node";
        EXPECT_FALSE(FirstOutputAccepting(view, kInput, -1).valid());
    }

    // Which node a canvas point is on: the frontmost of those whose last laid-out rect holds it.
    TEST(NodeViewDrop, TheNodeAtACanvasPoint)
    {
        NodeView view = MakeView();
        view.nodes[0].pos = { 0, 0 };     view.nodes[0].lastSize = { 100, 60 };
        view.nodes[1].pos = { 50, 30 };   view.nodes[1].lastSize = { 100, 60 };
        view.ordering = { 0, 1 };
        EXPECT_EQ(NodeAtCanvasPos(view, { 10.0f, 10.0f }), 0);
        EXPECT_EQ(NodeAtCanvasPos(view, { 60.0f, 40.0f }), 1) << "both: the front one";
        view.ordering = { 1, 0 };
        EXPECT_EQ(NodeAtCanvasPos(view, { 60.0f, 40.0f }), 0);
        EXPECT_EQ(NodeAtCanvasPos(view, { 149.0f, 89.0f }), 1) << "its last pixel";
        EXPECT_EQ(NodeAtCanvasPos(view, { 150.0f, 50.0f }), -1) << "right of it";
        EXPECT_EQ(NodeAtCanvasPos(view, { 60.0f, 90.0f }), -1) << "below it";
        EXPECT_EQ(NodeAtCanvasPos(view, { 0.0f, 0.0f }), 0) << "its first pixel";
        EXPECT_EQ(NodeAtCanvasPos(view, { -1.0f, 10.0f }), -1) << "left of it";
        EXPECT_EQ(NodeAtCanvasPos(view, { 10.0f, -1.0f }), -1) << "above it";
        view.ordering = { 0, 5, -1 };
        EXPECT_EQ(NodeAtCanvasPos(view, { 10.0f, 10.0f }), 0) << "indices out of range are skipped";
    }

    // Through the place and pan/zoom the view was last drawn with, and back.
    TEST(NodeViewDrop, CanvasAndScreenPointsThroughTheView)
    {
        NodeView view;
        view.lastCanvasRect = { 100, 50, 800, 600 };
        view.lastInnerSize  = { 800, 600 };
        view.transform      = float3x2::Scale(0.5f);
        const float2 screen = NodeViewCanvasToScreen(view, { 100.0f, -100.0f });
        EXPECT_FLOAT_EQ(screen.x, 550.0f);
        EXPECT_FLOAT_EQ(screen.y, 300.0f);
        const float2 back = NodeViewScreenToCanvas(view, { 550, 300 });
        EXPECT_FLOAT_EQ(back.x, 100.0f);
        EXPECT_FLOAT_EQ(back.y, -100.0f);
        view.transform = float3x2::Translate(float2{ 40.0f, -20.0f });
        const float2 panned = NodeViewCanvasToScreen(view, { 0.0f, 0.0f });
        EXPECT_FLOAT_EQ(panned.x, 540.0f);
        EXPECT_FLOAT_EQ(panned.y, 330.0f);
    }

    // A screen point in node.pos space: from the canvas centre, through the inverse pan/zoom.
    TEST(NodeViewDrop, AScreenPointOnTheCanvas)
    {
        const IntRect canvas{ 100, 50, 800, 600 };   // centre (500, 350)
        EXPECT_EQ(NodeViewScreenToCanvas(float3x2::Scale(1.0f), canvas, { 500, 350 }).x, 0.0f);
        const float2 panned = NodeViewScreenToCanvas(float3x2::Translate(float2{ 40.0f, -20.0f }), canvas, { 510, 360 });
        EXPECT_FLOAT_EQ(panned.x, -30.0f);
        EXPECT_FLOAT_EQ(panned.y, 30.0f);
        const float2 zoomed = NodeViewScreenToCanvas(float3x2::Scale(0.5f), canvas, { 550, 300 });
        EXPECT_FLOAT_EQ(zoomed.x, 100.0f);
        EXPECT_FLOAT_EQ(zoomed.y, -100.0f);
    }
}
