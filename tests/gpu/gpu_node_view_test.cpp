// The node view on a real device, where its wires are drawn: a menu item that adds a node
// (as "Add reroute" does) is drawn with the rest, the frame it's chosen and after.

#include <gtest/gtest.h>

#include <gui_controls/gui_application_internal.h>
#include <gui_controls/gui_node_view_helpers.h>

#include "gpu_injected_input.h"

namespace
{
    using namespace tachyon;

    class NodeViewApp : public GUIApplicationInternals
    {
    public:
        ID windowID = ID::Invalid();
        NodeView view;
        int frame = 0;
        int added = 0;
        bool addAfterRender = false;   // the app adds a node later in its frame, after the view

        NodeModel Node(int2 pos, bool input, bool output)
        {
            NodeModel node;
            node.stableID = idManager.CreateStaticID();
            node.title    = "Node";
            node.pos      = pos;
            node.properties.push_back(NodeProperty{
                .input  = input ? std::optional<PropertyEndpoint>(PropertyEndpoint{}) : std::nullopt,
                .output = output });
            return node;
        }

        void OnGUI() override
        {
            if (BeginWindow(windowID, "Nodes", theme.mainWindowStyleRef, LayoutOverride{}.Size(900, 600),
                            ElementFlags::WindowRenderToImage))
                RenderNodeView(view);
            EndWindow();
            if (addAfterRender && frame == 5)
            {
                CommitNodeViewEdit(view, RerouteWireEdit(view, WireKey(1, 0), idManager.CreateStaticID(), { -100, -20 }));
                ++added;
            }

            // Focus the canvas with a click on its empty part, then the Menu key opens its
            // menu and Enter picks the first item, which adds a node: a reroute on the wire.
            const int2 empty{ 820, 500 };
            if (frame == 3) { InjectMouseMove(windowID, empty); InjectMouseButton(windowID, empty, MouseButtons::Left, true); }
            if (frame == 4) InjectMouseButton(windowID, empty, MouseButtons::Left, false);
            if (!addAfterRender && frame == 6) InjectKey(windowID, KeyCode::Application, KeyAction::KeyDown);
            if (!addAfterRender && frame == 9) InjectKey(windowID, KeyCode::Return, KeyAction::KeyDown);
            if (++frame >= 16)
                applicationQuit = true;
        }

        bool Start()
        {
            UseInjectedInputOnly(*this);
            if (!Initialize())
                return false;
            windowID = idManager.CreateStaticID();
            view.nodes.push_back(Node({ -300, -100 }, false, true));
            view.nodes.push_back(Node({ 100, 60 }, true, false));
            view.nodes[1].properties[0].input = PropertyEndpoint{ 0, 0 };
            view.onCanvasContextMenu = [this](float2) {
                MenuItem("Add reroute", [this] {
                    CommitNodeViewEdit(view, RerouteWireEdit(view, WireKey(1, 0), idManager.CreateStaticID(), { -100, -20 }));
                    ++added;
                });
            };
            return true;
        }
    };

    TEST(GpuNodeView, ANodeAddedFromAMenuIsDrawnWithItsWires)
    {
        NodeViewApp app;
        if (!app.Start())
        {
            app.Cleanup();
            GTEST_SKIP() << "no Vulkan device available";
        }
        app.SetRedrawMode(tachyon::RedrawMode::Always);   // a frame each pass: the test counts them
        ASSERT_TRUE(app.Run());
        EXPECT_EQ(app.added, 1) << "the menu item was chosen";
        ASSERT_EQ(app.view.nodes.size(), 3u);
        EXPECT_TRUE(app.view.nodes[2].reroute);
        EXPECT_EQ(*app.view.nodes[1].properties[0].input, (PropertyEndpoint{ 2, 0 }));
        app.Cleanup();
    }

    // The app adds a node after drawing the view, later in its frame: that frame's wires are
    // drawn with the nodes laid out, the new one with the next.
    TEST(GpuNodeView, ANodeAddedAfterTheViewInTheFrameIsDrawnNext)
    {
        NodeViewApp app;
        app.addAfterRender = true;
        if (!app.Start())
        {
            app.Cleanup();
            GTEST_SKIP() << "no Vulkan device available";
        }
        app.SetRedrawMode(tachyon::RedrawMode::Always);   // a frame each pass: the test counts them
        ASSERT_TRUE(app.Run());
        EXPECT_EQ(app.added, 1);
        EXPECT_EQ(app.view.nodes.size(), 3u);
        app.Cleanup();
    }

    // One wire, from a node up and left to one down and right, drawn in a given shape; the
    // pixels it covers are those that change when it's taken off.
    class WireShapeApp : public GUIApplicationInternals
    {
    public:
        ID windowID = ID::Invalid();
        NodeView view;
        size_t handles = 0;   // lane handles the draw found, with the wire on

        void OnGUI() override
        {
            if (BeginWindow(windowID, "Nodes", theme.mainWindowStyleRef, LayoutOverride{}.Size(900, 600),
                            ElementFlags::WindowRenderToImage))
                RenderNodeView(view);
            EndWindow();
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

        // Where, a quarter of the way along the wire, it crosses: the middle row of the
        // changed pixels in that column, or -1.
        int QuarterRow(NodeWireShape shape)
        {
            if (!Initialize())
                return -2;
            windowID = idManager.CreateStaticID();
            NodeModel a, b;
            a.stableID = idManager.CreateStaticID();
            a.pos      = { -300, -200 };
            a.properties.push_back(NodeProperty{ .output = true });
            b.stableID = idManager.CreateStaticID();
            b.pos      = { 100, 200 };
            b.properties.push_back(NodeProperty{ .input = PropertyEndpoint{ 0, 0 } });
            view.nodes = { std::move(a), std::move(b) };
            view.showMinimap = view.showZoomControls = false;
            view.wireShape = shape;
            if (!Frames(6)) return -1;
            handles = view.wireHandleCache.size();
            const std::vector<uint8_t> with = Pixels();
            view.nodes[1].properties[0].input = PropertyEndpoint{};
            if (!Frames(4)) return -1;
            const std::vector<uint8_t> without = Pixels();
            const IntRect rect = windowManager->GetWindowRect(windowID);
            if (with.empty() || with.size() != without.size()) return -1;

            auto changed = [&](int x, int y) {
                const size_t i = (size_t(y) * size_t(rect.w) + size_t(x)) * 4;
                return std::abs(int(with[i]) - int(without[i])) + std::abs(int(with[i + 1]) - int(without[i + 1]))
                     + std::abs(int(with[i + 2]) - int(without[i + 2])) > 60;
            };
            int minX = rect.w, maxX = -1;
            for (int y = 0; y < rect.h; ++y)
                for (int x = 0; x < rect.w; ++x)
                    if (changed(x, y)) { minX = std::min(minX, x); maxX = std::max(maxX, x); }
            if (maxX < minX) return -1;
            const int column = minX + (maxX - minX) / 4;
            int top = -1, bottom = -1;
            for (int y = 0; y < rect.h; ++y)
                if (changed(column, y)) { if (top < 0) top = y; bottom = y; }
            if (top < 0) return -1;

            // Wired again, with the mouse there: the view finds the wire under it.
            view.nodes[1].properties[0].input = PropertyEndpoint{ 0, 0 };
            if (!Frames(2)) return -1;
            InjectMouseMove(windowID, { column, (top + bottom) / 2 });
            if (!Frames(3)) return -1;
            hovered = view.hoveredWire.part;
            return (top + bottom) / 2;
        }

        std::optional<uint32_t> hovered;   // the wire under the mouse, a quarter of the way along it
    };

    // A grid wire leaves the output level, so a quarter of the way along it's still on the
    // output's row; a curve has already turned down towards the input by then. Only the grid
    // wire has lanes to drag.
    TEST(GpuNodeView, ACurvedWireTurnsWhereAGridWireRunsLevel)
    {
        int grid = 0, curve = 0;
        size_t gridHandles = 0, curveHandles = 0;
        std::optional<uint32_t> gridHovered, curveHovered;
        {
            WireShapeApp app;
            grid = app.QuarterRow(NodeWireShape::Orthogonal);
            gridHandles = app.handles;
            gridHovered = app.hovered;
            app.Cleanup();
        }
        if (grid == -2)
            GTEST_SKIP() << "no Vulkan device available";
        {
            WireShapeApp app;
            curve = app.QuarterRow(NodeWireShape::Curved);
            curveHandles = app.handles;
            curveHovered = app.hovered;
            app.Cleanup();
        }
        ASSERT_GE(grid, 0);
        ASSERT_GE(curve, 0);
        EXPECT_GT(curve, grid + 8) << "the curve is below the output's row there";
        EXPECT_GT(gridHandles, 0u);
        EXPECT_EQ(curveHandles, 0u);
        EXPECT_EQ(gridHovered, WireKey(1, 0));
        EXPECT_EQ(curveHovered, WireKey(1, 0)) << "hovered along the curve it's drawn as";
    }
}
