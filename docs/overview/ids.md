# IDs

Every element has a `tachyon::ID`, and the library uses it to look up the state it keeps
between frames: which element is hovered or focused, the text being edited, scroll
offsets, window rects. If an element gets a different ID next frame, it loses that
state.

There are three kinds of ID. You make all of them through `idManager`, a member of
`GUIApplication`.

## Call-site IDs (dynamic)

Most controls have overloads that don't take an ID. Those get their ID from where you
call them: the file and line of the call, picked up through a defaulted
`std::source_location` parameter.

```cpp
Label("Name");
Field(name);                  // ID from this line
Button("OK", onOK);           // ID from this line
```

These IDs stay stable as long as the structure of your code doesn't change:

- When a control appears or disappears, the controls after it keep their IDs, because
  each call site has its own counter.
- When the same call site runs more than once in a frame, as in a loop, the calls are
  numbered. The first call gets one ID, the second gets another, and so on.

Some controls only take an explicit ID. To give one a call-site ID, call
`idManager.AllocateDynamicID()`, which comes in three forms:

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

Dynamic IDs are made fresh every frame, so you can only make them during the frame, in
`OnGUI` or code it calls. If you try it anywhere else, the call is reported and returns
`ID::Invalid()`.

## Static IDs

A static ID is made once and stays the same until you release it. Make it outside the
frame, usually in `Initialize`, and keep it somewhere:

```cpp
windowID = idManager.CreateStaticID();
// ...
idManager.ReleaseStaticID(windowID);   // when the thing it names is gone for good
```

`IsStaticIDValid(id)` tells you whether the ID is still the current one in its slot.
Once released, an ID stays invalid, even if its slot gets reused.

## Ranged IDs

A ranged ID stands for a block of IDs, one for each item in a list. `GetSubID` gives you
the ID of the item at `index`:

```cpp
const uint32_t count = uint32_t(std::max<size_t>(1, files.size()));   // the size must be at least 1
const tachyon::ID rows = idManager.AllocateRangedID(count, "files");
for (size_t i = 0; i < files.size(); ++i)
    Button(idManager.GetSubID(rows, uint32_t(i)), files[i], [this, i] { Open(i); });
```

As with dynamic IDs, you make ranged IDs during the frame, from the call site or a
string hint. A sub-ID depends on the range and the index but not on the size, so items
keep their IDs when the list grows or shrinks at the end.

## Parent scopes

Dynamic and ranged IDs are combined with whatever ID is on top of the parent stack. To
give a group of calls their own scope, push a parent:

```cpp
if (idManager.PushParent(panelID))
{
    Field(value);   // its ID is scoped under panelID
    (void)idManager.PopParent();
}
```

`BeginWindow` pushes the window's ID and `EndWindow` pops it, so the IDs inside a window
are scoped to that window. Containers that might skip their content, or only build some
of their rows, do the same. These are `BeginCached`, docked panels, virtual tables and
virtual grid views. Plain layout containers (`BeginVertical`, `BeginHorizontal`,
`BeginArea`) don't push a parent.

`PushParent` returns false and pushes nothing if the ID is invalid or you call it
outside the frame. Only pop when it returned true. If a parent is still on the stack at
the end of the frame, it's reported and cleared.

## When an ID has to be stable

For most controls, call-site IDs are all you need. Use your own ID in these cases.

- **It's a window.** `BeginWindow` takes an explicit ID, and you should give it a static
  one. The window's OS window, position and size are tied to that ID.
- **Its state has to last longer than the call site can manage.** Examples are a control
  drawn from different places, or a panel whose content is created and destroyed. Panels
  made with `CreatePanel` get static IDs.
- **It's in a list whose items move around.** Call-site and ranged IDs follow the
  position in the list. If items are reordered, or inserted or removed in the middle,
  the state (focus, an edit in progress) stays at the same position instead of moving
  with the item. To fix that, give each item a static ID when you create it, and push
  it as the parent while you draw the item:

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

- **It's drawn by a helper function you call from several places.** The controls inside
  the helper share their call sites, so they're numbered in the order they're called. If
  an earlier call is skipped, the later ones get renumbered. Push a parent for each call
  to the helper.

If you call a control with `ID::Invalid()`, it's reported. The control is still drawn,
but it takes no input and keeps no state.

IDs are runtime values and aren't meant to be saved. Saved UI state uses string keys
instead (see [Errors and saved state](errors-and-state.md#saved-ui-state)).

---

[Previous: The application and the frame](application.md) | [Overview](../overview.md) | [Next: Begin and End](begin-end.md)
