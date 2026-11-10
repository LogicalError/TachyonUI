# Common conventions

All controls follow the same rules for their parameters, their results and their layout.
This page explains those rules once, so the pages for each control don't repeat them.

**Parameters.** Every control takes its parameters in the same order:
`(id, content or value, ..., callback, style, constraints)`. Most of them can be left out.

- `id` is the control's `ID`: the key under which TachyonUI keeps the control's state
  between frames, such as hover, focus or the text being edited.
- The content or value is what the control shows or edits.
- A callback is a function you pass in; the control calls it when the user acts on it.
- `style` is a `StyleRef`: a handle to a CSS class of the loaded theme.
- `constraints` is a `LayoutOverride` (below).

Each control has these overloads:

- With `ID id` first, when you keep the ID yourself.
  - Make it once, outside the frame, with `idManager.CreateStaticID()`.

  ```cpp
  Button(saveID, "Save", onSave);   // member: ID saveID, made in Initialize
  ```

- Without an ID: it comes from the call site (`std::source_location`), so
  `Button("Save", onSave)` works.
  - An ID made this way is called a dynamic ID.
  - See [IDs](../overview/ids.md) for when an ID must be stable.
- With `StyleRef style`, replacing the default CSS class.
  - Get the `StyleRef` from `theme.GetStyle("my-class")`.

  ```cpp
  Button("Delete", onDelete, theme.GetStyle("my-danger-button"));   // a class from your CSS
  ```

Signatures on these pages show the overload with both ID and style.

**Content.** Buttons, toggles, tabs, list items and menu items take a `GUIContent`. It
holds:

- text, an icon, or both
- an optional tooltip: text shown while the pointer rests on the control

A string or icon converts implicitly:

```cpp
Button("Save", onSave);
Button(saveIcon, onSave);
Button({ saveIcon, "Save", "Write the file to disk" }, onSave);
```

**Results.** A control reports the user's input in two ways:

- The callback runs when the input is handled, after the frame it arrived in.
- The control returns `true` in the next frame.
- Controls taking a `T&` (a reference to your value) also write the new value.

See [Return values and callbacks](../overview/values.md#return-values-and-callbacks).

```cpp
if (Button("Save", [this] { Save(); }))   // Save() has already run
    statusText = "Saved";

if (Field(name))                          // the field wrote the new text into name
    nameChanged = true;
```

**LayoutOverride.** The last parameter, `LayoutOverride&& constraints = {}`, overrides
size and position for this call, on top of the style. Each builder returns the
`LayoutOverride` again, so builders chain:

```cpp
Button("OK", onOK, LayoutOverride{}.NoGrow().NoShrink());
BeginVertical(LayoutOverride{}.FlexGrow(1));
BeginWindow(windowID, "Tool", LayoutOverride::FixedSize(900, 600).Centered());
```

| Builder | Effect |
| --- | --- |
| `LayoutOverride::Auto()`, `FixedSize(w, h)`, `FixedWidth(w)`, `FixedHeight(h)` | Factories |
| `Size(w, h)`, `Width(w)`, `Height(h)` | Fixed size |
| `MinWidth`, `MinHeight`, `MaxWidth`, `MaxHeight` | Size limits |
| `FlexGrow(n)`, `FlexShrink(n)`, `NoGrow()`, `NoShrink()` | Flex factors |
| `Position(x, y)`, `Centered()`, `Relative(...)`, `Absolute(...)` | Position |
| `Anchored(target, placement, offset)` | Position next to another element |
| `Overhang(int2)` (after `Anchored`) | How far it may reach past the screen edge, as see-through room around its content |
| `StickyX()`, `StickyY()`, `Sticky()` | Stick while scrolling |
| `BreakBefore()`, `BreakAfter()`, `KeepWithNext()` | Line breaks in wrapping containers |
| `GridColumn(start, span)`, `GridRow(start, span)`, `ColumnTrack`, `RowTrack` | Grid placement |
| `SetIconSide(side)`, `IconTextGap(gap)` | Icon placement |

Units:

- Plain numbers and `CssPx` are CSS pixels: they scale with the UI scale.
- `Pixels{ n }` and `int2` are real pixels.

```cpp
Button("OK", onOK, LayoutOverride{}.Width(80));             // 80 CSS pixels
Button("OK", onOK, LayoutOverride{}.Width(Pixels{ 80 }));   // 80 real pixels
```

They are defined in `src/gui_core/layout/gui_constraints.h`.

**State scopes.** Each pair applies to every control emitted between Begin and End:

- `BeginDisabled()` / `EndDisabled()`:
  - drawn at 40% opacity, menu items at 45%
  - no input
  - menu items can't be chosen
  - the tooltip still shows
  - `SetDisabledTooltip` gives the reason
- `BeginSelected()` / `EndSelected()`:
  - drawn as selected
  - menu items show a check mark
- `BeginIndeterminate()` / `EndIndeterminate()`:
  - fields show a dash for mixed values, such as a value that differs between the
    selected objects
  - they return `false`
- `BeginInvalid()` / `EndInvalid()`:
  - an error border and halo in any state, in the colors `--error` and `--invalid-halo`
  - the halo grows in over 150 ms
  - fields also get an error icon
  - `SetErrorTooltip` gives the reason

```cpp
BeginDisabled();
Button("Export", [this] { Export(); });
SetDisabledTooltip("Export the scene", "Open a scene first");
EndDisabled();

BeginInvalid();
Field(fileName);
SetErrorTooltip("A file name can't contain '/'");
EndInvalid();
```

**Scope objects.** A scope object is a local variable that calls a Begin function when
it's made and the matching End when it goes out of scope. It takes the Begin's arguments.
There are scope objects for `Window`, `GhostWindow`, `CursorGhostWindow`, `Menu`,
`MenuBar`, `ContextMenu`, `Tooltip`, `Cached`, `Horizontal`, `Vertical`,
`HorizontalWrap`, `VerticalWrap`, `Area`, `LabelGroup`, `ButtonGroup`, `ToggleGroup`,
`Table`, `Row`, `ListView`, `GridView`, `TabBar`, `Toolbar`, `StatusBar`, `TitleBar`,
`ReorderableGroup`, `ReorderableItem`, `Disabled`, `Selected`, `Indeterminate`, `Invalid`
and `DockingContainer`. See [Scopes](../overview/begin-end.md#scopes).

```cpp
if (auto menu = Menu("File"))
    MenuItem("Open", [this] { Open(); });
```

```cpp
{
    auto disabled = Disabled();
    Button("Export", [this] { Export(); });
}   // EndDisabled runs here
```

**Begin / End.** `BeginWindow`, `BeginMenu`, `BeginContextMenu`, `BeginTooltip` and
`BeginCached` return whether to build the content. Call End either way. Other Begin
functions always return `true`. See [Begin and End](../overview/begin-end.md).

```cpp
if (BeginMenu("File"))
    MenuItem("Open", [this] { Open(); });
EndMenu();   // also when BeginMenu returned false
```

**Panning and zooming.** The controls that show a canvas you move around in (the node view,
the curve and timeline editors, a plot) all do it the same way:

- **Dragging the view:** the middle button.
  - For a mouse or trackpad without one: Alt + the left button. It drags the view over a
    node, key or handle too, without moving it.
  - The right button doesn't drag; a right click opens a menu.
  - Escape during the drag puts the view back.
- **The wheel**, and a trackpad's two-finger scroll, does what `SetWheelMode` says:

  | `WheelMode` | The wheel | Shift + wheel | Ctrl + wheel |
  | --- | --- | --- | --- |
  | `Zoom` | zooms round the pointer | scrolls sideways | zooms |
  | `Pan` | scrolls the view | scrolls sideways | zooms round the pointer |

  - `DefaultWheelMode()` is `Pan` on macOS, where most pointers are trackpads, and `Zoom`
    elsewhere.
  - A trackpad's scroll reaches the app as wheel turns: with `Zoom` it zooms too.
- **A pinch** on a trackpad zooms, whatever the mode.

Let your users pick the mode, as a setting:

```cpp
SetWheelMode(settings.wheelScrolls ? WheelMode::Pan : WheelMode::Zoom);
```

For a canvas of your own, `gui_navigation.h` gives the same rules:

- `PanDragButton(event.state)`: the button a press drags the view with, or `None`.
- `NavigateByWheel(event.state, GetWheelMode(), zoomStep)`: how far a wheel or pinch event
  zooms, and scrolls.

```cpp
case EventType::MouseScroll:
{
    const WheelNavigation nav = NavigateByWheel(event.state, wheelMode, 1.1f);
    if (nav.zoom != 1.0f) ZoomAround(pointer, nav.zoom);
    if (nav.pan.x != 0.0f || nav.pan.y != 0.0f) Scroll(nav.pan * notchPixels);
    return nav.Any();
}
```

---

[Controls](../controls.md) | [Next: Windows and layout](windows-and-layout.md)
