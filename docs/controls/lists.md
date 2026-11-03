# Lists and reorderable groups

## BeginListView / ListItem

```cpp
bool BeginListView(ID id, int* selectedIndex, int itemCount, ItemDragDropConfig* dragDrop, StyleRef style,
                   LayoutOverride&& constraints = {});
void EndListView();
bool ListItem(ID id, const GUIContent& content, StyleRef style, LayoutOverride&& constraints = {});
int  FindListItemIndex(ID id) const;
```

- `selectedIndex`: the selected item; Up / Down move it while the list has focus.
- `itemCount`: keeps keyboard movement inside the list.
- `dragDrop`: with a `payloadType` other than 0, items get a drag handle and accept drops.
- `ListItem` returns `true` the frame after a click.
- `FindListItemIndex` maps an item ID back to its index, for end-of-frame handlers such as
  drag and drop.

Classes: `list-view`, `list-item`; with drag and drop also `list-item-row`,
`list-item-body`, `list-item-drag-knob-vert`.

```cpp
BeginListView(&selected, int(names.size()));
for (const std::string& name : names)
    ListItem(name);
EndListView();
```

## BeginReorderableGroup / BeginReorderableItem

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

`EmitDragHandle` adds a grip inside an item; clicks on it reach the item.

Keyboard: Alt plus an arrow along the group moves the focused item one place (Up / Down
vertical, Left / Right horizontal, any arrow wrapped). It calls the same `onReorder`, once
`EndReorderableGroup` has seen every item. Focus stays with the moved item: if IDs travel
with the data (tabs), it already is; if IDs belong to the slot (`Field` list rows), focus
moves to the same control in the new slot.

Classes: group `horzReorderableGroup` / `vertReorderableGroup` (`...Wrap` when wrapped),
item `reorderableItem`, drag `reorder-drop-slot`, `reorder-lifted`.

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

---

[Previous: Tooltips and message dialogs](tooltips-and-dialogs.md) | [Controls](../controls.md) | [Next: Tables](tables.md)
