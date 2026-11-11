// Tests for the node-view widget (GUIApplication::RenderNodeView,
// controls_node_view.cpp) through the headless harness.
//
// The canvas fills its parent's height the way it does in a docked panel, so the
// root here is a main-window root (a stretching column): the default `.window`
// root is a wrapping row, where the canvas would collapse to its padding.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_core/gui_id.h>
#include <gui_controls/gui_node_view_helpers.h>

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace {

        class NodeViewControlTest : public ::testing::Test
        {
        protected:
            HeadlessGUIApp app;
            NodeView       view;
            tachyon::ID        nodeA{};
            tachyon::ID        nodeB{};

            void SetUp() override
            {
                ASSERT_TRUE(app.Initialize());
                nodeA = app.idManager.CreateStaticID();
                nodeB = app.idManager.CreateStaticID();

                NodeModel a{};
                a.stableID = nodeA;
                a.title    = "A";
                a.pos      = { 100, 80 };
                a.properties.push_back(NodeProperty{ .output = true });

                NodeModel b{};
                b.stableID = nodeB;
                b.title    = "B";
                b.pos      = { 520, 320 };
                b.properties.push_back(NodeProperty{ .input = PropertyEndpoint{} });

                view = {};
                view.transform = tachyon::float3x2::Scale(1.0f); // identity
                view.nodes = { std::move(a), std::move(b) };
            }

            void TearDown() override { app.Cleanup(); }

            void Frame()
            {
                app.Frame([&] {
                    app.BeginRootWindow({ 1920, 1080 }, ElementFlags::None, app.theme.mainWindowStyleRef);
                    app.RenderNodeView(view);
                    app.EndRootWindow();
                });
            }

            void Settle() { Frame(); Frame(); }

            // The canvas's inner rect (the node-view style is unique to it).
            tachyon::IntRect CanvasInner() const
            {
                const auto style = app.theme.nodeViewStyleRef;
                for (uint32_t d = 0; d < app.elementStorage.NumberOfDepthLevels(); d++)
                {
                    const uint32_t count = app.elementStorage.DepthSizeOf(d);
                    for (uint32_t i = 0; i < count; i++)
                    {
                        tachyon::ElementRef ref{ d, i };
                        if (!app.elementStorage.IsValidRef(ref)) continue;
                        if (app.elementStorage.GetElementUnsafe(ref).styleRef != style) continue;
                        if (const auto* rp = app.elementStorage.GetElementRectPtr(ref))
                            return rp->innerRect;
                    }
                }
                return {};
            }

            // A point on the canvas background, clear of the nodes and the overlays.
            tachyon::int2 CanvasPoint() const
            {
                const auto inner = CanvasInner();
                return { inner.x + inner.w * 3 / 4, inner.y + inner.h / 2 };
            }

            // The rects of the elements with `style`, in layout order.
            std::vector<tachyon::IntRect> RectsWithStyle(tachyon::StyleRef style) const
            {
                std::vector<tachyon::IntRect> out;
                for (uint32_t d = 0; d < app.elementStorage.NumberOfDepthLevels(); d++)
                    for (uint32_t i = 0; i < app.elementStorage.DepthSizeOf(d); i++)
                    {
                        tachyon::ElementRef ref{ d, i };
                        if (!app.elementStorage.IsValidRef(ref)) continue;
                        if (app.elementStorage.GetElementUnsafe(ref).styleRef != style) continue;
                        if (const auto* rp = app.elementStorage.GetElementRectPtr(ref)) out.push_back(rp->rect);
                    }
                return out;
            }

            // The centre of the first socket inside `node`'s rect.
            tachyon::int2 SocketOf(tachyon::ID node) const
            {
                const auto n = app.GetElementRect(node);
                for (const auto& r : RectsWithStyle(app.theme.nodeSocketStyleRef))
                {
                    const tachyon::int2 c{ r.x + r.w / 2, r.y + r.h / 2 };
                    if (c.x >= n.x - r.w && c.x <= n.x + n.w + r.w && c.y >= n.y && c.y < n.y + n.h)
                        return c;
                }
                return { -1, -1 };
            }

            // The texts of the tooltip showing, in order (empty when none is).
            std::vector<std::string> TooltipTexts() const
            {
                std::vector<std::string> out;
                const tachyon::ElementRef window = app.elementStorage.FindElementRefByID(app.tooltipWindowID);
                if (!window.IsValid())
                    return out;
                auto walk = [&](auto&& self, tachyon::ElementRef ref) -> void {
                    for (const tachyon::Element& e : app.elementStorage.GetChildrenSpan(ref))
                    {
                        if (const std::string_view t = app.TextOf(e.id); !t.empty())
                            out.emplace_back(t);
                        self(self, app.elementStorage.FindElementRefByID(e.id));
                    }
                };
                walk(walk, window);
                return out;
            }

            // Points at `at` and waits past the tooltip's delay.
            void Hover(tachyon::int2 at)
            {
                app.InjectMouseMove(at);
                for (int i = 0; i < 8; ++i) { app.AdvanceTime(100); Frame(); }
            }

            // A left drag the way a mouse makes one: press, moves and release on frames of
            // their own.
            void Drag(tachyon::int2 from, tachyon::int2 to)
            {
                app.InjectMouseMove(from);
                app.InjectMouseDown(from);
                Frame();
                const tachyon::int2 mid{ (from.x + to.x) / 2, (from.y + to.y) / 2 };
                app.InjectMouseMove(mid, MouseButtons::Left);
                Frame();
                app.InjectMouseMove(to, MouseButtons::Left);
                Frame();
                app.InjectMouseUp(to);
                Frame();
            }

            // A point on `node`'s header, clear of its sockets.
            tachyon::int2 HeaderOf(tachyon::ID node) const
            {
                const auto n = app.GetElementRect(node);
                return { n.x + n.w / 2, n.y + app.theme.nodeGridSize };
            }
        };

        // A collapsed node's sockets sit on its header's centre line, like an
        // expanded row's sockets sit on the row's.
        TEST_F(NodeViewControlTest, CollapsedSocketsCentreOnTheHeader)
        {
            view.nodes[0].collapsed = true;
            Settle();
            auto rectsWithStyle = [&](tachyon::StyleRef style) {
                std::vector<tachyon::IntRect> out;
                for (uint32_t d = 0; d < app.elementStorage.NumberOfDepthLevels(); d++)
                    for (uint32_t i = 0; i < app.elementStorage.DepthSizeOf(d); i++)
                    {
                        tachyon::ElementRef ref{ d, i };
                        if (!app.elementStorage.IsValidRef(ref)) continue;
                        if (app.elementStorage.GetElementUnsafe(ref).styleRef != style) continue;
                        if (const auto* rp = app.elementStorage.GetElementRectPtr(ref)) out.push_back(rp->rect);
                    }
                return out;
            };
            const auto nodeRect = app.GetElementRect(nodeA);
            tachyon::IntRect header{}, socket{};
            for (const auto& r : rectsWithStyle(app.theme.nodeHeaderStyleRef))
                if (r.x >= nodeRect.x && r.x < nodeRect.x + nodeRect.w && r.y >= nodeRect.y && r.y < nodeRect.y + nodeRect.h) header = r;
            for (const auto& r : rectsWithStyle(app.theme.nodeSocketStyleRef))
                if (std::abs(r.y - nodeRect.y) < nodeRect.h) socket = r;
            ASSERT_GT(header.h, 0);
            ASSERT_GT(socket.h, 0);
            const int rowPitch = 2 * app.theme.nodeGridSize;
            EXPECT_EQ(socket.y + socket.h / 2, nodeRect.y + rowPitch / 2)
                << "on the header slot's centre (a grid line), not a fixed offset";
        }

        TEST_F(NodeViewControlTest, TheCanvasFillsItsParent)
        {
            Settle();
            const auto inner = CanvasInner();
            EXPECT_GT(inner.h, 1000) << "the canvas collapsed (#68)";
            EXPECT_GT(inner.w, 1800);
        }

        TEST_F(NodeViewControlTest, ClickingANodeSelectsIt)
        {
            Settle();
            app.ClickAt(HeaderOf(nodeB));
            Frame();
            EXPECT_EQ(view.selectedNodeIDs, std::unordered_set<tachyon::ID>{ nodeB });
        }

        // A drag on a node's header moves it (on the grid) and reports one edit.
        TEST_F(NodeViewControlTest, DraggingANodeMovesIt)
        {
            Settle();
            int edits = 0;
            view.onEdit = [&](const NodeViewEdit& edit) { edits++; EXPECT_EQ(edit.moves.size(), 1u); };
            const tachyon::int2 before = view.nodes[1].pos;
            const tachyon::int2 from = HeaderOf(nodeB);
            Drag(from, { from.x + 5 * app.theme.nodeGridSize, from.y + 3 * app.theme.nodeGridSize });
            Settle();
            EXPECT_GT(view.nodes[1].pos.x, before.x);
            EXPECT_GT(view.nodes[1].pos.y, before.y);
            EXPECT_EQ(view.nodes[1].pos.x % app.theme.nodeGridSize, 0) << "positions snap to the grid";
            EXPECT_EQ(edits, 1);
        }

        // A position off the grid moves onto it while snapping, and stays put without.
        TEST_F(NodeViewControlTest, PositionsSnapToTheGridOnlyWhileSnapping)
        {
            const int32_t grid = app.theme.nodeGridSize;
            ASSERT_GT(grid, 1);
            view.snapToGrid = false;
            view.nodes[1].pos = { 10 * grid + 1, 6 * grid + 1 };
            Settle();
            EXPECT_EQ(view.nodes[1].pos, (tachyon::int2{ 10 * grid + 1, 6 * grid + 1 }));
            EXPECT_EQ(app.GetElementRect(nodeB).x - app.GetElementRect(nodeA).x, 10 * grid + 1 - view.nodes[0].pos.x) << "laid out there";

            view.snapToGrid = true;
            Settle();
            EXPECT_EQ(view.nodes[1].pos, (tachyon::int2{ 10 * grid, 6 * grid }));
        }

        // Without snapping, a drag moves the node by exactly as far as the pointer went.
        TEST_F(NodeViewControlTest, DraggingWithoutSnappingLandsWhereDropped)
        {
            view.snapToGrid = false;
            Settle();
            std::vector<NodeViewEdit> edits;
            view.onEdit = [&](const NodeViewEdit& edit) { edits.push_back(edit); };
            const int32_t grid = app.theme.nodeGridSize;
            const tachyon::int2 before = view.nodes[1].pos;
            const tachyon::int2 by{ 5 * grid + 3, 3 * grid + 7 };
            const tachyon::int2 from = HeaderOf(nodeB);
            Drag(from, { from.x + by.x, from.y + by.y });
            Settle();
            EXPECT_EQ(view.nodes[1].pos, (tachyon::int2{ before.x + by.x, before.y + by.y }));
            ASSERT_EQ(edits.size(), 1u);
            ASSERT_EQ(edits[0].moves.size(), 1u);
            EXPECT_EQ(edits[0].moves[0].to, view.nodes[1].pos);
        }

        // A middle click on a wire removes it; a middle drag from it pans and leaves it.
        TEST_F(NodeViewControlTest, AMiddleClickRemovesAWireAndAMiddleDragLeavesIt)
        {
            view.nodes.resize(2);
            view.ordering.clear();
            view.nodes[1].properties[0].input = PropertyEndpoint{ 0, 0 };
            Settle();
            auto wired = [&] { return view.nodes[1].properties[0].input && view.nodes[1].properties[0].input->valid(); };
            const tachyon::int2 at = CanvasPoint() + tachyon::int2{ 3, 5 };
            app.InjectMouseMove(at);
            Frame();
            view.hoveredWire.part = WireKey(1, 0);
            app.DragFrom(at, { at.x + 40, at.y }, MouseButtons::Middle);
            Frame();
            EXPECT_TRUE(wired()) << "a drag pans";
            app.InjectMouseMove(at);
            Frame();
            view.hoveredWire.part = WireKey(1, 0);
            app.ClickAt(at, MouseButtons::Middle);
            Frame();
            EXPECT_FALSE(wired()) << "a click removes it";
        }

        // "Add reroute here" puts the dot on the nearest grid point while snapping, and right
        // where the wire was clicked without.
        TEST_F(NodeViewControlTest, AddRerouteHereSnapsOnlyWhileSnapping)
        {
            const int32_t grid = app.theme.nodeGridSize;
            for (const bool snap : { true, false })
            {
                SCOPED_TRACE(snap ? "snapping" : "not snapping");
                view.snapToGrid = snap;
                view.nodes.resize(2);
                view.ordering.clear();
                view.nodes[1].properties[0].input = PropertyEndpoint{ 0, 0 };
                Settle();
                const tachyon::int2 at = CanvasPoint() + tachyon::int2{ 3, 5 };
                app.InjectMouseMove(at);
                Frame();
                view.hoveredWire.part = WireKey(1, 0);
                app.ClickAt(at, MouseButtons::Right);
                Frame();
                Frame();
                ASSERT_TRUE(view.wireMenuWire);
                ASSERT_NE(app.FindByText("Add reroute here"), tachyon::ID::Invalid());
                const tachyon::int2 clicked{ int32_t(std::lround(view.wireMenuPos.x)), int32_t(std::lround(view.wireMenuPos.y)) };
                std::vector<NodeViewEdit> edits;
                view.onEdit = [&](const NodeViewEdit& edit) { edits.push_back(edit); };
                app.InjectKey(KeyCode::Down);   // past "Delete wire", the first
                Frame();
                app.InjectKey(KeyCode::Return);
                Settle();
                view.onEdit = {};
                ASSERT_EQ(view.nodes.size(), 3u);
                ASSERT_TRUE(view.nodes[2].reroute);
                ASSERT_EQ(edits.size(), 1u);
                ASSERT_EQ(edits[0].nodes.size(), 1u);
                const tachyon::int2 pos = edits[0].nodes[0].node.pos;   // as undo and redo have it
                EXPECT_EQ(view.nodes[2].pos, pos);
                if (snap)
                {
                    EXPECT_EQ(pos.x % grid, 0);
                    EXPECT_EQ(pos.y % grid, 0);
                }
                else
                    EXPECT_EQ(pos, (tachyon::int2{ clicked.x - grid, clicked.y - grid })) << "its dot where it was clicked";
            }
        }

        // Dragging from an output socket onto an input socket wires them.
        TEST_F(NodeViewControlTest, DraggingFromAnOutputToAnInputConnects)
        {
            Settle();
            const tachyon::int2 out = SocketOf(nodeA), in = SocketOf(nodeB);
            ASSERT_GE(out.x, 0);
            ASSERT_GE(in.x, 0);
            Drag(out, in);
            Settle();
            ASSERT_TRUE(view.nodes[1].properties[0].input.has_value());
            ASSERT_TRUE(view.nodes[1].properties[0].input->valid());
            EXPECT_EQ(view.nodes[1].properties[0].input->nodeIdx, 0);
        }

        // ---- Wires dropped on the empty canvas, and wires dragged from an input ----

        // The nodes' sizes, which a drop on the canvas is checked against, as the app's frames
        // leave them (the harness runs no post-layout callbacks here).
        class NodeViewWireDropTest : public NodeViewControlTest
        {
        protected:
            void Settle()
            {
                NodeViewControlTest::Settle();
                for (NodeModel& node : view.nodes)
                    node.lastSize = app.GetElementRect(node.stableID).size;
            }
        };

        struct CanvasDrop { PropertyEndpoint from; bool fromOutput; float2 at; };

        // A new wire from an output, released over the empty canvas: the app hears where.
        TEST_F(NodeViewWireDropTest, ANewWireFromAnOutputDroppedOnTheCanvasIsReported)
        {
            Settle();
            std::vector<CanvasDrop> drops;
            int edits = 0;
            bool stillDragging = true;
            view.onWireDroppedOnCanvas = [&](PropertyEndpoint from, bool fromOutput, float2 at) {
                drops.push_back({ from, fromOutput, at });
                stillDragging = view.pendingConnection.IsActive();
            };
            view.onEdit = [&](const NodeViewEdit&) { edits++; };
            const tachyon::int2 to = CanvasPoint();
            Drag(SocketOf(nodeA), to);
            Settle();
            ASSERT_EQ(drops.size(), 1u);
            EXPECT_EQ(drops[0].from, (PropertyEndpoint{ 0, 0 }));
            EXPECT_TRUE(drops[0].fromOutput);
            // Through the transform the canvas was drawn with: centred for view.lastInnerSize.
            const float2 expected = NodeViewScreenToCanvas(view.transform, IntRect{ .position = CanvasInner().position, .size = view.lastInnerSize }, to);
            EXPECT_FLOAT_EQ(drops[0].at.x, expected.x);
            EXPECT_FLOAT_EQ(drops[0].at.y, expected.y);
            EXPECT_EQ(NodeAtCanvasPos(view, drops[0].at), -1);
            EXPECT_FALSE(stillDragging) << "the wire is done with when the app hears of it";
            EXPECT_EQ(edits, 0) << "nothing changed";
            EXPECT_FALSE(view.pendingConnection.IsActive());
        }

        // An unwired input starts a new wire too; released on the canvas, the app hears it
        // came from that input.
        TEST_F(NodeViewWireDropTest, ANewWireFromAnInputDroppedOnTheCanvasIsReported)
        {
            Settle();
            std::vector<CanvasDrop> drops;
            view.onWireDroppedOnCanvas = [&](PropertyEndpoint from, bool fromOutput, float2 at) { drops.push_back({ from, fromOutput, at }); };
            Drag(SocketOf(nodeB), CanvasPoint());
            Settle();
            ASSERT_EQ(drops.size(), 1u);
            EXPECT_EQ(drops[0].from, (PropertyEndpoint{ 1, 0 }));
            EXPECT_FALSE(drops[0].fromOutput);
            EXPECT_FALSE(view.nodes[1].properties[0].input->valid());
        }

        // Dragged from an input onto an output, a wire connects them, reported as one edit;
        // while it's over the output, that's where it would connect.
        TEST_F(NodeViewWireDropTest, DraggingFromAnInputToAnOutputConnects)
        {
            Settle();
            std::vector<NodeViewEdit> edits;
            view.onEdit = [&](const NodeViewEdit& e) { edits.push_back(e); };
            const tachyon::int2 in = SocketOf(nodeB), out = SocketOf(nodeA);
            app.InjectMouseMove(in);
            app.InjectMouseDown(in);
            Frame();
            app.InjectMouseMove({ (in.x + out.x) / 2, (in.y + out.y) / 2 }, MouseButtons::Left);
            Frame();
            EXPECT_TRUE(view.pendingConnection.IsActive());
            EXPECT_TRUE(view.pendingConnection.fromInput);
            EXPECT_EQ(view.pendingConnection.source, (PropertyEndpoint{ 1, 0 }));
            app.InjectMouseMove(out, MouseButtons::Left);
            Frame();
            Frame();
            EXPECT_EQ(view.pendingConnection.target, (PropertyEndpoint{ 0, 0 })) << "over the output";
            app.InjectMouseUp(out);
            Frame();
            Settle();
            EXPECT_EQ(*view.nodes[1].properties[0].input, (PropertyEndpoint{ 0, 0 }));
            ASSERT_EQ(edits.size(), 1u);
            ASSERT_EQ(edits[0].rewires.size(), 1u);
            EXPECT_EQ(edits[0].rewires[0].input, (PropertyEndpoint{ 1, 0 }));
            EXPECT_EQ(edits[0].rewires[0].to, (PropertyEndpoint{ 0, 0 }));
            EXPECT_FALSE(view.pendingConnection.IsActive());
        }

        // Dropped on a node's body, a wire from an input connects to the node's output.
        TEST_F(NodeViewWireDropTest, AWireFromAnInputDroppedOnANodeConnectsToItsOutput)
        {
            Settle();
            int drops = 0;
            view.onWireDroppedOnCanvas = [&](PropertyEndpoint, bool, float2) { drops++; };
            Drag(SocketOf(nodeB), HeaderOf(nodeA));
            Settle();
            EXPECT_EQ(*view.nodes[1].properties[0].input, (PropertyEndpoint{ 0, 0 }));
            EXPECT_EQ(drops, 0) << "a node isn't the empty canvas";
        }

        // An output a wire from an input may not reach takes nothing, and neither does its
        // node; that isn't the empty canvas either.
        TEST_F(NodeViewWireDropTest, AWireDroppedWhereItMayNotConnectIsNotOnTheCanvas)
        {
            Settle();
            int drops = 0;
            view.onWireDroppedOnCanvas = [&](PropertyEndpoint, bool, float2) { drops++; };
            view.canConnect = [](const NodeView&, PropertyEndpoint, PropertyEndpoint) { return false; };
            Drag(SocketOf(nodeB), SocketOf(nodeA));
            Settle();
            Drag(SocketOf(nodeA), HeaderOf(nodeB));
            Settle();
            EXPECT_FALSE(view.nodes[1].properties[0].input->valid());
            EXPECT_EQ(drops, 0);
        }

        // A wire pulled off an input and dropped on the canvas is a disconnect, not a new wire.
        TEST_F(NodeViewWireDropTest, AWirePulledOffAnInputOntoTheCanvasDisconnects)
        {
            view.nodes[1].properties[0].input = PropertyEndpoint{ 0, 0 };
            Settle();
            int drops = 0, edits = 0;
            view.onWireDroppedOnCanvas = [&](PropertyEndpoint, bool, float2) { drops++; };
            view.onEdit = [&](const NodeViewEdit&) { edits++; };
            Drag(SocketOf(nodeB), CanvasPoint());
            Settle();
            EXPECT_FALSE(view.nodes[1].properties[0].input->valid());
            EXPECT_EQ(drops, 0);
            EXPECT_EQ(edits, 1);
        }

        // While a wire from an input is over an output it may not reach, the output doesn't
        // take it and it isn't where the wire would connect.
        TEST_F(NodeViewWireDropTest, AnOutputThatMayNotTakeAWireRefusesIt)
        {
            Settle();
            view.canConnect = [](const NodeView&, PropertyEndpoint, PropertyEndpoint) { return false; };
            const tachyon::int2 in = SocketOf(nodeB), out = SocketOf(nodeA);
            app.InjectMouseMove(in);
            app.InjectMouseDown(in);
            Frame();
            app.InjectMouseMove({ (in.x + out.x) / 2, (in.y + out.y) / 2 }, MouseButtons::Left);
            Frame();
            app.InjectMouseMove(out, MouseButtons::Left);
            Frame();
            Frame();
            EXPECT_TRUE(view.pendingConnection.IsActive());
            EXPECT_FALSE(view.pendingConnection.target.valid());
            EXPECT_EQ(app.guiStateManager.Drag().GetValidatedTargetID(), tachyon::ID::Invalid()) << "nothing takes it there";
            app.InjectMouseUp(out);
            Frame();
        }

        // A wire pulled off one input and let go over another moves there: one edit.
        TEST_F(NodeViewWireDropTest, AWirePulledOffAnInputMovesToAnother)
        {
            NodeModel c{};
            c.stableID = app.idManager.CreateStaticID();
            c.title    = "C";
            c.pos      = { 520, 80 };
            c.properties.push_back(NodeProperty{ .input = PropertyEndpoint{} });
            view.nodes.push_back(std::move(c));
            view.nodes[1].properties[0].input = PropertyEndpoint{ 0, 0 };
            Settle();
            std::vector<NodeViewEdit> edits;
            view.onEdit = [&](const NodeViewEdit& e) { edits.push_back(e); };
            Drag(SocketOf(nodeB), SocketOf(view.nodes[2].stableID));
            Settle();
            EXPECT_FALSE(view.nodes[1].properties[0].input->valid());
            EXPECT_EQ(*view.nodes[2].properties[0].input, (PropertyEndpoint{ 0, 0 }));
            ASSERT_EQ(edits.size(), 1u);
            EXPECT_EQ(edits[0].rewires.size(), 2u) << "the detach and the connect";
        }

        // A drag of something else (a file, a tab) let go on the canvas isn't a wire.
        TEST_F(NodeViewWireDropTest, ADragOfSomethingElseOnTheCanvasIsNotAWire)
        {
            Settle();
            int drops = 0;
            view.onWireDroppedOnCanvas = [&](PropertyEndpoint, bool, float2) { drops++; };
            const tachyon::int2 at = CanvasPoint(), to{ at.x + 40, at.y };
            app.InjectMouseMove(at);
            Frame();
            auto& drag = app.guiStateManager.Drag();
            drag.Begin(nodeA, 0x46494c45 /* "FILE" */, 7, at);
            InputState moved{};
            moved.screenPos = to;
            moved.buttons   = MouseButtons::Left;
            drag.UpdateFromInput(moved);
            ASSERT_TRUE(drag.IsDragging());
            app.InjectMouseMove(to, MouseButtons::Left);
            Frame();
            app.InjectMouseUp(to);
            Frame();
            Frame();
            EXPECT_FALSE(drag.IsDragging());
            EXPECT_EQ(drops, 0);
        }

        // Without the callback, a new wire released on the canvas just goes.
        TEST_F(NodeViewWireDropTest, WithoutTheCallbackANewWireOnTheCanvasGoes)
        {
            Settle();
            int edits = 0;
            view.onEdit = [&](const NodeViewEdit&) { edits++; };
            Drag(SocketOf(nodeB), CanvasPoint());
            Settle();
            EXPECT_FALSE(view.pendingConnection.IsActive());
            EXPECT_FALSE(view.nodes[1].properties[0].input->valid());
            EXPECT_EQ(edits, 0);
        }

        // ---- The app's context menus for the canvas and for a node ----

        // Also where the canvas was laid out, which the harness's frames don't note.
        class NodeViewContextMenuTest : public NodeViewWireDropTest
        {
        protected:
            void Settle()
            {
                NodeViewWireDropTest::Settle();
                view.lastCanvasRect = CanvasInner();
            }
            // The canvas element: the node-view style is unique to it.
            tachyon::ID CanvasID() const
            {
                for (uint32_t d = 0; d < app.elementStorage.NumberOfDepthLevels(); d++)
                    for (uint32_t i = 0; i < app.elementStorage.DepthSizeOf(d); i++)
                    {
                        const tachyon::ElementRef ref{ d, i };
                        if (app.elementStorage.IsValidRef(ref) && app.elementStorage.GetElementUnsafe(ref).styleRef == app.theme.nodeViewStyleRef)
                            return app.elementStorage.GetElementUnsafe(ref).id;
                    }
                return tachyon::ID::Invalid();
            }
            void RightClick(tachyon::int2 at)
            {
                app.InjectMouseMove(at);
                Frame();
                app.ClickAt(at, MouseButtons::Right);
                Frame();
                Frame();
            }
        };

        // A right click on the empty canvas opens its menu, built by the app each frame,
        // for where it was clicked.
        TEST_F(NodeViewContextMenuTest, ARightClickOnTheCanvasOpensItsMenu)
        {
            Settle();
            std::vector<float2> built;
            int nodeMenus = 0;
            view.onCanvasContextMenu = [&](float2 at) { built.push_back(at); app.MenuItem("Add node", [] {}); };
            view.onNodeContextMenu   = [&](int) { nodeMenus++; };
            const tachyon::int2 at = CanvasPoint();
            RightClick(at);
            ASSERT_FALSE(built.empty());
            const float2 expected = NodeViewScreenToCanvas(view, at);
            EXPECT_FLOAT_EQ(built.back().x, expected.x);
            EXPECT_FLOAT_EQ(built.back().y, expected.y);
            EXPECT_TRUE(app.menuManager.IsOpen());
            EXPECT_EQ(nodeMenus, 0);
            const size_t frames = built.size();
            Frame();
            EXPECT_EQ(built.size(), frames + 1) << "built again each frame it's open";

            app.menuManager.CloseAll(app.guiStateManager);
            Frame();
            EXPECT_FALSE(view.canvasMenuPos) << "closed, it's forgotten";
            EXPECT_EQ(built.size(), frames + 1);
        }

        // On a node: that node's menu, and the node selected.
        TEST_F(NodeViewContextMenuTest, ARightClickOnANodeOpensItsMenuAndSelectsIt)
        {
            Settle();
            view.selectedNodeIDs = { nodeA };
            std::vector<int> built;
            int canvasMenus = 0;
            view.onNodeContextMenu   = [&](int n) { built.push_back(n); app.MenuItem("Rename", [] {}); };
            view.onCanvasContextMenu = [&](float2) { canvasMenus++; };
            RightClick(HeaderOf(nodeB));
            ASSERT_FALSE(built.empty());
            EXPECT_EQ(built.back(), 1);
            EXPECT_EQ(view.selectedNodeIDs, std::unordered_set<tachyon::ID>{ nodeB });
            EXPECT_EQ(view.lastClickedNodeID, nodeB);
            EXPECT_EQ(canvasMenus, 0);

            // A selected node keeps the rest of the selection.
            view.selectedNodeIDs = { nodeA, nodeB };
            RightClick(HeaderOf(nodeA));
            EXPECT_EQ(built.back(), 0);
            EXPECT_EQ(view.selectedNodeIDs.size(), 2u);
        }

        // A right drag doesn't pan (the middle button does) and opens nothing.
        TEST_F(NodeViewContextMenuTest, ARightDragNeitherPansNorOpensAMenu)
        {
            Settle();
            int menus = 0;
            view.onCanvasContextMenu = [&](float2) { menus++; };
            view.onNodeContextMenu   = [&](int) { menus++; };
            const tachyon::float3x2 before = view.transform;
            const tachyon::int2 from = CanvasPoint();
            app.DragFrom(from, { from.x - 120, from.y + 60 }, MouseButtons::Right);
            Frame();
            Frame();
            EXPECT_EQ(view.transform.TransformPoint(float2{}).x, before.TransformPoint(float2{}).x) << "it didn't pan";
            EXPECT_EQ(menus, 0);
            EXPECT_FALSE(app.menuManager.IsOpen());
        }

        // A middle drag pans, and opens nothing.
        TEST_F(NodeViewContextMenuTest, AMiddleDragPansAndOpensNoMenu)
        {
            Settle();
            int menus = 0;
            view.onCanvasContextMenu = [&](float2) { menus++; };
            const tachyon::float3x2 before = view.transform;
            const tachyon::int2 from = CanvasPoint();
            app.DragFrom(from, { from.x - 120, from.y + 60 }, MouseButtons::Middle);
            Frame();
            Frame();
            EXPECT_NE(view.transform.TransformPoint(float2{}).x, before.TransformPoint(float2{}).x) << "it panned";
            EXPECT_EQ(menus, 0);
            EXPECT_FALSE(app.menuManager.IsOpen());
        }

        // Without the app's menus, a right click opens none.
        TEST_F(NodeViewContextMenuTest, WithoutTheAppsMenusARightClickOpensNone)
        {
            Settle();
            RightClick(HeaderOf(nodeB));
            EXPECT_FALSE(app.menuManager.IsOpen());
            RightClick(CanvasPoint());
            EXPECT_FALSE(app.menuManager.IsOpen());
            EXPECT_FALSE(view.nodeMenuNode);
            EXPECT_FALSE(view.canvasMenuPos);
        }

        // From the keys, with the canvas focused: the selected node's menu (the one clicked
        // last), else the canvas's, for its centre.
        TEST_F(NodeViewContextMenuTest, TheMenuKeyOpensTheSelectedNodesMenuElseTheCanvas)
        {
            Settle();
            std::vector<int> nodeMenus;
            std::vector<float2> canvasMenus;
            view.onNodeContextMenu   = [&](int n) { nodeMenus.push_back(n); app.MenuItem("Rename", [] {}); };
            view.onCanvasContextMenu = [&](float2 at) { canvasMenus.push_back(at); app.MenuItem("Add node", [] {}); };
            // With the canvas focused (and the menus closed), the key.
            auto press = [&](KeyCode key, KeyModifier mods = KeyModifier::None) {
                app.menuManager.CloseAll(app.guiStateManager);
                app.guiStateManager.Interaction().SetFocusID(CanvasID());
                app.InjectKey(key, KeyAction::KeyDown, mods);
                Frame();
                Frame();
            };

            press(KeyCode::Application);
            ASSERT_FALSE(canvasMenus.empty());
            const tachyon::IntRect inner = CanvasInner();
            const float2 centre = NodeViewScreenToCanvas(view, inner.position + tachyon::int2{ inner.w / 2, inner.h / 2 });
            EXPECT_FLOAT_EQ(canvasMenus.back().x, centre.x);
            EXPECT_FLOAT_EQ(canvasMenus.back().y, centre.y);
            EXPECT_TRUE(nodeMenus.empty());

            view.selectedNodeIDs   = { nodeA, nodeB };
            view.lastClickedNodeID = nodeB;
            press(KeyCode::F10, KeyModifier::Shift);
            ASSERT_FALSE(nodeMenus.empty());
            EXPECT_EQ(nodeMenus.back(), 1) << "the one clicked last";
            const NodeModel& b = view.nodes[1];
            const float2 below = NodeViewCanvasToScreen(view, float2{ float(b.pos.x), float(b.pos.y + b.lastSize.y) });
            ASSERT_TRUE(app.menuManager.IsOpen());
            EXPECT_EQ(app.GetElementRect(app.menuManager.levels[0].windowID).position,
                      (tachyon::int2{ int32_t(below.x), int32_t(below.y) })) << "the menu just below it";

            view.lastClickedNodeID = tachyon::ID::Invalid();
            press(KeyCode::Menu);
            EXPECT_EQ(nodeMenus.back(), 0) << "else the first selected";

            view.onNodeContextMenu = {};
            const size_t canvasBefore = canvasMenus.size();
            press(KeyCode::Application);
            EXPECT_EQ(canvasMenus.size(), canvasBefore) << "a node is selected: not the canvas's";
            EXPECT_FALSE(view.nodeMenuNode) << "and no node menu without the app's";
            EXPECT_FALSE(app.menuManager.IsOpen());

            view.selectedNodeIDs.clear();
            press(KeyCode::F10);
            EXPECT_EQ(canvasMenus.size(), canvasBefore) << "F10 alone isn't the menu key";
        }

        // ---- Reroutes ----

        // A reroute between A and B: A -> R -> B.
        class NodeViewRerouteTest : public NodeViewWireDropTest
        {
        protected:
            tachyon::ID reroute{};
            void SetUp() override
            {
                NodeViewWireDropTest::SetUp();
                reroute = app.idManager.CreateStaticID();
                NodeModel r{};
                r.stableID = reroute;
                r.reroute  = true;
                r.pos      = { 360, 192 };
                r.properties.push_back(NodeProperty{ .input = PropertyEndpoint{ 0, 0 }, .output = true });
                view.nodes.push_back(std::move(r));
                view.nodes[1].properties[0].input = PropertyEndpoint{ 2, 0 };
            }
            // The reroute's two sockets: its input (left edge) and output (right edge).
            std::vector<tachyon::IntRect> RerouteSockets() const
            {
                const auto square = app.GetElementRect(reroute);
                std::vector<tachyon::IntRect> out;
                for (const auto& r : RectsWithStyle(app.theme.nodeSocketStyleRef))
                    if (r.y >= square.y && r.y < square.y + square.h && r.x >= square.x - r.w && r.x < square.x + square.w)
                        out.push_back(r);
                std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.x < b.x; });
                return out;
            }
        };

        // A dot on a square two grid cells wide: no header, no rows, a socket on each edge.
        TEST_F(NodeViewRerouteTest, ARerouteIsADotWithASocketOnEachEdge)
        {
            Settle();
            const int pitch = 2 * app.theme.nodeGridSize;
            const auto square = app.GetElementRect(reroute);
            EXPECT_EQ(square.w, pitch);
            EXPECT_EQ(square.h, pitch);
            const auto sockets = RerouteSockets();
            ASSERT_EQ(sockets.size(), 2u);
            EXPECT_EQ(sockets[0].x + sockets[0].w / 2, square.x) << "the input on the left edge";
            EXPECT_EQ(sockets[1].x + sockets[1].w / 2, square.x + pitch) << "the output on the right";
            EXPECT_EQ(sockets[0].y + sockets[0].h / 2, square.y + pitch / 2);
            EXPECT_EQ(RectsWithStyle(app.theme.nodeHeaderStyleRef).size(), 2u) << "a header for A and B, none for the reroute";
            EXPECT_EQ(RectsWithStyle(app.theme.nodeRowStyleRef).size(), 2u) << "and no rows";
        }

        // Dragging its middle moves it, one edit; double-clicking doesn't collapse it.
        TEST_F(NodeViewRerouteTest, ARerouteMovesAndDoesNotCollapse)
        {
            Settle();
            int edits = 0;
            view.onEdit = [&](const NodeViewEdit&) { edits++; };
            const auto square = app.GetElementRect(reroute);
            const tachyon::int2 start = view.nodes[2].pos;   // on the grid
            const tachyon::int2 middle{ square.x + square.w / 2, square.y + square.h / 2 };
            Drag(middle, { middle.x + 4 * app.theme.nodeGridSize, middle.y + 2 * app.theme.nodeGridSize });
            Settle();
            EXPECT_EQ(view.nodes[2].pos, (tachyon::int2{ start.x + 4 * app.theme.nodeGridSize, start.y + 2 * app.theme.nodeGridSize }));
            EXPECT_EQ(edits, 1);

            const auto moved = app.GetElementRect(reroute);
            const tachyon::int2 at{ moved.x + moved.w / 2, moved.y + moved.h / 2 };
            app.ClickAt(at);
            Frame();
            app.ClickAt(at);
            Frame();
            EXPECT_FALSE(view.nodes[2].collapsed);
            EXPECT_EQ(edits, 1);
        }

        // A wire drags out of its right edge into another input, and its left edge takes one.
        TEST_F(NodeViewRerouteTest, WiresDragOutOfAndIntoARerouteAtItsEdges)
        {
            NodeModel c{};
            c.stableID = app.idManager.CreateStaticID();
            c.pos      = { 520, 520 };
            c.properties.push_back(NodeProperty{ .input = PropertyEndpoint{} });
            view.nodes.push_back(std::move(c));
            Settle();
            const auto sockets = RerouteSockets();
            ASSERT_EQ(sockets.size(), 2u);
            const tachyon::int2 out{ sockets[1].x + sockets[1].w / 2, sockets[1].y + sockets[1].h / 2 };
            Drag(out, SocketOf(view.nodes[3].stableID));
            Settle();
            EXPECT_EQ(*view.nodes[3].properties[0].input, (PropertyEndpoint{ 2, 0 })) << "one output feeds many";
            EXPECT_EQ(*view.nodes[1].properties[0].input, (PropertyEndpoint{ 2, 0 }));

            view.nodes[2].properties[0].input = PropertyEndpoint{};
            Settle();
            const auto again = RerouteSockets();
            const tachyon::int2 in{ again[0].x + again[0].w / 2, again[0].y + again[0].h / 2 };
            Drag(SocketOf(nodeA), in);
            Settle();
            EXPECT_EQ(*view.nodes[2].properties[0].input, (PropertyEndpoint{ 0, 0 }));
        }

        // A right click on a reroute opens its menu, even with a wire hovered last that reaches
        // into it (a lane handle); and without the app's node menu, as it has one of its own.
        TEST_F(NodeViewRerouteTest, ARightClickOnARerouteOpensItsMenuNotAWires)
        {
            Settle();
            const auto square = app.GetElementRect(reroute);
            const tachyon::int2 middle{ square.x + square.w / 2, square.y + square.h / 2 };
            app.InjectMouseMove(middle);
            Frame();
            view.hoveredWire.part = WireKey(2, 0);
            app.ClickAt(middle, MouseButtons::Right);
            Frame();
            Frame();
            EXPECT_FALSE(view.wireMenuWire) << "not the wire's";
            ASSERT_TRUE(view.nodeMenuNode);
            EXPECT_EQ(*view.nodeMenuNode, reroute);
            EXPECT_TRUE(app.menuManager.IsOpen());
            EXPECT_TRUE(view.selectedNodeIDs.contains(reroute));
        }

        // From the keys too, with it selected. The app's node menu is for its nodes, not this.
        TEST_F(NodeViewRerouteTest, TheMenuKeyOpensARerouteMenu)
        {
            Settle();
            app.ClickAt(CanvasPoint());   // focuses the canvas
            Frame();
            view.selectedNodeIDs   = { reroute };
            view.lastClickedNodeID = reroute;
            app.InjectKey(KeyCode::Application);
            Frame();
            Frame();
            ASSERT_TRUE(view.nodeMenuNode) << "though the app has no node menu";
            EXPECT_EQ(*view.nodeMenuNode, reroute);
            EXPECT_NE(app.FindByText("Remove reroute"), tachyon::ID::Invalid());

            int appItems = 0;
            view.onNodeContextMenu = [&](int) { ++appItems; app.MenuItem("Rename", [] {}); };
            Frame();
            Frame();
            EXPECT_EQ(appItems, 0) << "not the app's items";
            EXPECT_NE(app.FindByText("Remove reroute"), tachyon::ID::Invalid());
            EXPECT_EQ(app.FindByText("Rename"), tachyon::ID::Invalid());
        }

        // Enter collapses the selected nodes, but not a reroute: a dot has nothing to hide.
        TEST_F(NodeViewRerouteTest, EnterDoesNotCollapseAReroute)
        {
            Settle();
            app.ClickAt(CanvasPoint());   // focuses the canvas
            Frame();
            view.selectedNodeIDs = { reroute, nodeA };
            std::vector<NodeViewEdit> edits;
            view.onEdit = [&](const NodeViewEdit& e) { edits.push_back(e); };
            app.InjectKey(KeyCode::Return);
            Frame();
            EXPECT_TRUE(view.nodes[0].collapsed) << "A";
            EXPECT_FALSE(view.nodes[2].collapsed) << "the reroute";
            ASSERT_EQ(edits.size(), 1u);
            EXPECT_EQ(edits[0].collapses.size(), 1u);
        }

        // Its "Remove reroute" takes it out; A feeds B again.
        TEST_F(NodeViewRerouteTest, RemoveRerouteFromItsMenu)
        {
            Settle();
            const auto square = app.GetElementRect(reroute);
            const tachyon::int2 middle{ square.x + square.w / 2, square.y + square.h / 2 };
            app.InjectMouseMove(middle);
            Frame();
            app.ClickAt(middle, MouseButtons::Right);
            Frame();
            Frame();
            ASSERT_NE(app.FindByText("Remove reroute"), tachyon::ID::Invalid()) << "its own item, though the app has no node menu";
            app.InjectKey(KeyCode::Down);   // to the first item
            Frame();
            app.InjectKey(KeyCode::Return);
            Frame();
            Frame();
            ASSERT_EQ(view.nodes.size(), 2u);
            EXPECT_EQ(*view.nodes[1].properties[0].input, (PropertyEndpoint{ 0, 0 }));
        }

        // Its pins show while it's hovered or selected, else only the dot.
        TEST_F(NodeViewRerouteTest, ARerouteShowsItsPinsWhileHoveredOrSelected)
        {
            auto pinsDrawn = [&] {
                const auto square = app.GetElementRect(reroute);
                int drawn = 0;
                for (uint32_t d = 0; d < app.elementStorage.NumberOfDepthLevels(); d++)
                    for (uint32_t i = 0; i < app.elementStorage.DepthSizeOf(d); i++)
                    {
                        const tachyon::ElementRef ref{ d, i };
                        if (!app.elementStorage.IsValidRef(ref) || app.elementStorage.GetElementUnsafe(ref).styleRef != app.theme.nodeSocketStyleRef)
                            continue;
                        const auto& r = app.elementStorage.GetElementRectPtr(ref)->rect;
                        if (r.y >= square.y && r.y < square.y + square.h && r.x >= square.x - r.w && r.x < square.x + square.w
                            && app.elementStorage.GetCustomDraw(ref))
                            ++drawn;
                    }
                return drawn;
            };
            Settle();
            EXPECT_EQ(pinsDrawn(), 0) << "neither hovered nor selected";
            view.selectedNodeIDs = { reroute };
            Settle();
            EXPECT_EQ(pinsDrawn(), 2) << "selected";
            view.selectedNodeIDs.clear();
            const auto square = app.GetElementRect(reroute);
            app.InjectMouseMove({ square.x + square.w / 2, square.y + square.h / 2 });
            Settle();
            EXPECT_EQ(pinsDrawn(), 2) << "hovered";
        }

        // After a frame the view knows where its canvas is, for the conversions.
        TEST_F(NodeViewControlTest, TheViewNotesWhereItsCanvasIs)
        {
            app.runPostLayoutCallbacks = true;
            Settle();
            const tachyon::IntRect inner = CanvasInner();
            EXPECT_EQ(view.lastCanvasRect, inner);
            EXPECT_EQ(view.lastInnerSize, inner.size);
        }

        // A node removed while its menu is open: the menu goes. One moved to another index
        // still gets its own.
        TEST_F(NodeViewContextMenuTest, ANodesMenuFollowsTheNode)
        {
            Settle();
            std::vector<int> built;
            view.onNodeContextMenu = [&](int n) { built.push_back(n); app.MenuItem("Rename", [] {}); };
            RightClick(HeaderOf(nodeB));
            ASSERT_FALSE(built.empty());
            EXPECT_EQ(built.back(), 1);

            const int first[] = { 0 };
            ApplyNodeViewEdit(view, RemoveNodesEdit(view, first));
            Frame();
            EXPECT_EQ(built.back(), 0) << "B is node 0 now";

            const int last[] = { 0 };
            ApplyNodeViewEdit(view, RemoveNodesEdit(view, last));
            const size_t count = built.size();
            Frame();
            Frame();
            EXPECT_EQ(built.size(), count) << "gone with its node";
            EXPECT_FALSE(view.nodeMenuNode);
        }

        // ---- The selection changing ----

        // A click that selects a node is reported once, with the generation up one; the same
        // click again changes nothing and isn't.
        TEST_F(NodeViewControlTest, ASelectionChangeIsReportedOnce)
        {
            Settle();
            std::vector<std::unordered_set<tachyon::ID>> reported;
            view.onSelectionChanged = [&] { reported.push_back(view.selectedNodeIDs); };
            const uint32_t before = view.selectionGeneration;

            app.ClickAt(HeaderOf(nodeB));
            Frame();
            Frame();
            ASSERT_EQ(reported.size(), 1u);
            EXPECT_EQ(reported[0], std::unordered_set<tachyon::ID>{ nodeB });
            EXPECT_EQ(view.selectionGeneration, before + 1);

            app.AdvanceTime(1000);   // not a double-click
            app.ClickAt(HeaderOf(nodeB));
            Frame();
            Frame();
            EXPECT_EQ(reported.size(), 1u) << "selected already";
            EXPECT_EQ(view.selectionGeneration, before + 1);

            app.AdvanceTime(1000);
            app.ClickAt(HeaderOf(nodeA));
            Frame();
            Frame();
            ASSERT_EQ(reported.size(), 2u);
            EXPECT_EQ(reported[1], std::unordered_set<tachyon::ID>{ nodeA }) << "another node instead";
            EXPECT_EQ(view.selectionGeneration, before + 2);

            app.AdvanceTime(1000);
            app.ClickAt(CanvasPoint());
            Frame();
            Frame();
            ASSERT_EQ(reported.size(), 3u);
            EXPECT_TRUE(reported[2].empty()) << "a click on the canvas clears it";
        }

        // However it changes: a removed node, or the app setting it, is reported too.
        TEST_F(NodeViewControlTest, ASelectionChangedByARemovalOrTheAppIsReported)
        {
            Settle();
            int reported = 0;
            view.onSelectionChanged = [&] { ++reported; };
            view.selectedNodeIDs = { nodeA, nodeB };
            Frame();
            EXPECT_EQ(reported, 1) << "the app set it";
            Frame();
            EXPECT_EQ(reported, 1) << "nothing changed since";

            const int first[] = { 0 };
            ApplyNodeViewEdit(view, RemoveNodesEdit(view, first));
            Frame();
            EXPECT_EQ(reported, 2) << "a selected node went";
            EXPECT_EQ(view.selectedNodeIDs, std::unordered_set<tachyon::ID>{ nodeB });

            // Without the callback, the generation still counts.
            view.onSelectionChanged = {};
            const uint32_t before = view.selectionGeneration;
            view.selectedNodeIDs.clear();
            Frame();
            EXPECT_EQ(view.selectionGeneration, before + 1);
        }

        // ---- Virtual layout: nodes far out of view stand in as their box ----

        // A and B near the middle, each with a field; F far off to the right, with two.
        class NodeViewVirtualTest : public NodeViewControlTest
        {
        protected:
            tachyon::ID far{};
            int farBodies = 0;   // F's renderBody calls
            float a = 1.0f, b = 2.0f, f1 = 3.0f, f2 = 4.0f;
            void SetUp() override
            {
                NodeViewControlTest::SetUp();
                app.runPostLayoutCallbacks = true;   // the canvas's size and the nodes', as in the app
                view.nodes[0].properties[0].renderBody = [this] { app.Field(a); };
                view.nodes[1].properties[0].renderBody = [this] { app.Field(b); };
                far = app.idManager.CreateStaticID();
                NodeModel node{};
                node.stableID = far;
                node.title    = "F";
                node.pos      = { 40000, 0 };
                node.properties.push_back(NodeProperty{ .input = PropertyEndpoint{ 0, 0 }, .renderBody = [this] { ++farBodies; app.Field(f1); } });
                node.properties.push_back(NodeProperty{ .output = true, .renderBody = [this] { ++farBodies; app.Field(f2); }, .rowSpan = 2 });
                view.nodes.push_back(std::move(node));
            }
            tachyon::ID FocusedID() const { return app.guiStateManager.Interaction().GetFocusedID(); }
            // The socket rects inside `node`'s rect, relative to it, left to right then top down.
            std::vector<tachyon::IntRect> SocketsOf(tachyon::ID node) const
            {
                const auto n = app.GetElementRect(node);
                std::vector<tachyon::IntRect> out;
                for (auto r : RectsWithStyle(app.theme.nodeSocketStyleRef))
                    if (r.y >= n.y && r.y < n.y + n.h && r.x >= n.x - r.w && r.x < n.x + n.w + r.w)
                        out.push_back({ r.x - n.x, r.y - n.y, r.w, r.h });
                std::sort(out.begin(), out.end(), [](const auto& p, const auto& q) { return p.x != q.x ? p.x < q.x : p.y < q.y; });
                return out;
            }
        };

        // F stands in: its box at its last size, its sockets where a whole layout puts them,
        // and no renderBody.
        TEST_F(NodeViewVirtualTest, ANodeFarOutOfViewStandsIn)
        {
            view.virtualizeLayout = false;
            Settle();
            Settle();
            const tachyon::IntRect whole = app.GetElementRect(far);
            const auto wholeSockets = SocketsOf(far);
            ASSERT_EQ(wholeSockets.size(), 2u);
            ASSERT_GT(farBodies, 0);

            view.virtualizeLayout = true;
            Settle();
            farBodies = 0;
            Settle();
            EXPECT_EQ(farBodies, 0) << "no renderBody for it";
            EXPECT_EQ(app.GetElementRect(far).size, whole.size) << "its last size";
            EXPECT_EQ(SocketsOf(far), wholeSockets) << "its sockets where they'd be";
            EXPECT_EQ(view.nodes[2].lastSize, whole.size);
            EXPECT_FALSE(app.HasElement(app.FindByText("F"))) << "no header";
        }

        // Tab from the last control in view goes into F: laid out, the focus on its first
        // field, the view panned to it. Shift+Tab into it lands on its last field.
        TEST_F(NodeViewVirtualTest, TabReachesANodeOutOfView)
        {
            Settle();   // laid out whole once, to learn its size
            farBodies = 0;
            Settle();
            ASSERT_EQ(farBodies, 0) << "F stands in";
            app.guiStateManager.Interaction().SetFocusID(app.FindByText("2"));   // B's field
            Frame();
            const tachyon::ID bField = FocusedID();
            ASSERT_NE(bField, tachyon::ID::Invalid());

            app.InjectKey(KeyCode::Tab);
            for (int i = 0; i < 4; ++i) Frame();
            EXPECT_GT(farBodies, 0) << "laid out";
            const tachyon::ID first = FocusedID();
            EXPECT_EQ(view.focusedNodeID, far) << "the focus is in F";
            EXPECT_EQ(app.TextOf(first), "3") << "on its first field";
            const tachyon::float2 seen = view.transform.TransformPoint(tachyon::float2{ 40000.0f, 0.0f });
            EXPECT_LT(std::abs(seen.x), float(view.lastInnerSize.x)) << "panned to it";

            // Back to B, then the other way round into F: its last field.
            app.InjectKey(KeyCode::Tab, KeyAction::KeyDown, KeyModifier::Shift);
            for (int i = 0; i < 4; ++i) Frame();
            EXPECT_EQ(FocusedID(), bField);
            view.transform = tachyon::float3x2::Scale(1.0f);   // F out of view again
            for (int i = 0; i < 3; ++i) Frame();
            farBodies = 0;
            Frame();
            ASSERT_EQ(farBodies, 0) << "a stand-in again";
            app.guiStateManager.Interaction().SetFocusID(app.FindByText("\xE2\x88\x92"));   // the zoom controls' minus, right after F
            Frame();
            app.InjectKey(KeyCode::Tab, KeyAction::KeyDown, KeyModifier::Shift);
            for (int i = 0; i < 4; ++i) Frame();
            EXPECT_EQ(app.TextOf(FocusedID()), "4") << "on its last field";
        }

        // A large graph: 900 nodes on a wide grid, a few of them in view. Only those near it
        // call renderBody each frame; all of them, with the virtual layout off.
        TEST_F(NodeViewVirtualTest, ALargeGraphLaysOutOnlyWhatsNearTheView)
        {
            int bodies = 0;
            view.nodes.clear();
            for (int y = 0; y < 30; ++y)
                for (int x = 0; x < 30; ++x)
                {
                    NodeModel node{};
                    node.stableID = app.idManager.CreateStaticID();
                    node.pos      = { (x - 15) * 600, (y - 15) * 400 };
                    node.properties.push_back(NodeProperty{ .output = true, .renderBody = [&bodies] { ++bodies; } });
                    view.nodes.push_back(std::move(node));
                }
            Settle();   // each laid out once, to learn its size
            bodies = 0;
            Frame();
            EXPECT_GT(bodies, 0);
            EXPECT_LT(bodies, 60) << "only those in or near view, of 900";

            view.virtualizeLayout = false;
            bodies = 0;
            Frame();
            EXPECT_EQ(bodies, 900);
        }

        // A node out of the view but within half its size of it is laid out whole.
        TEST_F(NodeViewVirtualTest, ANodeNearTheViewIsLaidOut)
        {
            Settle();
            ASSERT_GT(view.lastInnerSize.x, 0);
            view.nodes[2].pos = { view.lastInnerSize.x * 3 / 4, 0 };   // past the right edge, within half
            Settle();
            const tachyon::float2 seen = view.transform.TransformPoint(tachyon::float2{ float(view.nodes[2].pos.x), 0.0f });
            ASSERT_GT(seen.x, float(view.lastInnerSize.x) / 2) << "out of the view";
            farBodies = 0;
            Frame();
            EXPECT_GT(farBodies, 0);
        }

        // A node added far out of view is laid out once, so it has a size to stand in with.
        TEST_F(NodeViewVirtualTest, ANewNodeFarAwayIsLaidOutOnce)
        {
            Settle();
            NodeModel node{};
            node.stableID = app.idManager.CreateStaticID();
            node.title    = "G";
            node.pos      = { 0, 40000 };
            node.properties.push_back(NodeProperty{ .output = true, .renderBody = [this] { app.Field(f1); } });
            view.nodes.push_back(std::move(node));
            Settle();
            EXPECT_GT(view.nodes[3].lastSize.x, 0);
            EXPECT_GT(view.nodes[3].lastSize.y, 0);
        }

        // Collapsed nodes and reroutes are laid out as they are wherever they are: their
        // headers and sockets as with the virtual layout off.
        TEST_F(NodeViewVirtualTest, CollapsedNodesAndReroutesNeverStandIn)
        {
            view.nodes[2].collapsed = true;
            NodeModel dot{};
            dot.stableID = app.idManager.CreateStaticID();
            dot.reroute  = true;
            dot.pos      = { 0, 40000 };
            dot.properties.push_back(NodeProperty{ .input = PropertyEndpoint{}, .output = true });
            view.nodes.push_back(std::move(dot));
            const tachyon::ID dotID = view.nodes[3].stableID;

            view.virtualizeLayout = false;
            Settle();
            Settle();
            const auto farSockets = SocketsOf(far);
            const auto dotSockets = SocketsOf(dotID);
            ASSERT_EQ(dotSockets.size(), 2u);

            view.virtualizeLayout = true;
            Settle();
            Settle();
            EXPECT_TRUE(app.HasElement(app.FindByText("F"))) << "its header";
            EXPECT_EQ(SocketsOf(far), farSockets);
            EXPECT_EQ(SocketsOf(dotID), dotSockets);
        }

        // Home and the Fit button frame the selected nodes: B selected, B in the middle.
        TEST_F(NodeViewVirtualTest, HomeAndFitFrameTheSelection)
        {
            view.nodes.resize(2);   // A and B
            Settle();
            const tachyon::float2 nodeMiddle{ float(view.nodes[1].pos.x + view.nodes[1].lastSize.x / 2),
                                     float(view.nodes[1].pos.y + view.nodes[1].lastSize.y / 2) };
            auto middle = [&] { return view.transform.Inverse().TransformPoint(tachyon::float2{ 0.0f, 0.0f }); };

            app.ClickAt(CanvasPoint());   // focuses the canvas
            Frame();
            view.selectedNodeIDs = { nodeB };
            view.transform = tachyon::float3x2::Scale(1.0f);
            app.InjectKey(KeyCode::Home);
            Frame();
            EXPECT_NEAR(middle().x, nodeMiddle.x, 1.0f) << "Home";
            EXPECT_NEAR(middle().y, nodeMiddle.y, 1.0f);

            view.transform = tachyon::float3x2::Scale(1.0f);
            Settle();
            app.ClickAt(app.CenterOf(app.FindByText("Fit")));
            Frame();
            EXPECT_NEAR(middle().x, nodeMiddle.x, 1.0f) << "Fit";
            EXPECT_NEAR(middle().y, nodeMiddle.y, 1.0f);
        }

        // A node holding the focus stays laid out whole when the view leaves it.
        TEST_F(NodeViewVirtualTest, TheFocusedNodeStaysWhole)
        {
            view.virtualizeLayout = false;
            Settle();
            app.guiStateManager.Interaction().SetFocusID(app.FindByText("3"));   // F's field
            Settle();
            view.virtualizeLayout = true;
            farBodies = 0;
            Settle();
            EXPECT_GT(farBodies, 0);
            EXPECT_EQ(app.TextOf(FocusedID()), "3");
        }

        TEST_F(NodeViewControlTest, NodesLayOutAtTheirPositions)
        {
            Settle();
            ASSERT_TRUE(app.HasElement(nodeA));
            ASSERT_TRUE(app.HasElement(nodeB));
            const auto a = app.GetElementRect(nodeA);
            const auto b = app.GetElementRect(nodeB);
            EXPECT_GT(a.w, 0);
            EXPECT_GT(b.w, 0);
            // B sits below-right of A (pos {100,80} vs {520,320}).
            EXPECT_LT(a.x, b.x);
            EXPECT_LT(a.y, b.y);
        }

        TEST_F(NodeViewControlTest, RenderNodeViewIsReentrantAcrossFrames)
        {
            // Construction-balance checks (registered in the harness) fire on
            // every frame; running several proves RenderNodeView opens and
            // closes its element scopes correctly each time.
            for (int i = 0; i < 4; ++i) Frame();
            EXPECT_TRUE(app.HasElement(nodeA));
            EXPECT_TRUE(app.HasElement(nodeB));
        }

        TEST_F(NodeViewControlTest, BareClickOnEmptyCanvasClearsSelection)
        {
            Settle();
            view.selectedNodeIDs.insert(nodeA);

            app.ClickAt(CanvasPoint());
            Frame();

            EXPECT_TRUE(view.selectedNodeIDs.empty())
                << "a click on the empty canvas clears the node selection";
        }

        TEST_F(NodeViewControlTest, BackgroundDownActivatesMarquee)
        {
            Settle();
            const tachyon::int2 from = CanvasPoint();

            app.InjectMouseMove(from);
            app.InjectMouseDown(from);
            Frame();
            EXPECT_TRUE(view.marquee.IsHeld()) << "background MouseDown starts a marquee";

            app.InjectMouseMove({ from.x - 80, from.y + 40 }, MouseButtons::Left);
            Frame();
            EXPECT_TRUE((view.marquee.Phase() == GesturePhase::Dragging))
                << "dragging past the 4px threshold engages the marquee";
        }

        TEST_F(NodeViewControlTest, MiddleDragPansTransform)
        {
            Settle();
            const tachyon::int2 from = CanvasPoint();
            const tachyon::int2 to{ from.x + 60, from.y + 25 };

            app.InjectMouseMove(from);
            app.InjectMouseDown(from, MouseButtons::Middle);
            Frame();
            app.InjectMouseMove(to, MouseButtons::Middle);
            Frame();
            app.InjectMouseUp(to, MouseButtons::Middle);
            Frame();

            // Pan accumulates into the transform's translation column.
            EXPECT_NEAR(view.transform.r0.z, 60.0f, 0.5f);
            EXPECT_NEAR(view.transform.r1.z, 25.0f, 0.5f);
        }

        // Without a middle button: Alt + left drags the view, over a node too, and moves no node.
        TEST_F(NodeViewControlTest, AltLeftDragPans)
        {
            Settle();
            const tachyon::int2 positionBefore = view.nodes[0].pos;
            for (const tachyon::int2 from : { CanvasPoint(), HeaderOf(nodeA) })
            {
                const float startX = view.transform.r0.z;
                app.InjectMouseMove(from, MouseButtons::None, KeyModifier::Alt);
                app.InjectMouseDown(from, MouseButtons::Left, KeyModifier::Alt);
                Frame();
                app.InjectMouseMove({ from.x + 50, from.y }, MouseButtons::Left, KeyModifier::Alt);
                Frame();
                app.InjectMouseUp({ from.x + 50, from.y }, MouseButtons::Left, KeyModifier::Alt);
                Frame();
                EXPECT_NEAR(view.transform.r0.z, startX + 50.0f, 0.5f);
            }
            EXPECT_EQ(view.nodes[0].pos, positionBefore) << "the node stays put";
            EXPECT_FALSE((view.marquee.Phase() == GesturePhase::Dragging)) << "no marquee either";
        }

        TEST_F(NodeViewControlTest, ScrollZoomsTransform)
        {
            Settle();
            const tachyon::int2 p = CanvasPoint();

            app.InjectMouseMove(p);
            app.InjectMouseScroll(p, { 0, -1 });
            Frame();

            EXPECT_NE(view.transform.r0.x, 1.0f) << "scroll over the canvas must zoom";
        }

        // With the wheel set to scroll, it pans the view, Ctrl+wheel zooms; a pinch always zooms.
        TEST_F(NodeViewControlTest, TheWheelScrollsInPanMode)
        {
            app.SetWheelMode(WheelMode::Pan);
            Settle();
            const tachyon::int2 p = CanvasPoint();
            app.InjectMouseMove(p);
            app.InjectMouseScroll(p, { 0, -1 });
            Frame();
            EXPECT_EQ(view.transform.r0.x, 1.0f) << "no zoom";
            EXPECT_LT(view.transform.r1.z, 0.0f) << "toward the user scrolls down: the canvas moves up";
            app.InjectMouseScroll(p, { 0, 1 }, KeyModifier::Control);
            Frame();
            EXPECT_GT(view.transform.r0.x, 1.0f) << "Ctrl+wheel zooms in";
            const float zoom = view.transform.r0.x;
            app.InjectPinch(0.8f);
            Frame();
            EXPECT_LT(view.transform.r0.x, zoom) << "a pinch zooms out";
        }

        TEST_F(NodeViewControlTest, EditModeFollowsCanvasFocus)
        {
            view.commandMode = CommandMode::Default;
            Settle();
            // Focusing the canvas (via a background click) sets the shortcut
            // manager's edit mode to the view's; this exercises the focus/mode
            // wiring without needing a node to be hittable.
            app.ClickAt(CanvasPoint());
            Frame();
            // The default view editMode is Default, so this just verifies the
            // path runs without disturbing the mode.
            EXPECT_EQ(app.commandManager.GetMode(), CommandMode::Default);
        }

        // With the canvas focused, the keys do what the mouse does.
        TEST_F(NodeViewControlTest, TheKeyboardPansZoomsSelectsAndMovesNodes)
        {
            Settle();
            app.ClickAt(CanvasPoint());   // focuses the canvas, selects nothing
            Settle();
            auto press = [&](KeyCode key, KeyModifier mods = KeyModifier::None) {
                app.InjectKey(key, KeyAction::KeyDown, mods);
                Frame();
            };

            const float panBefore = view.transform.r0.z;
            press(KeyCode::Right);
            EXPECT_LT(view.transform.r0.z, panBefore) << "Right with nothing selected pans";

            press(KeyCode::Equals);
            EXPECT_GT(NodeViewZoom(view), 0.99f);
            view.transform = tachyon::float3x2::Scale(0.5f);
            press(KeyCode::Equals);
            EXPECT_NEAR(NodeViewZoom(view), 0.55f, 1e-3f) << "+ zooms in";
            press(KeyCode::Minus);
            EXPECT_NEAR(NodeViewZoom(view), 0.5f, 1e-3f) << "- zooms out";

            press(KeyCode::KeyA, KeyModifier::Control);
            EXPECT_EQ(view.selectedNodeIDs.size(), 2u) << "Ctrl+A selects every node";

            const int2 a = view.nodes[0].pos, b = view.nodes[1].pos;
            const int32_t grid = app.theme.nodeGridSize;
            press(KeyCode::Down);
            EXPECT_EQ(view.nodes[0].pos, (int2{ a.x, a.y + grid })) << "Down moves the selected nodes a grid step";
            EXPECT_EQ(view.nodes[1].pos, (int2{ b.x, b.y + grid }));
            press(KeyCode::Left, KeyModifier::Shift);
            EXPECT_EQ(view.nodes[0].pos, (int2{ a.x - 4 * grid, a.y + grid })) << "Shift: four steps";

            press(KeyCode::Return);
            EXPECT_TRUE(view.nodes[0].collapsed && view.nodes[1].collapsed) << "Enter collapses them";
            press(KeyCode::Return);
            EXPECT_FALSE(view.nodes[0].collapsed) << "and expands them";

            press(KeyCode::Escape);
            EXPECT_TRUE(view.selectedNodeIDs.empty()) << "Escape selects none";
        }

        // Tab reaches the fields of a node out of view, and the view pans to that node.
        TEST_F(NodeViewControlTest, TabToANodeOutOfViewPansToIt)
        {
            app.runPostLayoutCallbacks = true;   // the view learns its size and the nodes theirs there
            const ID farID = app.idManager.CreateStaticID();
            NodeModel far{};
            far.stableID = farID;
            far.title    = "Far";
            far.pos      = { 4000, 80 };
            far.properties.push_back(NodeProperty{ .renderBody = [this] { app.Button("Go", [] {}); } });
            view.nodes.push_back(std::move(far));
            Settle();

            // The node's rect on screen: the canvas centre plus the view transform of its
            // canvas position.
            const tachyon::IntRect inner = CanvasInner();
            auto onScreen = [&] {
                const NodeModel& node = view.nodes[2];
                const float2 a = view.transform.TransformPoint(float2{ (float)node.pos.x, (float)node.pos.y });
                const float2 b = view.transform.TransformPoint(float2{ (float)(node.pos.x + node.lastSize.x),
                                                                       (float)(node.pos.y + node.lastSize.y) });
                const float2 c{ inner.x + inner.w * 0.5f, inner.y + inner.h * 0.5f };
                return tachyon::IntRect{ int(c.x + a.x), int(c.y + a.y), int(b.x - a.x), int(b.y - a.y) };
            };
            ASSERT_GT(view.nodes[2].lastSize.x, 0);
            ASSERT_GT(onScreen().x, inner.x + inner.w) << "test setup: the node starts out of view";

            auto focusInFar = [&] {
                return (app.guiStateManager.GetCurrentState(farID) & ElementState::Focused) != ElementState::None;
            };
            for (int i = 0; i < 8 && !focusInFar(); ++i)
            {
                app.InjectKey(KeyCode::Tab);
                Settle();
            }
            ASSERT_TRUE(focusInFar()) << "Tab reaches the far node's button";
            Settle();

            const tachyon::IntRect node = onScreen();
            EXPECT_GE(node.x, inner.x);
            EXPECT_LE(node.x + node.w, inner.x + inner.w) << "the view panned to the node";
            EXPECT_GE(node.y, inner.y);
            EXPECT_LE(node.y + node.h, inner.y + inner.h);
        }

        // Panning away from the focused node keeps the view where it was put.
        TEST_F(NodeViewControlTest, PanningAwayFromTheFocusedNodeStays)
        {
            app.runPostLayoutCallbacks = true;
            view.nodes[0].properties.push_back(NodeProperty{ .renderBody = [this] { app.Button("Go", [] {}); } });
            Settle();
            app.InjectKey(KeyCode::Tab);
            Settle();
            ASSERT_TRUE((app.guiStateManager.GetCurrentState(nodeA) & ElementState::Focused) != ElementState::None);

            view.transform = tachyon::float3x2::Translate(tachyon::float2{ -5000.0f, 0.0f });
            Settle();
            EXPECT_EQ(view.transform.r0.z, -5000.0f) << "focus that didn't move doesn't pan";
        }

        // ---- Pressing and dragging: a click isn't a move, Escape puts things back ----

        // A press that moves less than the drag distance is a click: the node stays where it
        // was and nothing is recorded.
        TEST_F(NodeViewControlTest, AClickOnANodeDoesNotMoveIt)
        {
            Settle();
            int edits = 0;
            view.onEdit = [&](const NodeViewEdit&) { edits++; };
            const tachyon::int2 before = view.nodes[1].pos;
            const tachyon::int2 from = HeaderOf(nodeB);
            app.InjectMouseMove(from);
            app.InjectMouseDown(from);
            Frame();
            app.InjectMouseMove({ from.x + 2, from.y + 1 }, MouseButtons::Left);
            Frame();
            EXPECT_TRUE(view.nodeDrag.IsPressed(nodeB));
            EXPECT_EQ(view.nodeDrag.Phase(), GesturePhase::Pressed) << "under the drag distance it isn't a move yet";
            app.InjectMouseUp({ from.x + 2, from.y + 1 });
            Settle();
            EXPECT_EQ(view.nodes[1].pos, before);
            EXPECT_EQ(edits, 0);
            EXPECT_TRUE(view.selectedNodeIDs.count(nodeB)) << "the click still selects it";
        }

        TEST_F(NodeViewControlTest, EscapeDuringANodeMovePutsItBack)
        {
            Settle();
            int edits = 0;
            view.onEdit = [&](const NodeViewEdit&) { edits++; };
            const tachyon::int2 before = view.nodes[1].pos;
            const tachyon::int2 from = HeaderOf(nodeB);
            const tachyon::int2 to{ from.x + 6 * app.theme.nodeGridSize, from.y + 2 * app.theme.nodeGridSize };
            app.InjectMouseMove(from);
            app.InjectMouseDown(from);
            Frame();
            app.InjectMouseMove(to, MouseButtons::Left);
            Frame();
            ASSERT_NE(view.nodes[1].pos, before) << "test setup: the node follows the drag";

            app.InjectKey(KeyCode::Escape);
            Frame();
            EXPECT_EQ(view.nodes[1].pos, before);
            app.InjectMouseMove({ to.x + 40, to.y }, MouseButtons::Left);
            app.InjectMouseUp({ to.x + 40, to.y });
            Settle();
            EXPECT_EQ(view.nodes[1].pos, before) << "the moves after Escape are ignored";
            EXPECT_EQ(edits, 0) << "a cancelled move isn't recorded";
        }

        // The node follows the cursor at any zoom: twice as far on screen at 200%.
        TEST_F(NodeViewControlTest, ANodeFollowsTheCursorWhenZoomed)
        {
            view.transform = tachyon::float3x2::Scale(2.0f);
            Settle();
            const int32_t grid = app.theme.nodeGridSize;
            const tachyon::int2 before = view.nodes[0].pos;
            // The canvas scales its content around its inner rect's top-left.
            const tachyon::int2 pivot = CanvasInner().position;
            const tachyon::int2 header = HeaderOf(nodeA);
            const tachyon::int2 from{ pivot.x + 2 * (header.x - pivot.x), pivot.y + 2 * (header.y - pivot.y) };
            Drag(from, { from.x + 2 * 4 * grid, from.y + 2 * 2 * grid });
            Settle();
            EXPECT_EQ(view.nodes[0].pos.x - before.x, 4 * grid);
            EXPECT_EQ(view.nodes[0].pos.y - before.y, 2 * grid);
        }

        TEST_F(NodeViewControlTest, EscapeDuringAMarqueePutsTheSelectionBack)
        {
            Settle();
            view.selectedNodeIDs = { nodeA };
            const tachyon::int2 from = CanvasPoint();
            app.InjectMouseMove(from);
            app.InjectMouseDown(from);
            Frame();
            app.InjectMouseMove({ from.x + 30, from.y + 30 }, MouseButtons::Left);
            Frame();
            ASSERT_TRUE((view.marquee.Phase() == GesturePhase::Dragging));
            ASSERT_TRUE(view.selectedNodeIDs.empty()) << "test setup: the marquee selects what it covers, nothing";

            app.InjectKey(KeyCode::Escape);
            Frame();
            EXPECT_EQ(view.selectedNodeIDs, (std::unordered_set<tachyon::ID>{ nodeA }));
            EXPECT_FALSE(view.marquee.IsHeld());
            app.InjectMouseUp({ from.x + 30, from.y + 30 });
            Frame();
            EXPECT_EQ(view.selectedNodeIDs, (std::unordered_set<tachyon::ID>{ nodeA })) << "the release isn't a click either";
        }

        TEST_F(NodeViewControlTest, EscapeDuringAPanPutsTheViewBack)
        {
            Settle();
            const tachyon::float3x2 before = view.transform;
            const tachyon::int2 from = CanvasPoint();
            app.InjectMouseMove(from);
            app.InjectMouseDown(from, MouseButtons::Middle);
            Frame();
            app.InjectMouseMove({ from.x + 60, from.y + 25 }, MouseButtons::Middle);
            Frame();
            ASSERT_NE(view.transform.r0.z, before.r0.z);

            app.InjectKey(KeyCode::Escape);
            Frame();
            EXPECT_EQ(view.transform.r0.z, before.r0.z);
            EXPECT_EQ(view.transform.r1.z, before.r1.z);
            app.InjectMouseMove({ from.x + 90, from.y + 25 }, MouseButtons::Middle);
            app.InjectMouseUp({ from.x + 90, from.y + 25 }, MouseButtons::Middle);
            Frame();
            EXPECT_EQ(view.transform.r0.z, before.r0.z) << "the moves after Escape don't pan";
        }

        // The minimap follows only a press of its own: a drag from the canvas that crosses it
        // leaves the view alone.
        TEST_F(NodeViewControlTest, TheMinimapFollowsOnlyItsOwnPress)
        {
            Settle();
            const auto minimaps = RectsWithStyle(app.theme.nodeViewMinimapStyleRef);
            ASSERT_EQ(minimaps.size(), 1u);
            const tachyon::IntRect m = minimaps[0];
            const tachyon::int2 inMinimap{ m.x + m.w / 4, m.y + m.h / 4 };
            // Drawing sets the minimap's mapping; headless, nothing is drawn.
            view.minimapScale  = 0.25f;
            view.minimapOrigin = { -200.0f, -100.0f };
            const tachyon::float3x2 before = view.transform;

            const tachyon::int2 from = CanvasPoint();
            app.InjectMouseMove(from);
            app.InjectMouseDown(from);
            Frame();
            app.InjectMouseMove(inMinimap, MouseButtons::Left);
            Frame();
            EXPECT_EQ(view.transform.r0.z, before.r0.z);
            EXPECT_EQ(view.transform.r1.z, before.r1.z);
            app.InjectMouseUp(inMinimap);
            Frame();

            // Its own press centres the view there; Escape puts it back.
            app.InjectMouseMove(inMinimap);
            app.InjectMouseDown(inMinimap);
            Frame();
            ASSERT_NE(view.transform.r0.z, before.r0.z) << "a press in the minimap centres the view";
            app.InjectMouseMove({ inMinimap.x + 10, inMinimap.y + 5 }, MouseButtons::Left);
            Frame();
            app.InjectKey(KeyCode::Escape);
            Frame();
            EXPECT_EQ(view.transform.r0.z, before.r0.z);
            EXPECT_EQ(view.transform.r1.z, before.r1.z);
            app.InjectMouseUp({ inMinimap.x + 10, inMinimap.y + 5 });
            Frame();
        }

        // Dragging a wire's lane moves it; Escape puts it back and records nothing.
        TEST_F(NodeViewControlTest, EscapeDuringALaneDragPutsTheLaneBack)
        {
            view.nodes[1].properties[0].input = PropertyEndpoint{ 0, 0 };
            Settle();
            int edits = 0;
            view.onEdit = [&](const NodeViewEdit&) { edits++; };
            const uint32_t key = WireKey(1, 0);
            // Each frame's route pass caches the wires' draggable segments for the next one; this
            // wire is straight, so the test gives it a vertical segment before every frame.
            const CachedWireHandle handle{ .wireKey = key, .role = WireHandle::LaneX, .vertical = true,
                                           .a = { 300, 100 }, .b = { 300, 300 } };
            auto frame = [&] { view.wireHandleCache = { handle }; Frame(); };
            frame();
            // Node A's laid-out rect against its pos gives where node.pos space starts.
            const tachyon::int2 origin = app.GetElementRect(nodeA).position - view.nodes[0].pos;
            const tachyon::int2 grab = origin + tachyon::int2{ 300, 200 };
            app.InjectMouseMove(grab);
            app.InjectMouseDown(grab);
            frame();
            app.InjectMouseMove({ grab.x + 40, grab.y }, MouseButtons::Left);
            frame();
            const auto moved = view.wireOverrides.find(key);
            ASSERT_NE(moved, view.wireOverrides.end()) << "test setup: the lane follows the drag";
            EXPECT_EQ(moved->second.laneX, 340);

            app.InjectKey(KeyCode::Escape);
            frame();
            EXPECT_EQ(view.wireOverrides.count(key), 0u) << "the wire had no lane of its own";
            app.InjectMouseMove({ grab.x + 80, grab.y }, MouseButtons::Left);
            app.InjectMouseUp({ grab.x + 80, grab.y });
            frame();
            EXPECT_EQ(view.wireOverrides.count(key), 0u);
            EXPECT_EQ(edits, 0);
        }

        // A move with the button held over the minimap, from a press that went to an element
        // that didn't take the mouse, leaves the view alone.
        TEST_F(NodeViewControlTest, TheMinimapIgnoresAMoveWithoutItsPress)
        {
            Settle();
            view.minimapScale  = 0.25f;
            view.minimapOrigin = { -200.0f, -100.0f };
            ElementRef minimapRef = ElementRef::Invalid();
            for (uint32_t d = 0; d < app.elementStorage.NumberOfDepthLevels() && !minimapRef.IsValid(); d++)
                for (uint32_t i = 0; i < app.elementStorage.DepthSizeOf(d); i++)
                {
                    const tachyon::ElementRef ref{ d, i };
                    if (app.elementStorage.IsValidRef(ref) &&
                        app.elementStorage.GetElementUnsafe(ref).styleRef == app.theme.nodeViewMinimapStyleRef) { minimapRef = ref; break; }
                }
            ASSERT_TRUE(minimapRef.IsValid());
            const Element& minimap = app.elementStorage.GetElementUnsafe(minimapRef);
            const ElementRect& rect = app.elementStorage.GetElementRectUnsafe(minimapRef);
            InputState move{};
            move.screenPos   = { rect.innerRect.x + 10, rect.innerRect.y + 10 };
            move.buttons     = MouseButtons::Left;
            move.prevButtons = MouseButtons::Left;
            const tachyon::float3x2 before = view.transform;
            EXPECT_FALSE(minimap.eventHandler(ElementEvent{ .id = minimap.id, .type = EventType::MouseMove, .state = move,
                                                            .rect = &rect, .elementRef = minimapRef }));
            EXPECT_EQ(view.transform.r0.z, before.r0.z);
            EXPECT_EQ(view.transform.r1.z, before.r1.z);
        }
        // A pin's tooltip: its name, its type and its value now, and for a wired input where
        // the value comes from. The value is asked for only while it shows.
        TEST_F(NodeViewControlTest, APinsTooltipSaysWhatFlowsThroughIt)
        {
            view.nodes[0].title = "Noise";
            view.nodes[0].properties[0].name = "Value";
            view.nodes[0].properties[0].type = "float";
            int asked = 0;
            auto& strength = view.nodes[1].properties[0];
            strength.name  = "Strength";
            strength.type  = "float";
            strength.value = [&] { ++asked; return std::string("0.75"); };
            strength.input = PropertyEndpoint{ 0, 0 };
            Settle();
            EXPECT_EQ(asked, 0);

            Hover(SocketOf(nodeB));
            EXPECT_EQ(TooltipTexts(), (std::vector<std::string>{ "Strength", "float", "0.75", "from Noise.Value" }));
            EXPECT_GT(asked, 0);
            const ID value = app.FindByText("0.75"), from = app.FindByText("from Noise.Value");
            EXPECT_EQ(app.elementStorage.GetElementUnsafe(app.elementStorage.FindElementRefByID(value)).styleRef, app.theme.tooltipPinValueStyleRef);
            EXPECT_EQ(app.elementStorage.GetElementUnsafe(app.elementStorage.FindElementRefByID(from)).styleRef, app.theme.tooltipNoteStyleRef);
            EXPECT_EQ(app.elementStorage.GetElementUnsafe(app.elementStorage.FindElementRefByID(app.FindByText("float"))).styleRef,
                      app.theme.tooltipPinTypeStyleRef);

            // An output says nothing of a source; a source without a name, only its node.
            view.nodes[0].properties[0].name.clear();
            Hover(SocketOf(nodeA));
            EXPECT_EQ(TooltipTexts(), (std::vector<std::string>{ "float" }));
            Hover(SocketOf(nodeB));
            ASSERT_FALSE(TooltipTexts().empty());
            EXPECT_EQ(TooltipTexts().back(), "from Noise");
        }

        // A pin with nothing to say has no tooltip.
        TEST_F(NodeViewControlTest, APinWithNothingToSayHasNoTooltip)
        {
            Settle();
            Hover(SocketOf(nodeB));
            EXPECT_FALSE(app.elementStorage.FindElementRefByID(app.tooltipWindowID).IsValid());
        }

        // A row with an input and an output: its output says nothing of the input's source.
        TEST_F(NodeViewControlTest, AnOutputSaysNothingOfItsRowsSource)
        {
            view.nodes[0].properties[0].name = "Value";
            auto& through = view.nodes[1].properties[0];
            through.name   = "Through";
            through.input  = PropertyEndpoint{ 0, 0 };
            through.output = true;
            Settle();
            // B's output socket: right of B.
            const tachyon::IntRect n = app.GetElementRect(nodeB);
            tachyon::int2 out{ -1, -1 };
            for (const auto& r : RectsWithStyle(app.theme.nodeSocketStyleRef))
                if (r.x + r.w / 2 > n.x + n.w / 2 && r.y >= n.y && r.y < n.y + n.h)
                    out = { r.x + r.w / 2, r.y + r.h / 2 };
            ASSERT_NE(out.x, -1);
            Hover(out);
            EXPECT_EQ(TooltipTexts(), (std::vector<std::string>{ "Through" }));
        }

        // Panned and zoomed, the tooltip is still at its pin: below it, centered under it, on
        // the screen.
        TEST_F(NodeViewControlTest, APinsTooltipFollowsThePanAndZoom)
        {
            view.nodes[1].properties[0].name = "Strength";
            view.transform = tachyon::float3x2::Translate(-150, -90) * tachyon::float3x2::Scale(1.5f);
            Settle();
            tachyon::ElementRef socket = tachyon::ElementRef::Invalid();
            for (uint32_t d = 0; d < app.elementStorage.NumberOfDepthLevels() && !socket.IsValid(); d++)
                for (uint32_t i = 0; i < app.elementStorage.DepthSizeOf(d); i++)
                {
                    const tachyon::ElementRef ref{ d, i };
                    if (app.elementStorage.IsValidRef(ref) && app.elementStorage.GetElementUnsafe(ref).styleRef == app.theme.nodeSocketStyleRef &&
                        app.elementStorage.GetElementRectUnsafe(ref).rect.Contains(SocketOf(nodeB)))
                    { socket = ref; break; }
                }
            ASSERT_TRUE(socket.IsValid());
            const tachyon::IntRect onScreen = app.elementStorage.ElementRectOnScreen(socket);
            ASSERT_NE(onScreen, app.elementStorage.GetElementRectUnsafe(socket).rect) << "the view is panned and zoomed";

            Hover(onScreen.position + tachyon::int2{ onScreen.w / 2, onScreen.h / 2 });
            const ID name = app.FindByText("Strength");
            ASSERT_NE(name, ID::Invalid());
            tachyon::ElementRef box = app.elementStorage.FindElementRefByID(name);
            while (app.elementStorage.GetElementUnsafe(app.elementStorage.GetParentRef(box)).id != app.tooltipWindowID)
                box = app.elementStorage.GetParentRef(box);
            const tachyon::IntRect tip = app.elementStorage.GetElementRectUnsafe(box).rect;
            EXPECT_EQ(tip.y - onScreen.Max(1), 9) << "the gap below its pin";
            EXPECT_NEAR(tip.x + tip.w / 2, onScreen.x + onScreen.w / 2, 1) << "centered under it";
            // ... in its window, its margin in: the window is where the box is drawn.
            const tachyon::IntRect window = app.GetElementRect(app.tooltipWindowID);
            const tachyon::LayoutStyle* boxLayout = app.elementStorage.GetElementRectUnsafe(box).layoutStyle;
            ASSERT_NE(boxLayout, nullptr);
            EXPECT_EQ(tip.x - window.x, boxLayout->margin.left);
            EXPECT_EQ(tip.y - window.y, boxLayout->margin.top);
        }

    } // namespace
}
