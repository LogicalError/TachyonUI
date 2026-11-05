// The node view on a real device, where its wires are drawn: a menu item that adds a node
// (as "Add reroute" does) is drawn with the rest, the frame it's chosen and after.

#include <gtest/gtest.h>

#include <gui_controls/gui_application_internal.h>
#include <gui_controls/gui_node_view_helpers.h>

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
        ASSERT_TRUE(app.Run());
        EXPECT_EQ(app.added, 1);
        EXPECT_EQ(app.view.nodes.size(), 3u);
        app.Cleanup();
    }
}
