# Lists and reorderable groups

## BeginListView / ListItem

A list of selectable items. `selectedIndex` holds the index of the selected item, and Up
and Down move it while the list has focus. `itemCount` keeps that keyboard movement
within the list. If you pass an `ItemDragDropConfig` with a `payloadType` other than 0,
each item gets a drag handle and can be dragged and dropped onto.

```cpp
bool BeginListView(ID id, int* selectedIndex, int itemCount, ItemDragDropConfig* dragDrop, StyleRef style,
                   LayoutOverride&& constraints = {});
void EndListView();
bool ListItem(ID id, const GUIContent& content, StyleRef style, LayoutOverride&& constraints = {});
int  FindListItemIndex(ID id) const;
```

`ListItem` returns `true` in the frame after a click. In end-of-frame handlers, such as
drag and drop, `FindListItemIndex` turns an item ID back into its index. The list uses
the classes `list-view` and `list-item`. With drag and drop on, it also uses
`list-item-row`, `list-item-body` and `list-item-drag-knob-vert`.

```cpp
BeginListView(&selected, int(names.size()));
for (const std::string& name : names)
    ListItem(name);
EndListView();
```

## BeginReorderableGroup / BeginReorderableItem

A container whose items you can drag into a new order. When an item is dropped,
`onReorder(fromIndex, toIndex)` is called, and it's up to your app to move its data. Set
`isWrapped` to let the items wrap onto more lines. If an item is released outside the
group, `onDragOut(index, screenPos)` is called. While an item is dragged outside the
group, `dragVisualizer(index, screenPos)` runs every frame, usually to draw a ghost
window.

Items can be moved from the keyboard too. Alt plus an arrow key along the group moves
the item that has the focus by one place: Up / Down in a vertical group, Left / Right in
a horizontal one, and any arrow in a wrapped one. This goes through the same
`onReorder`, which is called once `EndReorderableGroup` has seen every item. The focus
stays with the moved item. If the item's IDs travel with its data (as with tabs), the
focus is already in the right place. If the IDs belong to the slot (as with the rows of
a `Field` list), the focus moves to the same control in the item's new slot.

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

`EmitDragHandle` adds a grip inside an item, and clicks on the grip reach the item. The
group uses `horzReorderableGroup` or `vertReorderableGroup` (`horzReorderableGroupWrap`
or `vertReorderableGroupWrap` when wrapped). Items use `reorderableItem`, and the drag
uses `reorder-drop-slot` and `reorder-lifted`.

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
