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
`List(path)` gives a folder's entries, `Move` and `Rename` change them, `Import` copies a
file or folder in from the disk (one dropped from another application), and `Generation()`
changes whenever a list may have, so a view knows when to read again. Paths are relative
to the file system's root, with `/` between names; `""` is the root. A refused move or
rename returns false with the reason in `error`, ready to show.

`MemoryFileSystem` keeps it all in memory: filled with `AddFolder` / `AddFile`, or a copy
of a folder on the disk, each folder read the first time it's needed. Its moves, renames
and imports change only the copy (an imported folder is read as it's browsed), so a demo can
browse the real disk without writing to it.
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
`ITextSystem::UnregisterIcon` lets go of one again, freeing its image slot.

`FileThumbnails` (`gui_controls/gui_file_thumbnails.h`) gives a file's thumbnail, what
Explorer shows for it: a picture's contents, a frame of a video. They're read from the
Windows shell on a thread of their own, so `Get` is `IconID::Invalid` until one is ready,
and for a file without one: show the type's icon meanwhile. A `FileEntry` from a disk
folder, or imported from the disk, says where its file is (`disk`) and when it was last
written (`modified`); one that's only in memory has no `disk`, and no thumbnail.

```cpp
FileThumbnails thumbnails(*textSystem);   // the 512 asked for last are kept
IconID icon = thumbnails.Get(entry.disk, entry.modified, uint32_t(styleManager.Px(34)));
if (icon == IconID::Invalid)
    icon = icons.Get(entry.name, entry.isFolder);
```

They're kept by path, modification time and size, so a file that changed is read again.
Past the cap, the one asked for longest ago goes, and its icon is unregistered; one let go
before it was read isn't read. One asked for in the last half second (on screen still)
isn't let go: with more files on screen than the cap, the rest keep their type icons.
Destroy it before the text system. Elsewhere than Windows
there are none; pass a `FileThumbnails::Reader` of your own to read them another way.

---

[← Tables](tables.md) · [Controls](../controls.md) · [Tabs, docking and toolbars →](tabs-docking-toolbars.md)
