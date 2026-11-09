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

- `minZoom`, `maxZoom`: how far the wheel, the keys and the zoom controls go, 10% to 400%
  by default (1 is 100%).
  - Text is drawn at the zoom's size, so it stays sharp above 100%.
  - Fit (and Home) shows a small graph at 100% at most, rather than magnifying it (see
    Framing below).

  ```cpp
  view.maxZoom = 2.0f;   // up to 200%
  ```

- `snapToGrid`: on by default, which keeps every node's position on the grid.
  - A dragged node lands on the grid, and a position set in code is moved onto it.
  - Off, nodes stay where they're dropped or put, and "Add reroute here" puts the dot
    right where the wire was clicked.
  - Either way, the arrow keys move nodes one grid step, a node's width is whole grid
    cells, and wires run along the grid between the sockets.

  ```cpp
  view.snapToGrid = false;   // free placement
  ```

- `wireShape`: how the wires are drawn.
  - `NodeWireShape::Orthogonal`, the default: along the grid, round the nodes, their
    lanes dragged by hand.
  - `NodeWireShape::Curved`: one smooth curve each, leaving the output and entering the
    input level. A wire to a node behind its source loops round. There are no lanes to drag.
  - Hovering, clicking and the wire menu work the same on both.

  ```cpp
  view.wireShape = NodeWireShape::Curved;
  ```

**Frames.** A frame (a comment box) is a titled, colored box behind a group of nodes, to
organize a large graph. They're in `view.frames`, behind every wire and node, the first at
the back.

**`NodeFrame`** describes one:

- `stableID`: its ID, set once, as a node's
- `title`
- `color`: its tint, faint over the box and stronger over the title bar
  - alpha 0 (the default) uses the theme's neutral grey
- `pos` and `size`: its top-left and its size, in the same space as `NodeModel::pos`
- `collapsed`: shown as one node instead (see below)

```cpp
view.frames.push_back(NodeFrame{ .stableID = idManager.CreateStaticID(), .title = "Lighting",
                                 .color = { 0.42f, 0.61f, 1.0f, 1.0f }, .pos = { 0, -48 }, .size = { 480, 288 } });
```

What it does:

- **Moving:** dragging its title moves it, the frames inside it, and the nodes inside any of
  them.
- **Resizing:** dragging its right edge, bottom edge or corner resizes it.
- **The grid:** while `snapToGrid` is on, it stays on the grid, whole grid cells big. It's
  never smaller than a title bar over one row.
- **The rest of the box:** it takes no clicks, so a marquee or a pan starts inside it as
  anywhere on the canvas.
- **Selecting:** a click on its title selects it as one on a node does: Shift adds, Ctrl
  toggles. The marquee and Ctrl+A select frames too, and Escape or a click on the background
  clears them.
  - `selectedFrameIDs` has the selected ones.
  - A change calls `onSelectionChanged`, as one to the nodes does.
- **Renaming:** a double-click on its title puts a field there. Enter keeps the new title,
  as does clicking elsewhere; Escape keeps the old one.
  - `RenameFrame(view, frame)` starts the same, for a "Rename" menu item.
- **Its menu:** `onFrameContextMenu(frame)` builds it, as `onNodeContextMenu` does a node's.
  A right click on the title opens it and selects the frame. So does the Menu key with only
  frames selected.
- **Undo:** each change is an edit, so it undoes.

```cpp
view.onFrameContextMenu = [this](int frame)
{
    MenuItem("Rename", [this, frame] { RenameFrame(view, frame); });
    MenuItem("Delete frame", [this, frame] {
        const int only[] = { frame };
        CommitNodeViewEdit(view, RemoveFramesEdit(view, only));   // its nodes stay
    });
};
```

**Collapsed.** A collapsed frame is shown as one node:

- **The box:** its title bar, with a row for each wire crossing its edge.
  - On the left, an input for each wire coming in from outside.
  - On the right, an output for each output inside that feeds a node outside.
  - Each row is named after its node and pin, for example "Mix.Strength".
- **What's hidden:** the nodes and frames it holds, and the wires between them. They can't
  be clicked or selected, and don't show on the minimap.
- **What it holds:** what was inside it when it collapsed (`NodeFrame::members`). A node
  dragged over it afterwards stays outside it; expanded, it holds what lies inside it again.
- **Moving and size:** dragging its title still moves what it holds. It has no edges to
  resize, and keeps its size for when it expands.
- **Toggling:** the chevron at the left of its title collapses or expands it, as does Enter
  with it selected.
  - `CollapseFrameEdit(view, frame, collapse)` makes the same edit, noting what it holds.

```cpp
CommitNodeViewEdit(view, CollapseFrameEdit(view, frame, true));
```

In `gui_node_view_helpers.h`:

- `AddFrameEdit(view, frame, index)` adds one, in front of the others when `index` is -1.
- `RemoveFramesEdit(view, indices)` removes some. The nodes in them stay.
- `FrameAroundNodes(view, indices)` is a frame round nodes as they're laid out: a grid cell
  of room on each side and the title bar above. Give it a `stableID` and a title.
- `NodesInFrame(view, frame)` is the nodes a frame holds, which move with it.
- `FrameTitleAtCanvasPos(view, canvasPos)` is the frame whose title is at a point.

```cpp
NodeFrame frame = FrameAroundNodes(view, selected);   // std::vector<int> of node indices
frame.stableID = idManager.CreateStaticID();
frame.title    = "Lighting";
CommitNodeViewEdit(view, AddFrameEdit(view, std::move(frame)));
```

Frame classes: `node-frame` and `node-frame-band` (the box and its title bar, drawn behind
the wires), `node-frame-title`, `node-frame-rename`, and, collapsed, `node-frame-ports`,
`node-frame-port` and `node-frame-port-out`. Palette colors: `--frame-fill`,
`--frame-band`, `--frame-border`.

**Focus.**

- Tab reaches the controls in every node, visible or not.
- Focusing a partly hidden node pans just enough to show it. The zoom stays the same.
- A node far out of view isn't laid out (see Large graphs below); Tab into it lays it out
  and pans to it, the focus on its first control (its last, with Shift+Tab).
- From code: `ScrollNodeIntoView` in `gui_node_view_helpers.h`.

**Framing.** The Fit button and Home zoom and centre the view on the selected nodes, or on
all of them when none is selected.

- From code, in `gui_node_view_helpers.h`:
  - `FrameSelectedNodes(view)` does what Fit does.
  - `FrameAllNodes(view)` frames every node, selected or not.
  - `FrameNodes(view, indices)` frames the nodes you give it.
- None of them zooms past 100%, so a small group shows at its own size.

```cpp
MenuItem(view.selectedNodeIDs.empty() ? "Frame all" : "Frame selection",
         [this] { FrameSelectedNodes(view); });
```

**Keys**, while the canvas has focus (click its background):

| Key | Action |
| --- | --- |
| Home | Frame the selected nodes, or all of them |
| Arrows | Move selected nodes one grid step (four with Shift) as one edit; pan if nothing is selected |
| + / - | Zoom in / out |
| Ctrl+A / Escape | Select all nodes and frames / clear selection |
| Enter | Collapse or expand selected nodes and frames |

Wires are made and removed with the mouse only.

**Wires.**

- Drag from an output to an input to wire them. Either way round works: drag from an
  input with no wire to an output.
- Let go over a node, and the wire goes to the node's first socket that takes it.
- Drag from a wired input to take its wire off: let go over another input to move it, or
  over the empty canvas to remove it.
- A middle click on a wire removes it; a right click offers to, or to add a reroute on it.
- A wire's middle segments can be dragged to move its lanes. A lane goes where it's put as
  long as it clears the two nodes: between them, or round them (above or below them both).
  One let go inside a node goes to that node's nearer edge.

**Large graphs.**

- Only the nodes in or near the view are laid out, and only theirs call `renderBody`. Near
  means within half the view's size of it.
- A node further away stands in as its box at its last size, with its sockets, so its
  wires, the minimap and selection still have it.
- A node is laid out whole when it has the focus, has never been laid out yet, or comes
  near the view.
- `virtualizeLayout = false` lays out every node every frame, for a `renderBody` that has
  to run.

  ```cpp
  view.virtualizeLayout = false;   // every node, every frame
  ```

**Dragging.**

- Moving past the drag distance on a node moves it and the rest of the selection.
- Less than that counts as a click, which only selects.
- Escape in the middle of a drag undoes it. This works for:
  - moving nodes
  - moving or resizing a frame
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
