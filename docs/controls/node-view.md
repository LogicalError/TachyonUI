# Node view

## RenderNodeView

Graph editor: nodes, property rows, sockets, wires, pan and zoom, box selection, minimap,
zoom controls, optional breadcrumb. All state lives in a `NodeView` your app owns, so
several views can coexist.

```cpp
void RenderNodeView(NodeView& view);
void NodeText(std::string_view text);
```

**`NodeModel`** (one per node): `stableID` (set once, e.g. `idManager.CreateStaticID()`),
`title`, optional `category` and `categoryColor`, `pos` in pixels, `properties`.

**`NodeProperty`** (one per row):

| Field | Meaning |
| --- | --- |
| `input` | Input socket. `PropertyEndpoint{}` for an unwired input; once wired, holds the source output. |
| `output` | Flag: has an output socket |
| `renderBody` | Builds the row, with any controls |
| `socketColor`, `rowSpan` | Socket color, rows spanned |
| `name`, `type`, `value` | Pin tooltip: name, type (muted, monospace), current value. `value` is called only while the tooltip shows. None of the three: no tooltip. |

A wired input's tooltip also names the source, e.g. "from Noise.Value". Tooltip classes:
`tooltip-lines`, `tooltip-pin-row`, `tooltip-pin-type`, `tooltip-pin-value`,
`tooltip-note`.

`NodeText` draws text aligned to the row's sockets: left for an input, right for an
output, centered for both.

**`NodeView`** members:

- `canConnect(view, output, input)`: return false to refuse a wire.
- `onEdit`: gets each finished edit as a `NodeViewEdit`, for undo. `ApplyNodeViewEdit` in
  `gui_node_view_helpers.h` applies or reverts one.
- `onBreadcrumb(index)`: breadcrumb clicks.
- `showZoomControls`, `showMinimap`: toggle those overlays.

**Focus.** Tab reaches controls in every node, visible or not. Focusing a partly hidden
node pans just enough to show it, zoom unchanged. From code: `ScrollNodeIntoView`,
`FrameAllNodes` in `gui_node_view_helpers.h`.

**Keys** (canvas focused; click its background):

| Key | Action |
| --- | --- |
| Home | Frame all nodes |
| Arrows | Move selected nodes one grid step (four with Shift) as one edit; pan if nothing is selected |
| + / - | Zoom in / out |
| Ctrl+A / Escape | Select all / clear selection |
| Enter | Collapse or expand selected nodes |

Wires are made and removed with the mouse only.

**Dragging.** Moving past the drag distance on a node moves it and the rest of the
selection; less counts as a click (select only). Escape mid-drag undoes it: node move,
wire lane, box selection, right-button pan, minimap drag.

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
