# Tables

Column widths, selection and sort state live in a `TableState` your app owns. Columns are
an array of `VirtualTableColumn`s passed every frame; reorder the array to reorder columns.

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

Keyboard: a header that sorts or moves is a Tab stop. Space/Enter sorts (again reverses);
Alt+Left/Right moves the column, focus follows.

Classes: `table` (`table-fill` fills the parent), `table-header`, `table-row`,
`table-row-alt`, `table-cell`, `table-cell-number`, `table-sizer`, `table-separator`.

## BeginTable / BeginRow / Cell

You emit rows and cells. Each `Cell` advances one column; `EndTable` does the layout.

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

`Cell(emit)` builds custom content. It runs during `EndTable`, so its captures must stay
alive until then.

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

Builds only visible rows, for very large row counts. Each column's `generateCell(rowIndex)`
emits its cell, usually with `Cell` or `TreeCell`. Columns with `sortAscending` sort on
header click; the order is kept as a permutation, your data isn't moved.

```cpp
void VirtualTable(ID id, TableState& state, std::span<VirtualTableColumn> columns, int totalRowCount,
                  CssPx rowHeight = 0, StyleRef style = {}, LayoutOverride&& constraints = {});
```

`rowHeight` is a minimum; 0 uses the cell style's height. Give the table a bounded height:
sized to fit all rows it would exceed the layout's range.

```cpp
std::vector<VirtualTableColumn> columns = {
    { .column = { .header = "Name", .initialWidth = 160 },
      .generateCell  = [this](int row) { Cell(rows[row].name); },
      .sortAscending = [this](int a, int b) { return rows[a].name < rows[b].name; } },
};
VirtualTable(tableID, tableState, columns, int(rows.size()), 0, theme.tableFillStyleRef,
             LayoutOverride::Auto().FlexGrow(1).MinHeight(0));
```

Frozen columns: consecutive columns with the same `sectionId` form a section; sections sit
side by side. All scroll vertically together, each scrolls horizontally on its own, and
columns reorder only within their section. `SectionSizing::FitContent` makes a section
exactly as wide as its columns, with no horizontal scroll bar.

## TreeCell

A tree-node cell: indentation, expand arrow, optional icon, text. Clicking the arrow
toggles `*isOpen` without selecting; clicking elsewhere selects the row.

```cpp
void TreeCell(std::string_view text, int depth, bool* isOpen, IconID icon = IconID::Invalid,
              uint32_t ancestorLineMask = 0, bool isLastSibling = true,
              std::string* renameBuffer = nullptr, bool reserveExpander = true);
```

- `isOpen`: null on leaf rows.
- `renameBuffer`: enables rename in place (F2, Ctrl+Shift+Click or slow second click).
- `ancestorLineMask`, `isLastSibling`: guide lines; `RebuildTreeViewIndex` computes them.

Classes: `tree-table-cell`, `tree-table-arrow`, `tree-table-icon`, `tree-table-text`,
`tree-table-text-edit`.

---

[Previous: Lists and reorderable groups](lists.md) | [Controls](../controls.md) | [Next: Tree and grid views](trees-and-grids.md)
