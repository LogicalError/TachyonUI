# Windows and layout

## BeginWindow / EndWindow

Creates an OS window. Children are laid out in wrapping rows; put a `BeginVertical` inside
to stack them.

```cpp
bool BeginWindow(ID windowLayoutID, std::string_view title, StyleRef styleRef, LayoutOverride&& constraints,
                 ElementFlags flags = ElementFlags::WindowUserControlled | ElementFlags::WindowIsCentered,
                 int monitorIndex = -1);
void EndWindow();
```

- Overloads can omit title, style or constraints.
- The ID must be stable: `idManager.CreateStaticID()`.
- Returns `false` once the user closed the window; call `EndWindow` either way.
- Classes: `window`; `theme.mainWindowStyleRef` uses `main-window`.

| Flag | Effect |
| --- | --- |
| `WindowUserControlled` | User can move and resize |
| `WindowIsCentered` | Centered |
| `WindowNoCaption` | No caption |
| `WindowCustomTitleBar` | Draw your own title bar with `BeginTitleBar` |
| `WindowConfirmClose` | Ask your app before closing (see `CloseRequested`) |
| `WindowIsModal` | Blocks input to the app's other windows |
| `WindowRenderToImage` | No OS window: renders into an image, the host passes input ([Rendering into a host image](../integration/render-to-image.md)) |

```cpp
if (BeginWindow(windowID, "Hello", LayoutOverride{}.Size(360, 160)))
{
    BeginVertical(LayoutOverride{}.FlexGrow(1));
    Text("Hello from TachyonUI!");
    EndVertical();
}
EndWindow();
```

With `WindowConfirmClose`, `bool CloseRequested(ID windowID)` returns whether the user
asked to close since the last check. The window stays open until you stop declaring it.

## Ghost windows

Borderless transparent windows for drag previews. `BeginCursorGhostWindow` places it at
the cursor, offset clear of the OS cursor.

```cpp
bool BeginGhostWindow(ID windowLayoutID, int2 position, int2 size, StyleRef styleRef);
bool BeginCursorGhostWindow(ID windowLayoutID, int2 cursorPos);
void AddGhostIcon(IconID icon);
void EndGhostWindow();
```

```cpp
// A reorderable group's dragVisualizer: the dragged item's icon follows the cursor.
auto ghost = [this](int index, int2 screenPos)
{
    if (BeginCursorGhostWindow(ghostWindowID, screenPos))   // member: ID ghostWindowID = idManager.CreateStaticID()
        AddGhostIcon(itemIcons[index]);
    EndGhostWindow();
};
```

Overloads can omit the size (fits content) or the style. Classes: `ghost-window`;
`AddGhostIcon` uses `ghost-icon`.

## BeginHorizontal / BeginVertical and the Wrap variants

Undecorated flex containers: row, column, and wrapping versions.

```cpp
bool BeginHorizontal(ID id, StyleRef style, LayoutOverride&& constraints = {});
bool BeginVertical(ID id, StyleRef style, LayoutOverride&& constraints = {});
bool BeginHorizontalWrap(ID id, StyleRef style, LayoutOverride&& constraints = {});
bool BeginVerticalWrap(ID id, StyleRef style, LayoutOverride&& constraints = {});
void EndHorizontal();   // EndVertical, EndHorizontalWrap, EndVerticalWrap
```

Default styles are built in code, not CSS classes: `theme.emptyHorzStyleRef`,
`emptyVertStyleRef`, `emptyHorzWrapStyleRef`, `emptyVertWrapStyleRef`.

```cpp
BeginHorizontal();
Button("Greet", [this] { greeting = "Hello, " + name + "!"; });
Button("Clear", [this] { greeting.clear(); });
EndHorizontal();
```

## BeginArea / EndArea

A general container. Default class `collection`: bordered, wrapping flex box with
`overflow: auto`.

```cpp
bool BeginArea(ID id, StyleRef style, LayoutOverride&& constraints = {});
void EndArea();
```

```cpp
BeginArea(LayoutOverride{}.Size(300, 120));
for (const std::string& tag : tags)
    Text(tag);
EndArea();
```

## Scroll bars

A container scrolls when its style has `overflow: auto` and its size is bounded. A flex
child is bounded if it has a fixed size, or if it and every container up to a bounded
ancestor have `flex-basis: 0` with `flex-grow` (`flex-fill-row` / `flex-fill-col`,
`theme.flexFillRowStyleRef` / `flexFillColStyleRef`). Otherwise it grows to fit its content
and nothing scrolls.

Call these inside the container. Bars draw over the content, and draw nothing without
overflow.

```cpp
void VerticalScrollbar(ID containerID);
void HorizontalScrollbar(ID containerID);
void ScrollbarOverlay(ID containerID, int2 lastInnerSize = int2{0, 0},
                      ID vertScrollBarID = ID::Invalid(), ID horzScrollBarID = ID::Invalid(),
                      bool emitVert = true, bool emitHorz = true);
```

```cpp
const ID logID = idManager.AllocateDynamicID("log");
BeginArea(logID, LayoutOverride{}.Size(300, 200));
for (const std::string& line : logLines)
    Text(line);
VerticalScrollbar(logID);
EndArea();
```

`ScrollbarOverlay` draws both bars and the corner. Classes: `scrollbar-vertical`,
`scrollbar-vertical-thumb`, `scrollbar-horizontal`, `scrollbar-horizontal-thumb`.

## BeginCached / EndCached

Like `BeginArea`, but keeps its rendered output. If nothing changed, `BeginCached` returns
`false`: skip the body and the kept output is drawn. Rebuilt when its size changes, the
cursor or keyboard focus is inside, `signature` changes, or after `Invalidate(id)`.

```cpp
bool BeginCached(ID id, StyleRef style, uint64_t signature = 0, LayoutOverride&& constraints = {});
void EndCached();
void Invalidate(ID id);          // safe from event handlers
void InvalidatePanel(ID panelId);
void SetDrawCacheEnabled(bool enabled);
```

- The ID must be stable (static or salted). Default class: `collection`.
- Docked panels are already cached (see [Docking](tabs-docking-toolbars.md#docking)).
- The output stays on the GPU; redrawing uploads nothing, even if moved. Output not drawn
  for 240 frames is dropped and rebuilt when next shown.

```cpp
if (BeginCached(listID, dataVersion))
{
    for (const Item& item : items)
        Text(item.name);
}
EndCached();
```

## Label groups

Every `Label` and `DragLabel` inside shares one width, so labels and controls line up.
Dragging any label's right edge resizes all. Widths in CSS pixels.

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

In a resizable group: `group-label` and `group-drag-label`.

## Button and toggle groups

Packs `Button`s (button group) or `Toggle`s and `Field(bool)`s (toggle group) together with
shared borders. A lone item keeps its normal style.

```cpp
bool BeginButtonGroup(LayoutOverride&& constraints = {});
void EndButtonGroup();
bool BeginToggleGroup(LayoutOverride&& constraints = {});
void EndToggleGroup();
```

Classes: `button-group` with `group-button`; `toggle-group` with `group-toggle`; a lone
item `button` or `toggle`. Style the ends with `.group-button:first-child` and
`:last-child` (see [Syntax](../css/syntax.md#child-position)).

```cpp
BeginToggleGroup(LayoutOverride{}.NoGrow());
Toggle(bold, "B");
Toggle(italic, "I");
Toggle(underline, "U");
EndToggleGroup();
```

---

[Previous: Common conventions](conventions.md) | [Controls](../controls.md) | [Next: Text and images](text-and-images.md)
