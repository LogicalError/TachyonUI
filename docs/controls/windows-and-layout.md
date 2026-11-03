# Windows and layout

## BeginWindow / EndWindow

Creates an OS window. A window lays out its children in rows that wrap, so put a
`BeginVertical` inside if you want them stacked.

```cpp
bool BeginWindow(ID windowLayoutID, std::string_view title, StyleRef styleRef, LayoutOverride&& constraints,
                 ElementFlags flags = ElementFlags::WindowUserControlled | ElementFlags::WindowIsCentered,
                 int monitorIndex = -1);
void EndWindow();
```

Shorter overloads let you leave out the title, the style or the constraints. The ID has
to be stable, so create it with `idManager.CreateStaticID()`. `BeginWindow` returns
`false` once the user has closed the window, and you call `EndWindow` either way. The
default class is `window`, and `theme.mainWindowStyleRef` uses `main-window`.

These flags are the ones you'll reach for most:

- `WindowUserControlled` lets the user move and resize the window.
- `WindowIsCentered` centers it.
- `WindowNoCaption` hides the caption.
- `WindowCustomTitleBar` lets you draw your own title bar with `BeginTitleBar`.
- `WindowConfirmClose` asks your app before closing (see `CloseRequested`).
- `WindowIsModal` and `WindowRenderToImage`.

```cpp
if (BeginWindow(windowID, "Hello", LayoutOverride{}.Size(360, 160)))
{
    BeginVertical(LayoutOverride{}.FlexGrow(1));
    Text("Hello from TachyonUI!");
    EndVertical();
}
EndWindow();
```

For a window with `WindowConfirmClose`, `bool CloseRequested(ID windowID)` tells you
whether the user has asked to close it since you last checked. The window stays open until
your app stops declaring it.

## Ghost windows

Borderless, transparent windows for drag previews. `BeginCursorGhostWindow` places the
window at the cursor, offset a little so it doesn't sit under the OS cursor.

```cpp
bool BeginGhostWindow(ID windowLayoutID, int2 position, int2 size, StyleRef styleRef);
bool BeginCursorGhostWindow(ID windowLayoutID, int2 cursorPos);
void AddGhostIcon(IconID icon);
void EndGhostWindow();
```

Other overloads leave out the size, in which case the window fits its content, or the
style. The default class is `ghost-window`, and `AddGhostIcon` uses `ghost-icon`.

## BeginHorizontal / BeginVertical and the Wrap variants

Plain flex containers with no decoration. You get a row, a column, and wrapping versions
of each.

```cpp
bool BeginHorizontal(ID id, StyleRef style, LayoutOverride&& constraints = {});
bool BeginVertical(ID id, StyleRef style, LayoutOverride&& constraints = {});
bool BeginHorizontalWrap(ID id, StyleRef style, LayoutOverride&& constraints = {});
bool BeginVerticalWrap(ID id, StyleRef style, LayoutOverride&& constraints = {});
void EndHorizontal();   // EndVertical, EndHorizontalWrap, EndVerticalWrap
```

Their default styles aren't CSS classes. They're plain flex boxes built in code:
`theme.emptyHorzStyleRef`, `emptyVertStyleRef`, `emptyHorzWrapStyleRef` and
`emptyVertWrapStyleRef`.

```cpp
BeginHorizontal();
Button("Greet", [this] { greeting = "Hello, " + name + "!"; });
Button("Clear", [this] { greeting.clear(); });
EndHorizontal();
```

## BeginArea / EndArea

A container you can put anything in. Its default class, `collection`, is a bordered flex
box that wraps and has `overflow: auto`.

```cpp
bool BeginArea(ID id, StyleRef style, LayoutOverride&& constraints = {});
void EndArea();
```

## Scroll bars

A container scrolls when its style has `overflow: auto` and its size is bounded.

A flex child is bounded in one of two ways. Either it has a fixed size, or it has
`flex-basis: 0` with `flex-grow`, and so does every container between it and a bounded
ancestor. The `flex-fill-row` and `flex-fill-col` classes do this, as do
`theme.flexFillRowStyleRef` and `flexFillColStyleRef`. If neither applies, the child grows
to fit its content and there's nothing to scroll.

Call these functions from inside the container. The bar is drawn on top of the content,
and it draws nothing when there's no overflow.

```cpp
void VerticalScrollbar(ID containerID);
void HorizontalScrollbar(ID containerID);
void ScrollbarOverlay(ID containerID, int2 lastInnerSize = int2{0, 0},
                      ID vertScrollBarID = ID::Invalid(), ID horzScrollBarID = ID::Invalid(),
                      bool emitVert = true, bool emitHorz = true);
```

`ScrollbarOverlay` draws both bars plus the corner where they meet. Classes:
`scrollbar-vertical`, `scrollbar-vertical-thumb`, `scrollbar-horizontal`,
`scrollbar-horizontal-thumb`.

## BeginCached / EndCached

A container like `BeginArea` that keeps its rendered output. If nothing inside it has
changed, `BeginCached` returns `false`, you skip the body, and the kept output is drawn
instead. It's rebuilt when its size changes, when the cursor or keyboard focus is inside
it, when `signature` changes, or after you call `Invalidate(id)`.

```cpp
bool BeginCached(ID id, StyleRef style, uint64_t signature = 0, LayoutOverride&& constraints = {});
void EndCached();
void Invalidate(ID id);          // safe from event handlers
void InvalidatePanel(ID panelId);
void SetDrawCacheEnabled(bool enabled);
```

The ID must be stable, either static or salted. The default class is `collection`. Docked
panels are already cached (see
[Docking](tabs-docking-toolbars.md#docking)).

The kept output lives on the GPU. Once it's there, drawing it again uploads nothing, even
if it has moved. Output that hasn't been drawn for 240 frames is dropped, and it's built
again the next time it's shown.

```cpp
if (BeginCached(listID, dataVersion))
{
    for (const Item& item : items)
        Text(item.name);
}
EndCached();
```

## Label groups

Inside a label group, every `Label` and `DragLabel` shares one width, so each label and its
control line up in rows. Dragging the right edge of any label resizes all of them. Widths
are in CSS pixels.

```cpp
bool BeginLabelGroup(ID id, CssPx defaultWidth = 120, CssPx minWidth = 40, CssPx maxWidth = 500,
                     bool resizable = true);
void EndLabelGroup();
```

```cpp
BeginLabelGroup(140);
Label("Name");
Field(name);
DragLabel("Scale");
Field(scale);
EndLabelGroup();
```

In a resizable group, labels use the `group-label` class and drag labels use
`group-drag-label`.

## Button and toggle groups

Put `Button`s in a button group, or `Toggle`s and `Field(bool)`s in a toggle group, and
they're packed together with shared borders. A group with only one item keeps that item's
normal style.

```cpp
bool BeginButtonGroup(LayoutOverride&& constraints = {});
void EndButtonGroup();
bool BeginToggleGroup(LayoutOverride&& constraints = {});
void EndToggleGroup();
```

A button group uses `button-group` and its buttons use `group-button`. A toggle group uses
`toggle-group` with `group-toggle`. A lone item uses `button` or `toggle`. You can style
the ends of a group with `.group-button:first-child` and `:last-child` (see
[Syntax](../css/syntax.md#child-position)).

```cpp
BeginToggleGroup(LayoutOverride{}.NoGrow());
Toggle(bold, "B");
Toggle(italic, "I");
Toggle(underline, "U");
EndToggleGroup();
```

---

[Previous: Common conventions](conventions.md) | [Controls](../controls.md) | [Next: Text and images](text-and-images.md)
