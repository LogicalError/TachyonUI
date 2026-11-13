# Graphite theme

- `graphite-theme-spec.md`: the full spec (colors, metrics, per-control rules, documents and docking, dialogs, states and motion, dark/light table).
  Its **Review decisions** section answers `design-review.md` item by item (what the design changed, what the app should change).
- `tokens/graphite-tokens.json`: every color, metric and motion value for both themes (source of truth).
- `tokens/graphite-dark.css`, `tokens/graphite-light.css`: the same tokens as CSS custom properties.
- `graphite-dark.png`, `graphite-light.png`: overview sheets of everything (large; use the per-piece images below for detail).
- `images/`: one image per design piece and close-up crops of individual panels, for both themes (listed below; `images/index.json` too).
- `mockups/`: source of every design board (`*.dc.html`). Styles are inline, so exact values can be read from them;
  they need the design canvas runtime to render, so use the images to look at them.

Token names are suggestions; map them onto the style names in your own `themes/*.css`.

## Images

### Dark

- `images/dark/Main.png`: **Dark · Main window · document tabs, menu row, panels**. Full main window: colored document tabs, document menu row (menus, search, Layouts), Hierarchy, NodeView (tinted node headers, higher contrast), Profiler, Files, Log, status bar
  - `images/dark/panels/Main--Hierarchy.png`: Dark · Main window · document tabs, menu row, panels › Hierarchy panel
  - `images/dark/panels/Main--NodeView.png`: Dark · Main window · document tabs, menu row, panels › NodeView panel
  - `images/dark/panels/Main--Profiler.png`: Dark · Main window · document tabs, menu row, panels › Profiler panel
  - `images/dark/panels/Main--Files.png`: Dark · Main window · document tabs, menu row, panels › Files panel
  - `images/dark/panels/Main--Log.png`: Dark · Main window · document tabs, menu row, panels › Log panel
- `images/dark/Inspector.png`: **Dark · Viewport + Inspector panels**. Viewport (floating toolbars, labelled axis gizmo) + Inspector panel (all field types, color field with alpha, text selection)
  - `images/dark/panels/Inspector--Viewport.png`: Dark · Viewport + Inspector panels › Viewport panel
  - `images/dark/panels/Inspector--Inspector.png`: Dark · Viewport + Inspector panels › Inspector panel
- `images/dark/States.png`: **Dark · States & transitions**. State matrix for 12 controls x 7 states (focus = own accent border), transition timings, state-layer graph, Oklab vs sRGB
- `images/dark/Timeline.png`: **Dark · Timeline**. Timeline panel: transport, tracks, keys, loop range, audio clip, events, text-1 playhead
- `images/dark/CurveEditor.png`: **Dark · Curve editor**. Curve editor: channels, grid, bezier curves, selected key with tangent handles, text-1 playhead
- `images/dark/Tooltips.png`: **Dark · Tooltips**. Tooltip variants: simple + shortcut, rich help, truncation, validation, disabled reason, node pin
- `images/dark/Progress.png`: **Dark · Progress & loading, in the editor**. Progress and loading inside an editor window: loading document tab, skeleton rows, streaming row, viewport compile pill, import tile, build steps, busy button, log failure with Retry, status bar task + Background tasks popover
  - `images/dark/panels/Progress--Hierarchy.png`: Dark · Progress & loading, in the editor › Hierarchy panel
  - `images/dark/panels/Progress--Viewport.png`: Dark · Progress & loading, in the editor › Viewport panel
  - `images/dark/panels/Progress--Build.png`: Dark · Progress & loading, in the editor › Build panel
  - `images/dark/panels/Progress--Files.png`: Dark · Progress & loading, in the editor › Files panel
  - `images/dark/panels/Progress--Log.png`: Dark · Progress & loading, in the editor › Log panel
- `images/dark/Notifications.png`: **Dark · Notifications & confirmations**. Toasts, destructive confirmation dialog, inline panel banners
- `images/dark/Settings.png`: **Dark · Settings + Table panels**. Settings panel (shortcuts with keycaps) + Table panel (right-aligned numbers)
  - `images/dark/panels/Settings--Settings.png`: Dark · Settings + Table panels › Settings panel
  - `images/dark/panels/Settings--Table.png`: Dark · Settings + Table panels › Table panel
- `images/dark/DocumentWindow.png`: **Dark · Second main window · dragged-out document**. Second main window holding a dragged-out document (Hero.prefab) with its own menu row and layout
  - `images/dark/panels/DocumentWindow--Prefab-hierarchy.png`: Dark · Second main window · dragged-out document › Prefab hierarchy panel
  - `images/dark/panels/DocumentWindow--Prefab-viewport.png`: Dark · Second main window · dragged-out document › Prefab viewport panel
  - `images/dark/panels/DocumentWindow--Animation.png`: Dark · Second main window · dragged-out document › Animation panel
  - `images/dark/panels/DocumentWindow--Inspector.png`: Dark · Second main window · dragged-out document › Inspector panel
- `images/dark/Docking.png`: **Dark · Torn-off panel window**. Torn-off panel as its own OS window
- `images/dark/ColorPicker.png`: **Dark · Color picker**. Color picker dialog: New over Original, RGB/HSV/HSL/Lab/Oklab
- `images/dark/Tokens.png`: **Dark · Tokens & metrics**. Token sheet: colors, spacing scale, metrics, type
- `images/dark/GradientEditor.png`: **Dark · Gradient editor**. Gradient editor: per-stop alpha, interpolation, blend space dropdown
- `images/dark/InspectorLists.png`: **Dark · Lists, dictionary, menus**. Inspector lists (string/float with drag reorder), dictionary, dropdowns with an open menu
- `images/dark/MessageBox.png`: **Dark · Message boxes (yes / no / cancel)**. Message boxes: save/don't save/cancel, confirm quit, question with verb buttons, error with details
- `images/dark/Menus.png`: **Dark · Menus & context menus**. Menu frame and rows: File (icons, shortcuts, submenu arrow), Edit (disabled, pressed, destructive), View (checks, radio group, open submenu), context menu at the pointer, metrics
  - `images/dark/panels/Menus--File-menu.png`: Dark · Menus & context menus › File menu panel
  - `images/dark/panels/Menus--Edit-menu.png`: Dark · Menus & context menus › Edit menu panel
  - `images/dark/panels/Menus--View-menu.png`: Dark · Menus & context menus › View menu panel
  - `images/dark/panels/Menus--Context-menu.png`: Dark · Menus & context menus › Context menu panel
- `images/dark/NodeGraph.png`: **Dark · NodeView details · frames, reroutes, states, wire shapes**. NodeView details: frame and collapsed frame, reroute, node hover/selected/focus, hovered wire, dragging a wire, orthogonal wires with lane handle, add-node and wire menus
  - `images/dark/panels/NodeGraph--NodeView.png`: Dark · NodeView details · frames, reroutes, states, wire shapes › NodeView panel
  - `images/dark/panels/NodeGraph--Orthogonal-wires.png`: Dark · NodeView details · frames, reroutes, states, wire shapes › Orthogonal wires panel
  - `images/dark/panels/NodeGraph--Add-node-menu.png`: Dark · NodeView details · frames, reroutes, states, wire shapes › Add node menu panel
- `images/dark/Tables.png`: **Dark · TreeTable, FrozenTable, row & cell states**. TreeTable, FrozenTable (frozen columns and header, cell selection), row and cell states
  - `images/dark/panels/Tables--TreeTable.png`: Dark · TreeTable, FrozenTable, row & cell states › TreeTable panel
  - `images/dark/panels/Tables--FrozenTable.png`: Dark · TreeTable, FrozenTable, row & cell states › FrozenTable panel
- `images/dark/InspectorMore.png`: **Dark · Inspector · banners, search, mixed values, curves, levels**. Inspector banners, search filter, autocomplete, mixed values, HDR gradient, curve and color-curve fields, levels
  - `images/dark/panels/InspectorMore--Inspector-editing.png`: Dark · Inspector · banners, search, mixed values, curves, levels › Inspector, editing panel
  - `images/dark/panels/InspectorMore--Inspector-curves-and-levels.png`: Dark · Inspector · banners, search, mixed values, curves, levels › Inspector, curves and levels panel
- `images/dark/StateMachine.png`: **Dark · State machine**. State machine: colored states, initial arrow, paired transitions, loop, labels, selected transition, rename in place, active state, nested machine, transition band, transition menu
- `images/dark/Reorder.png`: **Dark · Reorderable · drag, drop gap, hover**. Reorderable tabs, list, wrapped groups: hover, lifted item, drop gap
- `images/dark/Misc.png`: **Dark · Misc · drawing, icon buttons, rich text, images, sketch**. Vector drawing, icon buttons (states, sizes, groups), rich text, image thumbnails, clipboard image wells, sketch pad
  - `images/dark/panels/Misc--Vector-drawing.png`: Dark · Misc · drawing, icon buttons, rich text, images, sketch › Vector drawing panel
  - `images/dark/panels/Misc--Icon-buttons.png`: Dark · Misc · drawing, icon buttons, rich text, images, sketch › Icon buttons panel
  - `images/dark/panels/Misc--Rich-text.png`: Dark · Misc · drawing, icon buttons, rich text, images, sketch › Rich text panel
  - `images/dark/panels/Misc--Sketch.png`: Dark · Misc · drawing, icon buttons, rich text, images, sketch › Sketch panel
  - `images/dark/panels/Misc--Image-thumbnails.png`: Dark · Misc · drawing, icon buttons, rich text, images, sketch › Image thumbnails panel
  - `images/dark/panels/Misc--Clipboard-image-wells.png`: Dark · Misc · drawing, icon buttons, rich text, images, sketch › Clipboard image wells panel
  - `images/dark/panels/Misc--Rules.png`: Dark · Misc · drawing, icon buttons, rich text, images, sketch › Rules panel

### Light

- `images/light/Main.png`: **Light · Main window · document tabs, menu row, panels**. Full main window: colored document tabs, document menu row (menus, search, Layouts), Hierarchy, NodeView (tinted node headers, higher contrast), Profiler, Files, Log, status bar
  - `images/light/panels/Main--Hierarchy.png`: Light · Main window · document tabs, menu row, panels › Hierarchy panel
  - `images/light/panels/Main--NodeView.png`: Light · Main window · document tabs, menu row, panels › NodeView panel
  - `images/light/panels/Main--Profiler.png`: Light · Main window · document tabs, menu row, panels › Profiler panel
  - `images/light/panels/Main--Files.png`: Light · Main window · document tabs, menu row, panels › Files panel
  - `images/light/panels/Main--Log.png`: Light · Main window · document tabs, menu row, panels › Log panel
- `images/light/Inspector.png`: **Light · Viewport + Inspector panels**. Viewport (floating toolbars, labelled axis gizmo) + Inspector panel (all field types, color field with alpha, text selection)
  - `images/light/panels/Inspector--Viewport.png`: Light · Viewport + Inspector panels › Viewport panel
  - `images/light/panels/Inspector--Inspector.png`: Light · Viewport + Inspector panels › Inspector panel
- `images/light/States.png`: **Light · States & transitions**. State matrix for 12 controls x 7 states (focus = own accent border), transition timings, state-layer graph, Oklab vs sRGB
- `images/light/Timeline.png`: **Light · Timeline**. Timeline panel: transport, tracks, keys, loop range, audio clip, events, text-1 playhead
- `images/light/CurveEditor.png`: **Light · Curve editor**. Curve editor: channels, grid, bezier curves, selected key with tangent handles, text-1 playhead
- `images/light/Tooltips.png`: **Light · Tooltips**. Tooltip variants: simple + shortcut, rich help, truncation, validation, disabled reason, node pin
- `images/light/Progress.png`: **Light · Progress & loading, in the editor**. Progress and loading inside an editor window: loading document tab, skeleton rows, streaming row, viewport compile pill, import tile, build steps, busy button, log failure with Retry, status bar task + Background tasks popover
  - `images/light/panels/Progress--Hierarchy.png`: Light · Progress & loading, in the editor › Hierarchy panel
  - `images/light/panels/Progress--Viewport.png`: Light · Progress & loading, in the editor › Viewport panel
  - `images/light/panels/Progress--Build.png`: Light · Progress & loading, in the editor › Build panel
  - `images/light/panels/Progress--Files.png`: Light · Progress & loading, in the editor › Files panel
  - `images/light/panels/Progress--Log.png`: Light · Progress & loading, in the editor › Log panel
- `images/light/Notifications.png`: **Light · Notifications & confirmations**. Toasts, destructive confirmation dialog, inline panel banners
- `images/light/Settings.png`: **Light · Settings + Table panels**. Settings panel (shortcuts with keycaps) + Table panel (right-aligned numbers)
  - `images/light/panels/Settings--Settings.png`: Light · Settings + Table panels › Settings panel
  - `images/light/panels/Settings--Table.png`: Light · Settings + Table panels › Table panel
- `images/light/DocumentWindow.png`: **Light · Second main window · dragged-out document**. Second main window holding a dragged-out document (Hero.prefab) with its own menu row and layout
  - `images/light/panels/DocumentWindow--Prefab-hierarchy.png`: Light · Second main window · dragged-out document › Prefab hierarchy panel
  - `images/light/panels/DocumentWindow--Prefab-viewport.png`: Light · Second main window · dragged-out document › Prefab viewport panel
  - `images/light/panels/DocumentWindow--Animation.png`: Light · Second main window · dragged-out document › Animation panel
  - `images/light/panels/DocumentWindow--Inspector.png`: Light · Second main window · dragged-out document › Inspector panel
- `images/light/Docking.png`: **Light · Torn-off panel window**. Torn-off panel as its own OS window
- `images/light/ColorPicker.png`: **Light · Color picker**. Color picker dialog: New over Original, RGB/HSV/HSL/Lab/Oklab
- `images/light/Tokens.png`: **Light · Tokens & metrics**. Token sheet: colors, spacing scale, metrics, type
- `images/light/GradientEditor.png`: **Light · Gradient editor**. Gradient editor: per-stop alpha, interpolation, blend space dropdown
- `images/light/InspectorLists.png`: **Light · Lists, dictionary, menus**. Inspector lists (string/float with drag reorder), dictionary, dropdowns with an open menu
- `images/light/MessageBox.png`: **Light · Message boxes (yes / no / cancel)**. Message boxes: save/don't save/cancel, confirm quit, question with verb buttons, error with details
- `images/light/Menus.png`: **Light · Menus & context menus**. Menu frame and rows: File (icons, shortcuts, submenu arrow), Edit (disabled, pressed, destructive), View (checks, radio group, open submenu), context menu at the pointer, metrics
  - `images/light/panels/Menus--File-menu.png`: Light · Menus & context menus › File menu panel
  - `images/light/panels/Menus--Edit-menu.png`: Light · Menus & context menus › Edit menu panel
  - `images/light/panels/Menus--View-menu.png`: Light · Menus & context menus › View menu panel
  - `images/light/panels/Menus--Context-menu.png`: Light · Menus & context menus › Context menu panel
- `images/light/NodeGraph.png`: **Light · NodeView details · frames, reroutes, states, wire shapes**. NodeView details: frame and collapsed frame, reroute, node hover/selected/focus, hovered wire, dragging a wire, orthogonal wires with lane handle, add-node and wire menus
  - `images/light/panels/NodeGraph--NodeView.png`: Light · NodeView details · frames, reroutes, states, wire shapes › NodeView panel
  - `images/light/panels/NodeGraph--Orthogonal-wires.png`: Light · NodeView details · frames, reroutes, states, wire shapes › Orthogonal wires panel
  - `images/light/panels/NodeGraph--Add-node-menu.png`: Light · NodeView details · frames, reroutes, states, wire shapes › Add node menu panel
- `images/light/Tables.png`: **Light · TreeTable, FrozenTable, row & cell states**. TreeTable, FrozenTable (frozen columns and header, cell selection), row and cell states
  - `images/light/panels/Tables--TreeTable.png`: Light · TreeTable, FrozenTable, row & cell states › TreeTable panel
  - `images/light/panels/Tables--FrozenTable.png`: Light · TreeTable, FrozenTable, row & cell states › FrozenTable panel
- `images/light/InspectorMore.png`: **Light · Inspector · banners, search, mixed values, curves, levels**. Inspector banners, search filter, autocomplete, mixed values, HDR gradient, curve and color-curve fields, levels
  - `images/light/panels/InspectorMore--Inspector-editing.png`: Light · Inspector · banners, search, mixed values, curves, levels › Inspector, editing panel
  - `images/light/panels/InspectorMore--Inspector-curves-and-levels.png`: Light · Inspector · banners, search, mixed values, curves, levels › Inspector, curves and levels panel
- `images/light/StateMachine.png`: **Light · State machine**. State machine: colored states, initial arrow, paired transitions, loop, labels, selected transition, rename in place, active state, nested machine, transition band, transition menu
- `images/light/Reorder.png`: **Light · Reorderable · drag, drop gap, hover**. Reorderable tabs, list, wrapped groups: hover, lifted item, drop gap
- `images/light/Misc.png`: **Light · Misc · drawing, icon buttons, rich text, images, sketch**. Vector drawing, icon buttons (states, sizes, groups), rich text, image thumbnails, clipboard image wells, sketch pad
  - `images/light/panels/Misc--Vector-drawing.png`: Light · Misc · drawing, icon buttons, rich text, images, sketch › Vector drawing panel
  - `images/light/panels/Misc--Icon-buttons.png`: Light · Misc · drawing, icon buttons, rich text, images, sketch › Icon buttons panel
  - `images/light/panels/Misc--Rich-text.png`: Light · Misc · drawing, icon buttons, rich text, images, sketch › Rich text panel
  - `images/light/panels/Misc--Sketch.png`: Light · Misc · drawing, icon buttons, rich text, images, sketch › Sketch panel
  - `images/light/panels/Misc--Image-thumbnails.png`: Light · Misc · drawing, icon buttons, rich text, images, sketch › Image thumbnails panel
  - `images/light/panels/Misc--Clipboard-image-wells.png`: Light · Misc · drawing, icon buttons, rich text, images, sketch › Clipboard image wells panel
  - `images/light/panels/Misc--Rules.png`: Light · Misc · drawing, icon buttons, rich text, images, sketch › Rules panel
