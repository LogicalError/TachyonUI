# Common conventions

**Parameters.** Controls take their arguments in the order
`(id, content or value, ..., callback, style, constraints)`, and you can leave most of
them out. Each family comes with a few overloads:

- One takes an `ID id` first, for when you keep the ID yourself.
- One leaves the ID out and takes it from the call site (`std::source_location`), so
  `Button("Save", onSave)` needs no ID at all. [IDs](../overview/ids.md) explains when an
  ID has to be stable.
- One takes a `StyleRef style` that replaces the default CSS class. You get one with
  `theme.GetStyle("my-class")`. Leave it out and the control uses its default class.

The signatures on these pages show the overload with both an ID and a style. The shorter
ones are the same without those.

**Content.** Buttons, toggles, tabs, list items and menu items take a `GUIContent`. That's
a text, an icon, or both, plus an optional tooltip. You can pass a plain string or an
icon and it converts:

```cpp
Button("Save", onSave);
Button(saveIcon, onSave);
Button({ saveIcon, "Save", "Write the file to disk" }, onSave);
```

**Results.** You hear about a change in two ways. The callback runs when the input is
handled, after the frame it arrived in. Then the control returns `true` in the next
frame. Value controls that take a `T&` also write the new value for you. For the details,
see [Return values and callbacks](../overview/values.md#return-values-and-callbacks).

**LayoutOverride.** The last parameter, `LayoutOverride&& constraints = {}`, changes the
size and position for this one call, on top of what the style says. You can chain its
builders:

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
| `Overhang(int2)` (after `Anchored`) | How far it may reach past the edge of the screen, as see-through room around what it shows |
| `StickyX()`, `StickyY()`, `Sticky()` | Stick while scrolling |
| `BreakBefore()`, `BreakAfter()`, `KeepWithNext()` | Line breaks in wrapping containers |
| `GridColumn(start, span)`, `GridRow(start, span)`, `ColumnTrack`, `RowTrack` | Grid placement |
| `SetIconSide(side)`, `IconTextGap(gap)` | Icon placement |

Plain numbers and `CssPx` values are CSS pixels, so they scale with the UI scale.
`Pixels{ n }` and `int2` values are real pixels. You'll find the builders in
`src/gui_core/layout/gui_constraints.h`.

**State scopes.** Every control you emit between one of these pairs shares that state:

| Pair | Effect |
| --- | --- |
| `BeginDisabled()` / `EndDisabled()` | Drawn at 40% opacity (menu items at 45%) and takes no input. Menu items can't be chosen. The tooltip still shows, and `SetDisabledTooltip` lets you say why it's disabled. |
| `BeginSelected()` / `EndSelected()` | Drawn as selected. A menu item shows a check mark. |
| `BeginIndeterminate()` / `EndIndeterminate()` | Fields show a dash for mixed values and return `false`. |
| `BeginInvalid()` / `EndInvalid()` | Marks the value as not accepted. The control gets an error border and halo (`--error`, `--invalid-halo`) in any state, and the halo grows in over 150 ms. A field also gets an error icon at its end, and `SetErrorTooltip` lets you say why. |

**Scope objects.** Each Begin / End pair also has a scope object. It takes the same
arguments as the Begin and calls the End when it goes out of scope. These exist:
`Window`, `GhostWindow`, `CursorGhostWindow`, `Menu`, `MenuBar`, `ContextMenu`,
`Tooltip`, `Cached`, `Horizontal`, `Vertical`, `HorizontalWrap`, `VerticalWrap`, `Area`,
`LabelGroup`, `ButtonGroup`, `ToggleGroup`, `Table`, `Row`, `ListView`, `GridView`,
`TabBar`, `Toolbar`, `StatusBar`, `TitleBar`, `ReorderableGroup`, `ReorderableItem`,
`Disabled`, `Selected`, `Indeterminate`, `Invalid` and `DockingContainer`. See
[Scopes](../overview/begin-end.md#scopes) for more.

```cpp
if (auto menu = Menu("File"))
    MenuItem("Open", [this] { Open(); });
```

**Begin / End.** `BeginWindow`, `BeginMenu`, `BeginContextMenu`, `BeginTooltip` and
`BeginCached` return whether you should build the content. Call their End either way.
The other Begin functions always return `true`. [Begin and End](../overview/begin-end.md)
has the full story.

---

[Controls](../controls.md) | [Next: Windows and layout](windows-and-layout.md)
