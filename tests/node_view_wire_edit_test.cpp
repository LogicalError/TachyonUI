// Picking and removing wires: WireAt finds the wire under the mouse, DisconnectEdit
// removes it (gui_node_view_helpers).

#include <gtest/gtest.h>

#include <gui_controls/gui_node_view_helpers.h>

#include <vector>

namespace
{
    using namespace tachyon;

    const std::vector<DrawnWire> kWires{
        { .wireKey = WireKey(1, 0), .points = { { 0, 0 },  { 100, 0 }, { 100, 100 } } },
        { .wireKey = WireKey(2, 3), .points = { { 0, 20 }, { 60, 20 },  { 60, 100 } } },
    };

    TEST(NodeViewWireEdit, WireAtFindsTheWireUnderThePoint)
    {
        EXPECT_EQ(WireAt(kWires, { 50, 2 },   5.0f), WireKey(1, 0)) << "near a horizontal run";
        EXPECT_EQ(WireAt(kWires, { 103, 60 }, 5.0f), WireKey(1, 0)) << "near a vertical run";
        EXPECT_EQ(WireAt(kWires, { 30, 18 },  5.0f), WireKey(2, 3));
    }

    TEST(NodeViewWireEdit, WireAtTakesTheClosest)
    {
        EXPECT_EQ(WireAt(kWires, { 30, 8 },  20.0f), WireKey(1, 0));
        EXPECT_EQ(WireAt(kWires, { 30, 13 }, 20.0f), WireKey(2, 3));
    }

    TEST(NodeViewWireEdit, WireAtFindsNothingFarAway)
    {
        EXPECT_FALSE(WireAt(kWires, { 30, 60 }, 5.0f).has_value());
        EXPECT_FALSE(WireAt(kWires, { 108, 0 }, 5.0f).has_value()) << "past the end of a run";
        EXPECT_FALSE(WireAt({}, { 0, 0 }, 5.0f).has_value());
    }

    // A curved wire is hit along the curve it's drawn as, not along its control points.
    TEST(NodeViewWireEdit, WireAtFollowsACurvedWire)
    {
        const DrawnWire curve{ .wireKey = WireKey(1, 0), .points = { { 0, 0 }, { 100, 0 }, { 100, 100 }, { 200, 100 } }, .curved = true };
        const std::vector<DrawnWire> wires{ curve };
        for (const float t : { 0.1f, 0.3f, 0.5f, 0.7f, 0.9f })
        {
            const float2 p = WireCurvePoint(curve.points[0], curve.points[1], curve.points[2], curve.points[3], t);
            EXPECT_EQ(WireAt(wires, { int(std::lround(p.x)), int(std::lround(p.y)) }, 3.0f), WireKey(1, 0)) << t;
        }
        EXPECT_FALSE(WireAt(wires, { 100, 0 }, 3.0f).has_value()) << "a control point, off the curve";
        EXPECT_FALSE(WireAt(wires, { 130, 100 }, 3.0f).has_value()) << "on the control lines, off the curve";

        DrawnWire straight = curve;
        straight.curved = false;
        EXPECT_EQ(WireAt(std::vector<DrawnWire>{ straight }, { 100, 0 }, 3.0f), WireKey(1, 0)) << "as a polyline";
    }

    // The curve the renderer draws for four points: two quadratics meeting halfway between
    // the control points.
    TEST(NodeViewWireEdit, WireCurvePointRunsThroughTheEndsAndTheMiddle)
    {
        const int2 p0{ 0, 0 }, p1{ 100, 0 }, p2{ 60, 80 }, p3{ 200, 100 };
        auto at = [&](float t, float x, float y) {
            const float2 p = WireCurvePoint(p0, p1, p2, p3, t);
            EXPECT_NEAR(p.x, x, 1e-3f) << t;
            EXPECT_NEAR(p.y, y, 1e-3f) << t;
        };
        at(0.0f, 0.0f, 0.0f);
        at(1.0f, 200.0f, 100.0f);
        at(0.5f, 80.0f, 40.0f);    // halfway between p1 and p2
        at(0.25f, 70.0f, 10.0f);   // halfway along p0-p1-m: (p0 + 2 p1 + m) / 4
        at(0.75f, 100.0f, 75.0f);  // halfway along m-p2-p3: (m + 2 p2 + p3) / 4
    }

    // A curved route leaves the output and enters the input level, its control points half
    // the distance across, or `reach` when that's further, as when the input is behind.
    TEST(NodeViewWireEdit, ACurvedRouteReachesOutLevel)
    {
        const WireRoute route = BuildCurvedWireRoute({ 0, 0 }, { 400, 100 }, 48);
        EXPECT_TRUE(route.curved);
        EXPECT_TRUE(route.handles.empty());
        EXPECT_EQ(route.points, (std::vector<int2>{ { 0, 0 }, { 200, 0 }, { 200, 100 }, { 400, 100 } }));

        EXPECT_EQ(BuildCurvedWireRoute({ 0, 0 }, { 40, 100 }, 48).points,
                  (std::vector<int2>{ { 0, 0 }, { 48, 0 }, { -8, 100 }, { 40, 100 } })) << "close: at least reach";
        EXPECT_EQ(BuildCurvedWireRoute({ 0, 0 }, { -200, 100 }, 48).points,
                  (std::vector<int2>{ { 0, 0 }, { 100, 0 }, { -300, 100 }, { -200, 100 } })) << "behind: loops round";
    }

    TEST(NodeViewWireEdit, WireInputUndoesWireKey)
    {
        EXPECT_EQ(WireInput(WireKey(7, 12)), (PropertyEndpoint{ 7, 12 }));
    }

    TEST(NodeViewWireEdit, DisconnectRemovesTheWireAndItsLanes)
    {
        NodeView view;
        view.nodes.resize(2);
        view.nodes[0].properties.resize(1);
        view.nodes[0].properties[0].output = true;
        view.nodes[1].properties.resize(1);
        view.nodes[1].properties[0].input = PropertyEndpoint{ 0, 0 };
        view.wireOverrides[WireKey(1, 0)].laneX = 120;

        ApplyNodeViewEdit(view, DisconnectEdit(view, { 1, 0 }));
        ASSERT_TRUE(view.nodes[1].properties[0].input.has_value()) << "the socket stays";
        EXPECT_FALSE(view.nodes[1].properties[0].input->valid());
        EXPECT_EQ(view.wireOverrides.count(WireKey(1, 0)), 0u);

        EXPECT_TRUE(DisconnectEdit(view, { 5, 0 }).empty()) << "out of range: nothing to do";
    }
}
