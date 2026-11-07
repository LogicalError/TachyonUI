# Tabs, docking and toolbars

## Tabs

### BeginTabBar / Tab

A tab bar is a row of tabs. The user clicks a tab to show its content. Only one tab is
selected at a time.

- `*selectedIndex` is the index of the selected tab.
- The user changes it with a click or with the arrow keys.
- The content under the bar is yours to build, from `*selectedIndex`.

| Input | Effect |
| --- | --- |
| Drag a tab | Reorders; calls `onReorder(from, to)` |
| `onClose(index)` set | "Close Panel" context menu (Shift+F10), Ctrl+W / Ctrl+F4 close the focused tab |
| Alt+Left / Right | Moves the focused tab (clicking a tab focuses it) |

```cpp
// members: std::vector<std::string> openFiles; int selectedFile = 0
BeginTabBar(&selectedFile,
            [this](int from, int to) {
                std::string moved = std::move(openFiles[size_t(from)]);
                openFiles.erase(openFiles.begin() + from);
                openFiles.insert(openFiles.begin() + to, std::move(moved));
            },
            [this](int index) { openFiles.erase(openFiles.begin() + index); });
for (const std::string& file : openFiles)
    Tab(file);
EndTabBar();
```

When the tabs don't fit in the bar, its `TabBarLayout` decides what happens. The user
switches it per bar, from the context menu of the bar, of a tab or of the "+N" button:

- **One row** ("Collapse Rows", the default): the first tabs that fit, then a "+N" button
  that lists the rest.
  - The selected tab is always shown. It takes the place of the last tabs that fit, so
    the arrow keys and Home/End reach the listed tabs too.
  - A `Tab` that is in the list builds nothing and returns false.
  - What fits is worked out from the previous frame's sizes, so after a resize the bar
    settles one frame later.
- **Several rows** ("Multiple Rows"): all tabs, wrapping onto more rows.
  - Dragging a tab to another row moves it there.
  - The arrow keys and Alt+arrows continue across rows.

Where the choice is kept:

- Pass `layout` to keep the choice yourself.
- Otherwise the bar remembers it by its ID, for as long as the app runs.
- A docked tab group (see [Docking](#docking)) stores it on its dock node, so it's saved
  with the layout.

`onDragOut` and `dragVisualizer` work as in the
[reorderable group](lists.md#beginreorderablegroup--beginreorderableitem):

- `onDragOut` gets a tab released outside the bar.
- `dragVisualizer` runs every frame while a tab is dragged outside the bar, to draw it.

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

A bar that keeps its layout in a member, always on several rows, with its own close label:

```cpp
// members: ID tabsID = idManager.CreateStaticID(); TabBarLayout tabLayout = TabBarLayout::Rows
BeginTabBar(tabsID, &selectedFile, nullptr,
            [this](int index) { openFiles.erase(openFiles.begin() + index); },
            nullptr, nullptr, {}, {}, &tabLayout,
            TabBarOptions{ .closeLabel = "Close File", .layoutMenu = false });
for (const std::string& file : openFiles)
    Tab(file);
EndTabBar();
```

`BeginTab` and `EndTab` are for tabs with more in them than an icon and text.

- `BeginTab` draws the icon and text, in `contentStyle` (default `tab-content`).
- Then comes what you add before `EndTab`.
- Always pair it with `EndTab`.
- If `BeginTab` returns false, the tab is in the "+N" list. Add nothing to it then.

```cpp
if (BeginTab(errorsTabID, "Errors", theme.tabStyleRef, theme.tabContentStyleRef))
    Text(std::to_string(errorCount));   // members: ID errorsTabID; int errorCount
EndTab();
```

- The forms without an ID make one from the place they're called from. See
  [IDs](../overview/ids.md).
- The CSS classes:
  - `tab-bar`
  - `tab`
  - `tab-content`
  - `tab-overflow` for the "+N" button. Its list uses the menu classes.
- In the several-rows layout the bar's style gets `flex-wrap: wrap`
  (`StyleManager::WithFlexWrap`). In the one-row layout it gets `nowrap`.

```cpp
BeginTabBar(idManager.AllocateDynamicID("tabs"), &selectedTab, nullptr);
Tab("First");
Tab("Second");
EndTabBar();
Text(selectedTab == 0 ? "Content of the first tab" : "Content of the second tab");
```


## Docking

### BeginDockingContainer / EndDockingContainer

Docking lets the user arrange the parts of a window, called panels, by dragging them
around. A panel is a part of the UI with a title, such as "Scene" or "Log".

- The panels sit in a tree of splits, stored in a `DockingLayout`.
  - A split divides its space between two children, side by side or one above the other.
  - Each node of the tree is a dock node: a split, or a leaf.
  - Each leaf is a tab group: a tab bar with one tab per panel, showing the selected one.
- The user drags a panel's tab to:
  - move it within its tab group
  - dock it on another leaf
  - float it outside the window, in a window of its own
- Create each panel once, with `CreatePanel`. Its callback builds the panel's content.

```cpp
ID   CreatePanel(std::unordered_map<ID, Panel>& panels, std::string title, IconID icon,
                 std::function<void()> onPanelGUI, std::string panelType = {},
                 std::function<void()> onEnable = {}, std::function<void()> onDisable = {});
bool BeginDockingContainer(ID id, DockingLayout* layout, StyleRef style, LayoutOverride&& constraints = {});
bool BeginDockingContainer(ID id, DockingLayout* layout, LayoutOverride&& constraints = {});
void EndDockingContainer();
void EmitFloatingDockWindows();
```

- Build the tree with `MakeLeaf` and `MakeSplit` (`gui_dock_node.h`).
  - `MakeLeaf({ panelIDs... })` makes a tab group of those panels.
  - `MakeSplit(mode, size, first, second)` makes a split of two nodes.
  - `DockSplitMode` says which child keeps `size`: `LeftPinned`, `RightPinned`,
    `TopPinned` or `BottomPinned`.
- Call `EmitFloatingDockWindows()` after the container. It builds the windows of the
  floating panels.
- Closing a panel destroys it.
- The CSS classes:
  - `dock-container`
  - `dock-leaf`
  - `dock-content`
  - `dock-split-horz` and `dock-split-vert`
  - `dock-sizer-horz` and `dock-sizer-vert` for the handles between the halves of a split
  - `dock-window` for a floating panel's window
  - `panel-drop-visualization` for the highlight where a dragged panel will land

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

`onEnable` and `onDisable` run when a panel becomes visible and when it's hidden. A
hidden panel isn't built, so it can let go of what it made:

```cpp
const ID previewPanel = CreatePanel(layout.panels, "Preview", IconID::Invalid, [this] { PreviewPanel(); }, {},
                                    [this] { LoadPreview(); },      // shown
                                    [this] { UnloadPreview(); });   // hidden
```

Docked panels are cached: their drawn output is kept and reused while nothing in them
changes (see [BeginCached](windows-and-layout.md#begincached--endcached)). A panel's
`cacheSignature` controls this:

- Return a value to also rebuild the panel whenever that value changes. Use it for data
  the cache can't see change.
- Return `std::nullopt` to not cache the panel this frame, for a panel that changes every
  frame.

```cpp
layout.panels[log].cacheSignature = [this] { return std::optional<uint64_t>(logLines.size()); };
```

To change the layout from code:

- `FloatPanel(layout, panelID, screenPos)` moves a panel into a window of its own.
- `CloseDockedPanel(layout, leaf, tabIndex)` closes a docked panel.
- `CloseFloatingDockWindow(index)` closes a floating window and its panels.
- `DisableAllPanels(layout)` hides every panel, for example before a theme reload.

```cpp
(void)FloatPanel(layout, log, int2{ styleManager.Px(200), styleManager.Px(200) });
```

### Documents

Documents are the files the user edits, such as levels, prefabs or graphs. Each document
has its own `DockingLayout`, with its own panels.

- A document tab bar sits under the title bar, with one tab per open document.
- The active document's panels fill the window below it.
- Only the active document's panels are built and accept drops. The others get
  `onDisable`.

Each tab shows:

- the document's icon
- its name
- its extension, muted ("Forest" + ".level")
- on the active or hovered tab, a close button
- for a `dirty` document, a dot in place of the close button, meaning it has unsaved
  changes

The tab colors come from the document's `kind`:

| Class | Colors |
| --- | --- |
| `document-tab-<kind>` | Tab tint; solid fill when active |
| `document-name-<kind>` | Icon |
| `document-line-<kind>` | Line under the bar (active document's color) |

- The themes define the kinds `level`, `material`, `layout`, `prefab` and `animation`.
- Other kinds fall back to `document-tab`, `document-name` and `document-line`.

What the user can do:

- Drag a tab to reorder.
  - Tabs that don't fit are hidden. The "N open" list names them all.
- Click "+" to call `onNew`.
  - The "+" is hidden if `onNew` is unset.
- Close a document with its close button, "Close Document" in the context menu, or
  Ctrl+W.
  - This calls `onClose` first. The document then closes in the next frame.
  - If `onClose` returns false, the document stays open. Ask about unsaved changes there,
    and call `CloseDocument` yourself.
  - Closing a document destroys its panels and their torn-off windows.
- Drag a tab off the bar.
  - Dropped on another document bar, in any main window, the document moves there, at
    the spot of the placeholder tab shown during the drag.
  - Dropped elsewhere, `onTearOff` gets the document, already removed from its set, and
    the screen position. Use them to open a new main window.
  - Without `onTearOff`, the tab goes back.
- Document tabs drop only on document bars, panel tabs only on panel docks.
  - A document dropped on panels is torn off.
  - A panel dropped on a document bar floats.
  - Drops apply at the start of the next frame.

```cpp
documents.onNew = [this] {
    Document& created = CreateDocument(documents, "Untitled", ".level", "level", levelIcon);
    created.layout.root = MakeLeaf({ CreatePanel(created.layout.panels, "Scene", IconID::Invalid, [this] { ScenePanel(); }) });
    documents.active = int(documents.documents.size()) - 1;
};
documents.onClose = [this](Document& document) {
    if (!document.dirty)
        return true;
    closingDocument = &document;   // member: Document* closingDocument; ask in OnGUI, then CloseDocument
    return false;
};
```

Each main window is built by your app, in this order:

1. the title bar and its menus
2. `Documents`, for the window's `DocumentSet`
3. the status bar
4. `EmitFloatingDockWindows(set)`, which builds the windows of the panels torn off from
   that set. They belong to that main window, and follow a document that moves to
   another one.

The bar can also go in the title bar, with the menus under it, so the menus sit with the
document they act on:

- `DocumentBar(id, set, theme.titleBarDocumentsStyleRef)` draws only the bar: the tabs,
  "+", "N open" and the line in the active document's color.
  - Put it in the title bar, after its other items.
  - The bar's empty space still drags the window.
- `DocumentPanels(set)` draws the active document's panels, after the bar.
- `Documents(id, set)` is the two together, the bar on top.

```cpp
BeginTitleBar(titleBarID);
Text(appIconID, "T", appIconStyle);
DocumentBar(documentsID, documents, theme.titleBarDocumentsStyleRef);
EndTitleBar();

BeginMenuBar(menuBarID);
// ... File, Edit, View ...
EndMenuBar();

DocumentPanels(documents);
```

- Close a main window when no documents are left in it.
- A set must stay alive until the end of the frame it was built in.

Saving:

- `PersistDocuments(key, windows)` takes a function that lists the main windows and their
  sets (`DocumentWindowRef`).
- `SaveUIState` then saves:
  - each window's position
  - the order of its documents
  - its active document
  - each document's name, extension, kind and dock tree

Restoring needs your app, since the library can't create your panels:

- After `RestoreUIState`, call `TakeSavedDocuments(key)`.
- For each window:
  - create its documents with their panels
  - lay each one out with `RestoreDocumentLayout`
  - set `active`
- For extra windows, call `RestoreWindowPlace` before declaring them.

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
void      DocumentBar(ID id, DocumentSet& set, StyleRef style, LayoutOverride&& constraints = LayoutOverride::Auto());
void      DocumentPanels(DocumentSet& set, LayoutOverride&& constraints = LayoutOverride::Auto());
void      EmitFloatingDockWindows(const DocumentSet& documents);
```

- `DocumentSet` holds:
  - the documents, in tab order
  - `active`, the index of the shown document
  - `onNew`, `onClose` and `onTearOff`
- `Active()` returns the shown document.
- The other CSS classes:
  - `documents`
  - `document-bar`
  - `document-row`
  - `document-tab-bar`
  - `document-extension`
  - `document-mark`
  - `document-close`
  - `document-dirty`
  - `document-new`
  - `document-list`
  - `title-bar-documents`, for a bar in a title bar

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

### Layout presets

A layout preset is a named arrangement of a document's panels, such as "Scene" or
"Graph". The user picks one for the document they're in, and the panels move to where the
preset puts them.

- A preset is a dock tree as text.
  - `SaveDockLayout(layout)` writes a layout's tree.
  - Panels are named in it by their `panelType`, or by their title when they have none.
  - Floating panels aren't in it.
- `ApplyDockLayout(layout, tree, makePanel)` lays a layout out as a tree says.
  - A panel the tree names that the layout already has stays, and moves to its new place.
  - A panel the tree names that the layout lacks is made by `makePanel`. It gets the
    panel's type or title, creates the panel in `layout.panels`, and returns its ID.
  - A panel the layout has that the tree doesn't name is closed, as closing its tab does.
  - It returns false, and changes nothing, when the tree can't be read or no panel is left.
  - Call it outside the layout's `Docking`, for example at the start of the frame.

```cpp
const std::string tree = SaveDockLayout(document.layout);

// Later, on any document:
(void)ApplyDockLayout(other.layout, tree, [this, &other](std::string_view title) -> std::optional<ID> {
    return MakeMyPanel(other.layout, title);   // CreatePanel by title
});
```

`LayoutPresets` keeps the presets:

- Built-in presets are the app's own.
  - Add them with `AddBuiltIn(name, tree)`.
  - They come first, and can't be replaced or deleted.
- Saved presets are the user's.
  - `Save(name, tree)` adds one, or replaces the saved one of that name.
  - It returns false for an empty name, or the name of a built-in preset.
  - `Remove(name)` deletes one.
- `All()` lists them all, built-in ones first, each in the order added.
- `Find(name)` returns the preset of that name, or null.

```cpp
LayoutPresets presets;
presets.AddBuiltIn("Graph", graphTree);
presets.Save("My layout", SaveDockLayout(document.layout));
for (const LayoutPreset& preset : presets.All())
    MenuItem(preset.name, [this, name = preset.name] { pendingPreset = name; });
```

A file type can open with a preset, so a file the user opens (a `.mat` file
double-clicked in a file view, say) gets its type's layout:

- `SetForFileType(".mat", "Graph")` makes `.mat` documents open with "Graph".
  - The extension's case doesn't matter.
  - An empty name stops it.
  - Deleting a saved preset stops the file types that used it.
- `ForFileType(extension)` returns that type's preset, or null when it has none.

```cpp
// A file opened: a document with its type's layout.
Document& document = CreateDocument(documents, name, ".mat", "material", materialIcon);
if (const LayoutPreset* preset = presets.ForFileType(document.extension))
    (void)ApplyDockLayout(document.layout, preset->tree, makePanel);
```

`PersistLayoutPresets(key, presets)` keeps the saved presets and the file types in the
saved UI state. Add the built-in presets yourself, before or after `RestoreUIState`.

The showcase:

- Has a "Layouts" dropdown in its title bar (`BeginDropdownMenu`, see [Menus](menus.md)).
  - It lists the presets; picking one lays out the active document.
  - "Save layout as..." asks for a name with a `MessageDialog` text field (see
    [Tooltips and dialogs](tooltips-and-dialogs.md)).
  - "Delete layout" deletes a saved preset.
- Opens `.level` files with "Scene" and `.mat` files with "Graph": double-click one in the
  Files panel's `documents` folder (see `GridViewVirtualState::onOpen` in
  [Trees and grids](trees-and-grids.md#virtualgridview)).

```cpp
// LayoutPresets (gui_layout_presets.h)
void                AddBuiltIn(std::string name, std::string tree);
bool                Save(std::string name, std::string tree);
bool                Remove(std::string_view name);
const LayoutPreset* Find(std::string_view name) const;
const std::vector<LayoutPreset>& All() const;
void                SetForFileType(std::string_view extension, std::string name);
const LayoutPreset* ForFileType(std::string_view extension) const;

// GUIApplication
std::string SaveDockLayout(const DockingLayout& layout) const;
bool        ApplyDockLayout(DockingLayout& layout, std::string_view tree,
                            const std::function<std::optional<ID>(std::string_view key)>& makePanel = {});
void        PersistLayoutPresets(std::string key, LayoutPresets& presets);
```


## Toolbars

### BeginToolbar / EndToolbar

A toolbar is a row of small buttons and fields for frequent actions. It wraps onto more
rows when it runs out of room, or it can be a column.

Controls with their default style use their toolbar versions inside it:

- `toolbar-button`
- `toolbar-toggle`
- `toolbar-textbox`
- `toolbar-number-field`
- `toolbar-menu-trigger`

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

A toolbar standing as a column:

```cpp
BeginToolbar(false);
Button(moveIcon, [this] { tool = Tool::Move; });
Button(rotateIcon, [this] { tool = Tool::Rotate; });
EndToolbar();
```

### RenderToolbarDock

A free function, not a member of `GUIApplication` (`controls_toolbar_dock.h`). It places
toolbars on the four sides of some content.

- The user drags toolbars between the sides, or tears them off into floating windows.
- Your app owns the lists of toolbars, and moves them in the callbacks. The dock never
  changes them itself.

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

- `ToolbarItem` is one toolbar:
  - `id`
  - `debugName`, a stable name. Saved UI state places the toolbars by it.
  - `renderBody`, which emits its controls. It's wrapped in `BeginToolbar` for you.
- `ToolbarDockCallbacks` has three callbacks:
  - `onReorder`: a toolbar moved within its side
  - `onCrossSideDrop`: a toolbar was dropped on another side
  - `onTearOff`: a toolbar was released outside every side
- Show the torn-off toolbars with `EmitFloatingToolbarWindows(floatingItems, &options)`.
- The CSS classes:
  - `toolbar-side-horz` and `toolbar-side-vert`
  - `toolbar-item-horz` and `toolbar-item-vert`
  - `toolbar-thumb-horz` and `toolbar-thumb-vert` for the handle the user drags
  - `toolbar-floating-window`

```cpp
callbacks.onReorder = [this](ToolbarSide side, int from, int to) {
    std::vector<ToolbarItem>& items = sides[int(side)];
    ToolbarItem moved = std::move(items[size_t(from)]);
    items.erase(items.begin() + from);
    items.insert(items.begin() + to, std::move(moved));
};
callbacks.onCrossSideDrop = [this](ToolbarSide from, int index, ToolbarSide to) {
    sides[int(to)].push_back(std::move(sides[int(from)][size_t(index)]));
    sides[int(from)].erase(sides[int(from)].begin() + index);
};
callbacks.onTearOff = [this](ToolbarSide from, int index, int2 at) {
    // member: std::vector<FloatingToolbarItem> floatingToolbars
    floatingToolbars.push_back(FloatingToolbarItem{ .item = std::move(sides[int(from)][size_t(index)]),
        .windowID = idManager.CreateStaticID(), .spawnPosition = at, .homeSide = from });
    sides[int(from)].erase(sides[int(from)].begin() + index);
};

// Each frame, inside the main window:
EmitFloatingToolbarWindows(floatingToolbars);
```

---

[Previous: Tree and grid views](trees-and-grids.md) | [Controls](../controls.md) | [Next: Node view](node-view.md)
