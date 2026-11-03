# Tabs, docking and toolbars

## Tabs

### BeginTabBar / Tab

A bar of tabs. `*selectedIndex` holds the selected tab, and the user changes it by
clicking or with the arrow keys. Tabs can be dragged into a new order, which calls
`onReorder(from, to)`. If you set `onClose(index)`, each tab also gets a "Close Panel"
context menu (Shift+F10 opens it from the keyboard), and Ctrl+W or Ctrl+F4 closes the
focused tab. Clicking a tab focuses it, and Alt+Left / Right moves the focused tab.

When the tabs don't fit on one row, the bar's layout (`TabBarLayout`) decides what
happens. The user can switch it per bar from the context menu of the strip, of any tab or
of the "+N" button, by choosing "Collapse Rows" or "Multiple Rows":

- **One row** ("Collapse Rows") is the default. The bar shows the first tabs and a "+N"
  button that lists the rest, and picking one from that list selects it. The selected tab
  is always in the bar, taking the place of the last tabs that fit, so the arrow keys and
  Home/End can reach the listed tabs too. A `Tab` that's in the list builds nothing and
  returns false. Which tabs fit is worked out from the previous frame's sizes, so after a
  resize the bar settles one frame later.
- **Several rows** ("Multiple Rows") shows every tab, wrapping onto as many rows as it
  needs. Dragging a tab onto another row moves it there. The arrow keys and Alt + the
  arrows carry on from the end of one row to the start of the next.

If you want to keep track of the choice yourself, pass `layout`. Without it, the bar
remembers the choice by its ID for the rest of the session. A docked tab group stores it
on its dock node, so it's part of the saved layout. `onDragOut` and `dragVisualizer` work
the same way as in the reorderable group. What goes under the bar is up to your app.

`TabBarOptions` covers the remaining settings:

- `closeLabel` is the text of the context menu's close item ("Close Panel").
- `listHidden = false` leaves out the "+N" button, for when you list the hidden tabs some
  other way.
- `layoutMenu = false` leaves out the "Collapse Rows" / "Multiple Rows" choice.
- `dropArea` names an element around the bar that counts as part of the bar while a tab is
  being dragged. Inside it the tab is reordered, and past the last tab it goes to the end.
  Only outside it is the tab dragged out.

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

Use `BeginTab` / `EndTab` when a tab needs more than an icon and text. It draws the icon
and text (in `contentStyle`, or `tab-content` by default), followed by whatever you add
before `EndTab`. Every `BeginTab` needs a matching `EndTab`. When `BeginTab` returns false,
the tab is in the "+N" list and you shouldn't add anything to it.

The forms without an ID use the call site as the ID. Classes: `tab-bar`, `tab`,
`tab-content` and `tab-overflow` (the "+N" button, whose list uses the menu classes). In
the several-rows layout the bar's style is used with `flex-wrap: wrap`
(`StyleManager::WithFlexWrap`), and in the one-row layout with `nowrap`.

```cpp
BeginTabBar(idManager.AllocateDynamicID("tabs"), &selectedTab, nullptr);
Tab("First");
Tab("Second");
EndTabBar();
Text(selectedTab == 0 ? "Content of the first tab" : "Content of the second tab");
```


## Docking

### BeginDockingContainer / EndDockingContainer

A docking container arranges panels in a tree of splits, where each leaf holds a group of
tabs. The tree lives in a `DockingLayout`. The user can drag a tab to move a panel, drop it
on another leaf to dock it there, or drag it out of the window to float it. You create each
panel once with `CreatePanel`, and it builds its content in its callback.

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
second)` from `gui_dock_node.h`. `DockSplitMode` is `LeftPinned`, `RightPinned`,
`TopPinned` or `BottomPinned`, which says which child keeps the given size. Call
`EmitFloatingDockWindows()` after the container to give floating panels their own windows.

A panel's `onEnable` and `onDisable` run when it becomes visible or hidden, and its
`cacheSignature` controls the render cache. Closing a panel destroys it.

Other calls you can use: `FloatPanel(layout, panelID, screenPos)`,
`CloseDockedPanel(layout, leaf, tabIndex)`, `CloseFloatingDockWindow(index)` and
`DisableAllPanels(layout)`.

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

Documents sit one level above panels. Think of levels, prefabs, material graphs, layouts or
animations, each with its own `DockingLayout`. A bar of document tabs sits under the title
bar, and the active document's panels fill the window below it. Clicking another tab swaps
out the whole workspace. Only the active document's panels are built (the others get
`onDisable`), and only they accept drops.

Each tab shows the document's icon and its name, followed by its extension in a muted
color ("Forest" + ".level"). The active or hovered tab also shows a close button, and a
document that's `dirty` shows a dot instead.

A tab's colors come from the document's `kind`, through three classes:

- `document-tab-<kind>` tints the tab, and fills it solid when it's active.
- `document-name-<kind>` sets the icon's color.
- `document-line-<kind>` colors the line under the bar, in the active document's color.

The themes define `level`, `material`, `layout`, `prefab` and `animation`. A kind without
its own classes falls back to `document-tab`, `document-name` and `document-line`.

The user reorders tabs by dragging. Tabs that don't fit are left out, but the "N open"
list on the right names every document. The "+" after the tabs calls `onNew`, and it isn't
shown if you don't set `onNew`.

The close button, "Close Document" in a tab's context menu, and Ctrl+W all ask `onClose`
first and then close the document on the next frame. If `onClose` returns false, the
document stays open. That's where you ask about unsaved changes, and then call
`CloseDocument` yourself. Closing a document destroys its panels and closes any of its
panel windows that were torn off.

Dragging a tab off the bar moves its document. If it's dropped on another document bar
(in another main window, or another set in the same window), it goes there, at the spot
where a phantom tab showed it would land. If it's dropped anywhere else, `onTearOff` gets
the document, already removed from its set, and the screen position, so you can give it a
new main window of its own. A preview of the window follows the cursor while dragging.
Without `onTearOff`, the tab goes back where it was.

Document tabs only drop onto document bars, and panel tabs only onto panel docks. A
document dropped on panels is torn off, and a panel dropped on a document bar floats. Drops
take effect at the start of the next frame.

Your app builds each main window itself: its title bar and menus, `Documents` for its own
`DocumentSet`, its status bar, and then `EmitFloatingDockWindows(set)`. That last call
builds the windows for panels torn off from that set's documents. Those windows belong to
that main window, and they move along with a document when it moves to another window. A
window that has no documents left should close. A set must stay alive until the end of the
frame it was built in.

You can keep the open documents between runs. `PersistDocuments(key, windows)` takes a
function that lists the main windows and their sets (`DocumentWindowRef`). `SaveUIState`
then saves each window's position, the document order, the active document and, for each
document, its name, extension, kind and dock tree.

The library can't create documents for you, because their panels belong to your app. So
after `RestoreUIState`, get them with `TakeSavedDocuments(key)`. For each window, create
its documents with their panels, lay each one out with `RestoreDocumentLayout`, and set
`active`. For any extra window, call `RestoreWindowPlace` before declaring it so it opens
where it was.

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

A `DocumentSet` holds the documents in tab order, plus `active`, `onNew`, `onClose` and
`onTearOff`. `Active()` returns the document that's shown. It's a good idea to name the
active document in the window's title or status bar. Other classes: `documents`,
`document-bar`, `document-row`, `document-tab-bar`, `document-extension`,
`document-mark`, `document-close`, `document-dirty`, `document-new`, `document-list`.

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

A row (or column) that wraps. Inside it, controls that use their default style switch to
their toolbar versions: `toolbar-button`, `toolbar-toggle`, `toolbar-textbox`,
`toolbar-number-field` and `toolbar-menu-trigger`.

```cpp
bool BeginToolbar(bool horizontal = true);
void EndToolbar();
```

### RenderToolbarDock

A free function (`controls_toolbar_dock.h`) that lays out toolbars on the four sides of
some content. The user can drag toolbar items between sides or tear them off into
floating windows. Your app owns the item lists and moves the items in the callbacks.

```cpp
void InitializeToolbarDockState(ToolbarDockState& state, GUIApplication& app);
void ReleaseToolbarDockState(ToolbarDockState& state, GUIApplication& app);
void RenderToolbarDock(GUIApplication& app, ToolbarDockState& state,
                       const std::vector<ToolbarItem> (&itemsPerSide)[kToolbarSideCount],
                       const ToolbarDockCallbacks& callbacks, std::function<void()> renderContent);
```

A `ToolbarItem` has an `id`, a `debugName` and a `renderBody` that emits its controls. The
dock wraps each body in `BeginToolbar` for you. `ToolbarDockCallbacks` has `onReorder`,
`onCrossSideDrop` and `onTearOff`. To show the torn-off toolbars, call
`EmitFloatingToolbarWindows(floatingItems, &options)`. Classes: `toolbar-side-horz`,
`toolbar-side-vert`, `toolbar-item-horz`, `toolbar-item-vert`, `toolbar-thumb-horz`,
`toolbar-thumb-vert`, `toolbar-floating-window`.

---

[Previous: Tree and grid views](trees-and-grids.md) | [Controls](../controls.md) | [Next: Node view](node-view.md)
