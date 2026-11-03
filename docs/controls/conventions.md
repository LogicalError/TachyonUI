# Common conventions

**Parameters.** Order: `(id, content or value, ..., callback, style, constraints)`. Most
can be left out. Overloads per control:

- With `ID id` first, when you keep the ID yourself.
- Without an ID: it comes from the call site (`std::source_location`), so
  `Button("Save", onSave)` works. See [IDs](../overview/ids.md) for when an ID must be
  stable.
- With `StyleRef style` (from `theme.GetStyle("my-class")`), replacing the default CSS class.

Signatures on these pages show the overload with both ID and style.

**Content.** Buttons, toggles, tabs, list items and menu items take a `GUIContent`: text,
icon or both, plus an optional tooltip. A string or icon converts implicitly:

```cpp
Button("Save", onSave);
Button(saveIcon, onSave);
Button({ saveIcon, "Save", "Write the file to disk" }, onSave);
```

**Results.** The callback runs when the input is handled, after the frame it arrived in;
the control returns `true` in the next frame. Controls taking a `T&` also write the new
value. See [Return values and callbacks](../overview/values.md#return-values-and-callbacks).

**LayoutOverride.** The last parameter, `LayoutOverride&& constraints = {}`, overrides
size and position for this call, on top of the style. Builders chain:

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

Units: plain numbers and `CssPx` are CSS pixels (scale with the UI scale); `Pixels{ n }`
and `int2` are real pixels. Defined in `src/gui_core/layout/gui_constraints.h`.

**State scopes.** Apply to every control emitted between the pair:

| Pair | Effect |
| --- | --- |
| `BeginDisabled()` / `EndDisabled()` | 40% opacity (menu items 45%), no input, menu items can't be chosen. Tooltip still shows; `SetDisabledTooltip` gives the reason. |
| `BeginSelected()` / `EndSelected()` | Drawn as selected; menu items show a check mark. |
| `BeginIndeterminate()` / `EndIndeterminate()` | Fields show a dash for mixed values and return `false`. |
| `BeginInvalid()` / `EndInvalid()` | Error border and halo (`--error`, `--invalid-halo`) in any state; halo grows in over 150 ms. Fields also get an error icon; `SetErrorTooltip` gives the reason. |

**Scope objects.** Take the Begin's arguments and call End on destruction: `Window`,
`GhostWindow`, `CursorGhostWindow`, `Menu`, `MenuBar`, `ContextMenu`, `Tooltip`, `Cached`,
`Horizontal`, `Vertical`, `HorizontalWrap`, `VerticalWrap`, `Area`, `LabelGroup`,
`ButtonGroup`, `ToggleGroup`, `Table`, `Row`, `ListView`, `GridView`, `TabBar`, `Toolbar`,
`StatusBar`, `TitleBar`, `ReorderableGroup`, `ReorderableItem`, `Disabled`, `Selected`,
`Indeterminate`, `Invalid`, `DockingContainer`. See [Scopes](../overview/begin-end.md#scopes).

```cpp
if (auto menu = Menu("File"))
    MenuItem("Open", [this] { Open(); });
```

**Begin / End.** `BeginWindow`, `BeginMenu`, `BeginContextMenu`, `BeginTooltip` and
`BeginCached` return whether to build the content; call End either way. Other Begin
functions always return `true`. See [Begin and End](../overview/begin-end.md).

---

[Controls](../controls.md) | [Next: Windows and layout](windows-and-layout.md)
