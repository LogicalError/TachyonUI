#include "application.h"

#include <gui_controls/gui_node_view_helpers.h>   // AddNodeEdit, RemoveNodesEdit, GridLineNearest

#include <algorithm>
#include <span>
#include <string>
#include <vector>

//
// Demo panel for the node view (RenderNodeView, controls_node_view.cpp). The
// graph data is built in application.cpp.
//

namespace tachyon
{

    void Application::RenderNodeViewPanel()
    {
        // The "Inputs" frame round Source and Texture, once their sizes are known. Not an edit:
        // it's where the graph starts, not something to undo.
        NodeView& view = showcase.nodeView;
        if (!showcase.nodeFramesSeeded && view.nodes.size() >= 4 && view.nodes[0].lastSize.x > 0 && view.nodes[3].lastSize.x > 0)
        {
            const int inputs[] = { 0, 3 };
            NodeFrame frame = FrameAroundNodes(view, inputs);
            frame.stableID = idManager.CreateStaticID();
            frame.title    = "Inputs";
            frame.color    = { 0.20f, 0.75f, 0.72f, 1.0f };
            ApplyNodeViewEdit(view, AddFrameEdit(view, std::move(frame)));
            showcase.nodeFramesSeeded = true;
        }
        RenderNodeView(view);
    }

    void Application::AddGraphNode(GraphNodeKind kind, float2 at)
    {
        // The graph's socket colors double as its types (canConnect lets only the same color
        // connect): teal for colors and images, amber for numbers, purple for switches.
        const color4f kTeal  { 0.20f, 0.75f, 0.72f, 1.0f };
        const color4f kAmber { 0.95f, 0.62f, 0.20f, 1.0f };
        const color4f kPurple{ 0.62f, 0.45f, 0.92f, 1.0f };

        NodeModel node;
        node.stableID = idManager.CreateStaticID();
        node.pos      = { int32_t(at.x), int32_t(at.y) };
        const ID id   = node.stableID;
        showcase.graphNodeValues[id] = {};
        // Its values, looked up each time: the map may have grown since.
        auto values = [this, id]() -> ShowcaseState::GraphNodeValues& { return showcase.graphNodeValues[id]; };
        auto row = [](std::string name, std::string type, color4f color, bool input, bool output, std::function<void()> body) {
            return NodeProperty{ .input = input ? std::optional<PropertyEndpoint>(PropertyEndpoint{}) : std::nullopt, .output = output,
                                 .renderBody = std::move(body), .socketColor = color, .name = std::move(name), .type = std::move(type) };
        };
        switch (kind)
        {
            case GraphNodeKind::Value:
                node.title = "Value";
                node.category = "Input";
                node.properties.push_back(row("Value", "float", kAmber, false, true, [this, values] {
                    Label("Value");
                    Field(values().value);
                }));
                break;
            case GraphNodeKind::Math:
            {
                node.title = "Math";
                node.category = "Operator";
                static constexpr std::string_view kOps[] = { "Add", "Multiply", "Min", "Max" };
                node.properties.push_back(row("Operation", "", kAmber, false, false, [this, values] {
                    Dropdown(idManager.AllocateDynamicID("math-op"), values().op, kOps);
                }));
                node.properties.push_back(row("A", "float", kAmber, true, false, [this] { NodeText("A"); }));
                node.properties.push_back(row("B", "float", kAmber, true, false, [this] { NodeText("B"); }));
                node.properties.push_back(row("Result", "float", kAmber, false, true, [this] { NodeText("Result"); }));
                break;
            }
            case GraphNodeKind::Noise:
                node.title = "Noise";
                node.category = "Texture";
                node.properties.push_back(row("Scale", "float", kAmber, true, false, [this, values] {
                    Label("Scale");
                    Field(values().scale);
                }));
                node.properties.push_back(row("Seed", "int", kAmber, false, false, [this, values] {
                    Label("Seed");
                    Field(values().seed);
                }));
                node.properties.push_back(row("Color", "color", kTeal, false, true, [this] { NodeText("Color"); }));
                break;
            case GraphNodeKind::Switch:
                node.title = "Switch";
                node.category = "Operator";
                node.properties.push_back(row("Condition", "bool", kPurple, true, false, [this] { NodeText("Condition"); }));
                node.properties.push_back(row("True", "color", kTeal, true, false, [this] { NodeText("True"); }));
                node.properties.push_back(row("False", "color", kTeal, true, false, [this] { NodeText("False"); }));
                node.properties.push_back(row("Result", "color", kTeal, false, true, [this] { NodeText("Result"); }));
                break;
        }
        // The category's color, from the palette as the seeded nodes' are; an operator's is
        // left unset, which shows the accent.
        if (node.category == "Input")   node.categoryColor = theme.GetColor("--node-input");
        if (node.category == "Texture") node.categoryColor = theme.GetColor("--node-generator");
        CommitNodeViewEdit(showcase.nodeView, AddNodeEdit(showcase.nodeView, std::move(node)));
    }

    void Application::AddFrameAround(std::span<const int> indices, std::string title, color4f color)
    {
        NodeView& view = showcase.nodeView;
        NodeFrame frame = FrameAroundNodes(view, indices);
        if (frame.size.x <= 0)
            return;
        frame.stableID = idManager.CreateStaticID();
        frame.title    = std::move(title);
        frame.color    = color;
        CommitNodeViewEdit(view, AddFrameEdit(view, std::move(frame)));
    }

    NodeModel Application::RerouteNode(float2 at, color4f color)
    {
        // Its dot on the grid point nearest `at`: the square's top-left one grid cell up and left.
        const int grid = std::max(1, theme.nodeGridSize);
        NodeModel reroute;
        reroute.stableID = idManager.CreateStaticID();
        reroute.title    = "Reroute";
        reroute.reroute  = true;
        reroute.pos      = { GridLineNearest(int32_t(at.x) - grid, 0, grid), GridLineNearest(int32_t(at.y) - grid, 0, grid) };
        reroute.properties.push_back(NodeProperty{ .input = PropertyEndpoint{}, .output = true, .socketColor = color });
        return reroute;
    }

    void Application::DeleteSelectedNodes()
    {
        NodeView& view = showcase.nodeView;
        std::vector<int> selected;
        for (int n = 0; n < int(view.nodes.size()); ++n)
            if (view.selectedNodeIDs.contains(view.nodes[size_t(n)].stableID))
                selected.push_back(n);
        std::vector<int> frames;
        for (int f = 0; f < int(view.frames.size()); ++f)
            if (view.selectedFrameIDs.contains(view.frames[size_t(f)].stableID))
                frames.push_back(f);
        // One edit, so one undo puts both back.
        NodeViewEdit edit = RemoveNodesEdit(view, selected);
        edit.frames = RemoveFramesEdit(view, frames).frames;
        CommitNodeViewEdit(view, edit);
    }

}
