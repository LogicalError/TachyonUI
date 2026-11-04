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

**Dragging.**

- Moving past the drag distance on a node moves it and the rest of the selection.
- Less than that counts as a click, which only selects.
- Escape in the middle of a drag undoes it. This works for:
  - moving nodes
  - moving a wire lane
  - box selection
  - panning with the right button
  - dragging in the minimap

Classes: `node-view`, `node`, `node-header`, `node-title`, `node-category`, `node-row`,
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
