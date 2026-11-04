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
        CommitNodeViewEdit(view, RemoveNodesEdit(view, selected));
    }

}
