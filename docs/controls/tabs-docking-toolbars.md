# Tabs, docking and toolbars

## Tabs

### BeginTabBar / Tab

A bar of tabs; `*selectedIndex` is the selected tab (click or arrow keys).

| Input | Effect |
| --- | --- |
| Drag a tab | Reorders; calls `onReorder(from, to)` |
| `onClose(index)` set | "Close Panel" context menu (Shift+F10), Ctrl+W / Ctrl+F4 close the focused tab |
| Alt+Left / Right | Moves the focused tab (clicking a tab focuses it) |

When tabs don't fit, `TabBarLayout` decides. The user switches it per bar from the context
menu of the strip, a tab or the "+N" button:

- **One row** ("Collapse Rows", default): first tabs plus a "+N" button listing the rest.
  The selected tab is always shown, replacing the last ones that fit, so arrows and
  Home/End reach listed tabs too. A `Tab` in the list builds nothing and returns false. Fit
  uses the previous frame's sizes, so after a resize the bar settles one frame later.
- **Several rows** ("Multiple Rows"): all tabs, wrapping. Dragging a tab to another row
  moves it there. Arrows and Alt+arrows continue across rows.

Pass `layout` to own the choice; otherwise the bar remembers it by ID for the session. A
docked tab group stores it on its dock node (saved with the layout). `onDragOut` and
`dragVisualizer` work as in the reorderable group. Content under the bar is yours.

`TabBarOptions`:

| Field | Effect |
| --- | --- |
| `closeLabel` | Text of the close item ("Close Panel") |
| `listHidden = false` | No "+N" button, for listing hidden tabs yourself |
| `layoutMenu = false` | No "Collapse Rows" / "Multiple Rows" choice |
| `dropArea` | Element around the bar that counts as the bar while dragging: reorders inside, past the last tab goes to the end; only outside it drags out |

```cpp
bool BeginTabBar(ID id, int* selectedIndex, std::function<void(int, int)> onReorder,
                 std::function<void(int)> onClose = nullptr,
                 std::function<void(int, int2)> onDragOut = nullptr,
                 std::function<void(int, int2)> dragVisualizer = nullptr,
                 StyleRef style = {}, LayoutOverride&& constraints = {},
                 TabBarLayout* layout = nullptr, const TabBarOptions& options = {});
void EndTabBar();
bool Tab(ID id, const GUIContent& content, StyleRef style, LayoutOverride&& constraints = {});
bool BeginTab(ID id, const GUIContent& content, StyleRef style, StyleRef contentStyle,
              LayoutOverride&& constraints = {});
void EndTab();
```

`BeginTab` / `EndTab`: for tabs with more than icon and text. Draws icon and text (in
`contentStyle`, default `tab-content`), then what you add before `EndTab`. Always pair with
`EndTab`. If `BeginTab` returns false the tab is in the "+N" list; add nothing.

Forms without an ID use the call site. Classes: `tab-bar`, `tab`, `tab-content`,
`tab-overflow` (the "+N" button; its list uses menu classes). The bar's style gets
`flex-wrap: wrap` in several-rows layout (`StyleManager::WithFlexWrap`), `nowrap` in
one-row.

```cpp
BeginTabBar(idManager.AllocateDynamicID("tabs"), &selectedTab, nullptr);
Tab("First");
Tab("Second");
EndTabBar();
Text(selectedTab == 0 ? "Content of the first tab" : "Content of the second tab");
```


## Docking

### BeginDockingContainer / EndDockingContainer

Panels in a tree of splits stored in a `DockingLayout`; each leaf is a tab group. Users drag
tabs to move, dock on another leaf, or float outside the window. Create each panel once
with `CreatePanel`; its callback builds its content.

```cpp
ID   CreatePanel(std::unordered_map<ID, Panel>& panels, std::string title, IconID icon,
                 std::function<void()> onPanelGUI, std::string panelType = {},
                 std::function<void()> onEnable = {}, std::function<void()> onDisable = {});
bool BeginDockingContainer(ID id, DockingLayout* layout, StyleRef style, LayoutOverride&& constraints = {});
bool BeginDockingContainer(ID id, DockingLayout* layout, LayoutOverride&& constraints = {});
void EndDockingContainer();
void EmitFloatingDockWindows();
```

- Build the tree with `MakeLeaf({ panelIDs... })` and `MakeSplit(mode, size, first,
  second)` (`gui_dock_node.h`). `DockSplitMode`: `LeftPinned`, `RightPinned`, `TopPinned`,
  `BottomPinned` (which child keeps `size`).
- Call `EmitFloatingDockWindows()` after the container for floating panels.
- `onEnable` / `onDisable` run when a panel becomes visible / hidden. `cacheSignature`
  controls the render cache. Closing a panel destroys it.
- Also: `FloatPanel(layout, panelID, screenPos)`, `CloseDockedPanel(layout, leaf,
  tabIndex)`, `CloseFloatingDockWindow(index)`, `DisableAllPanels(layout)`.

Classes: `dock-container`, `dock-leaf`, `dock-content`, `dock-split-horz`,
`dock-split-vert`, `dock-sizer-horz`, `dock-sizer-vert`, `dock-window`,
`panel-drop-visualization`.

```cpp
// Once:
const ID scene   = CreatePanel(layout.panels, "Scene",   IconID::Invalid, [this] { ScenePanel(); });
const ID details = CreatePanel(layout.panels, "Details", IconID::Invalid, [this] { DetailsPanel(); });
const ID log     = CreatePanel(layout.panels, "Log",     IconID::Invalid, [this] { Text("Nothing logged yet."); });
layout.root = MakeSplit(DockSplitMode::LeftPinned, 220,
    MakeLeaf({ scene }),
    MakeSplit(DockSplitMode::BottomPinned, 150, MakeLeaf({ details }), MakeLeaf({ log })));

// Each frame:
if (BeginWindow(windowID, "Docking", theme.mainWindowStyleRef, LayoutOverride::FixedSize(900, 600).Centered()))
{
    BeginDockingContainer(dockID, &layout, LayoutOverride::Auto());
    EndDockingContainer();
    EmitFloatingDockWindows();
}
EndWindow();
```

### Documents

Documents (levels, prefabs, graphs...) each have their own `DockingLayout`. A document tab
bar sits under the title bar; the active document's panels fill the window. Only the active
document's panels are built and accept drops; the others get `onDisable`.

Tabs show icon, name and a muted extension ("Forest" + ".level"). The active or hovered tab
shows a close button; a `dirty` document shows a dot instead.

Tab colors come from the document's `kind`:

| Class | Colors |
| --- | --- |
| `document-tab-<kind>` | Tab tint; solid fill when active |
| `document-name-<kind>` | Icon |
| `document-line-<kind>` | Line under the bar (active document's color) |

Themes define `level`, `material`, `layout`, `prefab`, `animation`. Other kinds fall back
to `document-tab`, `document-name`, `document-line`.

Behavior:

- Drag to reorder. Tabs that don't fit are hidden; the "N open" list names all.
- "+" calls `onNew`; hidden if `onNew` is unset.
- Close button, "Close Document" in the context menu and Ctrl+W call `onClose` first, then
  close next frame. If `onClose` returns false the document stays open; ask about unsaved
  changes there and call `CloseDocument` yourself. Closing destroys its panels and their
  torn-off windows.
- Dragging a tab off the bar: dropped on another document bar (any main window), it moves
  there at the phantom tab's spot. Dropped elsewhere, `onTearOff` gets the document
  (already removed from its set) and screen position, to open a new main window. Without
  `onTearOff` it goes back.
- Document tabs drop only on document bars, panel tabs only on panel docks. A document
  dropped on panels is torn off; a panel dropped on a document bar floats. Drops apply at
  the start of the next frame.

Each main window is built by your app: title bar and menus, `Documents` for its
`DocumentSet`, status bar, then `EmitFloatingDockWindows(set)` (windows for panels torn off
from that set; they belong to that main window and follow a document that moves). Close a
window with no documents left. A set must stay alive until the end of the frame it was
built in.

Saving: `PersistDocuments(key, windows)` takes a function listing main windows and their
sets (`DocumentWindowRef`). `SaveUIState` saves each window's position, document order,
active document, and each document's name, extension, kind and dock tree.

Restoring (the library can't create your panels): after `RestoreUIState`, call
`TakeSavedDocuments(key)`. Per window, create its documents with panels, lay each out with
`RestoreDocumentLayout`, set `active`. For extra windows, call `RestoreWindowPlace` before
declaring them.

```cpp
// Initialize:
PersistDocuments("documents", [this] { return std::vector<DocumentWindowRef>{ { mainWindowID, &documents } }; });
(void)RestoreUIState(savedText);

// The first frame:
for (const SavedDocumentWindow& window : TakeSavedDocuments("documents"))
{
    for (const SavedDocument& saved : window.documents)
        (void)RestoreDocumentLayout(MakeMyDocument(documents, saved.name, saved.extension, saved.kind), saved);
    documents.active = window.active;
}
```

```cpp
Document& CreateDocument(DocumentSet& set, std::string name, std::string extension,
                         std::string kind, IconID icon);
void      CloseDocument(DocumentSet& set, int index);
void      Documents(ID id, DocumentSet& set, LayoutOverride&& constraints = {});
void      EmitFloatingDockWindows(const DocumentSet& documents);
```

`DocumentSet` holds documents in tab order, plus `active`, `onNew`, `onClose`, `onTearOff`.
`Active()` returns the shown document. Other classes: `documents`, `document-bar`,
`document-row`, `document-tab-bar`, `document-extension`, `document-mark`,
`document-close`, `document-dirty`, `document-new`, `document-list`.

```cpp
// Once:
Document& forest = CreateDocument(documents, "Forest", ".level", "level", levelIcon);
forest.layout.root = MakeLeaf({ CreatePanel(forest.layout.panels, "Scene", IconID::Invalid, [this] { ScenePanel(); }) });
Document& terrain = CreateDocument(documents, "Terrain", ".mat", "material", materialIcon);
terrain.layout.root = MakeLeaf({ CreatePanel(terrain.layout.panels, "Graph", IconID::Invalid, [this] { GraphPanel(); }) });

// Each frame, under the title bar:
Documents(documentsID, documents);
EmitFloatingDockWindows(documents);

// A tab dropped off the bars: a window of its own, built like the first one.
documents.onTearOff = [this](std::unique_ptr<Document> document, int2 at) {
    auto& window = windows.emplace_back(std::make_unique<MyDocumentWindow>());
    window->windowID = idManager.CreateStaticID();
    window->position = at;
    window->documents.onTearOff = documents.onTearOff;
    window->documents.documents.push_back(std::move(document));
};
```


## Toolbars

### BeginToolbar / EndToolbar

A wrapping row (or column). Controls with default style inside use their toolbar versions:
`toolbar-button`, `toolbar-toggle`, `toolbar-textbox`, `toolbar-number-field`,
`toolbar-menu-trigger`.

```cpp
bool BeginToolbar(bool horizontal = true);
void EndToolbar();
```

```cpp
BeginToolbar();
Button(saveIcon, [this] { Save(); });
SetTooltip("Save", "Ctrl+S");
Toggle(snapToGrid, "Snap");
EndToolbar();
```

### RenderToolbarDock

Free function (`controls_toolbar_dock.h`): toolbars on the four sides of some content.
Users drag items between sides or tear them off to floating windows. Your app owns the item
lists and moves items in the callbacks.

```cpp
void InitializeToolbarDockState(ToolbarDockState& state, GUIApplication& app);
void ReleaseToolbarDockState(ToolbarDockState& state, GUIApplication& app);
void RenderToolbarDock(GUIApplication& app, ToolbarDockState& state,
                       const std::vector<ToolbarItem> (&itemsPerSide)[kToolbarSideCount],
                       const ToolbarDockCallbacks& callbacks, std::function<void()> renderContent);
```

```cpp
// Once, after InitializeToolbarDockState(dockState, *this):
sides[int(ToolbarSide::Top)].push_back(ToolbarItem{ .id = idManager.CreateStaticID(), .debugName = "File",
    .renderBody = [this] { Button(saveIcon, [this] { Save(); }); } });

// Each frame (members: std::vector<ToolbarItem> sides[kToolbarSideCount]; ToolbarDockCallbacks callbacks):
RenderToolbarDock(*this, dockState, sides, callbacks, [this] { ViewportPanel(); });
```

- `ToolbarItem`: `id`, `debugName`, `renderBody` (emits its controls; wrapped in
  `BeginToolbar` for you).
- `ToolbarDockCallbacks`: `onReorder`, `onCrossSideDrop`, `onTearOff`.
- Show torn-off toolbars with `EmitFloatingToolbarWindows(floatingItems, &options)`.
- Classes: `toolbar-side-horz`, `toolbar-side-vert`, `toolbar-item-horz`,
  `toolbar-item-vert`, `toolbar-thumb-horz`, `toolbar-thumb-vert`,
  `toolbar-floating-window`.

---

[Previous: Tree and grid views](trees-and-grids.md) | [Controls](../controls.md) | [Next: Node view](node-view.md)
