# Windows and layout

## BeginWindow / EndWindow

Creates an OS window: a real window of the operating system, with its own place on the
screen.

- Its children are laid out in rows that wrap onto the next line when full.
- Put a `BeginVertical` inside it to stack them instead.

```cpp
bool BeginWindow(ID windowLayoutID, std::string_view title, StyleRef styleRef, LayoutOverride&& constraints,
                 ElementFlags flags = ElementFlags::WindowUserControlled | ElementFlags::WindowIsCentered,
                 int monitorIndex = -1);
void EndWindow();
```

- Other overloads leave out the title, the style or the constraints.
- The ID must be stable: make it once with `idManager.CreateStaticID()` and keep it. See
  [Static IDs](../overview/ids.md#static-ids).
- `BeginWindow` returns `false` once the user closed the window. Call `EndWindow` either
  way.
- The CSS classes: `window`, and `main-window` for `theme.mainWindowStyleRef`.

| Flag | Effect |
| --- | --- |
| `WindowUserControlled` | User can move and resize |
| `WindowIsCentered` | Centered |
| `WindowNoCaption` | No caption |
| `WindowCustomTitleBar` | Draw your own title bar with `BeginTitleBar` |
| `WindowConfirmClose` | Ask your app before closing (see `CloseRequested`) |
| `WindowIsModal` | Blocks input to the app's other windows |
| `WindowRenderToImage` | No OS window: renders into an image, the host passes input ([Rendering into a host image](../integration/render-to-image.md)) |
| `WindowImagePopups` | With `WindowRenderToImage`: its menus, tooltips and dialogs render into images too |

```cpp
if (BeginWindow(windowID, "Hello", LayoutOverride{}.Size(360, 160)))
{
    BeginVertical(LayoutOverride{}.FlexGrow(1));
    Text("Hello from TachyonUI!");
    EndVertical();
}
EndWindow();
```

A window that blocks the others until it's closed:

```cpp
if (BeginWindow(settingsWindowID, "Settings", LayoutOverride{}.Size(400, 300),
                ElementFlags::WindowIsCentered | ElementFlags::WindowIsModal))
{
    Text("Settings go here.");
}
EndWindow();
```

With `WindowConfirmClose`, the window doesn't close by itself when the user closes it:

- `bool CloseRequested(ID windowID)` returns whether the user asked to close it since the
  last check.
- The window stays open until you stop declaring it.

```cpp
// members: bool editorOpen = true; bool askingToClose = false
if (editorOpen)
{
    if (BeginWindow(editorWindowID, "Editor", LayoutOverride{}.Size(600, 400),
                    ElementFlags::WindowUserControlled | ElementFlags::WindowConfirmClose))
    {
        if (CloseRequested(editorWindowID))
            askingToClose = true;
        if (askingToClose)
        {
            static constexpr MessageButton kButtons[] = { { .label = "Cancel" }, { .label = "Close" } };
            const int choice = MessageDialog(MessageDialogDesc{ .title = "Close the editor?", .buttons = kButtons });
            if (choice == 1) editorOpen = false;
            if (choice >= 0) askingToClose = false;
        }
    }
    EndWindow();
}
```

## Ghost windows

A ghost window is a borderless, see-through window that floats over everything, for
example to show what the user is dragging.

- `BeginGhostWindow` places it at a screen position.
- `BeginCursorGhostWindow` places it at the cursor, moved a little so the OS cursor
  doesn't cover it.

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

- Overloads leave out the size, to fit the content, or the style.
- The CSS classes: `ghost-window`, and `ghost-icon` for `AddGhostIcon`.

```cpp
if (BeginGhostWindow(ghostWindowID, dropPos, int2{ styleManager.Px(120), styleManager.Px(28) }))
    Text(draggedName);   // members: int2 dropPos (screen pixels); std::string draggedName
EndGhostWindow();
```

## BeginHorizontal / BeginVertical and the Wrap variants

Containers that only arrange their children, with no border or background:

- `BeginHorizontal`: in a row
- `BeginVertical`: in a column
- `BeginHorizontalWrap`: in rows, wrapping onto the next row when full
- `BeginVerticalWrap`: in columns, wrapping onto the next column when full

They are flex containers: they lay out their children as CSS flexbox does.

```cpp
bool BeginHorizontal(ID id, StyleRef style, LayoutOverride&& constraints = {});
bool BeginVertical(ID id, StyleRef style, LayoutOverride&& constraints = {});
bool BeginHorizontalWrap(ID id, StyleRef style, LayoutOverride&& constraints = {});
bool BeginVerticalWrap(ID id, StyleRef style, LayoutOverride&& constraints = {});
void EndHorizontal();   // EndVertical, EndHorizontalWrap, EndVerticalWrap
```

Their default styles are built in code, not taken from CSS classes:

- `theme.emptyHorzStyleRef`
- `theme.emptyVertStyleRef`
- `theme.emptyHorzWrapStyleRef`
- `theme.emptyVertWrapStyleRef`

```cpp
BeginHorizontal();
Button("Greet", [this] { greeting = "Hello, " + name + "!"; });
Button("Clear", [this] { greeting.clear(); });
EndHorizontal();
```

```cpp
BeginHorizontalWrap(LayoutOverride{}.Width(200));
for (const std::string& tag : tags)
    Button(tag, [this, &tag] { filter = tag; });
EndHorizontalWrap();
```

## BeginArea / EndArea

A general container. Its default CSS class is `collection`: a flex box with a border,
whose children wrap, and that scrolls (`overflow: auto`).

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

A container scrolls when:

- its style has `overflow: auto`
- and its size is bounded, so its content can be larger than it

A flex child is bounded if:

- it has a fixed size
- or it and every container up to a bounded ancestor have `flex-basis: 0` with
  `flex-grow`.
  - The classes `flex-fill-row` and `flex-fill-col` set that up.
  - In code they're `theme.flexFillRowStyleRef` and `theme.flexFillColStyleRef`.

Otherwise the container grows to fit its content, and nothing scrolls.

The scroll bars:

- Call them inside the container.
- They draw over the content.
- They draw nothing when the content fits.

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

- `ScrollbarOverlay` draws both bars, and the corner where they meet.
- The CSS classes:
  - `scrollbar-vertical` and `scrollbar-vertical-thumb`
  - `scrollbar-horizontal` and `scrollbar-horizontal-thumb`

```cpp
BeginArea(canvasID, LayoutOverride{}.Size(400, 300));   // member: ID canvasID = idManager.CreateStaticID()
Text(longText);
ScrollbarOverlay(canvasID);
EndArea();
```

## BeginCached / EndCached

Like `BeginArea`, but keeps what it drew and reuses it while nothing in it changes. This
saves the work of building the content every frame.

- If nothing changed, `BeginCached` returns `false`. Skip the content then, and the kept
  output is drawn.
- It's rebuilt when:
  - its size changes
  - the cursor or the keyboard focus is inside it
  - `signature` changes
  - you call `Invalidate(id)`

```cpp
bool BeginCached(ID id, StyleRef style, uint64_t signature = 0, LayoutOverride&& constraints = {});
void EndCached();
void Invalidate(ID id);          // safe from event handlers
void InvalidatePanel(ID panelId);
void SetDrawCacheEnabled(bool enabled);
```

- The ID must be stable: static or salted. See
  [When an ID has to be stable](../overview/ids.md#when-an-id-has-to-be-stable).
- The default CSS class is `collection`.
- Docked panels are already cached (see [Docking](tabs-docking-toolbars.md#docking)).
  `InvalidatePanel` rebuilds one.
- The output stays on the GPU. Drawing it again uploads nothing, even if it moved.
- Output not drawn for 240 frames is dropped, and rebuilt when it's next shown.

```cpp
if (BeginCached(listID, dataVersion))
{
    for (const Item& item : items)
        Text(item.name);
}
EndCached();
```

`signature` can be any number that changes when the content should change, such as a
counter you bump on every edit. Or call `Invalidate` when the data changes:

```cpp
Button("Add item", [this] { items.push_back(Item{ "New item" }); Invalidate(listID); });
```

## Label groups

A label group makes every `Label` and `DragLabel` inside it share one width, so the
labels and the controls after them line up in two columns.

- Dragging the right edge of any label resizes them all.
- Widths are in CSS pixels.

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

In a resizable group the labels get the CSS classes `group-label` and `group-drag-label`.

## Button and toggle groups

These draw a few controls packed together, sharing their borders, as one strip.

- A button group packs `Button`s.
- A toggle group packs `Toggle`s and `Field(bool)`s.
- A group with a single item keeps that item's normal style.

```cpp
bool BeginButtonGroup(LayoutOverride&& constraints = {});
void EndButtonGroup();
bool BeginToggleGroup(LayoutOverride&& constraints = {});
void EndToggleGroup();
```

The CSS classes:

- `button-group`, with `group-button` for its buttons
- `toggle-group`, with `group-toggle` for its toggles
- `button` or `toggle` for a lone item
- Style the ends with `.group-button:first-child` and `:last-child` (see
  [Syntax](../css/syntax.md#child-position)).

```cpp
BeginToggleGroup(LayoutOverride{}.NoGrow());
Toggle(bold, "B");
Toggle(italic, "I");
Toggle(underline, "U");
EndToggleGroup();
```

```cpp
BeginButtonGroup(LayoutOverride{}.NoGrow());
Button("Left", [this] { align = 0; });
Button("Center", [this] { align = 1; });
Button("Right", [this] { align = 2; });
EndButtonGroup();
```

---

[Previous: Common conventions](conventions.md) | [Controls](../controls.md) | [Next: Text and images](text-and-images.md)
