// The node view under random use: clicks, drags, right clicks and keys anywhere, with
// reroutes added and taken out, nodes removed and edits undone and redone in between.
// Nothing may crash or trip an assertion, and every wire must name a node that's there.

#include <gtest/gtest.h>

#include "headless_gui_app.h"

#include <gui_controls/gui_node_view_helpers.h>

#include <random>
#include <vector>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;

    namespace
    {
        NodeModel MakeNode(HeadlessGUIApp& app, int2 pos, int inputs, int outputs)
        {
            NodeModel node{};
            node.stableID = app.idManager.CreateStaticID();
            node.title    = "N";
            node.pos      = pos;
            for (int i = 0; i < std::max(inputs, outputs); ++i)
                node.properties.push_back(NodeProperty{
                    .input  = i < inputs ? std::optional<PropertyEndpoint>(PropertyEndpoint{}) : std::nullopt,
                    .output = i < outputs });
            return node;
        }

        // Every wire names a node and row that are there, and an input on that row.
        void ExpectSound(const NodeView& view)
        {
            for (const NodeModel& node : view.nodes)
                for (const NodeProperty& prop : node.properties)
                    if (prop.input && prop.input->valid())
                    {
                        ASSERT_LT(prop.input->nodeIdx, (int)view.nodes.size());
                        ASSERT_LT(prop.input->propIdx, (int)view.nodes[size_t(prop.input->nodeIdx)].properties.size());
                    }
            for (const int n : view.ordering)
                ASSERT_LT(n, (int)view.nodes.size());
        }
    }

    TEST(NodeViewStress, RandomUseNeverBreaksIt)
    {
        for (uint32_t seed = 1; seed <= 12; ++seed)
        {
            HeadlessGUIApp app;
            ASSERT_TRUE(app.Initialize());
            app.runPostLayoutCallbacks = (seed % 2) == 0;
            NodeView view;
            view.transform = float3x2::Scale(1.0f);
            view.nodes.push_back(MakeNode(app, { 40, 40 }, 0, 2));
            view.nodes.push_back(MakeNode(app, { 360, 60 }, 2, 1));
            view.nodes.push_back(MakeNode(app, { 680, 120 }, 2, 0));
            view.nodes.push_back(MakeNode(app, { 360, 360 }, 1, 1));
            view.nodes[1].properties[0].input = PropertyEndpoint{ 0, 0 };
            view.nodes[2].properties[0].input = PropertyEndpoint{ 1, 0 };
            view.nodes[3].properties[0].input = PropertyEndpoint{ 0, 1 };

            // The host's history: done edits, and undone ones to redo.
            std::vector<NodeViewEdit> done, undone;
            view.onEdit = [&](const NodeViewEdit& edit) { done.push_back(edit); undone.clear(); };
            view.onWireDroppedOnCanvas = [&](PropertyEndpoint, bool, float2 at) {
                NodeModel r = MakeNode(app, { int32_t(at.x), int32_t(at.y) }, 1, 1);
                r.reroute = true;
                CommitNodeViewEdit(view, AddNodeEdit(view, std::move(r)));
            };
            view.onCanvasContextMenu = [&](float2) { app.MenuItem("Item", [] {}); };
            view.onNodeContextMenu   = [&](int) { app.MenuItem("Item", [] {}); };

            auto frame = [&] {
                app.Frame([&] {
                    app.BeginRootWindow({ 1280, 800 }, ElementFlags::None, app.theme.mainWindowStyleRef);
                    app.RenderNodeView(view);
                    app.EndRootWindow();
                });
            };
            frame();
            frame();

            std::mt19937 rng(seed);
            auto pick = [&](int n) { return n > 0 ? int(rng() % uint32_t(n)) : 0; };
            // A point near a node (its edges and sockets are where things happen), or anywhere.
            auto point = [&]() -> int2 {
                if (!view.nodes.empty() && pick(4) != 0)
                {
                    const ID id = view.nodes[size_t(pick((int)view.nodes.size()))].stableID;
                    if (app.HasElement(id))
                    {
                        const IntRect r = app.GetElementRect(id);
                        return { r.x - 8 + pick(std::max(1, r.w + 16)), r.y - 8 + pick(std::max(1, r.h + 16)) };
                    }
                }
                return { pick(1280), pick(800) };
            };

            for (int step = 0; step < 300; ++step)
            {
                SCOPED_TRACE(testing::Message() << "seed " << seed << " step " << step);
                switch (pick(12))
                {
                    case 0: case 1: { const int2 p = point(); app.InjectMouseMove(p); app.ClickAt(p); break; }
                    case 2: { const int2 p = point(); app.InjectMouseMove(p); app.ClickAt(p, MouseButtons::Right); break; }
                    case 3: case 4:
                    {
                        const int2 a = point(), b = point();
                        app.InjectMouseMove(a);
                        app.InjectMouseDown(a);
                        frame();
                        app.InjectMouseMove({ (a.x + b.x) / 2, (a.y + b.y) / 2 }, MouseButtons::Left);
                        frame();
                        app.InjectMouseMove(b, MouseButtons::Left);
                        frame();
                        app.InjectMouseUp(b);
                        break;
                    }
                    case 5:
                    {
                        // A reroute on a wire, as the wire menu's "Add reroute here" makes one.
                        std::vector<uint32_t> wires;
                        for (int n = 0; n < (int)view.nodes.size(); ++n)
                            for (int p = 0; p < (int)view.nodes[size_t(n)].properties.size(); ++p)
                                if (const auto& in = view.nodes[size_t(n)].properties[size_t(p)].input; in && in->valid())
                                    wires.push_back(WireKey(n, p));
                        if (!wires.empty())
                            CommitNodeViewEdit(view, RerouteWireEdit(view, wires[size_t(pick((int)wires.size()))],
                                                                     app.idManager.CreateStaticID(), point()));
                        break;
                    }
                    case 6:
                    {
                        std::vector<int> some;
                        for (int n = 0; n < (int)view.nodes.size(); ++n)
                            if (pick(3) == 0) some.push_back(n);
                        if (view.nodes.size() > 2)
                            CommitNodeViewEdit(view, RemoveNodesEdit(view, some));
                        break;
                    }
                    case 7:
                        if (!done.empty())
                        {
                            ApplyNodeViewEdit(view, done.back(), /*undo*/ true);
                            undone.push_back(done.back());
                            done.pop_back();
                        }
                        break;
                    case 8:
                        if (!undone.empty())
                        {
                            ApplyNodeViewEdit(view, undone.back());
                            done.push_back(undone.back());
                            undone.pop_back();
                        }
                        break;
                    case 9:
                    {
                        static constexpr KeyCode keys[] = { KeyCode::Escape, KeyCode::Return, KeyCode::Home, KeyCode::Left,
                                                            KeyCode::Application, KeyCode::Delete, KeyCode::KeyA };
                        app.InjectKey(keys[pick(int(std::size(keys)))], KeyAction::KeyDown,
                                      pick(3) == 0 ? KeyModifier::Control : KeyModifier::None);
                        break;
                    }
                    case 10: app.menuManager.CloseAll(app.guiStateManager); break;
                    default: { const int2 p = point(); app.InjectMouseMove(p); break; }
                }
                frame();
                ExpectSound(view);
                if (testing::Test::HasFatalFailure())
                    return;
            }
            app.Cleanup();
        }
    }
}
