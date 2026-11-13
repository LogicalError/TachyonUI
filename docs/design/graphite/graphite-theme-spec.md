# Graphite dark theme: spec

Reference mockups: `Main.dc.html` (NodeView + Profiler), `Inspector.dc.html` (Viewport + Inspector),
`Settings.dc.html` (Settings + Table), `Tokens.dc.html` (token sheet), and one board per topic below
(`Menus`, `NodeGraph`, `StateMachine`, `Tables`, `Reorder`, `InspectorMore`, `Misc`, `Progress`, ...). They are HTML mockups with inline styles;
every value below is taken from them. Token names are suggestions, map them onto the existing `themes/dark.css` style names.

## Colors

### Surfaces (darkest → lightest)
| Token | Hex | Use |
|---|---|---|
| bg-app | #0B0C0F | Window background, gutters between panels, title/status bar |
| bg-input | #0D0F12 | Text fields, number fields, segmented-control wells, graph backgrounds |
| bg-canvas | #0E1013 | NodeView and StateMachine canvas (dot grid #39404D, 1px dots every 16px) |
| bg-panel | #121418 | Panel body |
| bg-header | #16191E | Panel header / tab strip, table header row |
| bg-raised | #181B20 | Keycaps (#1A1D23), lifted drag items |
| node-bg | #1F232A | Node and state body, clearly lighter than the canvas |
| node-border | #363C48 | Node and state outline |
| bg-hover | #232529 | Hover fill for rows and flat items (text-1 at 8% over bg-panel) |
| bg-control | #1E2128 | Buttons, node header |
| bg-active | #23272F | Active tab, active toggle button |
| segment-on | #2A2F39 | Selected segment in a segmented control (light: white chip + shadow) |
| bg-pressed | #303236 | Pressed fill for rows and flat items (text-1 at 14% over bg-panel) |
| row-stripe | #14161B | Odd rows in large tables (optional) |

### Lines
| Token | Hex | Use |
|---|---|---|
| panel-border | #1C1F25 | 1px outline around each panel |
| line-subtle | #23272F | Dividers, header bottom border, input border |
| line-node | #2A2F39 | Node border, node header divider |
| line-strong | #2E333D | Button border, scrollbar thumb |

### Text
| Token | Hex | Use |
|---|---|---|
| text-1 | #E8EAEF | Values, active labels, titles |
| text-2 | #AEB4C0 | Default UI text, property labels |
| text-3 | #8E96A7 | Captions, placeholders, timestamps, column headers (≥4.5:1 on every surface above) |
| text-link | #8FB3FF | Links (hover #B5CDFF) |

### Accent (single, user-tweakable)
| Token | Value | Use |
|---|---|---|
| accent | #6B9BFF | Active tab icon, toggles, slider fill, selected node border, focus border |
| accent-soft | accent @ 16% alpha | Selected row/item background, focus halo (3px) |
| on-accent | #0B0C0F | Text/icons on accent fills (dark text, not white) |

Alternate accents tested: #34D1BF, #A98BFF, #F59E4B.

### Semantic
| Token | Hex |
|---|---|
| ok | #4FC48A |
| warn | #E5B454 (also: NaN values, over-budget frames) |
| error | #EF6A6A (also: recording dot) |
| info | #6B9BFF |
| folder icon | #D9A443 |

### Data types / axes
| Token | Hex |
|---|---|
| pin-float | #34D1BF |
| pin-int | #F59E4B |
| pin-bool | #A98BFF |
| pin-color | #F472B6 |
| axis-x / y / z | #EF6A6A / #4FC48A / #6B9BFF |
| axis label bg | axis color @ 18% alpha, text X #F28B8B, Y #6FD3A1, Z #8FB3FF |

### Profiler flame chart fills (text #E8EAEF)
Frame #2B303B · Build/Layout #5A3B2E · Render/Record #3E3560 · OnGUI/EndFrame/BeginFrame #24505A · Window render #2E4A3A.
Frame graph bars #3E8F6B, over-budget #E5B454, current frame = accent; 16.7 ms budget line dashed #5A606C.

## Typography
- UI: Inter (fallback system-ui), 12px / 16px line height. Medium (500) for active tabs, 600 for titles and section headers.
- Captions: 11px, text-3.
- Section headers in Inspector: 11.5px / 600, text-1.
- Page title (Settings): 16px / 600.
- Numbers, timestamps, hex, keycaps: JetBrains Mono 11–11.5px. Right-align numeric table columns.

## Spacing scale (4px grid)
2 · 4 · 6 · 8 · 12 · 16 · 24 · 32

## Metrics
| Element | Size |
|---|---|
| Title + menu bar (merged) | 36 high; menu buttons 26 high, 9px horizontal padding, radius 4 |
| Command search box | 380 × 26, radius 6 |
| Window buttons | 44 × 36 |
| Status bar | 26 high, 11px text-3, 12px side padding, 16px gap |
| Panel gutter | 4 (window padding 0 4px; 4px between panels) |
| Panel | radius 6, 1px panel-border, overflow clipped |
| Panel header | 34 high, padding 0 4 0 5, tabs gap 2 |
| Tab | 24 high, padding 0 10, radius 4, icon 14 + 6 gap; active = bg-active + text-1 + accent-colored icon |
| Tab overflow | show first N tabs, then "+N ▾" button instead of wrapping to a second row |
| Icon | 14px, stroke 2, round caps/joins (Lucide-style) |
| Icon button | 24 × 24, radius 4 |
| Control height | 24 (inputs, buttons, number fields), radius 4, padding 0 8 |
| Search field in panel | 26 high, 8px panel padding around it |
| Tree row | 22 high, indent 16 per level, chevron slot 16, icon 14, radius 4 on selection |
| Table row | 22 (profiler), 24 (data table), 27 (shortcuts list) |
| Table header | 26–28 high, bg-header, 11px / 500 text-3 |
| Log row | 24 high; columns 96 (time) / 16 (level dot 7px) / rest |
| Inspector row | 26 high, label column 116, 12px gap, 12px side padding, 4px row gap |
| Inspector section header | 30 high, collapsible chevron, 1px #1E2128 top divider |
| Segmented control | well bg-input + 1px line-subtle, 2px inset, segments 20–22 high |
| Toggle switch | 28 × 16, knob 12; on = accent with dark knob; off = #2E333D with #AEB4C0 knob |
| Slider | track 4 high #2A2F39, fill accent, thumb 14 (#E8EAEF with 3px accent border) |
| Focus | the control's own 1px border turns accent; no halo |
| Scrollbar | 4px thumb, radius 2, #2E333D, no track |
| Keycap | min 20 × 20, 1px #2E333D border (2px bottom), bg #1A1D23, mono 10.5px |

## NodeView
Mockups: `Main.dc.html` (in the window), `NodeGraph.dc.html` (frames, reroutes, states, wire shapes, menus).
- Canvas bg-canvas with the dot grid (canvas-dot). Nodes sit on node-bg (#1F232A / #FFFFFF) with a 1px node-border
  (#363C48 / #C3C9D2) and the node shadow (0 8 20 rgba(0,0,0,.35) / 0 4 14 rgba(16,24,40,.14)), so they lift clearly off the canvas.
- Node radius 6. Header 30 high, **tinted with the node's color**: color over node-bg at 24% (dark) / 13% (light), header divider at
  38% / 28%; 8px full-strength color square, title 600, category caption right (11px text-3).
  Default colors by category: generator #E0716B / #D9534F, operator = accent, input #4FC48A / #1E9E62, output #A98BFF / #7C5CE0.
  A node can override its color (same tint rule), like frames and states.
- Body padding 6 top/bottom; pin rows 26, field rows 26 (20 high fields), side padding 14.
- Number fields in nodes: mono 11, right-aligned, fixed decimals (Gain 1.00). Color inputs show a gradient/color swatch, not a number.
  A texture node's preview is full node width and its output row is labelled (Color).
- Pin 10px, centred on the node edge; connected = filled with a 2px node-bg outline, unconnected = 2px ring.
- Wires 2px, colored by source pin type. **Default shape: curved** (bezier, horizontal tangents of half the distance).
  Orthogonal (runs along the grid and routes round nodes) is a per-view option; its vertical runs get a lane handle
  (8 by 22 pill, node-bg fill, 2px wire-colored border) to move the run sideways.
- States: hover = node-border becomes line-strong; selected = 1px accent border + 3px accent-soft halo; keyboard focus = 1px accent
  border, no halo. Hovered wire: 2.5px plus a 3.5px glow at 35%. Dragging a wire: dashed (5 4) to the pointer; compatible pins get a
  2px ring with a 22% fill, incompatible ones dim.
- Reroute: 12px dot in the wire color, 2px canvas-colored gap, 1px node-border ring.
- Frame (group): radius 8, color at 6-7% fill, 1px color at 40-45% border, 28px title bar at 18-20% with chevron, title 600 and a "..." menu;
  resize grip in the bottom-right corner. Collapsed: only the title bar, with the colors of the nodes inside as small squares.
- Canvas menus: Add node (Space or right click) = menu with a focused search field, items grouped under category headers with the
  category square, matched letters in accent 600; when opened from a dragged wire it is filtered to compatible pins.
  Wire menu: Insert reroute, Insert node, Straighten, Disconnect (destructive).
- Overlays (8-12px inset, bg-header, radius 6): breadcrumb top-left, zoom (- 100% + Fit) bottom-left, minimap 140 by 88 bottom-right.

## StateMachine
Mockup: `StateMachine.dc.html`. Built on the NodeView canvas.
- Toolbar 36 under the panel header: Step, Restart (secondary buttons with icons), breadcrumb of nested machines, run state on the right
  (6px ok dot + "Running").
- State: title bar only, 110 by 32 minimum, radius 6, 8px color square + name 600. **Every state has its own color**: fill = color over
  node-bg at 26% (dark) / 16% (light), border at 50% / 45%, square at full strength. Palette (dark / light): indigo #7B8CFF / #5B6CD9,
  teal #34D1BF / #0FA596, red #EF6A6A / #D64545, amber #E5B454 / #C27C0E, violet #A98BFF / #7C5CE0, plus the custom color picker.
- Initial state: 10px text-2 dot with a 1.5px arrow into the state.
- Transitions: 1.5px text-3 lines with a 7px filled arrowhead; two transitions between the same states run side by side, 12px apart.
  Self loop: an arc over the state's top edge. Selected transition: 2px accent line + accent head, its label in accent-soft with an accent border.
- Labels: 18px chips (bg-header, 1px line-subtle, 11px text-2) beside the line's midpoint, never on it. Rename in place: the chip turns
  into a 22px text field with the text selected.
- Active state (while running): 2px ok ring + 3px ok @ 18% halo, and a mono time-in-state pill under it (ok @ 14%, ok text).
- Nested machine: a second outline offset 4px behind the state (stacked look) and a machine icon at the right.
- Drawing a transition: hovering a state's edge shows a 1.5px dashed accent band 9px outside the state, with a 9px accent dot
  (3px accent-soft halo) where the pointer is; drag from it to another state.
- Transition menu: Conditions, Rename (F2), Reverse direction, Delete.

## Color field (Inspector)
- Swatch button 56×24, radius 4, 1px line-strong border, drawn on the checkerboard.
- Left half: color fully opaque. Right half: color at its actual alpha over the checkerboard.
- 3px alpha bar along the bottom edge: track #0B0C0F (dark) / #464C57 (light), fill text-1 (dark) / #FFFFFF (light), width = alpha.
- Followed by hex field and an alpha field ("A" caption left, value right, e.g. 60%).

## Text selection
| Token | Dark | Light |
|---|---|---|
| selection (focused) | accent @ 35% (rgba(107,155,255,.35)) | accent @ 25% (rgba(47,111,235,.25)) |
| selection-inactive (unfocused field / window) | #3A3F4A | #DCDFE5 |
| caret | 2px accent, 14px tall | 2px accent, 14px tall |
- Selected text keeps text-1; never invert to white-on-accent (keeps mono digits and syntax colors readable).
- Multi-line: each fully selected line extends ~4px past its last glyph to show the selected newline.
- Read-only text (log details, labels that allow copy) uses the same focused selection color.
- Numeric fields select all on focus (see Dictionary "100").
- Selection color derives from the accent, so it follows accent changes.

## Lists, dictionary, menus (Inspector)
Mockup: `InspectorLists.dc.html` (prop `theme`).
- Label column stays 116px but is **top-aligned** to the first row (padding-top 9), with a count caption under it (11px text-3: "3 items").
- List container: 4px padding, 2px row gap, 1px line-subtle border, radius 6, bg #101216 (dark) / #F6F7F9 (light).
- List row 26: grid 14 (grip, 6-dot, text-3) | 14 (index, mono 10.5 text-3, right-aligned) | field | 22 (remove ×, text-3).
  Hover row: bg-hover #232529 / #E9E9EB, remove turns error color on an error @ 12% chip.
- Numeric list fields: mono value + type tag right (f32), same as the Numbers section.
- Add button: inside the container, aligned with the fields (margin 2 24 0 42), 24 high, 1px dashed line-strong, "+ Add item".
- Drag reorder: source row stays at 35% opacity; dragged copy floats with bg-control/white, drop shadow and 1px accent ring, grip in accent;
  drop target = a 26px slot opened between rows (1px dashed accent border on accent-soft, radius 4), the dragged copy hovering over it offset ~6px.
- Dictionary: same container with a 20px header row (Key / Value captions + mono type: string, i32), no grip/index,
  key field (UI font) | value field (mono) | remove; "+ Add entry" button spans the key+value columns.
- Dropdown button: 160 × 24, bg-control, 1px line-strong, label left, chevron right (text-3).
  Open: pressed fill, chevron flips up (focus alone turns the border accent).
- Menu popup: 264 wide, 4px padding, radius 8, bg #1A1D23 / #FFFFFF, 1px #2E333D / #D5D9E0, shadow 0 16 40 rgba(0,0,0,.55) / 0 12 32 rgba(16,24,40,.16).
  Opens 6px below the button. Optional filter field on top for big menus.
  Item 26: grid 16 (icon or check) | label | shortcut (mono 10.5 text-3) | 12 (submenu chevron). Hover = accent-soft.
  Section header 22 (11px/500 text-3, indented to label column). Separator 1px line-subtle with 4px margin.
  Checked item: accent check icon; unchecked: empty icon slot. Disabled: 45% opacity. Destructive: error color text + icon.

## Torn-off panel window
Mockup: `Docking.dc.html` (prop `theme`).
A tab dragged out of a dock becomes its own OS window, holding only that panel.
- Window: bg-app, 1px #2E333D / #C9CED6 border, radius 8 (where the OS allows), shadow 0 24 56 rgba(0,0,0,.55) dark / 0 20 48 rgba(16,24,40,.22) light.
- Title bar 36 (custom chrome, no menu bar): 6-dot grip (text-3) + the panel's tab (26 high, active-tab style; light theme uses bg-panel so the tab joins the panel)
  + spacer (drag area) + "Dock" text button (returns the panel to its last dock position) + 44×36 minimize / maximize / close.
- The panel sits inset 4px (left, right, bottom) inside the window, same panel styling as when docked (bg-panel, panel-border, radius 6).
- Resize grip (two diagonal strokes, text-3) in the bottom-right corner.
- The tab strip in the title bar accepts more dragged tabs, so a torn-off window can host several panels.

## Tooltips
Mockup: `Tooltips.dc.html`.
- Simple: 12px text, padding 5×8, radius 6, 8px arrow (rotated square) toward the anchor, 6–10px gap from the anchor.
  Dark: bg #2A2F39, 1px #363C48, text-1. Light: inverted, bg #1A1D23, white text (no border).
  Shortcut chip inside: mono 10.5, bg white @ 8% (dark) / 12% (light), muted text.
- Rich (property help): popup surface (bg-popup, popup border, radius 8, popup shadow), 268 wide, padding 10×12:
  title 600 · description text-2 · divider · mono 10.5 text-3 footer (API path · type). Labels with rich help get a dotted underline.
- Truncated text: an expansion tooltip drawn exactly over the truncated text, same font, so it reads as the text growing.
- Validation: popup surface with a 1px error border (arrow too); the field keeps its error border + 3px error @ 16% halo and an error icon.
- Disabled controls get a tooltip that says why they are disabled (second line, muted).
- Node pins: name · mono type · live value, second line = connection source.
- Timing: show after 500 ms hover; instant when moving between tooltipped items; hide after 100 ms; never on touch-drag.

## Timeline
Mockup: `Timeline.dc.html` (1240 panel).
- Toolbar 40: transport (start, prev key, play = accent fill, next key, end, loop = pressed toggle), time display in a well
  (mono 13/500 current, text-3 total, frame chip), fps dropdown, Snap toggle, zoom slider right-aligned.
- Track list 240: header 28 (Tracks caption, filter, +). Rows 28 with 1px row lines (#181B20 / #EEF0F3).
  Group rows: chevron, 8px category chip, 600 name, eye + lock. Property rows indented to 34 with an add-key diamond on the right.
  Audio rows: M / S chips (solo uses warn @ 18%). Selected track = accent-soft on both the list row and its lane.
- Lanes are inset 12px left and right so keys at the range ends are fully visible.
- Ruler 28 (bg-header): major tick every second (10px, text-3) with mono 10.5 labels, minor ticks every quarter second (5px).
  Loop range = accent-soft band with 2px accent edges on the ruler; outside the range the lanes are dimmed (black @ 28% / navy @ 5%).
- Keys: 10px diamonds, fill #C9CED8 / #5F6673, 1px panel-colored edge. Selected: 11px accent with light edge.
  Group summary keys: 8px, #6B7280 / #9AA1AD. Box selection: 1px dashed accent on accent-soft.
- Audio clip: teal @ 10% fill, teal @ 45–55% border, radius 4, waveform bars teal @ 65–70%.
- Event markers: 2px orange line + small label chip (orange @ 16%).
- Playhead: 1px text-1 line through ruler and lanes, mono label pill on the ruler (text-1 fill, panel-colored text).
  Not the accent: the accent is selection (keys, loop range) and the Z axis color, the playhead must not be mistaken for either.

## Curve editor
Mockup: `CurveEditor.dc.html`.
- Toolbar: Interpolation segmented with curve glyphs (Constant | Linear | Bezier), Tangents segmented (Auto | Aligned | Free | Flat),
  Frame all (A), and numeric Frame / Value fields for the selected key on the right.
- Channel list 238: tree with 10px channel color squares, the value at the playhead (mono), eye / eye-off; hidden channels at 55% opacity.
- Plot bg #0E1013 / #FFFFFF. Grid: minor every 10 frames / 0.25 (#15181D / #F1F2F5), major every 30 frames / 0.5 (#1F232A / #E1E4E9),
  zero line #343A45 / #C9CED6. Value labels mono 10 text-3 at the left edge; frame ruler 24 on top.
- Channel colors = axis colors (X red, Y green, Z blue, scalar = warn). Active channel stroke 2px at full opacity; others 1.5px at 55%.
  Pre/post infinity drawn dashed (3 4).
- Keys 8px rounded squares in channel color with a plot-colored edge. Selected key 11px white with 2px accent border;
  tangent handles: 1.25px accent lines to 8px hollow accent circles.
- Key readout tooltip next to the selected key: "X · f 120 · 0.40" (mono 10.5, tooltip colors). Values show 2 decimals everywhere.
- Playhead identical to the timeline (text-1 line and pill, frame number in the pill).

## Progress & loading
Mockup: `Progress.dc.html`: an editor window where each indicator sits where the work happens.
Rule: show progress on the thing that is busy, not in a separate place. One global place (status bar + Background tasks) lists everything.
- Document tab still opening: a 13px spinner replaces the type icon, name in text-3; the tab works as soon as the document is usable.
- Panel content still loading: skeleton rows in the real layout (13px icon squares, 8px bars, skeleton #1C1F25 / #E9EBEF, a soft shine
  sweeping every 1.5 s). Rows that stream in after the panel is usable get a 12px spinner and "streaming" (11px text-3) at the right.
- Viewport busy (shader compile, lightmap preview): a 2px indeterminate bar along the top edge of the view and a floating pill
  (28 high, radius 14, floating-toolbar bg) centered at the top with a spinner and the task name. Unready materials draw striped.
- File operations: in the Files panel toolbar, a 200px block with name + mono % above a 3px bar, cancel x after it.
  The tile being imported dims its thumbnail to 60% and overlays a 36px circular progress (3px stroke, mono % inside).
  Thumbnails not generated yet are skeleton tiles.
- Multi-step job (Build panel): 18px status glyph column: done = filled ok circle with check + mono duration right; running = spinner +
  mono % right + a 4px bar and one 11px text-3 line under it (what it is doing, time left); pending = 1.5px dashed line-strong ring, name in text-3;
  failed = filled error circle with x, "failed" in error text. Footer: Cancel + the busy primary button.
- Busy button: keeps its width, spinner replaces the icon, label in -ing form ("Building"), 85% opacity, aria-busy.
- Failure: the log row gets error @ 10% fill, error text, and an inline Retry button (with refresh icon) at the right; the toast says the same.
  Completion: ok check + mono duration.
- Status bar (right): 12px spinner, the current task's name, a 120px 3px bar, then the tasks pill (accent-soft, accent text, chevron).
  The pill opens Background tasks above it: popup surface, 330 wide, header with "Pause all" link; rows = name, mono % / paused / duration,
  cancel x, 3px bar (warn for paused); finished tasks stay listed with a check until the popover closes.
- Bars: 6px standalone, 3-4px inside lists, toolbars and the status bar; radius = half height; track #23272F / #E3E6EB, fill accent.
  Indeterminate: a 30% segment with transparent ends sliding left to right, 1.2 s loop.
- Spinners: ring in the track color + 90 degree accent arc, 0.8 s per turn, linear. Sizes 12 / 13-14 / 18.
- No counts in progress text ("212 left", "1,240 / 3,800"): percentages, time left, or nothing.

## Notifications & confirmations
Mockup: `Notifications.dc.html`.
- Toasts: bottom-right of the main window, 16px from the edges, 8px apart, newest closest to the corner; older ones collapse into a "+N earlier" pill.
  380 wide, popup surface, radius 8, padding 12. Grid: 24px severity circle (severity @ 14–16% with a 2.4-stroke glyph) | content | 22px dismiss.
  Title 600 + relative time (11px text-3), body text-2 (mono 11 for code/file locations), optional 4px progress bar, text-button actions
  (primary in link color, secondary text-2). Errors use role=alert and stay until dismissed; others auto-hide after ~6 s (paused on hover).
  No colored side stripes, severity lives in the icon only.
- Confirmation dialog (destructive): 32px error-soft circle with icon, title 14/600 as a question, consequence sentence,
  list of affected items in a well (mono 11, max 3 + "and N more…"), optional "Don't ask again" checkbox.
  Footer: Cancel (default focus, shown with focus ring) + destructive button in danger fill, #EF6A6A with dark text (dark) /
  #C93C3C with white text (light); the button label repeats the action and count ("Delete 12 assets").
- Inline banners at the top of a panel: full-width, 8×12 padding, severity @ 10–14% fill with a 1px severity @ 30–35% bottom line,
  icon + message + actions right-aligned (primary accent button, secondary button, or a link).

## Documents: top-level docking
Mockups: `Main.dc.html` (document bar), `DocumentWindow.dc.html` (second main window).
Two levels of docking:
1. **Documents** (levels, prefabs/assets, material graphs, layouts, animations). Each document owns a complete panel layout;
   switching the document tab swaps the entire workspace below the document bar.
2. **Panels** inside a document (Hierarchy, Viewport, Inspector, …), docked as before.

Window layout, top to bottom
1. **Title bar with document tabs** (38 high incl. a 2px bottom line, bg-titlebar #08090B / #DADDE2): app icon, the document tabs
   (bottom-aligned, 3px apart), "+", flexible drag area, "N open" overflow list, 44 by 36 window buttons.
   The bottom edge is a 2px line in the active document's solid color.
2. **Document menu row** (34 high, bg-app, so it reads as part of the document): menus (File, Edit, View, Help) on the left,
   "Search or run a command" centered, "Layouts" dropdown on the right.
   Menus, search scope and panel layouts all belong to the active document, so this row sits under the tabs and its contents
   change when you switch tabs.
3. Panels, then the status bar.

Document tabs ("colored tabs")
- Tab: 30 high, min-width 150, radius 6 6 0 0, padding 0 8 0 12, gap 8. Type icon 14px, name + extension ("Forest" + ".level"),
  dirty dot (6px) or close ×.
- Every tab carries its document-type color:
  inactive = type color @ 14% (dark) / 12% (light) fill, text-1 name, type-colored icon, text-3 extension;
  active = solid type color, name 600 and icon in on-solid (#0B0C0F dark / #FFFFFF light), extension on-solid @ 62–78%.
- Hover / pressed on inactive tabs: tint @ 22% / 30% (dark), 18% / 24% (light). Keyboard focus: inset 1px accent outline.
- "+" after the last tab creates a document; "N open" on the right lists all open documents.
- Document-type colors, tint (all themes use the normal palette) / solid in light theme (darkened so white text passes 4.5:1):
  level #6B9BFF / #2F6FEB · material/graph #34D1BF / #0B7A6F · layout #F59E4B / #B35F0E · prefab #A98BFF / #7C5CE0 · animation #F472B6 / #B8306F.
  In the dark theme the solid color equals the tint color (dark text on it).

How they differ from panel tabs (on purpose)
| | Panel tab | Document tab |
|---|---|---|
| Height | 24 | 30 |
| Shape | floating pill (radius 4), neutral | top-rounded tab (radius 6 6 0 0) sitting on a colored 2px baseline |
| Color | neutral grey, accent icon when active | always tinted in the document-type color; active is solid |
| Name | label | name + muted extension, dirty dot |
| Location | panel header | title bar, above the document menu row |

Behaviour
- Dragging a document tab out of the bar creates a new main window (full chrome: title bar, menus, document bar, status bar)
  holding that document and its own panel layout. Dropping a document tab on another window's document bar moves it there.
- Document tabs only drop on document bars; panel tabs only drop on panel docks. The two drop-target sets never mix.
- Panels inside a document can still be torn off into panel windows (see Torn-off panel window); those belong to the document's main window.
- The window title / status bar names the active document.

## States & motion
Mockup: `States.dc.html` (matrix of 12 controls × rest, hover, pressed, focus, selected/on, disabled, invalid).

State model, every visual state is computed, not hand-picked:
- **Base**: the control's rest colors.
- **State layer**: the control's own content color over the base, at **8% hover**, **14% pressed**.
  Dark secondary buttons therefore lighten; a primary button (dark text on accent) darkens; light theme darkens.
  Exception: menu items and list rows highlight with accent-soft (they are keyboard-navigated).
- **Selection / on**: accent-soft fill (16% dark / 12% light) or the control's own "on" visual (toggle track, check fill).
- **Focus** (keyboard only, never on mouse click): the control's own 1px border turns accent, no halo (it would overlap neighbours in
  dense rows and get clipped by scroll views). Controls without a border (icon buttons, toggle track) draw a 1px accent border on their shape.
  Accent-filled controls (primary button) add a 1px panel-colored line just inside the accent border;
  inset 1px accent outline for tabs, rows and menu items (they sit flush with neighbours).
- **Disabled**: 40% opacity on the whole control (menus 45%), no state layer, no focus.
- **Pressed** never changes the border (in the app, buttons and fields get an accent border on press: drop it).
- **Selected button** (toggle buttons): accent-soft fill + accent text 500, normal border.
- **Invalid**: error border + 3px error @ 16–18% halo; text fields also show a 13px error icon at the right, in both themes.
  Check boxes and toggles can be invalid (error border / inset ring + halo); buttons cannot.
- **Check boxes and radios** light only the box on hover, not the label.

Transitions
| Change | Duration | Easing |
|---|---|---|
| Hover in | 70 ms | ease-out |
| Hover out | 140 ms | ease-out |
| Press | 0 ms | instant |
| Release | 120 ms | ease-out |
| Focus border | 100 ms | ease-out (border color) |
| Selection | 0 ms | instant, highlight follows input exactly |
| Toggle, check, radio | 120 ms | ease-in-out (knob + fill together) |
| Tooltip | 100 ms in / 80 ms out | ease-out (after 500 ms delay) |
| Menu, popup | 120 ms in / 80 ms out | ease-out, fade + 4px slide from the anchor |
| Toast | 180 ms in / 140 ms out | ease-out, slide from the right edge |
| Dock preview | 100 ms | ease-out |
| Invalid | 0 ms (halo 150 ms) | instant border |
| Theme switch | 200 ms | ease-in-out, every token at once |
ease-out = cubic-bezier(0.2, 0, 0, 1); ease-in-out = cubic-bezier(0.4, 0, 0.2, 1).

Color interpolation
- Same-hue changes (state layers, selection fades): lerp in sRGB, the delta is small and it's cheapest.
- Hue changes (ok → warn → error, accent → error on invalid, theme switch): lerp in **Oklab**; sRGB sags darker/greyer mid-way.
  Convert each token's endpoints to Oklab once at theme load, not per frame.

Implementation notes
- Per widget keep one scalar per animated channel (hover, press, focus; 8 bits each is enough) in a compact array indexed by widget id;
  colors are a lerp between precomputed endpoint colors, so no per-frame color math beyond one lerp per channel.
- Only widgets with an unfinished transition are ticked; when none are active, don't redraw.
- Retarget from the current value when a state flips mid-transition (never restart from 0).
- Reduced-motion setting: all durations 0, except progress indicators and spinners.

## Dialog surface (elevation)
Modal dialogs, message boxes and pickers must stand out from the window, so they get their own surface tier instead of bg-panel:
| Token | Dark | Light |
|---|---|---|
| dialog-bg | #1B1E25 | #FFFFFF |
| dialog-header (title bar + footer) | #20242C | #F6F7F9 |
| dialog-border | #3A404C | #C3C9D2 |
| dialog-divider | #2A2F39 | #E6E8EC |
| dialog-well (lists, details) | #13151A | #F6F7F9 |
| dialog-input | #0F1115 | #FFFFFF |
| dialog-control (buttons) | #272B34 | #FFFFFF |
| dialog-control-border | #3A404C | #C9CED6 |
| dialog-shadow | 0 0 0 1px rgba(0,0,0,.5), 0 24px 64px rgba(0,0,0,.6), inset 0 1px 0 rgba(255,255,255,.05) | 0 0 0 1px rgba(16,24,40,.06), 0 24px 64px rgba(16,24,40,.28) |
| scrim (over the owning window) | rgba(0,0,0,0.55) | rgba(16,24,40,0.30) |
- Dark: the dialog is a step *lighter* than panels (closer = lighter), with a 1px top highlight. Light: pure white over a dimmed window.
- Modal dialogs always dim their owning window with the scrim (fade 120 ms); non-modal pickers skip the scrim but keep the surface + shadow.
- Elevation order: panel < popup/menu (#1A1D23 / #FFFFFF) < dialog < tooltip. Toasts use the popup tier.

## Message boxes (yes / no / cancel)
Mockup: `MessageBox.dc.html`.
- 456 wide, bg-panel, 1px border, radius 8, dialog shadow; no title bar, a 24px close × in the top-right corner (same as Cancel / Esc).
- Body padding 20 (40 on the right to clear the ×): 32px severity circle (severity @ 12–16% with a 2.2-stroke glyph) |
  title as a question, 14/600 · one or two sentences saying what happens · optional extra (affected-items list in a well,
  "Don't ask again" checkbox, collapsible mono Details).
- Footer 52, bg-header, top divider. Buttons 28 high. Order: [destructive-alternative left] … [Cancel] [primary] right-aligned.
  Unsaved changes: "Don't save" alone on the left, Cancel, then "Save all" (primary).
- Button labels are verbs that say what happens ("Reload", "Keep mine", "Quit"), never bare Yes/No.
- Default button (Enter) has the focus ring. Safe default: the primary action when it's harmless (Save, Reload, OK);
  Cancel when the action destroys or stops work (Quit with running tasks, Delete). Destructive buttons use the danger fill.
- Esc = Cancel (or the close ×). Every button has an Alt mnemonic, shown underlined on its first letter.
- Severity icon: warning for data loss, power/warning for quitting, question for neutral choices, error for failures.
  The error box offers "Copy details" next to OK.

## Viewport
- Floating toolbars over the view (bg rgba(22,25,30,.92), 1px line-subtle, radius 6, 30 high, 3px inset): file/undo group, shading-mode group, camera dropdown.
- Vertical tool strip at left (select/move/rotate/scale); active tool = accent fill with on-accent icon.
- Axis gizmo top-right 64px on a dark disc: 14px X, Y, Z discs with the letter (700, on-accent color), lines to the positive axes,
  9px hollow dots (axis @ 25% fill, 1.5px axis stroke) for the negative axes. Click an axis to look along it.
- Selected object: 2px accent outline. FPS badge bottom-left (mono): fps, frame time, selection.

## Dialogs (shared)
Mockups: `ColorPicker.dc.html`, `GradientEditor.dc.html` (prop `theme` = dark | light; the `*Light.dc.html` files just set it).
- Frame: bg-panel, 1px panel-border, radius 8, shadow 0 24 64 rgba(0,0,0,.55) dark / rgba(16,24,40,.18) light.
- Header 44: title 13/600 text-1 + context path text-3 + 28px close button.
- Footer 52, bg-header, top divider: status text left; Cancel (secondary) + OK (accent fill, on-accent, 600) right.
- Transparency checkerboard: 8px squares, #2A2F39/#1E2128 dark, #FFFFFF/#E3E6EB light.
- Selected swatch/preset: 2px panel-colored gap + 2px accent ring.

### Color picker (560 wide)
- SV square 240×240, radius 6; cursor 14px ring (2px white + 1px dark outline).
- Hue strip and alpha strip 16×240, 10px apart; handle 22×6 white-outlined bar.
- Right column: New over Original, stacked (two 22 high halves, 44 total) on the checkerboard, labels at the right in text-3.
  Color model segmented: RGB | HSV | HSL | Lab | Oklab (padding 0 7). The value range (0–255 / 0–1) is set from the footer status ("sRGB · 8-bit · 0–255").
- Channel rows 26: label 14 / track 8 high showing the channel's actual gradient / numeric field 52. Thumb 12px white.
- Hex field (focused state shown) + eyedropper; HDR intensity field + Linear checkbox.
- Swatches row (22px squares, dashed add button) and Recent row (22px circles).
  Both use the color-field split: left half opaque, right half at actual alpha over the checkerboard.
  Swatch squares also get a 2px alpha bar along the bottom (circles omit it). Unselected outline 1px line-node.

### Gradient editor (720 wide)
- Preset chips 64×22; the preset in use gets the selected ring.
- Gradient bar 44 high on the checkerboard, so alpha shows; 24px side inset so end stops fit.
- **One kind of stop: every color stop carries its own alpha** (no separate alpha stops). Stops sit below the bar: pointer up, 18px square
  showing the stop color at its alpha over the checkerboard. Unselected edge text-1 (dark) / white (light); selected edge = accent +
  3px accent-soft halo, with a location badge under it.
- Left column: "Color stop" with prev/next, color swatch + hex + alpha field ("A 100 %"), location %, duplicate, delete (error color),
  Interpolation (Linear | Smooth | Constant, segmented), Blend space dropdown (sRGB, Linear, Oklab, Lab, HSL, HSV; default Oklab),
  Reverse, Distribute evenly.
- Right column: stops table (swatch, hex, alpha, location) sorted by location; selected row = accent-soft + 2px accent left bar.
- Footer: Cancel + OK only (no instructions, no stop counts).

## Menus and context menus
Mockup: `Menus.dc.html`. Menu bar menus, submenus and context menus share one frame.
- Frame: bg-popup, 1px popup border, radius 8, popup shadow, 4px padding; width = widest row (min 200), never truncated.
- Row 26: padding 0 8, radius 4, 16px icon slot + 8 gap, label, shortcut, submenu chevron.
  The icon slot is always reserved, so labels line up whether or not a row has an icon.
- Shortcut: mono 11, text-3, keys separated by spaces ("Ctrl Shift S"), at least 24px from the label (fixes "Save As..." touching its shortcut).
- Separator: 1px line-subtle with 4px above and below. Section header: 24 high, 11px/500 text-3, indented to the label.
- Hover = accent-soft; pressed = selection; the parent row of an open submenu stays accent-soft and its chevron turns text-2.
- Check rows: accent check (13px, stroke 2.5) in the icon slot. Radio groups: 6px accent dot.
- Disabled: 45% opacity, no hover. Destructive: error text and icon, error @ 10% hover.
- Submenus open to the right with a 2px overlap, top row aligned with the parent row; 200 ms hover delay to open, 300 ms to close.
- Context menu: opens at the pointer (top-left corner at the pointer + 2px), flips to stay on screen. The row it was opened on keeps
  its selection highlight while the menu is open.
- Menu bar item with its menu open: bg-active, text-1.

## Tables, TreeTable, FrozenTable
Mockup: `Tables.dc.html` (with the Table in `Settings.dc.html`).
- Toolbar 40: filter field, then "Select" caption + compact segmented control (Row | Cell). Never stretched buttons, no stray mode label.
- Header 28: bg-header, 11px/500 text-3, sorted column in text-1 with a 11px chevron. No column dividers; a resize handle shows on hover.
- Rows 22 (24 for data tables). No stripes, no tree guide lines, no left gutter.
- Numbers: mono 11, right-aligned, header aligned the same way. Text and hex codes left-aligned.
- TreeTable: first column is the tree (indent 16, 11px chevron, 13px typed icon: outline folder in folder color, cube for meshes,
  image for textures, material ball, note for audio, globe for scenes), other columns as the Table.
- FrozenTable: frozen columns (left and right) and the header row stay put; the frozen edge is a 1px line-strong with an 8px shadow
  falling onto the scrolling part. Scrollbars only cover the scrolling part.
- Status values: muted text when fine; problems as 16px chips (error @ 10%, error text).
- Row and cell states: hover bg-hover; selected accent-soft; focused selected row + 1px accent inset; selected but panel not focused =
  selection-inactive (#3A3F4A / #DCDFE5); Cell mode: selected cells accent-soft, the focused cell + 1px accent inset.

## Reorderable
Mockup: `Reorder.dc.html` (tabs, list, wrapped groups).
- Item: control bg, 1px control border, radius 4, text centred vertically (fixes tab text sitting at the top). Hover: 8% state layer;
  list grips (6 dots) show in text-3 on hover, in line-strong otherwise.
- Drag starts after 4px. The lifted copy: raised bg, 1px accent border, popup shadow, tilted -1.5 degrees, follows the pointer.
- Drop gap: the items part where it will land, leaving a slot the size of the item: 1px dashed accent, accent at 6% fill, radius 4.
  Neighbours slide 120 ms ease-out. Esc cancels; the item flies back in 160 ms. Alt + arrows move the focused item.

## Inspector: banners, search, mixed values, curves, levels
Mockup: `InspectorMore.dc.html`.
- Banners above the object header, full width: warning (warn @ 10% fill, warn @ 30-40% bottom line, Discard + Save) and info
  (info @ 8%, lock icon, a text link). Text without em dashes: "Read-only, this asset comes from a package".
- Search row under the header (26 high field, focused shown): rows whose label matches stay, matched letters get the selection color;
  sections without a match collapse and dim to 55%; a clear x sits in the field.
- Autocomplete: the completion shows inline after the caret in text-3 (Tab or Enter accepts); the list below matches by prefix,
  matched part 600, a usage hint right in 11px text-3.
- Mixed values (multi-selection): check box = accent box with a 8 by 2 bar + "Mixed" in text-3; number fields = muted dash, right-aligned;
  dropdowns and text = "Mixed" in text-3; color = diagonal stripes of the selected colors.
- Ranged numbers are always a slider + 56px value box.
- HDR gradient: tone-mapped bar like the gradient field; a 3px band above marks where values pass 1.0; a mono chip ("HDR ×8.0") right.
- Curve field: 44 high plot (plot-bg, 1px line-subtle, radius 4), grid-major zero line, 1.75px accent curve. Hover: line-strong border
  and a small open button top-right; click opens the curve editor. Color curves: R, G, B in axis colors + a dashed text-2 identity line.
- Levels: one row with channel chips (All, R, G, B with colored dots), a mode dropdown and a Log check box; 120 high histogram
  (accent @ 28% / 22% fill, text-2 outline), clipped ranges shaded; black, gamma, white triangle handles under it; Input fields
  (black, gamma, white); output ramp with two handles; Output fields.

## Misc: drawing, icon buttons, rich text, images, sketch
Mockup: `Misc.dc.html`.
- Vector drawing surfaces use the canvas look (bg-canvas, dot grid). Path editing: anchors 12px hollow accent circles, handles 8px
  accent squares on dashed accent lines.
- Icon buttons: square, sizes 20 / 24 (default) / 28 / 32, icon 14 (16 from 28). Rest transparent; hover and pressed = content color
  at 8% / 14% over the whole square (fixes the narrow hover strip); on = accent-soft + accent icon; focus = 1px accent border.
  Groups sit in a bg-header well with 2px padding. Primary: accent fill. Destructive: error icon.
- Rich text: links in text-link with an underline offset 3px (dotted for internal links), inline code mono 11 on bg-well, inline images
  at text height on the baseline, bold in text-1.
- Image thumbnails: 76 square, radius 6, 1px line-subtle; alpha over the checkerboard; selected = accent border + 2px accent-soft halo;
  loading = skeleton fill; missing = image icon on bg-well. Name under it, 11px, ellipsis.
- Clipboard image wells: 112 high, radius 6. Filled: the image with a Copy button bottom-right. Empty paste target: 1px dashed
  line-strong on bg-well with an image icon. Image dragged over: dashed accent on accent at 6%, accent icon. No instruction text inside.
- Sketch (pen pad): toolbar with pen (on), eraser, clear, then the pen readout as mono chips on bg-well (Pressure, Tilt, Barrel);
  the pad is plot-bg with a 1px border; the pen hover position is a 10px accent ring. Stroke width follows pressure.

## Review decisions
Where the app and the design differed, this is the outcome. "App" means the app should change; "design" means the design changed.
- bg-hover: design takes the app's #232529 / #E9E9EB (text at 8% over the panel); bg-pressed becomes the 14% layer (#303236 / #DCDDE0),
  the old segment chip value moves to segment-on.
- canvas-dot: design takes the brighter #39404D / #B5BCC7, together with lighter node bodies (node-bg, node-border) for contrast.
- text-3 #8E96A7 and light warn-text #8E5D11: design takes the app's values (better contrast). bg-well light stays #EEF0F3 (app).
- Wires: curved is the default, orthogonal an option (design).
- Playhead: text-1 line and pill in both Timeline and Curves (design, both changed).
- Focus: the control's own border, as the app does (design changed); pressed without accent border (app).
- Selected toggle button: accent-soft + accent text (app). Check box hover lights only the box (app).
- Color picker New/Original stacked, as the app does (design changed); Lab and Oklab modes kept (design added them).
- Gradient editor: per-stop alpha, as the app does (design dropped alpha stops); Interpolation stays (app to build);
  Blend space keeps all six spaces in a dropdown (design changed).
- Viewport axis gizmo: the app's labelled discs with negative-axis dots (design changed). Selection outline, fading grid, shadow and
  the fps readout: app.
- Hierarchy, Files, Profiler, Log, Settings, Table, Inspector "off" items: the design stands (no stripes, guide lines, thick dividers;
  bar chart in the profiler; 24px log rows with dots; keycaps; two-column numbers; full-width dropdowns; "+Inf" / "-Inf").
- Table number columns: right-aligned, as the app does (design changed).
- Counts: no "N of M" readouts ("1 of 10,000 selected", "2 / 3", "1,240 / 3,800") and no command counts; they were removed from the design.
- Tooltip caret toward the control: kept (both). Warning toasts use the triangle (design changed).
- Second window title bar: "N open" menu like the main window (design changed); the command search stays in every window (app).
- Light theme: toggle knob white when off, raised selected segment (app).

## Light theme (themes/light.css)
Same token names, metrics and structure. Mockups: `MainLight.dc.html`, `InspectorLight.dc.html`, `SettingsLight.dc.html`, `TokensLight.dc.html`.

| Token | Dark | Light |
|---|---|---|
| bg-app | #0B0C0F | #E6E8EC |
| bg-input | #0D0F12 | #FFFFFF |
| bg-canvas | #0E1013 (dots #39404D) | #ECEEF1 (dots #B5BCC7) |
| bg-hover | #232529 | #E9E9EB |
| bg-pressed | #303236 | #DCDDE0 |
| node-bg / node-border | #1F232A / #363C48 | #FFFFFF / #C3C9D2 |
| bg-panel | #121418 | #FBFBFC |
| bg-header | #16191E | #F3F4F6 |
| bg-raised | #181B20 | #FFFFFF |
| bg-control (buttons) | #1E2128 | #FFFFFF + 1px #CDD2DA, shadow 0 1 2 rgba(16,24,40,.06) |
| bg-active (active tab) | #23272F | #E4E7EC |
| segment well / segment-on | #0D0F12 / #2A2F39 | #EEF0F3 / #FFFFFF chip + shadow 0 1 2 rgba(16,24,40,.12) |
| sidebar nav, details pane | #101216 / #0F1114 | #F6F7F9 |
| row-stripe | #14161B | #F6F7F9 (rows on #FFFFFF) |
| panel-border | #1C1F25 | #DADDE3 |
| line-subtle | #23272F | #E3E6EB |
| line-node / input border | #2A2F39 | #D5D9E0 |
| line-strong / scrollbar / toggle-off | #2E333D | #C9CED6 |
| section divider | #1E2128 | #E9EBEF |
| text-1 | #E8EAEF | #1A1D23 |
| text-2 | #AEB4C0 | #464C57 |
| text-3 | #8E96A7 | #5F6673 |
| text-link | #8FB3FF | #2459C9 (hover #1A4296) |
| accent | #6B9BFF | #2F6FEB |
| accent-soft | accent @ 16% | accent @ 12% |
| on-accent | #0B0C0F | #FFFFFF |
| ok / warn / error | #4FC48A / #E5B454 / #EF6A6A | #1E9E62 / #D99A2B (text #8E5D11) / #D64545 |
| pin float / int / bool / color | #34D1BF / #F59E4B / #A98BFF / #F472B6 | #0FA596 / #D9771A / #7C5CE0 / #D63F8C |
| axis x / y / z | #EF6A6A / #4FC48A / #6B9BFF | #D64545 / #1E9E62 / #2F6FEB |
| axis label text | #F28B8B / #6FD3A1 / #8FB3FF | #B83232 / #157A4A / #2459C9 (bg axis @ 12%) |
| node category gen/in/out | #E0716B / #4FC48A / #A98BFF | #D9534F / #1E9E62 / #7C5CE0 |
| folder icon | #D9A443 | #E3AE4C (tree icon #C98F2A) |
| frame bars ok / over | #3E8F6B / #E5B454 | #59B88A / #E0A23A |
| budget line | #5A606C | #9AA1AD |
| node shadow | 0 8 20 rgba(0,0,0,.35) | 0 4 14 rgba(16,24,40,.14) |
| floating toolbar bg | rgba(22,25,30,.92) | rgba(255,255,255,.92) + shadow 0 1 3 rgba(16,24,40,.08) |
| slider thumb | #E8EAEF, 3px accent border | #FFFFFF, 3px accent border |

Flame chart fills (light, text #1A1D23): Frame #E3E6EB · Build/Layout #F4DCCF · Render/Record #E2DCF5 · OnGUI/EndFrame #D3ECEF · Window render #D7EBDD.
Alternate light accents (all ≥4.5:1 with white): #0E8277, #6D4FD6, #B85C10.

## Behavioral notes
- Selection = accent-soft background + text-1 (never a saturated solid fill behind text).
- Tables and trees: the focused row of a selection gets a 1px accent inset (see Tables).
- Bottom dock can collapse to a 30px tab strip (Files, Log with count badge).
