# IDs

Every element has an ID, a `tachyon::ID`. An element is one node of the UI tree that
`OnGUI` builds each frame, such as a button, a window or a piece of text.

- The ID is the key for the state the library keeps between frames:
  - hover
  - focus
  - text being edited
  - scroll offsets
  - window rects
- An element whose ID changes loses that state.
- All IDs come from `idManager`, a member of `GUIApplication`.

## Call-site IDs (dynamic)

The overloads of a control without an ID parameter take the ID from the call site, the
file and line of the call. A defaulted `std::source_location` parameter passes these in:

```cpp
Label("Name");
Field(name);                  // ID from this line
Button("OK", onOK);           // ID from this line
```

They stay stable while the code structure does:

- Each call site has its own counter, so controls after one that appears or disappears
  keep their IDs.
- A call site run several times in a frame, as in a loop, numbers its calls.

For controls that only take an explicit ID, make one with `idManager.AllocateDynamicID()`.
It can make the ID from:

- this call site
- a string hint
- a file and line you pass in

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

Dynamic IDs can only be made during the frame: in `OnGUI`, or in code it calls. Elsewhere
the call is reported and returns `ID::Invalid()`.

## Static IDs

A static ID is made once, outside the frame, and stays valid until you release it:

- Make it with `idManager.CreateStaticID()`, usually in `Initialize`.
- Release it with `idManager.ReleaseStaticID()` when the thing it names is gone for good.

```cpp
windowID = idManager.CreateStaticID();
// ...
idManager.ReleaseStaticID(windowID);   // when the thing it names is gone for good
```

- `IsStaticIDValid(id)` says whether the ID is still current in its slot.
- A released ID stays invalid even if its slot is reused.

## Ranged IDs

A ranged ID gives one ID per list item:

- Make the range with `idManager.AllocateRangedID(count, hint)`.
- `GetSubID(range, index)` gives the item's ID.

```cpp
const uint32_t count = uint32_t(std::max<size_t>(1, files.size()));   // the size must be at least 1
const tachyon::ID rows = idManager.AllocateRangedID(count, "files");
for (size_t i = 0; i < files.size(); ++i)
    Button(idManager.GetSubID(rows, uint32_t(i)), files[i], [this, i] { Open(i); });
```

- A range is made during the frame, from the call site or a string hint.
- A sub-ID depends on the range and the index, not the size. Items keep their IDs when the
  list grows or shrinks at the end.

## Parent scopes

The parent stack is a stack of IDs kept during the frame. Dynamic and ranged IDs are
combined with the ID on top of it:

```cpp
if (idManager.PushParent(panelID))
{
    Field(value);   // its ID is scoped under panelID
    (void)idManager.PopParent();
}
```

- These push a parent:
  - `BeginWindow`, popped by `EndWindow`
  - `BeginCached`
  - docked panels
  - virtual tables
  - virtual grid views
- These don't:
  - `BeginVertical`
  - `BeginHorizontal`
  - `BeginArea`
- `PushParent` returns false and pushes nothing for an invalid ID or outside the frame.
  Pop only when it returned true.
- A parent left on the stack at frame end is reported and cleared.

## When an ID has to be stable

Use your own ID when:

- **It's a window.**
  - `BeginWindow` takes an explicit ID; give it a static one.
  - The OS window, its position and its size are tied to it.
- **Its state must outlive the call site.**
  - For example, a control drawn from different places, or a panel whose content is
    created and destroyed.
  - `CreatePanel` panels get static IDs.
- **It's in a list whose items move.**
  - Call-site and ranged IDs follow the position.
  - So when items are reordered, or inserted or removed in the middle, state such as focus
    or an edit in progress stays at the position, not with the item.
  - Give each item a static ID when it is made, and push it as parent:

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

- **It's drawn by a helper called from several places.**
  - The helper's controls share call sites and are numbered in call order, so skipping one
    call renumbers the rest.
  - Push a parent per call:

  ```cpp
  void NameEditor(ID parent, std::string& name)
  {
      if (idManager.PushParent(parent))
      {
          Field(name);
          (void)idManager.PopParent();
      }
  }

  // In OnGUI:
  NameEditor(idManager.AllocateDynamicID("source"), sourceName);
  NameEditor(idManager.AllocateDynamicID("target"), targetName);
  ```

A control called with `ID::Invalid()` is reported and drawn, but takes no input and keeps
no state.

IDs are runtime values; don't save them. Saved UI state uses string keys (see
[Errors and saved state](errors-and-state.md#saved-ui-state)).

---

[Previous: The application and the frame](application.md) | [Overview](../overview.md) | [Next: Begin and End](begin-end.md)
