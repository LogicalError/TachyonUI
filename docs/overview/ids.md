# IDs

Every element has a `tachyon::ID`, the key for the state kept between frames (hover,
focus, text being edited, scroll offsets, window rects). An element whose ID changes
loses that state. All IDs come from `idManager`, a member of `GUIApplication`.

## Call-site IDs (dynamic)

Overloads without an ID take it from the call site (file and line, via a defaulted
`std::source_location` parameter):

```cpp
Label("Name");
Field(name);                  // ID from this line
Button("OK", onOK);           // ID from this line
```

They stay stable while the code structure does:

- Each call site has its own counter, so controls after one that appears or disappears
  keep their IDs.
- A call site run several times in a frame (a loop) numbers its calls.

For controls that only take an explicit ID, use `idManager.AllocateDynamicID()`:

```cpp
tachyon::ID a = idManager.AllocateDynamicID();             // this call site
tachyon::ID b = idManager.AllocateDynamicID("tabs");       // a string hint
tachyon::ID c = idManager.AllocateDynamicID(__FILE__, __LINE__);
```

```cpp
BeginTabBar(idManager.AllocateDynamicID("tabs"), &selectedTab, nullptr);
Tab("First");
Tab("Second");
EndTabBar();
```

Dynamic IDs can only be made during the frame (in `OnGUI` or code it calls). Elsewhere
the call is reported and returns `ID::Invalid()`.

## Static IDs

Made once, outside the frame (usually in `Initialize`), and valid until released:

```cpp
windowID = idManager.CreateStaticID();
// ...
idManager.ReleaseStaticID(windowID);   // when the thing it names is gone for good
```

`IsStaticIDValid(id)` says whether the ID is still current in its slot. A released ID
stays invalid even if its slot is reused.

## Ranged IDs

One ID per list item. `GetSubID(range, index)` gives the item's ID:

```cpp
const uint32_t count = uint32_t(std::max<size_t>(1, files.size()));   // the size must be at least 1
const tachyon::ID rows = idManager.AllocateRangedID(count, "files");
for (size_t i = 0; i < files.size(); ++i)
    Button(idManager.GetSubID(rows, uint32_t(i)), files[i], [this, i] { Open(i); });
```

Made during the frame, from the call site or a string hint. A sub-ID depends on range
and index, not size, so items keep their IDs when the list grows or shrinks at the end.

## Parent scopes

Dynamic and ranged IDs are combined with the ID on top of the parent stack:

```cpp
if (idManager.PushParent(panelID))
{
    Field(value);   // its ID is scoped under panelID
    (void)idManager.PopParent();
}
```

- Push a parent: `BeginWindow` (popped by `EndWindow`), `BeginCached`, docked panels,
  virtual tables, virtual grid views.
- Don't: `BeginVertical`, `BeginHorizontal`, `BeginArea`.
- `PushParent` returns false and pushes nothing for an invalid ID or outside the frame.
  Pop only when it returned true.
- A parent left on the stack at frame end is reported and cleared.

## When an ID has to be stable

Use your own ID when:

- **It's a window.** `BeginWindow` takes an explicit ID; give it a static one. The OS
  window, position and size are tied to it.
- **Its state must outlive the call site**, e.g. a control drawn from different places,
  or a panel whose content is created and destroyed. `CreatePanel` panels get static IDs.
- **It's in a list whose items move.** Call-site and ranged IDs follow the position, so
  on reorder or insert/remove in the middle, state (focus, an edit in progress) stays at
  the position. Give each item a static ID at creation and push it as parent:

```cpp
for (Item& item : items)   // item.id = idManager.CreateStaticID() when the item is made
{
    if (idManager.PushParent(item.id))
    {
        Field(item.name);
        (void)idManager.PopParent();
    }
}
```

- **It's drawn by a helper called from several places.** The helper's controls share
  call sites and are numbered in call order, so skipping one call renumbers the rest.
  Push a parent per call.

A control called with `ID::Invalid()` is reported, drawn, but takes no input and keeps
no state.

IDs are runtime values; don't save them. Saved UI state uses string keys (see
[Errors and saved state](errors-and-state.md#saved-ui-state)).

---

[Previous: The application and the frame](application.md) | [Overview](../overview.md) | [Next: Begin and End](begin-end.md)
