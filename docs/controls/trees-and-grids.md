# Tree and grid views

## Tree views

### VirtualTreeView

A single-column tree that builds only visible rows, from a flat row list your app owns. It's
a `VirtualTable` with one `TreeCell` column. A non-empty `header` shows a header row.

```cpp
void VirtualTreeView(ID id, std::vector<TreeViewRow>& rows, std::vector<int>& visibleIdx, TableState& state,
                     StyleRef style = {}, LayoutOverride&& constraints = {}, std::string_view header = {});
static void RebuildTreeViewIndex(std::vector<TreeViewRow>& rows);
static void WalkTreeViewVisibility(const std::vector<TreeViewRow>& rows, std::vector<int>& visibleIdx);
ID TreeDropTargetRow() const;
```

- Set these `TreeViewRow` fields: `id`, `name`, `depth`, `isOpen`, `icon`, `renameBuffer`.
- Keep rows in depth-first order; call `RebuildTreeViewIndex` after adding, removing or
  moving rows.
- `visibleIdx` is filled each frame with the flat indices of visible rows.
- Selection, scrolling and drag and drop go through the `TableState`. Selected flat index:
  `visibleIdx[state.selectedRowID.value.subRangeParts.index]`.
- Classes: `tree-view`; rows `list-row`.

```cpp
RebuildTreeViewIndex(rows);   // after structural changes
VirtualTreeView(treeID, rows, visibleIdx, treeState, {}, LayoutOverride{}.FlexGrow(1));
```

For a multi-column tree, use a `VirtualTable` with `TreeCell` in the first column.


## Grid views

### BeginGridView / GridViewItem

A 2D item grid, like a file explorer. `selectedIndex` for single selection;
`MultiSelectState` for Ctrl/Shift multi-select; `ItemDragDropConfig` makes items
draggable.

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

- `GridViewItem` returns `true` in the frame after the item was clicked.
- `renameBuffer` enables rename in place: F2, Ctrl+Shift+Click or slow second click starts
  it; Enter commits; Escape or losing focus cancels. `IsRenaming` says whether an editor is
  open.
- Classes: `grid-view`, `grid-view-item`, `grid-view-list-item`, `grid-view-rename-*`.

### VirtualGridView

A grid that builds only visible rows. `generateItem(index, itemID)` must emit exactly one
cell, usually `GridViewItem(itemID, ...)`. `itemID` is a stable ranged sub-ID used for
selection and drag payloads. Cells are `cellSize` pixels.

```cpp
void VirtualGridView(ID id, GridViewVirtualState& vstate, int itemCount, int2 cellSize,
                     const std::function<void(int index, ID itemID)>& generateItem,
                     int* selectedIndex = nullptr, MultiSelectState* multiSelect = nullptr,
                     ItemDragDropConfig* dragDrop = nullptr, StyleRef scrollStyle = {}, StyleRef rowStyle = {},
                     LayoutOverride&& constraints = {});
```

Column count comes from the previous frame's viewport. `vstate.cellStretchedW` is the cell
width at which columns fill the viewport exactly. Classes: `grid-view-scroll`,
`grid-view-row`.

```cpp
VirtualGridView(gridID, gridState, int(files.size()), int2{ styleManager.Px(96), styleManager.Px(96) },
                [this](int i, ID itemID) { GridViewItem(itemID, { files[i].icon, files[i].name }); },
                &selectedFile);
```

## File views

The file views show a tree and grid of files from an `IFileSystem`
(`gui_controls/gui_file_system.h`):

| Method | Effect |
| --- | --- |
| `List(path)` | A folder's entries |
| `Move`, `Rename` | Change entries; on refusal return false with the reason in `error` |
| `Import` | Copy a file or folder in from disk (e.g. dropped from another app) |
| `Generation()` | Changes whenever a listing may have changed |

Paths are relative to the root, `/`-separated; `""` is the root.

`MemoryFileSystem` keeps everything in memory. Fill it with `AddFolder` / `AddFile`, or
construct it from a disk folder (folders read on first use). Moves, renames and imports
change only the copy, so it can browse the real disk without writing to it. To write
through to disk, an archive or an asset database, implement `IFileSystem` yourself.

```cpp
MemoryFileSystem files(std::filesystem::path("C:/Projects"));
std::vector<std::string> names;
for (const FileEntry& entry : files.List("TachyonUI"))
    names.push_back(entry.isFolder ? entry.name + "/" : entry.name);
std::string error;
if (!files.Move("TachyonUI/notes.txt", "Archive", error))
    ShowToast(ToastDesc{ .severity = Severity::Error, .title = "Can't move it", .body = error });
```

`FileIcons` (`gui_controls/gui_file_icons.h`) gets the OS icon for a file or folder. On
Windows: shell icons, one per extension, read once, at the requested size or the next up.
Elsewhere it returns `IconID::Invalid` and the view uses its own icons.

```cpp
FileIcons icons(*textSystem, 48);
const IconID icon = icons.Get("photo.jpg", false);
```

`ITextSystem::RegisterIconFromPixels(name, size, rgba)` makes an icon from RGBA pixels. It
keeps its own colors (no style icon tint, no text shadow) and scales to fit its box keeping
its aspect ratio, standalone or inside text. `ITextSystem::UnregisterIcon` removes it and
frees its image slot.

`FileThumbnails` (`gui_controls/gui_file_thumbnails.h`) gets a file's thumbnail: a photo's
contents or a video frame, as Explorer shows it.

- Windows: read from the shell on a background thread. `Get` returns `IconID::Invalid`
  until it's ready, or if the file has none; show the type icon meanwhile.
- Other systems: no thumbnails, unless you pass your own `FileThumbnails::Reader`.
- Needs the file on disk: a `FileEntry` from a disk folder or an import has `disk` and
  `modified`; an in-memory file has no `disk` and gets no thumbnail.

```cpp
FileThumbnails thumbnails(*textSystem);   // keeps the last 512 asked for
IconID icon = thumbnails.Get(entry.disk, entry.modified, uint32_t(styleManager.Px(34)));
if (icon == IconID::Invalid)
    icon = icons.Get(entry.name, entry.isFolder);
```

- Cached by path, modification time and size, so a changed file is read again.
- When full, the least recently asked-for thumbnail is dropped and its icon unregistered;
  a file dropped before it was read isn't read.
- Thumbnails asked for in the last half second are never dropped. With more files on
  screen than the cache holds, the extra ones keep their type icons.
- Destroy the `FileThumbnails` before the text system.

---

[Previous: Tables](tables.md) | [Controls](../controls.md) | [Next: Tabs, docking and toolbars](tabs-docking-toolbars.md)
