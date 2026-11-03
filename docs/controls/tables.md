# Tables

A table keeps its column widths, selection and sort state in a `TableState`, which your app
owns. You describe the columns every frame as an array of `VirtualTableColumn`s. To change
the column order, reorder the array.

A `VirtualTableColumn` has these fields:

- `column`, a `TableColumn` (see below).
- `generateCell`, which builds the column's cell in a `VirtualTable`.
- `sortAscending(rowA, rowB)`, which you need if a `VirtualTable` should sort by this
  column.
- `sectionId` and `sectionSizing`. Columns with the same section scroll together (see
  "frozen columns" below).

A `TableColumn` has a `header`, an `initialWidth`, a `minWidth` and a `maxWidth`, all in
CSS pixels. It can be `sortable`. A `locked` column can't be dragged, and other columns
can't be moved past it. `sizing` is `ColumnSizing::Resizable`, `Fixed` or `Stretch`. Set
`interactiveContent` to keep the widgets in its cells clickable in row selection mode. You
can also give it a `columnBackground` and a `cellStyle`.

The `TableState` options are:

- `selectionMode`, either `TableSelectionMode::Row` or `Cell`, plus `selectedRowID`,
  `selectedColumn` and `multiSelect`.
- `rowDragDrop`.
- `sortColumn` and `sortDescending`.
- `followTail`, which keeps the table scrolled to the bottom as rows are added.
- `selectionBar`, `rowStyle`, `cellStyle`, `showHeaders` and `columnSeparators`.
- `onReorderColumns`. When you set it, the user can drag headers to reorder columns.
- `onRowDoubleClick`.

A header that sorts or moves its column is a Tab stop. When it has focus, Space or Enter
sorts by that column, and pressing it again sorts the other way. Alt+Left and Alt+Right
move the column, and the focus moves with it.

Classes: `table` (`table-fill` makes it fill its parent), `table-header`, `table-row`,
`table-row-alt`, `table-cell`, `table-cell-number`, `table-sizer`, `table-separator`.

## BeginTable / BeginRow / Cell

Here you emit the rows and cells yourself. Each `Cell` moves on to the next column, and
`EndTable` lays everything out.

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

`Cell(emit)` lets you build custom content in a cell. The function runs during `EndTable`,
so anything it captures has to stay alive until then.

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

## VirtualTable

A `VirtualTable` only builds the rows that are visible, so it copes with a very large
number of rows. Each column's `generateCell(rowIndex)` emits that column's cell, usually
with `Cell` or `TreeCell`. If the columns have `sortAscending` set, clicking a header sorts
the table. The table keeps the sorted order as a permutation, so your data isn't moved.

```cpp
void VirtualTable(ID id, TableState& state, std::span<VirtualTableColumn> columns, int totalRowCount,
                  CssPx rowHeight = 0, StyleRef style = {}, LayoutOverride&& constraints = {});
```

`rowHeight` is a minimum. Pass 0 to use the cell style's height. Give the table a bounded
height, because if it were sized to fit all of its rows it would go past the range the
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

Frozen columns work through sections. Consecutive columns with the same `sectionId` form a
section, and the sections sit side by side. They all scroll vertically together, but each
one scrolls horizontally on its own, and columns can only be reordered within their
section. A section with `SectionSizing::FitContent` is exactly as wide as its columns and
has no horizontal scroll bar.

## TreeCell

A cell that looks like a tree node, with indentation, an expand arrow, an optional icon and
text. Clicking the arrow toggles `*isOpen` without selecting the row. Clicking anywhere
else selects the row.

```cpp
void TreeCell(std::string_view text, int depth, bool* isOpen, IconID icon = IconID::Invalid,
              uint32_t ancestorLineMask = 0, bool isLastSibling = true,
              std::string* renameBuffer = nullptr, bool reserveExpander = true);
```

Pass null for `isOpen` on leaf rows. A `renameBuffer` lets the user rename in place with
F2, Ctrl+Shift+Click or a slow second click. `ancestorLineMask` and `isLastSibling` control
the guide lines, and `RebuildTreeViewIndex` can compute them for you. Classes:
`tree-table-cell`, `tree-table-arrow`, `tree-table-icon`, `tree-table-text`,
`tree-table-text-edit`.

---

[Previous: Lists and reorderable groups](lists.md) | [Controls](../controls.md) | [Next: Tree and grid views](trees-and-grids.md)
