# Tree and grid views

## Tree views

### VirtualTreeView

A single-column, virtualized tree over flat rows that the app owns: a `VirtualTable`
with one `TreeCell` column. A non-empty `header` shows a header row.

```cpp
void VirtualTreeView(ID id, std::vector<TreeViewRow>& rows, std::vector<int>& visibleIdx, TableState& state,
                     StyleRef style = {}, LayoutOverride&& constraints = {}, std::string_view header = {});
static void RebuildTreeViewIndex(std::vector<TreeViewRow>& rows);
static void WalkTreeViewVisibility(const std::vector<TreeViewRow>& rows, std::vector<int>& visibleIdx);
ID TreeDropTargetRow() const;
```

`TreeViewRow` fields: `id`, `name`, `depth`, `isOpen`, `icon`, `renameBuffer`; the rest
is derived by `RebuildTreeViewIndex`, which you call after adding, removing or moving
rows. Rows are in depth-first order. `visibleIdx` is filled each frame with the flat
indices of the visible rows. Selection, scrolling and drag and drop live on the
`TableState`: the selected row's flat index is
`visibleIdx[state.selectedRowID.value.subRangeParts.index]`. Default class: `tree-view`;
rows use `list-row`.

```cpp
RebuildTreeViewIndex(rows);   // after structural changes
VirtualTreeView(treeID, rows, visibleIdx, treeState, {}, LayoutOverride{}.FlexGrow(1));
```

For a tree with more columns, use `VirtualTable` with `TreeCell` in the first column.


## Grid views

### BeginGridView / GridViewItem

A 2D grid of items, like a file explorer. `selectedIndex` takes a single selection;
`MultiSelectState` takes several (Ctrl and Shift clicks); `ItemDragDropConfig` makes
items draggable.

```cpp
bool BeginGridView(ID id, int* selectedIndex, StyleRef style, MultiSelectState* multiSelect = nullptr,
                   ItemDragDropConfig* dragDrop = nullptr, LayoutOverride&& constraints = {});
bool BeginGridView(ID id, int* selectedIndex = nullptr, MultiSelectState* multiSelect = nullptr,
                   ItemDragDropConfig* dragDrop = nullptr, LayoutOverride&& constraints = {});
void EndGridView();
bool GridViewItem(ID id, const GUIContent& content, ElementState extraState = ElementState::None,
                  std::string* renameBuffer = nullptr, StyleRef itemStyle = {}, LayoutOverride&& constraints = {});
bool IsRenaming() const;
```

`renameBuffer` enables in-place rename (F2, Ctrl+Shift+Click or a slow second click;
Enter commits, Escape or focus loss cancels). `IsRenaming` says whether a rename editor
is open. `GridViewItem` returns `true` in the frame after a click. Classes:
`grid-view`, `grid-view-item`, `grid-view-list-item`, `grid-view-rename-*`.

### VirtualGridView

A grid that builds only the visible rows. `generateItem(index, itemID)` must emit
exactly one cell, usually `GridViewItem(itemID, ...)`; `itemID` is a stable ranged
sub-ID used for selection and drag payloads. Cells are `cellSize` pixels.

```cpp
void VirtualGridView(ID id, GridViewVirtualState& vstate, int itemCount, int2 cellSize,
                     const std::function<void(int index, ID itemID)>& generateItem,
                     int* selectedIndex = nullptr, MultiSelectState* multiSelect = nullptr,
                     ItemDragDropConfig* dragDrop = nullptr, StyleRef scrollStyle = {}, StyleRef rowStyle = {},
                     LayoutOverride&& constraints = {});
```

The column count comes from last frame's viewport. `vstate.cellStretchedW` is the cell
width that makes the columns exactly fill it. Classes: `grid-view-scroll`,
`grid-view-row`.

```cpp
VirtualGridView(gridID, gridState, int(files.size()), int2{ styleManager.Px(96), styleManager.Px(96) },
                [this](int i, ID itemID) { GridViewItem(itemID, { files[i].icon, files[i].name }); },
                &selectedFile);
```

## File views

A tree and a grid of files read from an `IFileSystem` (`gui_controls/gui_file_system.h`):
`List(path)` gives a folder's entries, `Move` and `Rename` change them, and `Generation()`
changes whenever a list may have, so a view knows when to read again. Paths are relative
to the file system's root, with `/` between names; `""` is the root. A refused move or
rename returns false with the reason in `error`, ready to show.

`MemoryFileSystem` keeps it all in memory: filled with `AddFolder` / `AddFile`, or a copy
of a folder on the disk, each folder read the first time it's needed. Its moves and
renames change only the copy, so a demo can browse the real disk without writing to it.
Implement `IFileSystem` over the disk itself, an archive or an asset database to change
those instead.

```cpp
MemoryFileSystem files(std::filesystem::path("C:/Projects"));
std::vector<std::string> names;
for (const FileEntry& entry : files.List("TachyonUI"))
    names.push_back(entry.isFolder ? entry.name + "/" : entry.name);
std::string error;
if (!files.Move("TachyonUI/notes.txt", "Archive", error))
    ShowToast(ToastDesc{ .severity = Severity::Error, .title = "Can't move it", .body = error });
```

`FileIcons` (`gui_controls/gui_file_icons.h`) gives the OS's icon for a file or folder:
the Windows shell's, one per extension, read once, at the size asked for or the next one
up. It's `IconID::Invalid` elsewhere, and a view shows its own icons then.

```cpp
FileIcons icons(*textSystem, 48);
const IconID icon = icons.Get("photo.jpg", false);
```

The icons are pictures: `ITextSystem::RegisterIconFromPixels(name, size, rgba)` makes an
icon of RGBA pixels, drawn in its own colors (not tinted by the style's icon color, and
without text shadows), fit to its box keeping its shape, on its own and in text.

---

[← Tables](tables.md) · [Controls](../controls.md) · [Tabs, docking and toolbars →](tabs-docking-toolbars.md)
