# Tables

A table shows data in rows and columns, with a header row on top. The user can resize,
sort and reorder the columns, and select rows or single cells.

Two things describe a table:

- A `TableState` your app owns and keeps from frame to frame. It holds the column widths,
  the selection and the sort order, plus options for how the table looks and behaves.
- An array of `VirtualTableColumn`s, one per column, passed every frame.
  - To reorder the columns, reorder the array.

```cpp
// members: TableState tableState; std::vector<VirtualTableColumn> columns
columns = {
    { .column = { .header = "Name", .initialWidth = 160, .sortable = true } },
    { .column = { .header = "Size", .initialWidth = 80, .sizing = ColumnSizing::Fixed } },
};
```

`VirtualTableColumn`:

| Field | Meaning |
| --- | --- |
| `column` | A `TableColumn` (below) |
| `generateCell` | Builds the column's cell in a `VirtualTable` |
| `sortAscending(rowA, rowB)` | Needed for a `VirtualTable` to sort by this column |
| `sectionId`, `sectionSizing` | Columns in the same section scroll together (see frozen columns below) |

`TableColumn` (sizes in CSS pixels):

| Field | Meaning |
| --- | --- |
| `header` | Header text |
| `initialWidth`, `minWidth`, `maxWidth` | Width limits |
| `sortable` | Header click sorts |
| `locked` | Can't be dragged; other columns can't move past it |
| `sizing` | `ColumnSizing::Resizable`, `Fixed` or `Stretch` |
| `interactiveContent` | Cell widgets stay clickable in row selection mode |
| `columnBackground`, `cellStyle` | Styles |

`TableState`:

| Field | Meaning |
| --- | --- |
| `selectionMode` | `TableSelectionMode::Row` or `Cell` |
| `selectedRowID`, `selectedColumn`, `multiSelect` | Selection |
| `rowDragDrop` | Row drag and drop |
| `sortColumn`, `sortDescending` | Sort state |
| `followTail` | Stay scrolled to the bottom as rows are added |
| `selectionBar`, `rowStyle`, `cellStyle`, `showHeaders`, `columnSeparators` | Look |
| `onReorderColumns` | When set, headers can be dragged to reorder |
| `onRowDoubleClick` | Row double-click callback |

Set the options once, or every frame:

```cpp
tableState.selectionMode    = TableSelectionMode::Cell;   // a click selects one cell
tableState.selectionBar     = true;
tableState.onRowDoubleClick = [this](ID rowID) { OpenRow(rowID); };
```

A log that keeps showing its newest line:

```cpp
logTableState.followTail = true;   // member: TableState logTableState
```

To let the user drag headers to reorder the columns, set `onReorderColumns` and move the
column in your array. The table moves the stored widths itself:

```cpp
tableState.onReorderColumns = [this](int from, int to)
{
    if (from < to) std::rotate(columns.begin() + from, columns.begin() + from + 1, columns.begin() + to + 1);
    else           std::rotate(columns.begin() + to, columns.begin() + from, columns.begin() + from + 1);
};   // member: std::vector<VirtualTableColumn> columns
```

Keyboard:

- A header that sorts or moves is a Tab stop.
- Space or Enter on a header sorts by its column. Pressing it again reverses the order.
- Alt+Left and Alt+Right move the column. The focus moves with it.

The CSS classes:

- `table` for the table, or `table-fill` to fill the parent
- `table-header`
- `table-row` and `table-row-alt` for the rows, alternating
- `table-cell`, and `table-cell-number` for numbers
- `table-sizer` for the handle that resizes a column
- `table-separator`

## BeginTable / BeginRow / Cell

The simple way to build a table: you emit every row and its cells, and the table lays them
out.

- Each `Cell` fills the next column.
- `EndTable` does the layout.

```cpp
bool BeginTable(ID id, VirtualTableColumn* columns, size_t columnCount, TableState& state, StyleRef style,
                LayoutOverride&& constraints = {});
template <size_t N>
bool BeginTable(ID id, VirtualTableColumn (&columns)[N], TableState& state, LayoutOverride&& constraints = {});
void EndTable();
bool BeginRow();
void EndRow();
void Cell(std::string_view text);
void Cell(std::function<void()> emit);
```

```cpp
VirtualTableColumn columns[] = {
    { .column = { .header = "Name", .initialWidth = 160 } },
    { .column = { .header = "Size" } },
};
if (BeginTable(tableID, columns, tableState, LayoutOverride{}.Size(400, 200)))
{
    for (const File& file : files)
    {
        BeginRow();
        Cell(file.name);
        Cell(file.sizeText);
        EndRow();
    }
    EndTable();
}
```

`Cell(emit)` builds custom content, such as a button in a cell.

- `emit` runs during `EndTable`, not when you call `Cell`.
- So everything it captures must stay alive until `EndTable`.

```cpp
BeginRow();
Cell(file.name);
Cell([this, &file] { Button("Open", [this, &file] { Open(file); }); });
EndRow();
```

## VirtualTable

A virtual table builds only the rows that are on screen. Use it for very large row counts,
where building every row each frame would be too slow.

- Each column's `generateCell(rowIndex)` emits that column's cell for one row, usually
  with `Cell` or `TreeCell`.
- Columns with `sortAscending` sort when the user clicks their header.
- The sorted order is kept as a list of row indices. Your data isn't moved.

```cpp
void VirtualTable(ID id, TableState& state, std::span<VirtualTableColumn> columns, int totalRowCount,
                  CssPx rowHeight = 0, StyleRef style = {}, LayoutOverride&& constraints = {});
```

- `rowHeight` is a minimum. 0 uses the cell style's height.
- Give the table a bounded height. Sized to fit all its rows, it would grow past what the
  layout can handle.

```cpp
std::vector<VirtualTableColumn> columns = {
    { .column = { .header = "Name", .initialWidth = 160 },
      .generateCell  = [this](int row) { Cell(rows[row].name); },
      .sortAscending = [this](int a, int b) { return rows[a].name < rows[b].name; } },
};
VirtualTable(tableID, tableState, columns, int(rows.size()), 0, theme.tableFillStyleRef,
             LayoutOverride::Auto().FlexGrow(1).MinHeight(0));
```

Frozen columns are columns that stay in view while the user scrolls the others sideways,
such as a row number on the left. Columns are grouped into sections for this:

- Consecutive columns with the same `sectionId` form a section.
- The sections sit side by side.
- All sections scroll up and down together.
- Each section scrolls sideways on its own.
- Columns can be reordered only within their own section.
- `SectionSizing::FitContent` makes a section exactly as wide as its columns, with no
  horizontal scroll bar.

```cpp
columns = {
    // Section 0: always in view
    { .column = { .header = "#", .initialWidth = 60, .locked = true },
      .generateCell = [this](int row) { Cell(std::to_string(row)); },
      .sectionId = 0, .sectionSizing = SectionSizing::FitContent },
    // Section 1: scrolls sideways
    { .column = { .header = "Name", .initialWidth = 160 },
      .generateCell = [this](int row) { Cell(rows[row].name); },
      .sectionId = 1 },
};
```

## TreeCell

A tree is data where items contain other items, like folders in folders. `TreeCell` shows
one item of a tree in a table cell. It draws, from left to right:

- an indent that grows with the item's depth in the tree
- an arrow that opens and closes the item, if it has children
- an optional icon
- the text

Clicking the arrow toggles `*isOpen` without selecting the row. Clicking elsewhere selects
the row.

```cpp
void TreeCell(std::string_view text, int depth, bool* isOpen, IconID icon = IconID::Invalid,
              uint32_t ancestorLineMask = 0, bool isLastSibling = true,
              std::string* renameBuffer = nullptr, bool reserveExpander = true);
```

```cpp
WalkTreeViewVisibility(rows, visibleIdx);   // members: std::vector<TreeViewRow> rows; std::vector<int> visibleIdx
std::vector<VirtualTableColumn> columns = {
    { .column = { .header = "Name", .initialWidth = 240 }, .generateCell = [this](int i) {
          TreeViewRow& r = rows[visibleIdx[i]];
          TreeCell(r.name, r.depth, r.HasChildren() ? &r.isOpen : nullptr, r.icon, r.ancestorLineMask, r.isLastSibling);
      } },
};
VirtualTable(tableID, tableState, columns, int(visibleIdx.size()), 0, {}, LayoutOverride{}.FlexGrow(1));
```

- `isOpen` is null on leaf rows, the rows without children.
- `ancestorLineMask` and `isLastSibling` draw the guide lines that connect an item to its
  parent. `RebuildTreeViewIndex` computes them (see
  [Tree views](trees-and-grids.md#tree-views)).
- `renameBuffer` lets the user rename the item in place. Any of these starts it:
  - F2
  - Ctrl+Shift+Click
  - a slow second click

```cpp
columns = {
    { .column = { .header = "Name", .initialWidth = 240 }, .generateCell = [this](int i) {
          TreeViewRow& r = rows[visibleIdx[i]];
          TreeCell(r.name, r.depth, r.HasChildren() ? &r.isOpen : nullptr, r.icon, r.ancestorLineMask,
                   r.isLastSibling, &r.name);   // a rename writes the new name to r.name
      } },
};
```

The CSS classes:

- `tree-table-cell`
- `tree-table-arrow`
- `tree-table-icon`
- `tree-table-text`
- `tree-table-text-edit` for the rename field

---

[Previous: Lists and reorderable groups](lists.md) | [Controls](../controls.md) | [Next: Tree and grid views](trees-and-grids.md)
