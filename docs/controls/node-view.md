# Node view

## RenderNodeView

A graph editor with nodes, property rows, sockets and wires. It supports pan and zoom,
box selection, a minimap, zoom controls and an optional breadcrumb. All of its state lives
in a `NodeView` that your app owns, so you can have several views at once.

```cpp
void RenderNodeView(NodeView& view);
void NodeText(std::string_view text);
```

Each node is a `NodeModel`. Give it a `stableID` once (for example from
`idManager.CreateStaticID()`), a `title`, optionally a `category` and `categoryColor`, a
`pos` in pixels, and its `properties`.

Each row is a `NodeProperty`. Its `input` is the input socket: set it to
`PropertyEndpoint{}` for an input with no wire, and once it's wired it holds the source
output. `output` is a flag for the output socket. `renderBody` builds the row, with any
controls you like. There's also a `socketColor` and a `rowSpan`. Inside a row, `NodeText`
draws text aligned to the row's sockets: on the left for an input, on the right for an
output, and centered when the row has both.

A property's `name`, `type` and `value` make up the tooltip on its pins. The tooltip shows
the name, the type (muted, in a monospace font) and the current value. `value` is only
called while the tooltip is showing. For a wired input, the tooltip also says where the
value comes from, as in "from Noise.Value" (the source node's title and its property's
name). A pin with none of the three has no tooltip. The tooltip uses the classes
`tooltip-lines`, `tooltip-pin-row`, `tooltip-pin-type`, `tooltip-pin-value` and
`tooltip-note`.

`NodeView` has a few callbacks:

- `canConnect(view, output, input)` lets you refuse a wire.
- `onEdit` gets each finished edit as a `NodeViewEdit`, which is what you need for undo.
  `ApplyNodeViewEdit` in `gui_node_view_helpers.h` applies or reverts one.
- `onBreadcrumb(index)` handles clicks on the breadcrumb.

`showZoomControls` and `showMinimap` turn those overlays on and off.

Tab reaches the controls in every node, whether it's in view or not. When keyboard focus
moves to a node that isn't fully visible, the view pans just far enough to show it,
without changing the zoom. To do the same from code, use `ScrollNodeIntoView` and
`FrameAllNodes` in `gui_node_view_helpers.h`.

These keys work while the canvas has focus (click its background to give it focus):

- Home frames all nodes.
- The arrow keys move the selected nodes by one grid step (four with Shift), as a single
  edit. With nothing selected, they pan the view.
- + and - zoom in and out.
- Ctrl+A selects every node, and Escape clears the selection.
- Enter collapses or expands the selected nodes.

Wires are made and removed with the mouse.

Pressing on a node and moving the pointer past the drag distance moves the node, along
with the rest of the selection. A shorter movement counts as a click and only selects.
Escape during a drag undoes it: it puts back the nodes during a move, a wire's lane, the
selection during a box select, and the view during a right-button pan or a drag in the
minimap.

The node view uses the classes `node-view`, `node`, `node-header`, `node-title`,
`node-category`, `node-row`, `node-socket`, `node-view-overlay` and `node-view-minimap`.

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
