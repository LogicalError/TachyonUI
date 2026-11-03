# Tree and grid views

## Tree views

### VirtualTreeView

A single-column tree that only builds the visible rows. Your app owns the rows as a flat
list. Under the hood it's a `VirtualTable` with one `TreeCell` column. If you pass a
non-empty `header`, it shows a header row.

```cpp
void VirtualTreeView(ID id, std::vector<TreeViewRow>& rows, std::vector<int>& visibleIdx, TableState& state,
                     StyleRef style = {}, LayoutOverride&& constraints = {}, std::string_view header = {});
static void RebuildTreeViewIndex(std::vector<TreeViewRow>& rows);
static void WalkTreeViewVisibility(const std::vector<TreeViewRow>& rows, std::vector<int>& visibleIdx);
ID TreeDropTargetRow() const;
```

You fill in these `TreeViewRow` fields: `id`, `name`, `depth`, `isOpen`, `icon` and
`renameBuffer`. `RebuildTreeViewIndex` works out the rest, so call it whenever you add,
remove or move rows. Keep the rows in depth-first order.

Each frame, `visibleIdx` is filled with the flat indices of the rows that are visible.
Selection, scrolling and drag and drop are handled through the `TableState`. To get the
selected row's flat index, use `visibleIdx[state.selectedRowID.value.subRangeParts.index]`.
The default class is `tree-view`, and rows use `list-row`.

```cpp
RebuildTreeViewIndex(rows);   // after structural changes
VirtualTreeView(treeID, rows, visibleIdx, treeState, {}, LayoutOverride{}.FlexGrow(1));
```

If you need a tree with more than one column, use a `VirtualTable` with `TreeCell` in the
first column.


## Grid views

### BeginGridView / GridViewItem

A 2D grid of items, like a file explorer. Pass `selectedIndex` for single selection, or a
`MultiSelectState` to let the user select several items with Ctrl and Shift clicks. An
`ItemDragDropConfig` makes the items draggable.

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

Pass a `renameBuffer` to allow renaming in place. The user starts a rename with F2,
Ctrl+Shift+Click or a slow second click. Enter commits it, and Escape or losing focus
cancels it. `IsRenaming` tells you whether a rename editor is open. `GridViewItem` returns
`true` in the frame after the item was clicked. Classes: `grid-view`, `grid-view-item`,
`grid-view-list-item`, `grid-view-rename-*`.

### VirtualGridView

A grid that only builds the visible rows. Your `generateItem(index, itemID)` must emit
exactly one cell, usually with `GridViewItem(itemID, ...)`. The `itemID` is a stable ranged
sub-ID that's used for selection and drag payloads. Each cell is `cellSize` pixels.

```cpp
void VirtualGridView(ID id, GridViewVirtualState& vstate, int itemCount, int2 cellSize,
                     const std::function<void(int index, ID itemID)>& generateItem,
                     int* selectedIndex = nullptr, MultiSelectState* multiSelect = nullptr,
                     ItemDragDropConfig* dragDrop = nullptr, StyleRef scrollStyle = {}, StyleRef rowStyle = {},
                     LayoutOverride&& constraints = {});
```

The number of columns is based on the previous frame's viewport.
`vstate.cellStretchedW` gives you the cell width at which the columns fill the viewport
exactly. Classes: `grid-view-scroll`, `grid-view-row`.

```cpp
VirtualGridView(gridID, gridState, int(files.size()), int2{ styleManager.Px(96), styleManager.Px(96) },
                [this](int i, ID itemID) { GridViewItem(itemID, { files[i].icon, files[i].name }); },
                &selectedFile);
```

## File views

The file views show a tree and a grid of files read from an `IFileSystem`
(`gui_controls/gui_file_system.h`). The interface is small:

- `List(path)` returns a folder's entries.
- `Move` and `Rename` change them.
- `Import` copies a file or folder in from disk, such as one dropped from another
  application.
- `Generation()` changes whenever a listing may have changed, so a view knows when to read
  it again.

Paths are relative to the file system's root and use `/` between names. `""` is the root.
When a move or rename is refused, the call returns false and puts the reason in `error`,
ready to show to the user.

`MemoryFileSystem` keeps everything in memory. You can fill it with `AddFolder` /
`AddFile`, or make it a copy of a folder on disk, where each folder is read the first time
it's needed. Moves, renames and imports only change the copy, and an imported folder is
read as the user browses it. That makes it handy for a demo that browses the real disk
without writing to it. If you want changes to reach the disk itself, an archive or an
asset database, implement `IFileSystem` over that instead.

```cpp
MemoryFileSystem files(std::filesystem::path("C:/Projects"));
std::vector<std::string> names;
for (const FileEntry& entry : files.List("TachyonUI"))
    names.push_back(entry.isFolder ? entry.name + "/" : entry.name);
std::string error;
if (!files.Move("TachyonUI/notes.txt", "Archive", error))
    ShowToast(ToastDesc{ .severity = Severity::Error, .title = "Can't move it", .body = error });
```

`FileIcons` (`gui_controls/gui_file_icons.h`) gets the operating system's icon for a file
or folder. On Windows these are the shell's icons, one per extension, read once, at the
size you ask for or the next size up. On other systems it returns `IconID::Invalid`, and
the view shows its own icons instead.

```cpp
FileIcons icons(*textSystem, 48);
const IconID icon = icons.Get("photo.jpg", false);
```

These icons are pictures rather than glyphs. `ITextSystem::RegisterIconFromPixels(name,
size, rgba)` makes an icon from RGBA pixels. It's drawn in its own colors, so the style's
icon color doesn't tint it and it gets no text shadow. It's scaled to fit its box while
keeping its shape, both on its own and inside text.

---

[Previous: Tables](tables.md) | [Controls](../controls.md) | [Next: Tabs, docking and toolbars](tabs-docking-toolbars.md)
