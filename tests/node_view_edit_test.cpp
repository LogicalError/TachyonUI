// Node-view edits as values a host can undo and redo: what the view changes is built
// as a NodeViewEdit, applied with ApplyNodeViewEdit and reported to NodeView::onEdit.

#include <gtest/gtest.h>

#include <gui_controls/gui_node_view_helpers.h>

#include <algorithm>
#include <vector>

namespace
{
    using namespace tachyon;

    // Node 0: one output. Node 1: two inputs, the first wired to node 0's output.
    NodeView MakeView()
    {
        NodeView view;
        view.nodes.resize(2);
        view.nodes[0].properties.resize(1);
        view.nodes[0].properties[0].output = true;
        view.nodes[1].properties.resize(2);
        view.nodes[1].properties[0].input = PropertyEndpoint{ 0, 0 };
        view.nodes[1].properties[1].input = PropertyEndpoint{};
        view.nodes[1].pos = { 300, 40 };
        return view;
    }

    PropertyEndpoint SourceOf(const NodeView& view, int node, int prop)
    {
        return *view.nodes[size_t(node)].properties[size_t(prop)].input;
    }

    TEST(NodeViewEdit, UndoAndRedoAMove)
    {
        NodeView view = MakeView();
        NodeViewEdit edit;
        edit.moves.push_back({ 1, { 300, 40 }, { 420, 80 } });
        ApplyNodeViewEdit(view, edit);
        EXPECT_EQ(view.nodes[1].pos.x, 420);
        ApplyNodeViewEdit(view, edit, /*undo*/ true);
        EXPECT_EQ(view.nodes[1].pos.x, 300);
        EXPECT_EQ(view.nodes[1].pos.y, 40);
    }

    TEST(NodeViewEdit, UndoAndRedoACollapse)
    {
        NodeView view = MakeView();
        NodeViewEdit edit;
        edit.collapses.push_back({ 1, false, true });
        ApplyNodeViewEdit(view, edit);
        EXPECT_TRUE(view.nodes[1].collapsed);
        ApplyNodeViewEdit(view, edit, true);
        EXPECT_FALSE(view.nodes[1].collapsed);
    }

    TEST(NodeViewEdit, AConnectRemembersTheInputsOldSource)
    {
        NodeView view = MakeView();
        view.nodes.resize(3);
        view.nodes[2].properties.resize(1);
        view.nodes[2].properties[0].output = true;

        const NodeViewEdit edit = ConnectEdit(view, { 2, 0 }, { 1, 0 });
        ApplyNodeViewEdit(view, edit);
        EXPECT_EQ(SourceOf(view, 1, 0), (PropertyEndpoint{ 2, 0 }));
        ApplyNodeViewEdit(view, edit, true);
        EXPECT_EQ(SourceOf(view, 1, 0), (PropertyEndpoint{ 0, 0 }));
    }

    TEST(NodeViewEdit, ConnectingWhatIsConnectedIsNoEdit)
    {
        const NodeView view = MakeView();
        EXPECT_TRUE(ConnectEdit(view, { 0, 0 }, { 1, 0 }).empty());
        EXPECT_TRUE(ConnectEdit(view, { 0, 0 }, { 0, 0 }).empty()) << "not an input";
    }

    TEST(NodeViewEdit, UndoingADisconnectBringsBackTheWireAndItsLanes)
    {
        NodeView view = MakeView();
        view.wireOverrides[WireKey(1, 0)].laneX = 160;

        const NodeViewEdit edit = DisconnectEdit(view, { 1, 0 });
        ApplyNodeViewEdit(view, edit);
        EXPECT_FALSE(SourceOf(view, 1, 0).valid());
        EXPECT_EQ(view.wireOverrides.count(WireKey(1, 0)), 0u);

        ApplyNodeViewEdit(view, edit, true);
        EXPECT_EQ(SourceOf(view, 1, 0), (PropertyEndpoint{ 0, 0 }));
        ASSERT_EQ(view.wireOverrides.count(WireKey(1, 0)), 1u);
        EXPECT_EQ(view.wireOverrides[WireKey(1, 0)].laneX, 160);
    }

    // Pulling a wire off one input and dropping it on another is one edit: a detach and
    // a connect, undone in reverse order.
    TEST(NodeViewEdit, AMovedWireUndoesInOneStep)
    {
        NodeView view = MakeView();
        NodeViewEdit edit;
        edit.rewires.push_back({ { 1, 0 }, { 0, 0 }, {} });
        edit.rewires.push_back({ { 1, 1 }, {}, { 0, 0 } });
        ApplyNodeViewEdit(view, edit);
        EXPECT_FALSE(SourceOf(view, 1, 0).valid());
        EXPECT_EQ(SourceOf(view, 1, 1), (PropertyEndpoint{ 0, 0 }));

        ApplyNodeViewEdit(view, edit, true);
        EXPECT_EQ(SourceOf(view, 1, 0), (PropertyEndpoint{ 0, 0 }));
        EXPECT_FALSE(SourceOf(view, 1, 1).valid());
    }

    TEST(NodeViewEdit, LaneEditsRestoreNoOverridesAsNone)
    {
        NodeView view = MakeView();
        NodeViewEdit edit;
        WireRouteOverrides moved;
        moved.laneX = 200;
        edit.lanes.push_back({ WireKey(1, 0), {}, moved });
        ApplyNodeViewEdit(view, edit);
        EXPECT_EQ(view.wireOverrides[WireKey(1, 0)].laneX, 200);
        ApplyNodeViewEdit(view, edit, true);
        EXPECT_EQ(view.wireOverrides.count(WireKey(1, 0)), 0u);
    }

    // ---- Adding and removing nodes ----

    // 0 Src: out. 1 Mid: in (from Src), out. 2 Out: in (from Mid), in (from Src), out.
    // 3 Extra: in (from Out). Lanes on three wires; all four selected, Mid clicked last.
    NodeView MakeGraph()
    {
        NodeView view;
        view.nodes.resize(4);
        for (int n = 0; n < 4; ++n) view.nodes[size_t(n)].stableID = ID{ uint64_t(n + 1) };
        view.nodes[0].properties.resize(1);
        view.nodes[0].properties[0].output = true;
        view.nodes[1].properties.resize(2);
        view.nodes[1].properties[0].input  = PropertyEndpoint{ 0, 0 };
        view.nodes[1].properties[1].output = true;
        view.nodes[2].properties.resize(3);
        view.nodes[2].properties[0].input  = PropertyEndpoint{ 1, 1 };
        view.nodes[2].properties[1].input  = PropertyEndpoint{ 0, 0 };
        view.nodes[2].properties[2].output = true;
        view.nodes[3].properties.resize(1);
        view.nodes[3].properties[0].input  = PropertyEndpoint{ 2, 2 };
        view.wireOverrides[WireKey(1, 0)].laneX = 50;
        view.wireOverrides[WireKey(2, 0)].laneX = 100;
        view.wireOverrides[WireKey(3, 0)].laneX = 200;
        view.ordering = { 0, 1, 2, 3 };
        for (const NodeModel& node : view.nodes) view.selectedNodeIDs.insert(node.stableID);
        view.lastClickedNodeID = view.nodes[1].stableID;
        return view;
    }

    // Each node's inputs' sources, in order: what the wires are.
    std::vector<PropertyEndpoint> Wires(const NodeView& view)
    {
        std::vector<PropertyEndpoint> out;
        for (const NodeModel& node : view.nodes)
            for (const NodeProperty& prop : node.properties)
                if (prop.input) out.push_back(*prop.input);
        return out;
    }

    std::vector<int> Sorted(std::vector<int> v) { std::sort(v.begin(), v.end()); return v; }

    TEST(NodeViewEdit, RemovingANodeTakesItsWiresAndMovesTheRestDown)
    {
        NodeView view = MakeGraph();
        const NodeView before = view;
        const int mid[] = { 1 };
        const NodeViewEdit edit = RemoveNodesEdit(view, mid);
        ASSERT_EQ(edit.nodes.size(), 1u);
        view.hoveredWire  = WireKey(3, 0);
        view.wireMenuWire = WireKey(3, 0);
        view.wireHandleCache.push_back(CachedWireHandle{ .wireKey = WireKey(3, 0) });
        ApplyNodeViewEdit(view, edit);
        EXPECT_FALSE(view.hoveredWire) << "they named wires by the old numbers";
        EXPECT_FALSE(view.wireMenuWire);
        EXPECT_TRUE(view.wireHandleCache.empty());

        ASSERT_EQ(view.nodes.size(), 3u);
        EXPECT_EQ(view.nodes[1].stableID, before.nodes[2].stableID);
        EXPECT_EQ(Wires(view), (std::vector<PropertyEndpoint>{ {}, { 0, 0 }, { 1, 2 } }))
            << "Out loses Mid's wire, keeps Src's; Extra's now comes from node 1";
        EXPECT_EQ(view.wireOverrides.size(), 1u) << "the lanes of Mid's wires go";
        EXPECT_EQ(view.wireOverrides[WireKey(2, 0)].laneX, 200) << "Extra's moved with it";
        EXPECT_EQ(Sorted(view.ordering), (std::vector<int>{ 0, 1, 2 }));
        EXPECT_EQ(view.selectedNodeIDs.count(before.nodes[1].stableID), 0u);
        EXPECT_EQ(view.selectedNodeIDs.size(), 3u);
        EXPECT_EQ(view.lastClickedNodeID, ID::Invalid());

        ApplyNodeViewEdit(view, edit, /*undo*/ true);
        ASSERT_EQ(view.nodes.size(), 4u);
        for (size_t n = 0; n < 4; ++n)
            EXPECT_EQ(view.nodes[n].stableID, before.nodes[n].stableID) << n;
        EXPECT_EQ(Wires(view), Wires(before));
        EXPECT_EQ(view.wireOverrides, before.wireOverrides);
        EXPECT_EQ(Sorted(view.ordering), (std::vector<int>{ 0, 1, 2, 3 }));
        EXPECT_EQ(view.ordering.back(), 1) << "back in front";

        ApplyNodeViewEdit(view, edit);   // redo
        EXPECT_EQ(Wires(view), (std::vector<PropertyEndpoint>{ {}, { 0, 0 }, { 1, 2 } }));
    }

    // Several at once, in any order, repeats and strays skipped; the wires between them come
    // back too.
    TEST(NodeViewEdit, RemovingSeveralNodesAndPuttingThemBack)
    {
        NodeView view = MakeGraph();
        const NodeView before = view;
        const int some[] = { 0, 2, 2, 9, -1 };
        const NodeViewEdit edit = RemoveNodesEdit(view, some);
        ASSERT_EQ(edit.nodes.size(), 2u);
        ApplyNodeViewEdit(view, edit);
        ASSERT_EQ(view.nodes.size(), 2u);
        EXPECT_EQ(view.nodes[0].stableID, before.nodes[1].stableID);
        EXPECT_EQ(view.nodes[1].stableID, before.nodes[3].stableID);
        EXPECT_EQ(Wires(view), (std::vector<PropertyEndpoint>{ {}, {} }));
        EXPECT_TRUE(view.wireOverrides.empty());
        EXPECT_EQ(Sorted(view.ordering), (std::vector<int>{ 0, 1 }));

        ApplyNodeViewEdit(view, edit, true);
        ASSERT_EQ(view.nodes.size(), 4u);
        for (size_t n = 0; n < 4; ++n)
            EXPECT_EQ(view.nodes[n].stableID, before.nodes[n].stableID) << n;
        EXPECT_EQ(Wires(view), Wires(before));
        EXPECT_EQ(view.wireOverrides, before.wireOverrides);
    }

    // A node added in the middle moves the ones after it up, wires and all.
    TEST(NodeViewEdit, AddingANodeMovesTheRestUp)
    {
        NodeView view = MakeGraph();
        const NodeView before = view;
        NodeModel added;
        added.stableID = ID{ 9 };
        added.properties.resize(1);
        added.properties[0].input = PropertyEndpoint{ 0, 0 };   // as numbered once it's in
        const NodeViewEdit edit = AddNodeEdit(view, added, 1);
        ApplyNodeViewEdit(view, edit);

        ASSERT_EQ(view.nodes.size(), 5u);
        EXPECT_EQ(view.nodes[1].stableID, added.stableID);
        EXPECT_EQ(Wires(view), (std::vector<PropertyEndpoint>{ { 0, 0 }, { 0, 0 }, { 2, 1 }, { 0, 0 }, { 3, 2 } }));
        EXPECT_EQ(view.wireOverrides[WireKey(2, 0)].laneX, 50);
        EXPECT_EQ(view.wireOverrides[WireKey(3, 0)].laneX, 100);
        EXPECT_EQ(view.wireOverrides[WireKey(4, 0)].laneX, 200);
        EXPECT_EQ(view.wireOverrides.count(WireKey(1, 0)), 0u);
        EXPECT_EQ(view.ordering, (std::vector<int>{ 0, 2, 3, 4, 1 })) << "the new one in front";

        ApplyNodeViewEdit(view, edit, true);
        EXPECT_EQ(view.nodes.size(), 4u);
        EXPECT_EQ(Wires(view), Wires(before));
        EXPECT_EQ(view.wireOverrides, before.wireOverrides);
        EXPECT_EQ(view.ordering, before.ordering);
    }

    // At -1 or past the end it goes last; with a rewire into it, wired in the same step.
    TEST(NodeViewEdit, AddingANodeLastAndWiringIt)
    {
        NodeView view = MakeGraph();
        NodeModel added;
        added.properties.resize(1);
        added.properties[0].input = PropertyEndpoint{};
        EXPECT_EQ(AddNodeEdit(view, added, 99).nodes[0].index, 4);
        EXPECT_EQ(AddNodeEdit(view, added, 4).nodes[0].index, 4);
        NodeViewEdit edit = AddNodeEdit(view, added);
        ASSERT_EQ(edit.nodes.size(), 1u);
        EXPECT_EQ(edit.nodes[0].index, 4);
        EXPECT_TRUE(edit.nodes[0].added);
        edit.rewires.push_back({ .input = { 4, 0 }, .from = {}, .to = { 2, 2 } });
        ApplyNodeViewEdit(view, edit);
        EXPECT_EQ(*view.nodes[4].properties[0].input, (PropertyEndpoint{ 2, 2 }));
        ApplyNodeViewEdit(view, edit, true);
        EXPECT_EQ(view.nodes.size(), 4u);
        EXPECT_EQ(Wires(view), Wires(MakeGraph()));
    }

    // A reroute on a wire: the wire runs into it and on out of it; undone, as it was.
    TEST(NodeViewEdit, ARerouteOnAWire)
    {
        NodeView view = MakeGraph();
        const NodeView before = view;
        view.nodes[1].properties[1].socketColor = { 0.2f, 0.4f, 0.6f, 1.0f };
        const NodeViewEdit edit = RerouteWireEdit(view, WireKey(2, 0), ID{ 42 }, { 240, 96 });
        ASSERT_EQ(edit.nodes.size(), 1u);
        ApplyNodeViewEdit(view, edit);

        ASSERT_EQ(view.nodes.size(), 5u);
        const NodeModel& reroute = view.nodes[4];
        EXPECT_TRUE(reroute.reroute);
        EXPECT_EQ(reroute.stableID, ID{ 42 });
        EXPECT_EQ(reroute.pos, (int2{ 240, 96 }));
        ASSERT_EQ(reroute.properties.size(), 1u);
        EXPECT_EQ(*reroute.properties[0].input, (PropertyEndpoint{ 1, 1 })) << "from the wire's output";
        EXPECT_TRUE(reroute.properties[0].output);
        EXPECT_EQ(reroute.properties[0].socketColor.b, 0.6f) << "in its color";
        EXPECT_EQ(*view.nodes[2].properties[0].input, (PropertyEndpoint{ 4, 0 })) << "on to the input";
        EXPECT_EQ(view.wireOverrides.count(WireKey(2, 0)), 0u) << "its lanes go";
        EXPECT_EQ(view.wireOverrides.size(), before.wireOverrides.size() - 1);

        ApplyNodeViewEdit(view, edit, true);
        EXPECT_EQ(view.nodes.size(), 4u);
        EXPECT_EQ(Wires(view), Wires(before));
        EXPECT_EQ(view.wireOverrides, before.wireOverrides);

        // A wire without lanes: no lane change.
        EXPECT_TRUE(RerouteWireEdit(view, WireKey(2, 1), ID{ 43 }, {}).lanes.empty());
    }

    TEST(NodeViewEdit, NoRerouteWhereThereIsNoWire)
    {
        NodeView view = MakeGraph();
        view.nodes[3].properties[0].input = PropertyEndpoint{};
        EXPECT_TRUE(RerouteWireEdit(view, WireKey(3, 0), ID{ 1 }, {}).empty()) << "an unwired input";
        EXPECT_TRUE(RerouteWireEdit(view, WireKey(0, 0), ID{ 1 }, {}).empty()) << "not an input";
        EXPECT_TRUE(RerouteWireEdit(view, WireKey(9, 0), ID{ 1 }, {}).empty()) << "no such node";
        EXPECT_TRUE(RerouteWireEdit(view, WireKey(2, 7), ID{ 1 }, {}).empty()) << "no such row";
        view.nodes[2].properties[0].input = PropertyEndpoint{ 9, 0 };
        EXPECT_TRUE(RerouteWireEdit(view, WireKey(2, 0), ID{ 1 }, {}).empty()) << "a source that isn't there";
        view.nodes[2].properties[0].input = PropertyEndpoint{ 1, 5 };
        EXPECT_TRUE(RerouteWireEdit(view, WireKey(2, 0), ID{ 1 }, {}).empty()) << "a source row that isn't there";
    }

    TEST(NodeViewEdit, RemovingNothingIsNoEdit)
    {
        const NodeView view = MakeGraph();
        const int none[] = { 4, -1 };
        EXPECT_TRUE(RemoveNodesEdit(view, none).empty());
        EXPECT_TRUE(RemoveNodesEdit(view, {}).empty());
        EXPECT_FALSE(AddNodeEdit(view, NodeModel{}).empty());
    }

    TEST(NodeViewEdit, CommitAppliesAndReportsOnce)
    {
        NodeView view = MakeView();
        std::vector<NodeViewEdit> reported;
        view.onEdit = [&](const NodeViewEdit& e) { reported.push_back(e); };

        CommitNodeViewEdit(view, DisconnectEdit(view, { 1, 0 }));
        EXPECT_FALSE(SourceOf(view, 1, 0).valid());
        ASSERT_EQ(reported.size(), 1u);
        ASSERT_EQ(reported[0].rewires.size(), 1u);
        EXPECT_EQ(reported[0].rewires[0].from, (PropertyEndpoint{ 0, 0 }));

        CommitNodeViewEdit(view, NodeViewEdit{});
        EXPECT_EQ(reported.size(), 1u) << "an empty edit isn't reported";
    }
}
