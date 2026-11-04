#include "application.h"

#include <gui_controls/gui_node_view_helpers.h>   // AddNodeEdit, RemoveNodesEdit, GridLineNearest

#include <algorithm>
#include <vector>

//
// Demo panel for the node view (RenderNodeView, controls_node_view.cpp). The
// graph data is built in application.cpp.
//

namespace tachyon
{

    void Application::RenderNodeViewPanel()
    {
        RenderNodeView(showcase.nodeView);
    }

    NodeModel Application::RerouteNode(float2 at, bool outputAt, color4f color)
    {
        const int grid = std::max(1, theme.nodeGridSize);
        // Its one row's socket on `at`: below the header, on the left edge for the input or
        // the right one for the output (a node is six grid cells wide at least).
        const int2 socketAt{ outputAt ? 6 * grid : 0, 3 * grid };
        NodeModel reroute;
        reroute.stableID = idManager.CreateStaticID();
        reroute.title    = "Reroute";
        reroute.pos      = { GridLineNearest(int32_t(at.x) - socketAt.x, 0, grid),
                             GridLineNearest(int32_t(at.y) - socketAt.y, 0, grid) };
        reroute.properties.push_back(NodeProperty
        {
            .input       = PropertyEndpoint{},
            .output      = true,
            .renderBody  = [this]() { NodeText("Value"); },
            .socketColor = color
        });
        return reroute;
    }

    void Application::DeleteSelectedNodes()
    {
        NodeView& view = showcase.nodeView;
        std::vector<int> selected;
        for (int n = 0; n < int(view.nodes.size()); ++n)
            if (view.selectedNodeIDs.contains(view.nodes[size_t(n)].stableID))
                selected.push_back(n);
        CommitNodeViewEdit(view, RemoveNodesEdit(view, selected));
    }

}
