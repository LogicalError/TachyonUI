# Tabs, docking and toolbars

## Tabs

### BeginTabBar / Tab

A bar of tabs. `*selectedIndex` is the selected tab; clicking or the arrow keys change
it. The tabs can be dragged into a new order (`onReorder(from, to)`), and with
`onClose(index)` set get a "Close Panel" context menu (Shift+F10 opens it from the
keyboard) and close on Ctrl+W or Ctrl+F4 while focused. A click focuses the tab; Alt+Left /
Right moves the focused one.

What happens to tabs that don't fit on one row is the bar's layout (`TabBarLayout`), which
the context menu of the strip, of any tab or of the "+N" button switches ("Collapse Rows" or
"Multiple Rows"), for that bar only:

- **One row** (the default, "Collapse Rows"): the bar shows the first tabs and a "+N" button that lists the
  rest; picking one there selects it. The selected tab is always in the bar, in place of
  the last ones that fit, so the arrow keys and Home/End also reach the listed tabs. A
  `Tab` that is in the list builds nothing and returns false. Which tabs fit is decided
  from the sizes of the frame before, so a resize settles a frame later.
- **Several rows** ("Multiple Rows"): every tab is shown, wrapping onto as many rows as they take. Dragging
  a tab onto another row puts it there; the arrow keys and Alt + the arrows go on from the
  end of a row to the next.

Pass `layout` to keep the choice yourself; without it the bar keeps it by its ID for the
session. A docked tab group keeps it on its dock node, in the saved layout. `onDragOut` and
`dragVisualizer` work as in the reorderable group. The content under the bar is up to the
app.

`TabBarOptions` changes the rest: `closeLabel` names the context menu's close item ("Close
Panel"), `listHidden = false` leaves out the "+N" button (the caller lists the hidden tabs
another way), `layoutMenu = false` leaves out "Collapse Rows" / "Multiple Rows", and
`dropArea` names an element around the bar that counts as the bar while a tab is dragged (in
it the tab reorders, past the last tab it goes last; only off it is it dragged out).

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

`BeginTab` / `EndTab` is a tab with more in it: its icon and text (in `contentStyle`, or
`tab-content`), then whatever is added before `EndTab`. Every `BeginTab` needs its `EndTab`;
when it returns false the tab is in the "+N" list and nothing should be added to it.

The form without an ID uses the call site. Classes: `tab-bar`, `tab`, `tab-content`,
`tab-overflow` (the "+N" button; its list uses the menu classes). On several rows the bar's
style is used with `flex-wrap: wrap` (`StyleManager::WithFlexWrap`), on one row with `nowrap`.

```cpp
BeginTabBar(idManager.AllocateDynamicID("tabs"), &selectedTab, nullptr);
Tab("First");
Tab("Second");
EndTabBar();
Text(selectedTab == 0 ? "Content of the first tab" : "Content of the second tab");
```


## Docking

### BeginDockingContainer / EndDockingContainer

Panels in a tree of splits and tabbed leaves, held in a `DockingLayout`. Drag a tab to
move a panel, onto another leaf to dock it there, or out of the window to float it.
Panels are created once with `CreatePanel`; each builds its content in its callback.

```cpp
ID   CreatePanel(std::unordered_map<ID, Panel>& panels, std::string title, IconID icon,
                 std::function<void()> onPanelGUI, std::string panelType = {},
                 std::function<void()> onEnable = {}, std::function<void()> onDisable = {});
bool BeginDockingContainer(ID id, DockingLayout* layout, StyleRef style, LayoutOverride&& constraints = {});
bool BeginDockingContainer(ID id, DockingLayout* layout, LayoutOverride&& constraints = {});
void EndDockingContainer();
void EmitFloatingDockWindows();
```

Build the tree with `MakeLeaf({ panelIDs... })` and `MakeSplit(mode, size, first,
second)` from `gui_dock_node.h`; `DockSplitMode` is `LeftPinned`, `RightPinned`,
`TopPinned` or `BottomPinned` (which child keeps the size). Call
`EmitFloatingDockWindows()` after the container; it gives floating panels their own
windows. A panel's `onEnable` / `onDisable` run when it becomes visible or hidden, and
its `cacheSignature` controls the render cache. Closing a panel destroys it.

Other calls: `FloatPanel(layout, panelID, screenPos)`, `CloseDockedPanel(layout, leaf,
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

The level above panels: documents (levels, prefabs, material graphs, layouts, animations),
each with its own `DockingLayout`. A bar of document tabs sits under the title bar; the
active document's panels fill the window below it, and clicking another tab swaps the whole
workspace. Only the active document's panels are built (the others' get `onDisable`) and
only they are drop targets.

Each tab shows the document's icon, its name and, muted, its extension ("Forest" + ".level"),
then its close button on the active or hovered tab, or a dot while `dirty`. Its colors come
from its `kind`: the classes `document-tab-<kind>` (tinted, solid when active),
`document-name-<kind>` (the icon's color) and `document-line-<kind>` (the line under the
bar, in the active document's color). The themes have `level`, `material`, `layout`,
`prefab` and `animation`; a kind without classes uses `document-tab`, `document-name` and
`document-line`. Tabs are reordered by dragging; ones that don't fit are left out, and the
"N open" list on the right names every document. "+" after the tabs calls `onNew` (none
without it).

The close button, "Close Document" in a tab's context menu and Ctrl+W ask `onClose`, then
close the document on the next frame; `onClose` returning false keeps it (to ask about
unsaved changes, then call `CloseDocument`). Closing a document destroys its panels and
closes its torn-off panel windows.

A tab dragged off the bar moves its document: dropped on another document bar (another main
window's, or another set's in the same window) it goes there, where a phantom tab showed it
would; dropped anywhere else, `onTearOff` gets the document, out of the set, and the screen
position, for a new main window of its own (a preview of it follows the cursor). Without
`onTearOff` the tab goes back. Document tabs only drop on document bars and panel tabs only
on panel docks: a document dropped on panels is torn off, a panel dropped on a document bar
floats. Drops are done at the start of the next frame.

The app builds each main window: its title bar and menus, `Documents` for its own
`DocumentSet`, its status bar, then `EmitFloatingDockWindows(set)`, which builds the windows
of the panels torn from that set's documents; they belong to that window, and to the one a
document moves to. A window left with no documents should close. A set must stay alive until
the end of the frame it was built in.

```cpp
Document& CreateDocument(DocumentSet& set, std::string name, std::string extension,
                         std::string kind, IconID icon);
void      CloseDocument(DocumentSet& set, int index);
void      Documents(ID id, DocumentSet& set, LayoutOverride&& constraints = {});
void      EmitFloatingDockWindows(const DocumentSet& documents);
```

`DocumentSet` holds the documents in tab order, `active`, `onNew`, `onClose` and `onTearOff`;
`Active()` is the shown document. Name the active document in the window's title or status
bar. Other classes: `documents`, `document-bar`, `document-row`, `document-tab-bar`,
`document-extension`, `document-mark`, `document-close`, `document-dirty`, `document-new`,
`document-list`.

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

A wrapping row (or column) in which default-style controls use their toolbar variants:
`toolbar-button`, `toolbar-toggle`, `toolbar-textbox`, `toolbar-number-field`,
`toolbar-menu-trigger`.

```cpp
bool BeginToolbar(bool horizontal = true);
void EndToolbar();
```

### RenderToolbarDock

A free function (`controls_toolbar_dock.h`) that lays out toolbars on the four sides of
some content; their items can be dragged between sides or torn off into floating
windows. The app owns the item lists and moves items in the callbacks.

```cpp
void InitializeToolbarDockState(ToolbarDockState& state, GUIApplication& app);
void ReleaseToolbarDockState(ToolbarDockState& state, GUIApplication& app);
void RenderToolbarDock(GUIApplication& app, ToolbarDockState& state,
                       const std::vector<ToolbarItem> (&itemsPerSide)[kToolbarSideCount],
                       const ToolbarDockCallbacks& callbacks, std::function<void()> renderContent);
```

A `ToolbarItem` has an `id`, a `debugName` and a `renderBody` that emits its controls
(wrapped in `BeginToolbar` by the dock). `ToolbarDockCallbacks` has `onReorder`,
`onCrossSideDrop` and `onTearOff`. Torn-off toolbars are shown with
`EmitFloatingToolbarWindows(floatingItems, &options)`. Classes: `toolbar-side-horz`,
`toolbar-side-vert`, `toolbar-item-horz`, `toolbar-item-vert`, `toolbar-thumb-horz`,
`toolbar-thumb-vert`, `toolbar-floating-window`.

---

[← Tree and grid views](trees-and-grids.md) · [Controls](../controls.md) · [Node view →](node-view.md)
