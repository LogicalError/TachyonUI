# Lists and reorderable groups

## BeginListView / ListItem

A list of items, one of which can be selected. Call `BeginListView`, then `ListItem` for
each item, then `EndListView`.

```cpp
bool BeginListView(ID id, int* selectedIndex, int itemCount, ItemDragDropConfig* dragDrop, StyleRef style,
                   LayoutOverride&& constraints = {});
void EndListView();
bool ListItem(ID id, const GUIContent& content, StyleRef style, LayoutOverride&& constraints = {});
int  FindListItemIndex(ID id) const;
```

- `selectedIndex` points to the index of the selected item.
  - Up / Down move it while the list has focus.
- `itemCount` keeps keyboard movement inside the list.
- `dragDrop` turns on drag and drop, when its `payloadType` is not 0:
  - items get a drag handle
  - items accept drops
- `ListItem` returns `true` the frame after a click.
- `FindListItemIndex` maps an item ID back to its index.
  - Use it in handlers that run at the end of the frame, such as drag and drop.

Classes:

- `list-view`, `list-item`
- with drag and drop also `list-item-row`, `list-item-body`, `list-item-drag-knob-vert`

```cpp
BeginListView(&selected, int(names.size()));
for (const std::string& name : names)
    ListItem(name);
EndListView();
```

```cpp
BeginListView(&selected, int(names.size()));
for (const std::string& name : names)
    if (ListItem(name))   // true the frame after a click
        OpenFile(name);
EndListView();
```

With drag and drop, the config says what an item carries and what a drop does:

```cpp
// Once, e.g. in Initialize (member: ItemDragDropConfig namesDragDrop):
namesDragDrop.payloadType = 1;
namesDragDrop.getPayload  = [this](ID itemID) { return uintptr_t(FindListItemIndex(itemID)); };
namesDragDrop.onReorder   = [this](uintptr_t from, ID targetID, TreeDropPosition)
{
    MoveName(int(from), FindListItemIndex(targetID));
};

// Each frame:
BeginListView(&selected, int(names.size()), &namesDragDrop);
for (const std::string& name : names)
    ListItem(name);
EndListView();
```

## BeginReorderableGroup / BeginReorderableItem

A group of items the user reorders by dragging. Each item is a `BeginReorderableItem` /
`EndReorderableItem` pair with any content inside.

```cpp
bool BeginReorderableGroup(ID id, ReorderDirection direction, bool isWrapped,
                           std::function<void(int fromIndex, int toIndex)> onReorder, StyleRef style,
                           LayoutOverride&& constraints = {},
                           std::function<void(int, int2)> onDragOut = nullptr,
                           std::function<void(int, int2)> dragVisualizer = nullptr);
bool BeginReorderableGroup(ID id, ReorderDirection direction, std::function<void(int fromIndex, int toIndex)> onReorder,
                           LayoutOverride&& constraints = {});
void EndReorderableGroup();
bool BeginReorderableItem(ID id, StyleRef style, LayoutOverride&& constraints = {});
void EndReorderableItem();
void EmitDragHandle();
```

| Parameter | Meaning |
| --- | --- |
| `onReorder(fromIndex, toIndex)` | Called on drop. Your app moves the data. |
| `isWrapped` | Items wrap onto more lines |
| `onDragOut(index, screenPos)` | Item released outside the group |
| `dragVisualizer(index, screenPos)` | Every frame while dragged outside the group, e.g. to draw a ghost window |

`EmitDragHandle` adds a grip inside an item. Clicks on the grip reach the item.

Keyboard:

- Alt plus an arrow along the group moves the focused item one place:
  - Up / Down in a vertical group
  - Left / Right in a horizontal group
  - any arrow in a wrapped group
- It calls the same `onReorder`, once `EndReorderableGroup` has seen every item.
- Focus stays with the moved item:
  - if the IDs travel with the data, as with tabs, it already does
  - if the IDs belong to the slot, as with `Field` list rows, focus moves to the same
    control in the new slot

Classes:

- the group: `horzReorderableGroup` or `vertReorderableGroup`, with `...Wrap` when
  wrapped
- an item: `reorderableItem`
- while dragging: `reorder-drop-slot`, `reorder-lifted`

```cpp
BeginReorderableGroup(groupID, ReorderDirection::Vertical, [this](int from, int to)
{
    std::string moved = std::move(items[from]);
    items.erase(items.begin() + from);
    items.insert(items.begin() + to, std::move(moved));
});
for (const std::string& item : items)
{
    BeginReorderableItem();
    EmitDragHandle();
    Text(item);
    EndReorderableItem();
}
EndReorderableGroup();
```

Wrapped, with an item dropped outside the group opening in its own window:

```cpp
BeginReorderableGroup(tagsID, ReorderDirection::Horizontal, true,
                      [this](int from, int to) { MoveTag(from, to); },
                      ReorderableGroupStyle(ReorderDirection::Horizontal, true), {},
                      [this](int index, int2 screenPos) { OpenTagWindow(index, screenPos); });
for (const std::string& tag : tags)
{
    BeginReorderableItem();
    Text(tag);
    EndReorderableItem();
}
EndReorderableGroup();
```

---

[Previous: Tooltips and message dialogs](tooltips-and-dialogs.md) | [Controls](../controls.md) | [Next: Tables](tables.md)
