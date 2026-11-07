# Tree and grid views

## Tree views

A tree view shows items that contain other items, like folders in folders. The user opens
an item with its arrow to see the items inside it, its children.

### VirtualTreeView

A single-column tree that builds only the rows on screen.

- Your app owns the rows, as one flat list (`TreeViewRow`), not as nested objects.
- It's a [`VirtualTable`](tables.md#virtualtable) with one [`TreeCell`](tables.md#treecell)
  column.
- A non-empty `header` shows a header row above the tree.

```cpp
void VirtualTreeView(ID id, std::vector<TreeViewRow>& rows, std::vector<int>& visibleIdx, TableState& state,
                     StyleRef style = {}, LayoutOverride&& constraints = {}, std::string_view header = {});
static void RebuildTreeViewIndex(std::vector<TreeViewRow>& rows);
static void WalkTreeViewVisibility(const std::vector<TreeViewRow>& rows, std::vector<int>& visibleIdx);
ID TreeDropTargetRow() const;
```

The rows:

- Set these `TreeViewRow` fields:
  - `id`
  - `name`
  - `depth`: 0 for a top-level item, 1 for its children, and so on
  - `isOpen`
  - `icon`
  - `renameBuffer`, to let the user rename the row in place
- Keep the rows in depth-first order: each item comes right before its children.
- Call `RebuildTreeViewIndex` after adding, removing or moving rows.

```cpp
// members: std::vector<TreeViewRow> rows; std::vector<int> visibleIdx; TableState treeState
rows = {
    { .name = "Scene",  .depth = 0, .isOpen = true },
    { .name = "Camera", .depth = 1 },
    { .name = "Light",  .depth = 1 },
};
RebuildTreeViewIndex(rows);
```

What the view gives back:

- `visibleIdx` is filled each frame with the indices into `rows` of the rows that can be
  seen, those whose parents are all open.
- Selection, scrolling and drag and drop go through the [`TableState`](tables.md).
- The selected row's index into `rows` is
  `visibleIdx[state.selectedRowID.value.subRangeParts.index]`.
- `TreeDropTargetRow()` is the row a drag is over, or an invalid ID.
- The CSS classes: `tree-view` for the view, `list-row` for the rows.

```cpp
RebuildTreeViewIndex(rows);   // after structural changes
VirtualTreeView(treeID, rows, visibleIdx, treeState, {}, LayoutOverride{}.FlexGrow(1));
```

```cpp
if (treeState.selectedRowID != ID::Invalid())
{
    const TreeViewRow& selected = rows[size_t(visibleIdx[treeState.selectedRowID.value.subRangeParts.index])];
    Text("Selected: " + selected.name);
}
```

For a tree with several columns, use a `VirtualTable` with a `TreeCell` in the first
column.


## Grid views

### BeginGridView / GridViewItem

A grid view lays items out in rows and columns, like the icons of a file explorer.

- `selectedIndex` holds the selected item, for a single selection.
- A `MultiSelectState` lets the user select several items with Ctrl+Click and
  Shift+Click.
- An `ItemDragDropConfig` lets the user drag items.

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

```cpp
if (BeginGridView(&selectedAsset))
{
    for (const Asset& asset : assets)
        GridViewItem({ asset.icon, asset.name });
    EndGridView();
}
```

With several items selected:

```cpp
// members: int selectedAsset = -1; MultiSelectState assetSelection
if (BeginGridView(&selectedAsset, &assetSelection))
{
    for (const Asset& asset : assets)
        GridViewItem({ asset.icon, asset.name });
    EndGridView();
}
Text(std::to_string(assetSelection.selectedIDs.size()) + " selected");
```

- `GridViewItem` returns `true` in the frame after the user clicked the item.
- `renameBuffer` lets the user rename the item in place:
  - F2, Ctrl+Shift+Click or a slow second click starts it.
  - Enter keeps the new name.
  - Escape, or clicking elsewhere, cancels.
- `IsRenaming()` says whether a rename field is open.
- The CSS classes:
  - `grid-view`
  - `grid-view-item`
  - `grid-view-list-item`
  - `grid-view-rename-*` for the rename field

```cpp
for (Asset& asset : assets)
    GridViewItem(asset.id, { asset.icon, asset.name }, ElementState::None, &asset.name);
```

### VirtualGridView

A grid that builds only the rows of items on screen, for folders with many files.

- `generateItem(index, itemID)` must emit exactly one cell, usually
  `GridViewItem(itemID, ...)`.
- `itemID` is a stable ID made for that item, used for the selection and for drag and
  drop. See [Ranged IDs](../overview/ids.md#ranged-ids).
- Every cell is `cellSize` pixels.

```cpp
void VirtualGridView(ID id, GridViewVirtualState& vstate, int itemCount, int2 cellSize,
                     const std::function<void(int index, ID itemID)>& generateItem,
                     int* selectedIndex = nullptr, MultiSelectState* multiSelect = nullptr,
                     ItemDragDropConfig* dragDrop = nullptr, StyleRef scrollStyle = {}, StyleRef rowStyle = {},
                     LayoutOverride&& constraints = {});
```

- The number of columns comes from the viewport's width in the previous frame.
- `vstate.cellStretchedW` is the cell width at which the columns fill the viewport exactly.
- The CSS classes: `grid-view-scroll` and `grid-view-row`.

```cpp
VirtualGridView(gridID, gridState, int(files.size()), int2{ styleManager.Px(96), styleManager.Px(96) },
                [this](int i, ID itemID) { GridViewItem(itemID, { files[i].icon, files[i].name }); },
                &selectedFile);
```

To make the cells fill the whole width, pass `cellStretchedW` back as the cell width:

```cpp
const int width = gridState.cellStretchedW > 0 ? gridState.cellStretchedW : styleManager.Px(96);
VirtualGridView(gridID, gridState, int(files.size()), int2{ width, styleManager.Px(96) },
                [this](int i, ID itemID) { GridViewItem(itemID, { files[i].icon, files[i].name }); },
                &selectedFile);
```

To open an item, set `vstate.onOpen`:

- It's called with the item's ID when the user double-clicks the item.
- It's also called on Enter, with the selected item: the last one clicked when there's a
  multi-selection.
- A slow second click still renames the item, as before.

```cpp
gridState.onOpen = [this](ID itemID) {
    const int i = int(itemID.value.subRangeParts.index);   // its index
    OpenFile(files[i]);
};
```

## File views

The file views show a tree and a grid of files. They read the files through an
`IFileSystem` (`gui_controls/gui_file_system.h`), so the files don't have to be on disk:

| Method | Effect |
| --- | --- |
| `List(path)` | A folder's entries |
| `Move`, `Rename` | Change entries; on refusal return false with the reason in `error` |
| `Import` | Copy a file or folder in from disk (e.g. dropped from another app) |
| `Generation()` | Changes whenever a listing may have changed |

Paths are relative to the root and separated by `/`. `""` is the root.

`MemoryFileSystem` keeps everything in memory.

- Fill it with `AddFolder` and `AddFile`.
- Or construct it from a folder on disk. It reads each folder the first time it's listed.
- Moves, renames and imports change only the copy in memory. So it can browse the real
  disk without writing to it.
- To write through to disk, an archive or an asset database, implement `IFileSystem`
  yourself.

```cpp
MemoryFileSystem files(std::filesystem::path("C:/Projects"));
std::vector<std::string> names;
for (const FileEntry& entry : files.List("TachyonUI"))
    names.push_back(entry.isFolder ? entry.name + "/" : entry.name);
std::string error;
if (!files.Move("TachyonUI/notes.txt", "Archive", error))
    ShowToast(ToastDesc{ .severity = Severity::Error, .title = "Can't move it", .body = error });
```

`FileIcons` (`gui_controls/gui_file_icons.h`) gets the OS icon for a file or folder.

- On Windows it uses the shell's icons:
  - one per extension
  - read once
  - at the requested size, or the next size up
- Elsewhere it returns `IconID::Invalid`, and the view uses its own icons.

```cpp
FileIcons icons(*textSystem, 48);
const IconID icon = icons.Get("photo.jpg", false);
```

`ITextSystem::RegisterIconFromPixels(name, size, rgba)` makes an icon from RGBA pixels.

- It keeps its own colors: no icon tint from the style, no text shadow.
- It scales to fit its box and keeps its aspect ratio, on its own or inside text.
- `ITextSystem::UnregisterIcon` removes it and frees its image slot.

```cpp
std::vector<uint8_t> rgba(16 * 16 * 4, 255);   // a white 16x16 square
const IconID square = textSystem->RegisterIconFromPixels("white-square", uint2{ 16, 16 }, rgba);
Button(square, [this] { Fill(); });
```

`FileThumbnails` (`gui_controls/gui_file_thumbnails.h`) gets a file's thumbnail: a small
picture of a photo's contents or of a video frame, as Explorer shows it.

- On Windows it reads them from the shell on a background thread.
  - `Get` returns `IconID::Invalid` until the thumbnail is ready, or if the file has none.
  - Show the file type's icon meanwhile.
- On other systems there are no thumbnails, unless you pass your own
  `FileThumbnails::Reader`.
- It needs the file on disk.
  - A `FileEntry` from a disk folder or an import has `disk` and `modified`.
  - A file made in memory has no `disk`, and gets no thumbnail.

```cpp
FileThumbnails thumbnails(*textSystem);   // keeps the last 512 asked for
IconID icon = thumbnails.Get(entry.disk, entry.modified, uint32_t(styleManager.Px(34)));
if (icon == IconID::Invalid)
    icon = icons.Get(entry.name, entry.isFolder);
```

How the thumbnails are kept:

- They're cached by path, modification time and size, so a changed file is read again.
- When the cache is full, the thumbnail asked for least recently is dropped, and its icon
  unregistered.
  - A file dropped before it was read isn't read.
- Thumbnails asked for in the last half second are never dropped. With more files on
  screen than the cache holds, the extra ones keep their type icons.
- Destroy the `FileThumbnails` before the text system.

---

[Previous: Tables](tables.md) | [Controls](../controls.md) | [Next: Tabs, docking and toolbars](tabs-docking-toolbars.md)
