# Node view

## RenderNodeView

A graph editor. It has:

- nodes, each with property rows and sockets
- wires between sockets
- pan and zoom
- box selection
- a minimap and zoom controls
- an optional breadcrumb

All its state lives in a `NodeView` that your app owns, so several views can coexist.

```cpp
void RenderNodeView(NodeView& view);
void NodeText(std::string_view text);
```

**`NodeModel`** describes one node:

- `stableID`: the node's ID, set once, for example with `idManager.CreateStaticID()`
- `title`
- `category` and `categoryColor`, both optional
- `pos`: the position, in pixels
- `properties`: the rows
- `reroute`: a dot that a wire passes through, a waypoint, instead of a box.
  - Its first row's input (one wire in) and output (any number out) meet in the dot.
  - It moves, selects and deletes like any node. Drag a wire into or out of it at its
    left or right edge, where its pins show while it's hovered or selected.
  - Right-click a wire and choose "Add reroute here" to put one on it there. In code,
    `RerouteWireEdit(view, wireKey, stableID, pos)` makes that edit.
  - Right-click a reroute and choose "Remove reroute" to take it out, its wire running on
    as it did through it. That's its whole menu: `onNodeContextMenu` is for your nodes. In
    code, `RemoveRerouteEdit(view, node)`.
  - It doesn't collapse.

  ```cpp
  NodeModel waypoint;
  waypoint.stableID = idManager.CreateStaticID();
  waypoint.reroute  = true;
  waypoint.pos      = { 240, 96 };   // its square's top-left; the dot is one grid cell in
  waypoint.properties.push_back(NodeProperty{ .input = PropertyEndpoint{}, .output = true });
  ```

**`NodeProperty`** describes one row:

- `input`: the input socket.
  - `PropertyEndpoint{}` for an input with no wire.
  - Once wired, it holds the source output.
- `output`: true gives the row an output socket.
- `renderBody`: a function that builds the row, with any controls.
- `socketColor`: the socket's color.
- `rowSpan`: how many rows it spans.
- `name`, `type` and `value`: the pin's tooltip.
  - `name` is the pin's name.
  - `type` is its type, shown muted and in a monospace font.
  - `value` returns its current value. It's called only while the tooltip shows.
  - With none of the three set, there's no tooltip.

A wired input's tooltip also names the source, for example "from Noise.Value". Tooltip
classes: `tooltip-lines`, `tooltip-pin-row`, `tooltip-pin-type`, `tooltip-pin-value`,
`tooltip-note`.

`NodeText` draws text aligned to the row's sockets:

- left for an input
- right for an output
- centered for both

**`NodeView`** members:

- `canConnect(view, output, input)`: return false to refuse a wire.

  ```cpp
  view.canConnect = [](const NodeView& v, PropertyEndpoint o, PropertyEndpoint i)
  {
      return v.nodes[o.nodeIdx].properties[o.propIdx].socketColor
          == v.nodes[i.nodeIdx].properties[i.propIdx].socketColor;   // same color only
  };
  ```

- `onEdit`: gets each finished edit as a `NodeViewEdit`, for undo.
  - `ApplyNodeViewEdit` in `gui_node_view_helpers.h` applies or reverts one.

  ```cpp
  view.onEdit = [this](const NodeViewEdit& edit)
  {
      commandManager.History().Push({ .description = "Edit node graph",
          .undo = [this, edit] { ApplyNodeViewEdit(view, edit, true); },
          .redo = [this, edit] { ApplyNodeViewEdit(view, edit, false); } });
      commandManager.History().EndMerge();
  };
  ```

- Nodes and wires refer to nodes by their index in `view.nodes`. To add or remove nodes
  without breaking the wires after them, make it an edit:
  - `AddNodeEdit(view, node, index)` adds one, last when `index` is -1.
  - `RemoveNodesEdit(view, indices)` removes some, with every wire into or out of them.
  - The nodes after them move up or down, and their wires, lanes and z-order with them.
  - Commit it with `CommitNodeViewEdit`, which reports it to `onEdit`, so undo puts it
    back. Keep a removed node's `stableID` while the edit can be undone.

  ```cpp
  std::vector<int> selected;
  for (int n = 0; n < int(view.nodes.size()); ++n)
      if (view.selectedNodeIDs.contains(view.nodes[n].stableID))
          selected.push_back(n);
  CommitNodeViewEdit(view, RemoveNodesEdit(view, selected));
  ```

  To wire a new node in the same step, add the rewire to its edit:

  ```cpp
  NodeViewEdit edit = AddNodeEdit(view, std::move(node));
  edit.rewires.push_back({ .input = { edit.nodes[0].index, 0 }, .from = {}, .to = output });
  CommitNodeViewEdit(view, edit);
  ```

- `selectedNodeIDs`: the selected nodes, by `stableID`. When it changes, however it
  changes (clicks, the marquee, keys, a removed node, your own code), the view calls
  `onSelectionChanged` once and adds one to `selectionGeneration`, at the end of the
  `RenderNodeView` that finds it changed. Use either; the counter suits code that already
  looks each frame.

  ```cpp
  view.onSelectionChanged = [this] { inspector.Show(view.selectedNodeIDs); };
  ```

- `onWireDroppedOnCanvas(from, fromOutput, canvasPos)`: a new wire was let go over the empty
  canvas, for example to offer the nodes it could connect to.
  - `from` is the socket it was dragged from: an output when `fromOutput`, else an input.
  - `canvasPos` is where, in the same space as `NodeModel::pos`.
  - A wire pulled off an input and let go there is a disconnect, reported to `onEdit`
    instead.

  ```cpp
  view.onWireDroppedOnCanvas = [this](PropertyEndpoint from, bool fromOutput, float2 at)
  {
      OpenNodePicker(from, fromOutput, at);   // your app's list of nodes to add there
  };
  ```

- `onCanvasContextMenu(canvasPos)` and `onNodeContextMenu(node)`: build the right-click
  menus, with `MenuItem` and the like. They're called each frame while their menu is open;
  without them there's no menu.
  - A right click on the empty canvas opens the canvas's, for where it was clicked.
  - One on a node opens the node's and selects the node, if it wasn't.
  - A right drag pans instead; a right click on a wire still offers to delete it.
  - With the canvas focused, the Menu key or Shift+F10 opens the selected node's (the
    one clicked last), or the canvas's when none is selected.

  ```cpp
  view.onCanvasContextMenu = [this](float2 at)
  {
      MenuItem("Add node here", [this, at] { AddNodeAt(at); });
  };
  view.onNodeContextMenu = [this](int node)
  {
      MenuItem("Delete", [this] { DeleteSelectedNodes(); });
  };
  ```

- `NodeViewScreenToCanvas(view, screenPos)` and `NodeViewCanvasToScreen(view, canvasPos)`
  in `gui_node_view_helpers.h` convert between screen pixels (as events have them) and
  node positions, as the view was last drawn.

  ```cpp
  const float2 at = NodeViewScreenToCanvas(view, event.state.screenPos);
  ```

- `onBreadcrumb(index)`: breadcrumb clicks.

  ```cpp
  view.breadcrumb   = { "Graphs", "Terrain material" };
  view.onBreadcrumb = [this](int segment) { OpenGraphLevel(segment); };
  ```

- `showZoomControls`, `showMinimap`: turn those overlays on or off.

  ```cpp
  view.showMinimap = false;
  ```

**Focus.**

- Tab reaches the controls in every node, visible or not.
- Focusing a partly hidden node pans just enough to show it. The zoom stays the same.
- From code: `ScrollNodeIntoView` and `FrameAllNodes` in `gui_node_view_helpers.h`.

```cpp
Button("Frame all", [this] { FrameAllNodes(view); });
```

**Keys**, while the canvas has focus (click its background):

| Key | Action |
| --- | --- |
| Home | Frame all nodes |
| Arrows | Move selected nodes one grid step (four with Shift) as one edit; pan if nothing is selected |
| + / - | Zoom in / out |
| Ctrl+A / Escape | Select all / clear selection |
| Enter | Collapse or expand selected nodes |

Wires are made and removed with the mouse only.

**Wires.**

- Drag from an output to an input to wire them. Either way round works: drag from an
  input with no wire to an output.
- Let go over a node, and the wire goes to the node's first socket that takes it.
- Drag from a wired input to take its wire off: let go over another input to move it, or
  over the empty canvas to remove it.
- A middle click on a wire removes it; a right click offers to, or to add a reroute on it.

**Dragging.**

- Moving past the drag distance on a node moves it and the rest of the selection.
- Less than that counts as a click, which only selects.
- Escape in the middle of a drag undoes it. This works for:
  - moving nodes
  - moving a wire lane
  - box selection
  - panning with the right button
  - dragging in the minimap

Classes: `node-view`, `node`, `node-reroute`, `node-header`, `node-title`, `node-category`, `node-row`,
`node-socket`, `node-view-overlay`, `node-view-minimap`.

```cpp
NodeModel mix;
mix.stableID = idManager.CreateStaticID();
mix.title    = "Mix";
mix.properties.push_back(NodeProperty{ .input = PropertyEndpoint{}, .renderBody = [this] { NodeText("Strength"); },
                                       .name = "Strength", .type = "float", .value = [this] { return std::format("{:.2f}", strength); } });
mix.properties.push_back(NodeProperty{ .output = true, .renderBody = [this] { Label("Gain"); Field(gain); } });
view.nodes.push_back(std::move(mix));

// Each frame, inside a sized container:
RenderNodeView(view);
```

---

[Previous: Tabs, docking and toolbars](tabs-docking-toolbars.md) | [Controls](../controls.md) | [Next: Other controls](other-controls.md)
